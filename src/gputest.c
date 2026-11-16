/* Does the GPU path work here, and how fast is it?
 *
 * Two questions, and they are different questions. The first is answered
 * by a shader whose answer is known in closed form, compared against the
 * same arithmetic in C: if those agree, the pipeline -- context, program,
 * float target, readback -- is sound. The second is answered by a workload
 * shaped like the room's ray marcher, which is the only thing worth
 * measuring, and which on a machine whose GL is software will honestly
 * come out slower than the C.
 *
 *   desk-gputest [width height [iterations]]
 *
 * And a third, which is the one that matters on a machine nobody has run
 * this on before: does the room come out of the GPU the way it comes out
 * of the C? Every kind of sheet, traced both ways and compared; with a
 * directory, the pairs and their difference are written there.
 *
 *   desk-gputest --room [width height [dir]]
 *
 * And a trial: the bookcase's view laid from pictures of its faces, next
 * to the same view traced (draw.c, desk_room_faces_test).
 *
 *   desk-gputest --faces [width height [dir]]
 */

#include "compat.h"
#include "desk.h"
#include "gpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

static double now_sec(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

/* A closed form both sides can compute. Nothing clever: the point is that
 * a wrong readback, a wrong row order or a wrong channel order all show up
 * as a large error, and a right one shows up as zero. */
static const char *CHECK_SRC =
	"#version 300 es\n"
	"precision highp float;\n"
	"uniform vec2 u_res;\n"
	"out vec4 o;\n"
	"void main() {\n"
	"  vec2 p = gl_FragCoord.xy / u_res;\n"
	"  o = vec4(p.x, p.y, p.x * p.y, 1.0);\n"
	"}\n";

/* The shape of the real thing: march a distance field, shade what it hits.
 * The field is three spheres and a plane instead of a room, because what
 * is being measured is the marching, not the scene. */
static const char *MARCH_SRC =
	"#version 300 es\n"
	"precision highp float;\n"
	"uniform vec2 u_res;\n"
	"uniform int u_steps;\n"
	"out vec4 o;\n"
	"float map(vec3 p) {\n"
	"  float d = p.y + 1.0;\n"
	"  d = min(d, length(p - vec3(-1.2, 0.0, 0.0)) - 0.9);\n"
	"  d = min(d, length(p - vec3( 0.4, 0.3, 0.6)) - 0.7);\n"
	"  d = min(d, length(p - vec3( 1.6, -0.2, -0.4)) - 0.8);\n"
	"  return d;\n"
	"}\n"
	"vec3 nrm(vec3 p) {\n"
	"  vec2 e = vec2(0.0015, 0.0);\n"
	"  return normalize(vec3(map(p + e.xyy) - map(p - e.xyy),\n"
	"                        map(p + e.yxy) - map(p - e.yxy),\n"
	"                        map(p + e.yyx) - map(p - e.yyx)));\n"
	"}\n"
	"void main() {\n"
	"  vec2 uv = (gl_FragCoord.xy / u_res) * 2.0 - 1.0;\n"
	"  uv.x *= u_res.x / u_res.y;\n"
	"  vec3 ro = vec3(0.0, 0.8, 5.0);\n"
	"  vec3 rd = normalize(vec3(uv, -1.6));\n"
	"  float t = 0.02;\n"
	"  vec3 col = vec3(0.02, 0.02, 0.03);\n"
	"  for (int i = 0; i < u_steps && t < 40.0; i++) {\n"
	"    vec3 p = ro + rd * t;\n"
	"    float d = map(p);\n"
	"    if (d < 0.0018) {\n"
	"      vec3 n = nrm(p);\n"
	"      vec3 l = normalize(vec3(0.5, 1.0, 0.4));\n"
	"      float diff = max(dot(n, l), 0.0);\n"
	"      float spec = pow(max(dot(reflect(-l, n), -rd), 0.0), 48.0);\n"
	"      col = vec3(0.62, 0.52, 0.40) * diff + vec3(1.0) * spec * 0.4;\n"
	"      break;\n"
	"    }\n"
	"    t += max(d, 0.0009);\n"
	"  }\n"
	"  o = vec4(col, 1.0);\n"
	"}\n";

/* ---- and the very same march, in C, on threads ----------------------
 *
 * Without this the GPU number means nothing: a toy scene says nothing
 * about a real one. What it does say something about is the RATIO between
 * the two on this machine, and the ratio is the thing that carries over to
 * the real scene and can be multiplied by a bake we have actually timed. */

struct cpu_job {
	int w, h, steps;
	int next;
	pthread_mutex_t lock;
};

/* The same again in single precision, to separate the two things the
 * shader is doing differently: it works in float, and it works on several
 * pixels at once. If float alone accounts for most of the gap then a
 * narrower change to the C would do, and the port is not worth its size. */
static float f_map(const float p[3])
{
	float d = p[1] + 1.0f;
	static const float sp[3][4] = {
		{ -1.2f, 0.0f, 0.0f, 0.9f }, { 0.4f, 0.3f, 0.6f, 0.7f },
		{ 1.6f, -0.2f, -0.4f, 0.8f },
	};
	for (int i = 0; i < 3; i++) {
		float dx = p[0] - sp[i][0], dy = p[1] - sp[i][1], dz = p[2] - sp[i][2];
		float l = sqrtf(dx * dx + dy * dy + dz * dz) - sp[i][3];
		if (l < d) {
			d = l;
		}
	}
	return d;
}

static double c_map(const double p[3])
{
	double d = p[1] + 1.0;
	const double sp[3][4] = {
		{ -1.2, 0.0, 0.0, 0.9 }, { 0.4, 0.3, 0.6, 0.7 }, { 1.6, -0.2, -0.4, 0.8 },
	};
	for (int i = 0; i < 3; i++) {
		double dx = p[0] - sp[i][0], dy = p[1] - sp[i][1], dz = p[2] - sp[i][2];
		double l = sqrt(dx * dx + dy * dy + dz * dz) - sp[i][3];
		if (l < d) {
			d = l;
		}
	}
	return d;
}

static void f_row(struct cpu_job *j, int y, double *sink)
{
	float aspect = (float)j->w / (float)j->h;
	float acc = 0.0f;
	for (int x = 0; x < j->w; x++) {
		float ux = ((x + 0.5f) / j->w) * 2.0f - 1.0f;
		float uy = ((y + 0.5f) / j->h) * 2.0f - 1.0f;
		ux *= aspect;
		float rl = sqrtf(ux * ux + uy * uy + 1.6f * 1.6f);
		float rd[3] = { ux / rl, uy / rl, -1.6f / rl };
		float t = 0.02f, col = 0.02f;
		for (int i = 0; i < j->steps && t < 40.0f; i++) {
			float p[3] = { rd[0] * t, 0.8f + rd[1] * t, 5.0f + rd[2] * t };
			float d = f_map(p);
			if (d < 0.0018f) {
				float e = 0.0015f, n[3];
				for (int k = 0; k < 3; k++) {
					float a[3] = { p[0], p[1], p[2] }, b[3] = { p[0], p[1], p[2] };
					a[k] += e;
					b[k] -= e;
					n[k] = f_map(a) - f_map(b);
				}
				float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-9f;
				float L[3] = { 0.5f, 1.0f, 0.4f };
				float ll = sqrtf(0.25f + 1.0f + 0.16f);
				float diff = (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / (nl * ll);
				col = diff > 0.0f ? 0.62f * diff : 0.0f;
				break;
			}
			t += d > 0.0009f ? d : 0.0009f;
		}
		acc += col;
	}
	*sink += acc;
}

static void c_row(struct cpu_job *j, int y, double *sink)
{
	double aspect = (double)j->w / (double)j->h;
	double acc = 0.0;
	for (int x = 0; x < j->w; x++) {
		double ux = ((x + 0.5) / j->w) * 2.0 - 1.0;
		double uy = ((y + 0.5) / j->h) * 2.0 - 1.0;
		ux *= aspect;
		double rl = sqrt(ux * ux + uy * uy + 1.6 * 1.6);
		double rd[3] = { ux / rl, uy / rl, -1.6 / rl };
		double t = 0.02, col = 0.02;
		for (int i = 0; i < j->steps && t < 40.0; i++) {
			double p[3] = { rd[0] * t, 0.8 + rd[1] * t, 5.0 + rd[2] * t };
			double d = c_map(p);
			if (d < 0.0018) {
				double e = 0.0015, n[3];
				for (int k = 0; k < 3; k++) {
					double a[3] = { p[0], p[1], p[2] }, b[3] = { p[0], p[1], p[2] };
					a[k] += e;
					b[k] -= e;
					n[k] = c_map(a) - c_map(b);
				}
				double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-9;
				double L[3] = { 0.5, 1.0, 0.4 };
				double ll = sqrt(0.25 + 1.0 + 0.16);
				double diff = (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / (nl * ll);
				col = diff > 0.0 ? 0.62 * diff : 0.0;
				break;
			}
			t += d > 0.0009 ? d : 0.0009;
		}
		acc += col;
	}
	*sink += acc;
}

static int g_single;

static void *c_worker(void *arg)
{
	struct cpu_job *j = arg;
	double sink = 0.0;
	for (;;) {
		pthread_mutex_lock(&j->lock);
		int y = j->next++;
		pthread_mutex_unlock(&j->lock);
		if (y >= j->h) {
			break;
		}
		if (g_single) {
			f_row(j, y, &sink);
		} else {
			c_row(j, y, &sink);
		}
	}
	return NULL;
}

static double cpu_march(int w, int h, int steps)
{
	struct cpu_job j = { w, h, steps, 0, PTHREAD_MUTEX_INITIALIZER };
	long np = sysconf(_SC_NPROCESSORS_ONLN);
	int n = (int)(np > 0 ? np : 1);
	if (n > 8) {
		n = 8;   /* the same cap the room bake uses */
	}
	double t0 = now_sec();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < n; i++) {
		if (pthread_create(&th[started], NULL, c_worker, &j) == 0) {
			started++;
		}
	}
	c_worker(&j);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
	return now_sec() - t0;
}

/* DESK_WOOD and DESK_STAIN, as shell.conf says them: the desk traced both ways, and laid from its
 * faces, of that (the faces' pictures themselves are always baked of the desk as it was made). */
static void wood_from_env(void)
{
	int wood = DESK_WOOD_WALNUT, stain = DESK_STAIN_NATURAL;
	if (getenv("DESK_WOOD") && !desk_wood_parse(getenv("DESK_WOOD"), &wood)) {
		fprintf(stderr, "desk-gputest: no wood called %s\n", getenv("DESK_WOOD"));
	}
	if (getenv("DESK_STAIN") && !desk_stain_parse(getenv("DESK_STAIN"), &stain)) {
		fprintf(stderr, "desk-gputest: no stain called %s\n", getenv("DESK_STAIN"));
	}
	desk_set_wood(wood, stain);
}

int main(int argc, char **argv)
{
	desk_compat();
	if (!(argc > 1 && strcmp(argv[1], "--faces-bake") == 0)) {
		wood_from_env();
	}
	if (argc > 1 && strcmp(argv[1], "--room") == 0) {
		int rw = argc > 3 ? atoi(argv[2]) : 480;
		int rh = argc > 3 ? atoi(argv[3]) : 300;
		if (rw < 16 || rh < 16) {
			fprintf(stderr, "usage: desk-gputest --room [width height [dir]]\n");
			return 2;
		}
		return desk_room_selftest(rw, rh, argc > 4 ? argv[4] : NULL) ? 0 : 1;
	}

	if (argc > 3 && strcmp(argv[1], "--tour") == 0) {
		int tw = argc > 5 ? atoi(argv[4]) : 1920;
		int th = argc > 5 ? atoi(argv[5]) : 1080;
		if (tw < 16 || th < 16) {
			fprintf(stderr, "usage: desk-gputest --tour PATH DIR [width height]\n");
			return 2;
		}
		return desk_room_tour(argv[2], argv[3], tw, th) ? 0 : 1;
	}

	if (argc > 2 && strcmp(argv[1], "--faces-bake") == 0) {
		return desk_room_faces_bake(argv[2], argc > 3 ? atoi(argv[3]) : 1920,
			argc > 4 ? atoi(argv[4]) : 3) ? 0 : 1;
	}
	if (argc > 1 && strcmp(argv[1], "--faces") == 0) {
		int rw = argc > 3 ? atoi(argv[2]) : 1440;
		int rh = argc > 3 ? atoi(argv[3]) : 900;
		if (rw < 16 || rh < 16) {
			fprintf(stderr, "usage: desk-gputest --faces [width height [dir]]\n");
			return 2;
		}
		return desk_room_faces_test(rw, rh, argc > 4 ? argv[4] : NULL) ? 0 : 1;
	}

	int w = argc > 2 ? atoi(argv[1]) : 2880;
	int h = argc > 2 ? atoi(argv[2]) : 1808;
	int steps = argc > 3 ? atoi(argv[3]) : 260;

	struct desk_gpu *g = desk_gpu_open();
	if (!g) {
		fprintf(stderr, "no GPU path on this machine\n");
		return 1;
	}
	printf("device : %s\n", desk_gpu_name(g));

	char err[4096];
	float *buf = malloc((size_t)w * h * 4 * sizeof *buf);
	if (!buf) {
		return 1;
	}

	/* ---- is it right ---- */
	if (!desk_gpu_program(g, CHECK_SRC, err, sizeof err)) {
		fprintf(stderr, "check shader: %s\n", err);
		return 1;
	}
	if (!desk_gpu_render(g, 64, 48, buf)) {
		fprintf(stderr, "check render failed\n");
		return 1;
	}
	double worst = 0.0;
	for (int y = 0; y < 48; y++) {
		for (int x = 0; x < 64; x++) {
			/* gl_FragCoord is pixel centres, and row 0 is the bottom --
			 * which is also the order glReadPixels hands them back, so the
			 * two cancel and the buffer is bottom-up in both */
			double px = (x + 0.5) / 64.0, py = (y + 0.5) / 48.0;
			const float *q = buf + ((size_t)y * 64 + x) * 4;
			worst = fmax(worst, fabs(q[0] - px));
			worst = fmax(worst, fabs(q[1] - py));
			worst = fmax(worst, fabs(q[2] - px * py));
			worst = fmax(worst, fabs(q[3] - 1.0));
		}
	}
	/* a 16F target carries eleven bits, and 1e-5 is more than it has */
	double tol = strstr(desk_gpu_name(g), "16F") ? 1e-3 : 1e-5;
	printf("readback: worst error %.3g  %s\n", worst,
		worst < tol ? "-- the pipeline is sound" : "-- WRONG");

	/* ---- how fast ---- */
	if (!desk_gpu_program(g, MARCH_SRC, err, sizeof err)) {
		fprintf(stderr, "march shader: %s\n", err);
		return 1;
	}
	desk_gpu_set_int(g, "u_steps", steps);
	desk_gpu_render(g, w, h, buf);          /* warm: compile, allocate */
	double t0 = now_sec();
	const int N = 3;
	for (int i = 0; i < N; i++) {
		if (!desk_gpu_render(g, w, h, buf)) {
			fprintf(stderr, "march render failed\n");
			return 1;
		}
	}
	double each = (now_sec() - t0) / N;
	printf("march   : %d x %d, up to %d steps\n", w, h, steps);
	printf("  gpu   : %.3f s a sheet\n", each);

	g_single = 0;
	double cpu = cpu_march(w, h, steps);
	g_single = 1;
	double cpuf = cpu_march(w, h, steps);
	int nt = (int)(sysconf(_SC_NPROCESSORS_ONLN) > 8 ? 8 : sysconf(_SC_NPROCESSORS_ONLN));
	printf("  cpu   : %.3f s a sheet   double, %d threads -- what the room does now\n",
		cpu, nt);
	printf("  cpu f : %.3f s a sheet   the same in float (%.1fx)\n", cpuf, cpu / cpuf);
	printf("  ratio : %.1fx over the double C, %.1fx over the float C\n",
		cpu / each, cpuf / each);
	printf("\nThe scene here is three spheres and a plane, so neither number is\n"
		"the room. The ratio is what carries. The room's bake is 78 s of the\n"
		"same marching: at this ratio that is %.0f s, on this device.\n",
		78.0 / (cpu / each));
	free(buf);
	desk_gpu_close(g);
	return 0;
}
