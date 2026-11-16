/* render.c -- for bake.py: play a Faust DSP compiled to C (faust -lang c -cn sfx) to a stereo 32-bit float
 * WAV, following a score: its buttons pressed and sliders moved at given times. Built once per DSP:
 *   cc -O2 -I<faust>/include -DDSP_C='"x.c"' render.c -o x -lm
 *   x SCORE OUT.wav SECONDS [PREROLL_SAMPLES]
 * Score lines (# comments):  T LABEL VALUE          set at T seconds (a button: 1 at T, back to 0 a block later)
 *                            T0 T1 LABEL V0 V1      slid from V0 at T0 to V1 at T1, a block at a time
 * The preroll runs the DSP that many samples first with nothing pressed, so that its noise is somewhere
 * else in its sequence: the same sound, another take. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faust/gui/CInterface.h"
#define FAUSTFLOAT float
#include DSP_C

#define SR 48000
#define BLOCK 16
#define MAXZ 64

static struct zone { char label[64]; FAUSTFLOAT *z; int button; } g_z[MAXZ];
static int g_nz;

static void add(const char *label, FAUSTFLOAT *zone, int button)
{
	if (g_nz < MAXZ) {
		snprintf(g_z[g_nz].label, sizeof g_z[g_nz].label, "%s", label);
		g_z[g_nz].z = zone;
		g_z[g_nz].button = button;
		g_nz++;
	}
}
static void ui_button(void *u, const char *l, FAUSTFLOAT *z) { (void)u; add(l, z, 1); }
static void ui_slider(void *u, const char *l, FAUSTFLOAT *z, FAUSTFLOAT i, FAUSTFLOAT a, FAUSTFLOAT b, FAUSTFLOAT s)
{ (void)u; (void)a; (void)b; (void)s; *z = i; add(l, z, 0); }
static void ui_box(void *u, const char *l) { (void)u; (void)l; }
static void ui_close(void *u) { (void)u; }
static void ui_bar(void *u, const char *l, FAUSTFLOAT *z, FAUSTFLOAT a, FAUSTFLOAT b) { (void)u; (void)l; (void)z; (void)a; (void)b; }
static void ui_sf(void *u, const char *l, const char *url, struct Soundfile **sf) { (void)u; (void)l; (void)url; (void)sf; }
static void ui_decl(void *u, FAUSTFLOAT *z, const char *k, const char *v) { (void)u; (void)z; (void)k; (void)v; }

static struct zone *find(const char *label)
{
	for (int i = 0; i < g_nz; i++) {
		if (!strcmp(g_z[i].label, label)) {
			return &g_z[i];
		}
	}
	fprintf(stderr, "no control called %s\n", label);
	exit(2);
}

struct ev { double t0, t1, v0, v1; struct zone *zn; };

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s SCORE OUT.wav SECONDS [PREROLL]\n", argv[0]);
		return 2;
	}
	double secs = atof(argv[3]);
	long pre = argc > 4 ? atol(argv[4]) : 0;
	sfx *d = newsfx();
	initsfx(d, SR);
	UIGlue ui = { 0 };
	ui.openTabBox = ui.openHorizontalBox = ui.openVerticalBox = ui_box;
	ui.closeBox = ui_close;
	ui.addButton = ui.addCheckButton = ui_button;
	ui.addVerticalSlider = ui.addHorizontalSlider = ui.addNumEntry = ui_slider;
	ui.addHorizontalBargraph = ui.addVerticalBargraph = ui_bar;
	ui.addSoundfile = ui_sf;
	ui.declare = ui_decl;
	buildUserInterfacesfx(d, &ui);
	if (getNumOutputssfx(d) != 2 || getNumInputssfx(d) != 0) {
		fprintf(stderr, "wants a DSP with no inputs and two outputs\n");
		return 2;
	}
	struct ev ev[512];
	int nev = 0;
	FILE *sf = fopen(argv[1], "r");
	if (!sf) {
		perror(argv[1]);
		return 2;
	}
	char line[256];
	while (fgets(line, sizeof line, sf) && nev < 512) {
		char lab[64];
		double a, b, c, e;
		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		if (sscanf(line, "%lf %lf %63s %lf %lf", &a, &b, lab, &c, &e) == 5) {
			ev[nev++] = (struct ev){ a, b, c, e, find(lab) };
		} else if (sscanf(line, "%lf %63s %lf", &a, lab, &c) == 3) {
			ev[nev++] = (struct ev){ a, -1.0, c, c, find(lab) };
		}
	}
	fclose(sf);
	float l[BLOCK], r[BLOCK];
	float *outs[2] = { l, r };
	for (long done = 0; done < pre; done += BLOCK) {
		computesfx(d, BLOCK, NULL, outs);
	}
	long n = (long)(secs * SR);
	float *out = calloc((size_t)n * 2, sizeof *out);
	for (long at = 0; at < n; at += BLOCK) {
		double t = (double)at / SR, tn = (double)(at + BLOCK) / SR;
		for (int k = 0; k < g_nz; k++) {
			if (g_z[k].button) {
				*g_z[k].z = 0.0f;
			}
		}
		for (int k = 0; k < nev; k++) {
			struct ev *e = &ev[k];
			if (e->t1 < 0.0) {
				if (e->zn->button) {
					if (e->t0 >= t && e->t0 < tn) {
						*e->zn->z = (FAUSTFLOAT)e->v0;
					}
				} else if (e->t0 < tn && e->t0 >= t - 1e-9) {
					*e->zn->z = (FAUSTFLOAT)e->v0;
				}
			} else if (t >= e->t0 && t <= e->t1) {
				double u = (t - e->t0) / fmax(e->t1 - e->t0, 1e-9);
				*e->zn->z = (FAUSTFLOAT)(e->v0 + (e->v1 - e->v0) * u);
			}
		}
		int m = n - at < BLOCK ? (int)(n - at) : BLOCK;
		computesfx(d, m, NULL, outs);
		for (int i = 0; i < m; i++) {
			out[(at + i) * 2] = l[i];
			out[(at + i) * 2 + 1] = r[i];
		}
	}
	FILE *f = fopen(argv[2], "wb");
	unsigned bytes = (unsigned)(n * 2 * 4);
	unsigned hdr[] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020003, SR, SR * 8, 0x00200008,
		0x61746164, bytes };
	fwrite(hdr, 4, 11, f);
	fwrite(out, 4, (size_t)n * 2, f);
	fclose(f);
	double peak = 0;
	for (long i = 0; i < n * 2; i++) {
		peak = fmax(peak, fabs(out[i]));
	}
	printf("%s: %.2f s, peak %.3f\n", argv[2], secs, peak);
	deletesfx(d);
	return 0;
}
