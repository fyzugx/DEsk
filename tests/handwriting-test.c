/* What is written is read as what it is (src/handwriting.c): letters written out by a hand that
 * shakes a little, each a few times over, read against the table of the Latin letters. The table is
 * made here, from what faces this machine has, and not kept. */

#include "handwriting.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned g_seed = 20261107u;

static double shake(void)
{
	g_seed = g_seed * 1664525u + 1013904223u;
	return ((g_seed >> 8) & 0xffff) / 32767.5 - 1.0;
}

/* Strokes as a pen goes, a point every little way, from a few points; turned, sheared and moved a
 * little, and a little wobble on top. */
static int write_out(const char *spec, float *xy, struct desk_hw_stroke *st, int most_strokes)
{
	double turn = shake() * 0.08, shear = shake() * 0.12, grow = 1.0 + shake() * 0.08;
	double dx = shake() * 4.0, dy = shake() * 4.0;
	int at = 0, ns = 0;
	const char *p = spec;
	while (*p && ns < most_strokes) {
		double pts[64][2];
		int n = 0;
		for (;;) {
			char *end;
			double x = strtod(p, &end);
			if (end == p || *end != ',' || n >= 64) {
				break;
			}
			p = end + 1;
			double y = strtod(p, &end);
			p = end;
			pts[n][0] = x;
			pts[n][1] = y;
			n++;
			if (*p != ' ') {
				break;
			}
			p++;
		}
		st[ns].xy = xy + at;
		int m = 0;
		for (int i = 0; i < n; i++) {
			int steps = i + 1 < n ? (int)ceil(hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]) / 2.0) : 1;
			for (int k = 0; k < steps; k++) {
				double t = i + 1 < n ? (double)k / steps : 0.0;
				double x = pts[i][0] + (i + 1 < n ? (pts[i + 1][0] - pts[i][0]) * t : 0.0) - 50.0;
				double y = pts[i][1] + (i + 1 < n ? (pts[i + 1][1] - pts[i][1]) * t : 0.0) - 50.0;
				double xr = (x * cos(turn) - y * sin(turn) + y * shear) * grow;
				double yr = (x * sin(turn) + y * cos(turn)) * grow;
				xy[at++] = (float)(50.0 + xr + dx + shake() * 0.6);
				xy[at++] = (float)(50.0 + yr + dy + shake() * 0.6);
				m++;
			}
		}
		st[ns].n = m;
		ns++;
		if (*p == ';') {
			p++;
		} else {
			break;
		}
	}
	return ns;
}

int main(void)
{
	static const struct {
		const char *want;
		const char *strokes;
	} SAMPLES[] = {
		{ "H", "28,12 30,85;70,10 68,88;30,48 68,46" },
		{ "e", "30,60 66,58 62,44 48,38 34,44 28,62 36,80 52,84 66,76" },
		{ "l", "48,12 47,84" },
		{ "o", "50,38 34,46 28,62 34,78 50,84 64,78 70,60 62,44 50,38" },
		{ "A", "26,82 48,14 70,82;34,58 62,58" },
		{ "k", "32,14 32,82;62,40 34,62;42,54 64,82" },
		{ "7", "28,16 68,16 42,82" },
		{ "4", "56,14 26,60 70,60;56,14 56,82" },
		{ "?", "32,28 46,15 62,24 58,40 48,50 48,62;48,76 48,79" },
		{ "x", "30,42 64,80;64,42 30,80" },
	};
	const char *const latin[] = { "en", NULL };
	setenv("XDG_CACHE_HOME", "/nonexistent", 1);
	desk_hw_want(latin);
	for (int i = 0; i < 600 && !desk_hw_ready(); i++) {
		nanosleep(&(struct timespec){ 0, 20000000 }, NULL);
	}
	if (!desk_hw_ready()) {
		fprintf(stderr, "handwriting: no table\n");
		return 1;
	}
	/* (the sheet in the typewriter shows nine to choose from) */
	int tried = 0, first = 0, near = 0;
	for (size_t s = 0; s < sizeof SAMPLES / sizeof *SAMPLES; s++) {
		for (int again = 0; again < 6; again++) {
			float xy[4000];
			struct desk_hw_stroke st[8];
			int n = write_out(SAMPLES[s].strokes, xy, st, 8);
			struct desk_hw_guess out[9];
			int got = desk_hw_read(st, n, 100.0, 100.0, out, 9);
			tried++;
			for (int k = 0; k < got; k++) {
				if (!strcmp(out[k].text, SAMPLES[s].want)) {
					first += k == 0;
					near += k < 9;
					break;
				}
			}
			if (got == 0 || strcmp(out[0].text, SAMPLES[s].want)) {
				printf("%s read as %s\n", SAMPLES[s].want, got ? out[0].text : "nothing");
			}
		}
	}
	printf("handwriting: %d of %d first, %d among the nine shown\n", first, tried, near);
	desk_hw_drop();
	/* (letters an upright and a round alike can be either: but nearly all of them first) */
	return first * 10 >= tried * 9 && near == tried ? 0 : 1;
}
