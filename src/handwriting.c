/* Reading what is written with a pen (handwriting.h).
 *
 * A character is drawn on a small square, scaled by how its ink is spread rather than by its
 * extremes, so that one long tail does not shrink all the rest; then what is kept of it is, for
 * each of eight parts by eight of the square, how much of its edges go each of eight ways. This
 * says a good deal about the shape of a character and little about whose hand it is in, or which
 * face. Two characters are as alike as those numbers are, and a character written is what it
 * is most like, with something off for being far too big or small for what it would be, or in
 * the wrong part of the box: so that a dot is a full stop or a dot on an i, and o is told from O.
 *
 * The table is made from the faces on the machine, for the sets of characters the old national
 * encodings have in their first levels, which were chosen for being what is used (through iconv,
 * so that there is no list to keep here). Some Latin letters are also written out below, as a pen
 * does them: faces do not write a or g as most people do. */

#define _GNU_SOURCE
#include "handwriting.h"

#include <cairo.h>
#include <errno.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <iconv.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SIDE 48
#define GRID 8
#define WAYS 8
#define DIM (GRID * GRID * WAYS)
/* The long side of a character's ink, on the square */
#define REACH 42.0
#define INK_WIDTH 2.7
#define TABLE_VERSION 5
#define PI 3.14159265358979323846

struct shape {
	char text[8];
	uint8_t w, h, cy, flags;   /* its ink in the box, in 255ths */
	uint8_t vec[DIM];
};

enum { FLAG_NO_SIZE = 1 };

static struct {
	atomic_int state;          /* 0 nothing, 1 being made, 2 there */
	char asked[64];
	struct shape *shapes;
	int n;
} g;

/* ---- letters as a pen writes them ------------------------------------------
 *
 * In a box a hundred square: the foot of a letter at 80, the top of a small letter at 40, of a
 * tall one at 15. Strokes are split by ';'. */

static const struct {
	const char *text;
	const char *strokes;
} WRITTEN[] = {
	{ "a", "30,46 42,40 58,42 64,52 64,80;64,58 42,58 30,66 32,78 46,82 64,72" },
	{ "a", "64,46 48,40 34,46 28,62 34,78 48,82 62,72;64,42 64,80" },
	{ "a", "64,46 48,40 34,46 28,62 34,78 48,82 62,72 64,42 64,80" },
	{ "b", "30,15 30,80;30,50 44,40 60,44 66,60 60,76 44,82 30,72" },
	{ "b", "30,15 30,78 44,82 60,76 66,60 60,44 44,40 30,52" },
	{ "c", "64,46 50,40 36,46 30,60 36,76 50,82 64,74" },
	{ "d", "64,46 48,40 34,46 28,62 34,78 48,82 64,70;64,15 64,80" },
	{ "d", "64,46 48,40 34,46 28,62 34,78 48,82 62,72 64,15 64,80" },
	{ "e", "30,60 64,60 62,46 48,40 34,46 28,62 36,78 50,82 64,74" },
	{ "f", "62,18 50,14 44,22 44,80;30,42 60,42" },
	{ "g", "64,46 48,40 34,46 28,60 34,74 48,78 64,66;64,42 64,90 54,98 38,96" },
	{ "g", "64,46 48,40 34,46 28,60 34,74 48,78 62,66 64,42 64,90 54,98 38,96" },
	{ "h", "30,15 30,80;30,52 44,40 60,44 64,56 64,80" },
	{ "h", "30,15 30,80 30,52 44,40 60,44 64,56 64,80" },
	{ "i", "46,42 46,80;46,24 46,27" },
	{ "j", "52,42 52,90 44,98 32,94;52,24 52,27" },
	{ "k", "32,15 32,80;62,40 32,62;42,54 64,80" },
	{ "k", "32,15 32,80;62,40 34,60 64,80" },
	{ "l", "46,15 46,80" },
	{ "l", "42,15 42,70 50,80 60,78" },
	{ "m", "22,42 22,80;22,52 32,42 42,46 44,56 44,80;44,52 56,42 68,46 70,56 70,80" },
	{ "m", "22,42 22,80 22,52 32,42 42,46 44,56 44,80 44,52 56,42 68,46 70,56 70,80" },
	{ "n", "30,42 30,80;30,52 44,40 60,44 64,56 64,80" },
	{ "n", "30,42 30,80 30,52 44,40 60,44 64,56 64,80" },
	{ "o", "48,40 34,46 28,60 34,76 48,82 62,76 68,60 62,46 48,40" },
	{ "p", "30,42 30,98;30,52 44,40 60,44 66,60 60,76 44,82 30,72" },
	{ "p", "30,42 30,98 30,50 44,40 60,44 66,60 60,76 44,82 30,72" },
	{ "q", "64,46 48,40 34,46 28,60 34,76 48,82 64,70;64,42 64,98" },
	{ "r", "34,42 34,80;34,54 46,42 62,42" },
	{ "r", "34,42 34,80 34,54 46,42 62,42" },
	{ "s", "62,46 48,40 34,44 34,54 48,60 62,66 62,76 48,82 32,76" },
	{ "t", "44,20 44,74 52,82 62,78;28,42 60,42" },
	{ "t", "44,20 44,80;28,42 60,42" },
	{ "u", "30,42 30,68 38,80 52,80 64,68;64,42 64,80" },
	{ "u", "30,42 30,68 38,80 52,80 64,68 64,42 64,80" },
	{ "v", "28,42 46,80 66,42" },
	{ "w", "20,42 34,80 46,52 58,80 72,42" },
	{ "x", "30,42 64,80;64,42 30,80" },
	{ "y", "30,42 46,76;64,42 46,76 36,96" },
	{ "y", "30,42 30,66 40,76 54,74 64,62;64,42 64,90 54,98 38,96" },
	{ "z", "30,42 64,42 30,80 64,80" },
	{ "A", "48,15 26,80;48,15 70,80;34,58 62,58" },
	{ "A", "26,80 48,15 70,80;34,58 62,58" },
	{ "B", "30,15 30,80;30,15 56,15 66,24 62,40 30,46;30,46 62,46 70,60 62,76 30,80" },
	{ "B", "30,15 30,80;30,15 56,15 66,24 62,40 30,46 62,46 70,60 62,76 30,80" },
	{ "C", "68,26 54,15 36,20 26,46 34,72 52,82 68,72" },
	{ "D", "30,15 30,80;30,15 52,16 68,34 68,60 52,78 30,80" },
	{ "E", "64,15 30,15 30,80 64,80;30,46 58,46" },
	{ "E", "30,15 30,80;30,15 64,15;30,46 58,46;30,80 64,80" },
	{ "F", "64,15 30,15 30,80;30,46 58,46" },
	{ "F", "30,15 30,80;30,15 64,15;30,46 58,46" },
	{ "G", "68,26 54,15 36,20 26,46 34,72 52,82 68,72 68,50 50,50" },
	{ "H", "30,15 30,80;66,15 66,80;30,46 66,46" },
	{ "I", "48,15 48,80" },
	{ "I", "34,15 62,15;48,15 48,80;34,80 62,80" },
	{ "J", "60,15 60,68 48,82 32,74" },
	{ "K", "30,15 30,80;66,15 30,50;40,42 68,80" },
	{ "K", "30,15 30,80;66,15 32,50 68,80" },
	{ "L", "32,15 32,80 64,80" },
	{ "M", "22,80 22,15 46,60 70,15 70,80" },
	{ "M", "22,15 22,80;22,15 46,60 70,15;70,15 70,80" },
	{ "N", "28,80 28,15 66,80 66,15" },
	{ "N", "28,15 28,80;28,15 66,80;66,15 66,80" },
	{ "O", "48,15 30,24 24,48 32,72 48,82 64,72 72,48 64,24 48,15" },
	{ "P", "30,15 30,80;30,15 56,15 66,28 56,44 30,46" },
	{ "Q", "48,15 30,24 24,48 32,72 48,82 64,72 72,48 64,24 48,15;56,66 72,84" },
	{ "R", "30,15 30,80;30,15 56,15 66,28 56,44 30,46;44,46 68,80" },
	{ "R", "30,15 30,80;30,15 56,15 66,28 56,44 30,46 68,80" },
	{ "S", "66,24 50,15 34,20 32,36 48,46 64,56 66,72 50,82 30,74" },
	{ "T", "26,15 70,15;48,15 48,80" },
	{ "U", "30,15 30,62 40,80 56,80 66,62 66,15" },
	{ "V", "26,15 48,80 70,15" },
	{ "W", "18,15 32,80 46,30 60,80 74,15" },
	{ "X", "28,15 68,80;68,15 28,80" },
	{ "Y", "26,15 48,46;70,15 48,46 48,80" },
	{ "Z", "28,15 68,15 28,80 68,80" },
	{ "0", "48,15 32,24 28,48 32,72 48,82 62,72 68,48 62,24 48,15" },
	{ "1", "36,26 50,15 50,80" },
	{ "1", "48,15 48,80" },
	{ "2", "30,30 40,16 58,16 66,30 56,50 30,80 68,80" },
	{ "3", "32,22 46,15 62,22 62,36 46,46 64,54 66,70 50,82 30,74" },
	{ "4", "56,15 26,60 70,60;56,15 56,80" },
	{ "4", "30,15 28,50 66,50;58,28 58,80" },
	{ "5", "62,15 36,15 32,44 48,40 64,50 66,68 50,82 30,74" },
	{ "5", "36,15 32,44 48,40 64,50 66,68 50,82 30,74;36,15 62,15" },
	{ "6", "60,15 40,30 30,56 38,78 54,82 66,68 58,50 40,50 30,62" },
	{ "7", "28,15 68,15 42,80" },
	{ "7", "28,15 68,15 42,80;36,48 60,48" },
	{ "8", "48,46 32,34 36,20 48,15 60,20 64,34 48,46 30,60 34,76 48,82 62,76 66,60 48,46" },
	{ "9", "62,30 50,15 34,22 32,38 46,46 62,36;62,22 62,80" },
	{ "9", "62,30 50,15 34,22 32,38 46,46 62,36 62,22 62,60 54,78 38,80" },
	{ ".", "46,76 47,78" },
	{ ",", "50,74 46,88" },
	{ "-", "30,50 66,50" },
	{ "_", "28,90 68,90" },
	{ "/", "62,15 34,85" },
	{ "\\", "34,15 62,85" },
	{ "?", "32,28 46,15 62,24 58,40 48,50 48,62;48,76 48,78" },
	{ "!", "48,15 48,60;48,76 48,78" },
	{ "'", "48,15 46,30" },
	{ "\"", "42,15 40,30;54,15 52,30" },
	{ ":", "48,46 48,48;48,74 48,76" },
	{ ";", "50,46 50,48;50,72 46,86" },
	{ "+", "28,50 68,50;48,30 48,70" },
	{ "=", "30,42 66,42;30,60 66,60" },
	{ "(", "58,12 44,30 40,50 44,72 58,90" },
	{ ")", "40,12 54,30 58,50 54,72 40,90" },
	{ "*", "48,30 48,60;34,38 62,52;62,38 34,52" },
	{ "#", "40,22 34,76;60,22 54,76;26,40 68,40;24,58 66,58" },
	{ "&", "62,80 34,36 40,20 54,20 56,34 30,58 32,76 48,82 66,56" },
	{ "@", "60,40 48,34 38,44 40,56 52,58 60,40 62,56 72,50 72,36 58,20 38,20 24,38 24,60 38,78 60,80" },
};

/* Letters with something over them: the letter without its dot, and what is over it. */
static const struct {
	const char *text, *base, *mark;
} MARKED[] = {
#define OVER_ACUTE "44,28 56,16"
#define OVER_GRAVE "42,16 54,28"
#define OVER_HAT "36,28 48,16 60,28"
#define OVER_DOTS "38,24 38,26;58,24 58,26"
#define OVER_TILDE "34,26 44,18 54,26 64,18"
#define LET_A "64,46 48,40 34,46 28,62 34,78 48,82 62,72;64,42 64,80"
#define LET_E "30,60 64,60 62,46 48,40 34,46 28,62 36,78 50,82 64,74"
#define LET_I "46,42 46,80"
#define LET_O "48,40 34,46 28,60 34,76 48,82 62,76 68,60 62,46 48,40"
#define LET_U "30,42 30,68 38,80 52,80 64,68;64,42 64,80"
#define LET_N "30,42 30,80;30,52 44,40 60,44 64,56 64,80"
	{ "á", LET_A, OVER_ACUTE }, { "à", LET_A, OVER_GRAVE }, { "â", LET_A, OVER_HAT }, { "ä", LET_A, OVER_DOTS },
	{ "é", LET_E, OVER_ACUTE }, { "è", LET_E, OVER_GRAVE }, { "ê", LET_E, OVER_HAT }, { "ë", LET_E, OVER_DOTS },
	{ "í", LET_I, OVER_ACUTE }, { "î", LET_I, OVER_HAT }, { "ï", LET_I, OVER_DOTS },
	{ "ó", LET_O, OVER_ACUTE }, { "ô", LET_O, OVER_HAT }, { "ö", LET_O, OVER_DOTS },
	{ "ú", LET_U, OVER_ACUTE }, { "ù", LET_U, OVER_GRAVE }, { "û", LET_U, OVER_HAT }, { "ü", LET_U, OVER_DOTS },
	{ "ñ", LET_N, OVER_TILDE },
};

/* What each of the languages adds to the plain Latin letters, printed. */
static const struct {
	const char *lang, *extra;
} EXTRA[] = {
	{ "es", "áéíóúüñÁÉÍÓÚÜÑ¿¡" },
	{ "fr", "àâæçéèêëîïôœùûüÿÀÂÆÇÉÈÊËÎÏÔŒÙÛÜŸ" },
	{ "de", "äöüßÄÖÜ" },
};

/* ---- a character on the square --------------------------------------------- */

struct spread {
	double mx, my, sx, sy;     /* where its ink is, and how far it is spread */
};

/* From how a character's ink is spread, how it goes on the square: what is long stays long, but
 * not so very much longer than it is wide. */
static void spread_scale(const struct spread *s, double *ax, double *ay)
{
	double lo = fmin(s->sx, s->sy), hi = fmax(s->sx, s->sy);
	hi = hi < 1e-3 ? 1e-3 : hi;
	double r = lo / hi;
	double kept = sqrt(sin(PI * 0.5 * r));
	kept = kept < 0.12 ? 0.12 : kept;
	double long_k = REACH / (4.0 * hi);
	double short_k = lo > 1e-3 ? REACH * kept / (4.0 * lo) : long_k;
	short_k = short_k > long_k * 4.0 ? long_k * 4.0 : short_k;
	*ax = s->sx >= s->sy ? long_k : short_k;
	*ay = s->sx >= s->sy ? short_k : long_k;
}

/* What is kept of the picture on the square. */
static void features(cairo_surface_t *img, float *out)
{
	cairo_surface_flush(img);
	const unsigned char *d = cairo_image_surface_get_data(img);
	int st = cairo_image_surface_get_stride(img);
	float planes[DIM];
	memset(planes, 0, sizeof planes);
	/* blurred a little first: a line's edge goes one way, not two */
	float b[SIDE][SIDE];
	for (int y = 0; y < SIDE; y++) {
		for (int x = 0; x < SIDE; x++) {
			int x0 = x > 0 ? x - 1 : x, x1 = x < SIDE - 1 ? x + 1 : x;
			int y0 = y > 0 ? y - 1 : y, y1 = y < SIDE - 1 ? y + 1 : y;
			const unsigned char *r0 = d + y0 * st, *r1 = d + y * st, *r2 = d + y1 * st;
			b[y][x] = (r0[x0] + 2.0f * r0[x] + r0[x1] + 2.0f * r1[x0] + 4.0f * r1[x] + 2.0f * r1[x1]
				+ r2[x0] + 2.0f * r2[x] + r2[x1]) / (16.0f * 255.0f);
		}
	}
	double cell = (double)SIDE / GRID;
	for (int y = 1; y < SIDE - 1; y++) {
		for (int x = 1; x < SIDE - 1; x++) {
			float gx = (b[y - 1][x + 1] + 2.0f * b[y][x + 1] + b[y + 1][x + 1])
				- (b[y - 1][x - 1] + 2.0f * b[y][x - 1] + b[y + 1][x - 1]);
			float gy = (b[y + 1][x - 1] + 2.0f * b[y + 1][x] + b[y + 1][x + 1])
				- (b[y - 1][x - 1] + 2.0f * b[y - 1][x] + b[y - 1][x + 1]);
			float m = sqrtf(gx * gx + gy * gy);
			if (m < 0.02f) {
				continue;
			}
			double way = (atan2(gy, gx) + PI) / (2.0 * PI) * WAYS;
			int w0 = (int)floor(way);
			double wf = way - w0;
			w0 = (w0 % WAYS + WAYS) % WAYS;
			int w1 = (w0 + 1) % WAYS;
			double fx = (x + 0.5) / cell - 0.5, fy = (y + 0.5) / cell - 0.5;
			int cx = (int)floor(fx), cy = (int)floor(fy);
			double tx = fx - cx, ty = fy - cy;
			for (int j = 0; j < 2; j++) {
				int gy2 = cy + j;
				if (gy2 < 0 || gy2 >= GRID) {
					continue;
				}
				double wy = j ? ty : 1.0 - ty;
				for (int i = 0; i < 2; i++) {
					int gx2 = cx + i;
					if (gx2 < 0 || gx2 >= GRID) {
						continue;
					}
					double wx = i ? tx : 1.0 - tx;
					float *p = &planes[(gy2 * GRID + gx2) * WAYS];
					p[w0] += (float)(m * wx * wy * (1.0 - wf));
					p[w1] += (float)(m * wx * wy * wf);
				}
			}
		}
	}
	double sum = 0.0;
	for (int i = 0; i < DIM; i++) {
		out[i] = sqrtf(planes[i]);
		sum += out[i] * out[i];
	}
	sum = sum > 1e-9 ? sqrt(sum) : 1.0;
	for (int i = 0; i < DIM; i++) {
		out[i] = (float)(out[i] / sum);
	}
}

static void vec_keep(const float *f, uint8_t *v)
{
	for (int i = 0; i < DIM; i++) {
		double q = f[i] * 512.0;
		v[i] = (uint8_t)(q > 255.0 ? 255.0 : q);
	}
}

/* Strokes, from middle to middle of what is between the points, as ink goes. */
static void strokes_path(cairo_t *cr, const struct desk_hw_stroke *st, int n, double mx, double my, double ax, double ay)
{
	for (int s = 0; s < n; s++) {
		const float *p = st[s].xy;
		int m = st[s].n;
		if (m < 1) {
			continue;
		}
#define PX(i) (SIDE * 0.5 + (p[(i) * 2] - mx) * ax)
#define PY(i) (SIDE * 0.5 + (p[(i) * 2 + 1] - my) * ay)
		cairo_move_to(cr, PX(0), PY(0));
		if (m == 1) {
			cairo_line_to(cr, PX(0) + 0.01, PY(0));
		}
		for (int i = 1; i + 1 < m; i++) {
			double mx1 = (PX(i) + PX(i + 1)) * 0.5, my1 = (PY(i) + PY(i + 1)) * 0.5;
			double cx0, cy0;
			cairo_get_current_point(cr, &cx0, &cy0);
			cairo_curve_to(cr, cx0 + (PX(i) - cx0) * 2.0 / 3.0, cy0 + (PY(i) - cy0) * 2.0 / 3.0,
				mx1 + (PX(i) - mx1) * 2.0 / 3.0, my1 + (PY(i) - my1) * 2.0 / 3.0, mx1, my1);
		}
		if (m >= 2) {
			cairo_line_to(cr, PX(m - 1), PY(m - 1));
		}
#undef PX
#undef PY
	}
}

/* How ink along strokes is spread. */
static struct spread strokes_spread(const struct desk_hw_stroke *st, int n, double *x0, double *y0, double *x1, double *y1)
{
	double sum = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0;
	*x0 = *y0 = 1e30;
	*x1 = *y1 = -1e30;
	for (int s = 0; s < n; s++) {
		const float *p = st[s].xy;
		for (int i = 0; i < st[s].n; i++) {
			double x = p[i * 2], y = p[i * 2 + 1];
			*x0 = fmin(*x0, x);
			*x1 = fmax(*x1, x);
			*y0 = fmin(*y0, y);
			*y1 = fmax(*y1, y);
			double len = 0.3;
			if (i + 1 < st[s].n) {
				len += hypot(p[i * 2 + 2] - x, p[i * 2 + 3] - y);
			}
			sum += len;
			sx += x * len;
			sy += y * len;
			sxx += x * x * len;
			syy += y * y * len;
		}
	}
	struct spread r = { 0 };
	if (sum > 0.0) {
		r.mx = sx / sum;
		r.my = sy / sum;
		r.sx = sqrt(fmax(sxx / sum - r.mx * r.mx, 0.0));
		r.sy = sqrt(fmax(syy / sum - r.my * r.my, 0.0));
	}
	return r;
}

static void strokes_features(const struct desk_hw_stroke *st, int n, float *out, double *x0, double *y0, double *x1, double *y1)
{
	struct spread s = strokes_spread(st, n, x0, y0, x1, y1);
	double ax, ay;
	spread_scale(&s, &ax, &ay);
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_A8, SIDE, SIDE);
	cairo_t *cr = cairo_create(img);
	cairo_set_line_width(cr, INK_WIDTH);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	strokes_path(cr, st, n, s.mx, s.my, ax, ay);
	cairo_stroke(cr);
	cairo_destroy(cr);
	features(img, out);
	cairo_surface_destroy(img);
}

/* A face on this machine, by the name of its family: at 56 pixels to the em. Read with a FreeType of
 * its own, not through cairo's faces: the table is made on a thread of its own, and cairo's faces are
 * the ones the desk's text is drawn with meanwhile (one closing a face as the other reads it). */
struct face {
	FT_Library lib;
	FT_Face ft;
};

#define EM 56.0
#define MOST_GLYPHS 8

static struct face face_open(const char *family)
{
	struct face f = { NULL, NULL };
	if (FT_Init_FreeType(&f.lib) != 0) {
		f.lib = NULL;
		return f;
	}
	/* the face cairo would give for it: upright, of the weight it calls normal */
	FcPattern *pat = FcPatternCreate();
	if (!pat) {
		return f;
	}
	FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
	FcPatternAddInteger(pat, FC_SLANT, FC_SLANT_ROMAN);
	FcPatternAddInteger(pat, FC_WEIGHT, FC_WEIGHT_MEDIUM);
	FcPatternAddDouble(pat, FC_PIXEL_SIZE, EM);
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult res;
	FcPattern *m = FcFontMatch(NULL, pat, &res);
	FcPatternDestroy(pat);
	FcChar8 *file = NULL;
	int index = 0;
	if (m && FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch) {
		FcPatternGetInteger(m, FC_INDEX, 0, &index);
		if (FT_New_Face(f.lib, (const char *)file, index, &f.ft) != 0) {
			f.ft = NULL;
		} else if (FT_Set_Char_Size(f.ft, 0, (FT_F26Dot6)(EM * 64.0), 72, 72) != 0) {
			FT_Done_Face(f.ft);
			f.ft = NULL;
		}
	}
	if (m) {
		FcPatternDestroy(m);
	}
	return f;
}

static void face_close(struct face *f)
{
	if (f->ft) {
		FT_Done_Face(f->ft);
	}
	if (f->lib) {
		FT_Done_FreeType(f->lib);
	}
}

/* An outline into the path, where the pen is: FreeType's y goes up, cairo's down. */
static int ol_move(const FT_Vector *to, void *cr)
{
	cairo_move_to(cr, to->x / 64.0, -to->y / 64.0);
	return 0;
}

static int ol_line(const FT_Vector *to, void *cr)
{
	cairo_line_to(cr, to->x / 64.0, -to->y / 64.0);
	return 0;
}

static int ol_conic(const FT_Vector *c, const FT_Vector *to, void *cr)
{
	double x0, y0;
	cairo_get_current_point(cr, &x0, &y0);
	double cx = c->x / 64.0, cy = -c->y / 64.0, x = to->x / 64.0, y = -to->y / 64.0;
	cairo_curve_to(cr, x0 + (cx - x0) * (2.0 / 3.0), y0 + (cy - y0) * (2.0 / 3.0),
		x + (cx - x) * (2.0 / 3.0), y + (cy - y) * (2.0 / 3.0), x, y);
	return 0;
}

static int ol_cubic(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *cr)
{
	cairo_curve_to(cr, c1->x / 64.0, -c1->y / 64.0, c2->x / 64.0, -c2->y / 64.0, to->x / 64.0, -to->y / 64.0);
	return 0;
}

/* A character in a face: the same, from its picture. Where the ink is in the box, as the box a pen
 * writes it in would be, its foot a little above the bottom. False if the face has not got it, or it
 * has no ink. */
static bool glyph_features(const struct face *fc, const char *text, float *out, double *w, double *h, double *cy,
	double (*jitter)(void))
{
	if (!fc->ft) {
		return false;
	}
	/* its glyphs, one after another along the line, and where their ink is, all of them */
	FT_UInt gl[MOST_GLYPHS];
	double at[MOST_GLYPHS];
	int ng = 0;
	double pen = 0.0, x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	for (const char *p = text; *p && ng < MOST_GLYPHS;) {
		FcChar32 ch;
		int len = FcUtf8ToUcs4((const FcChar8 *)p, &ch, (int)strlen(p));
		if (len <= 0) {
			return false;
		}
		p += len;
		FT_UInt gi = FT_Get_Char_Index(fc->ft, ch);
		if (ng == 0 && gi == 0) {
			return false;
		}
		if (FT_Load_Glyph(fc->ft, gi, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0
			|| fc->ft->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
			return false;
		}
		const FT_Glyph_Metrics *m = &fc->ft->glyph->metrics;
		if (m->width > 0 && m->height > 0) {
			x0 = fmin(x0, pen + m->horiBearingX / 64.0);
			y0 = fmin(y0, -m->horiBearingY / 64.0);
			x1 = fmax(x1, pen + (m->horiBearingX + m->width) / 64.0);
			y1 = fmax(y1, (m->height - m->horiBearingY) / 64.0);
		}
		gl[ng] = gi;
		at[ng++] = pen;
		pen += m->horiAdvance / 64.0;
	}
	struct { double x_bearing, y_bearing, width, height; } e = { x0, y0, x1 - x0, y1 - y0 };
	if (ng < 1 || e.width < 0.5 || e.height < 0.5) {
		return false;
	}
	/* all of it on a picture of its own */
	enum { RAW = 96 };
	static const FT_Outline_Funcs OUTLINE = { ol_move, ol_line, ol_conic, ol_cubic, 0, 0 };
	cairo_surface_t *raw = cairo_image_surface_create(CAIRO_FORMAT_A8, RAW, RAW);
	cairo_t *cr = cairo_create(raw);
	cairo_translate(cr, (RAW - e.width) * 0.5 - e.x_bearing, (RAW - e.height) * 0.5 - e.y_bearing);
	for (int i = 0; i < ng; i++) {
		if (FT_Load_Glyph(fc->ft, gl[i], FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0
			&& fc->ft->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
			cairo_save(cr);
			cairo_translate(cr, at[i], 0.0);
			FT_Outline_Decompose(&fc->ft->glyph->outline, &OUTLINE, cr);
			cairo_restore(cr);
		}
	}
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_flush(raw);
	const unsigned char *d = cairo_image_surface_get_data(raw);
	int st = cairo_image_surface_get_stride(raw);
	double sum = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0;
	for (int y = 0; y < RAW; y++) {
		for (int x = 0; x < RAW; x++) {
			double a = d[y * st + x] / 255.0;
			if (a > 0.0) {
				sum += a;
				sx += x * a;
				sy += y * a;
				sxx += (double)x * x * a;
				syy += (double)y * y * a;
			}
		}
	}
	if (sum < 1.0) {
		cairo_surface_destroy(raw);
		return false;
	}
	struct spread s;
	s.mx = sx / sum;
	s.my = sy / sum;
	s.sx = sqrt(fmax(sxx / sum - s.mx * s.mx, 0.0));
	s.sy = sqrt(fmax(syy / sum - s.my * s.my, 0.0));
	double ax, ay;
	spread_scale(&s, &ax, &ay);
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_A8, SIDE, SIDE);
	cr = cairo_create(img);
	cairo_translate(cr, SIDE * 0.5, SIDE * 0.5);
	if (jitter) {
		cairo_rotate(cr, jitter() * 0.14);
		cairo_matrix_t shear;
		cairo_matrix_init(&shear, 1.0 + jitter() * 0.10, jitter() * 0.10, jitter() * 0.14, 1.0 + jitter() * 0.10,
			jitter() * 1.5, jitter() * 1.5);
		cairo_transform(cr, &shear);
	}
	cairo_scale(cr, ax, ay);
	cairo_translate(cr, -s.mx, -s.my);
	cairo_set_source_surface(cr, raw, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(raw);
	features(img, out);
	cairo_surface_destroy(img);
	*w = e.width / EM;
	*h = e.height / EM;
	/* (the baseline at 0.80 of the box) */
	*cy = 0.80 + (e.y_bearing + e.height * 0.5) / EM;
	return true;
}

/* ---- the table ------------------------------------------------------------- */

/* The table being made. */
static struct {
	struct shape *s;
	int n, cap;
} made;

/* A new shape in the table. (Out of memory, the same one again: the table only ends up shorter.) */
static struct shape *shape_add(const char *text)
{
	static struct shape spare;
	if (made.n == made.cap) {
		int cap = made.cap ? made.cap * 2 : 1024;
		struct shape *more = realloc(made.s, (size_t)cap * sizeof *made.s);
		if (!more) {
			return &spare;
		}
		made.s = more;
		made.cap = cap;
	}
	struct shape *s = &made.s[made.n++];
	memset(s, 0, sizeof *s);
	snprintf(s->text, sizeof s->text, "%s", text);
	return s;
}

static uint8_t to255(double v)
{
	v = v * 255.0;
	return (uint8_t)(v < 0.0 ? 0.0 : v > 255.0 ? 255.0 : v);
}

/* A stroke as a pen would make it, from the few points it is written out by here: round them where it
 * bends, and sharp where it turns. How many points came of it. */
static int stroke_fill_out(const float *p, int n, float *out, int most)
{
	int got = 0;
#define PUT(x, y) do { if (got < most) { out[got * 2] = (float)(x); out[got * 2 + 1] = (float)(y); got++; } } while (0)
	for (int i = 0; i + 1 < n; i++) {
		double x1 = p[i * 2], y1 = p[i * 2 + 1], x2 = p[i * 2 + 2], y2 = p[i * 2 + 3];
		/* what comes before and after goes into the curve, unless there is a corner there */
		double x0 = x1, y0 = y1, x3 = x2, y3 = y2;
		if (i > 0) {
			double ax = x1 - p[i * 2 - 2], ay = y1 - p[i * 2 - 1], bx = x2 - x1, by = y2 - y1;
			double c = (ax * bx + ay * by) / (hypot(ax, ay) * hypot(bx, by) + 1e-6);
			if (c > 0.55) {
				x0 = p[i * 2 - 2];
				y0 = p[i * 2 - 1];
			}
		}
		if (i + 2 < n) {
			double ax = x2 - x1, ay = y2 - y1, bx = p[i * 2 + 4] - x2, by = p[i * 2 + 5] - y2;
			double c = (ax * bx + ay * by) / (hypot(ax, ay) * hypot(bx, by) + 1e-6);
			if (c > 0.55) {
				x3 = p[i * 2 + 4];
				y3 = p[i * 2 + 5];
			}
		}
		for (int k = 0; k < 8; k++) {
			double t = k / 8.0, t2 = t * t, t3 = t2 * t;
			PUT(0.5 * (2.0 * x1 + (x2 - x0) * t + (2.0 * x0 - 5.0 * x1 + 4.0 * x2 - x3) * t2
					+ (3.0 * x1 - x0 - 3.0 * x2 + x3) * t3),
				0.5 * (2.0 * y1 + (y2 - y0) * t + (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3) * t2
					+ (3.0 * y1 - y0 - 3.0 * y2 + y3) * t3));
		}
	}
	PUT(p[(n - 1) * 2], p[(n - 1) * 2 + 1]);
#undef PUT
	return got;
}

/* Strokes written out as a string: parsed, and read as a pen's would be. */
static void written_add(const char *text, const char *strokes, const char *mark)
{
	float raw[128], xy[2048];
	struct desk_hw_stroke st[8];
	int ns = 0, at = 0, out = 0;
	char all[320];
	snprintf(all, sizeof all, "%s%s%s", strokes, mark ? ";" : "", mark ? mark : "");
	const char *p = all;
	while (*p && ns < 8) {
		int n = 0;
		for (;;) {
			char *end;
			double x = strtod(p, &end);
			if (end == p || *end != ',' || n >= 60) {
				break;
			}
			p = end + 1;
			double y = strtod(p, &end);
			p = end;
			raw[n * 2] = (float)x;
			raw[n * 2 + 1] = (float)y;
			n++;
			if (*p != ' ') {
				break;
			}
			p++;
		}
		st[ns].xy = xy + at;
		st[ns].n = stroke_fill_out(raw, n, xy + at, (2048 - at) / 2);
		at += st[ns].n * 2;
		out += st[ns].n;
		ns++;
		if (*p == ';') {
			p++;
		} else {
			break;
		}
	}
	(void)out;
	float f[DIM];
	double x0, y0, x1, y1;
	strokes_features(st, ns, f, &x0, &y0, &x1, &y1);
	struct shape *s = shape_add(text);
	vec_keep(f, s->vec);
	s->w = to255((x1 - x0) / 100.0);
	s->h = to255((y1 - y0) / 100.0);
	s->cy = to255((y0 + y1) * 0.5 / 100.0);
}

static void printed_add(const struct face *fc, const char *text)
{
	float f[DIM];
	double w, h, cy;
	if (!glyph_features(fc, text, f, &w, &h, &cy, NULL)) {
		return;
	}
	struct shape *s = shape_add(text);
	vec_keep(f, s->vec);
	s->w = to255(w);
	s->h = to255(h);
	s->cy = to255(cy);
}

/* A character in several faces, as one. */
static void printed_add_mean(const struct face *fc, int faces, const char *text)
{
	float f[DIM], sum[DIM];
	memset(sum, 0, sizeof sum);
	double w = 0.0, h = 0.0, cy = 0.0;
	int got = 0;
	for (int i = 0; i < faces; i++) {
		double a, b, c;
		if (glyph_features(&fc[i], text, f, &a, &b, &c, NULL)) {
			for (int k = 0; k < DIM; k++) {
				sum[k] += f[k];
			}
			w += a;
			h += b;
			cy += c;
			got++;
		}
	}
	if (!got) {
		return;
	}
	double len = 0.0;
	for (int k = 0; k < DIM; k++) {
		len += sum[k] * sum[k];
	}
	len = len > 1e-9 ? sqrt(len) : 1.0;
	for (int k = 0; k < DIM; k++) {
		sum[k] = (float)(sum[k] / len);
	}
	struct shape *s = shape_add(text);
	vec_keep(sum, s->vec);
	s->w = to255(w / got);
	s->h = to255(h / got);
	s->cy = to255(cy / got);
}

static void latin_add(const char *lang_extra)
{
	static const char *const FACES[] = { "Klee One", "Noto Sans", "DejaVu Sans", "Nimbus Sans" };
	for (size_t i = 0; i < sizeof WRITTEN / sizeof *WRITTEN; i++) {
		written_add(WRITTEN[i].text, WRITTEN[i].strokes, NULL);
	}
	for (size_t i = 0; i < sizeof MARKED / sizeof *MARKED; i++) {
		written_add(MARKED[i].text, MARKED[i].base, MARKED[i].mark);
	}
	for (size_t f = 0; f < sizeof FACES / sizeof *FACES; f++) {
		struct face fc = face_open(FACES[f]);
		char one[2] = { 0, 0 };
		for (int c = 33; c < 127; c++) {
			one[0] = (char)c;
			printed_add(&fc, one);
		}
		for (const char *p = lang_extra; p && *p;) {
			char cp[8] = { 0 };
			int len = (*p & 0xe0) == 0xc0 ? 2 : (*p & 0xf0) == 0xe0 ? 3 : (*p & 0xf8) == 0xf0 ? 4 : 1;
			memcpy(cp, p, (size_t)len);
			printed_add(&fc, cp);
			p += len;
		}
		face_close(&fc);
	}
}

/* The decoder in use, by the thread making the table. */
static iconv_t g_decode = (iconv_t)-1;

static bool decode_open(const char *encoding)
{
	g_decode = iconv_open("UTF-8", encoding);
	if (g_decode == (iconv_t)-1) {
		fprintf(stderr, "desk: no way to read %s, for what is written\n", encoding);
		return false;
	}
	return true;
}

static void decode_close(void)
{
	if (g_decode != (iconv_t)-1) {
		iconv_close(g_decode);
		g_decode = (iconv_t)-1;
	}
}

/* What a national encoding has in these rows and cells: as many as it will say, decoded. */
static void encoding_add(const struct face *fc, int faces, const char *encoding, int row0, int row1, int cell0, int cell1)
{
	if (!decode_open(encoding)) {
		return;
	}
	iconv_t cd = g_decode;
	for (int r = row0; r <= row1; r++) {
		for (int c = cell0; c <= cell1; c++) {
			char in[2] = { (char)(0xa0 + r), (char)(0xa0 + c) };
			char out[8] = { 0 };
			char *ip = in, *op = out;
			size_t il = 2, ol = sizeof out - 1;
			if (iconv(cd, &ip, &il, &op, &ol) == (size_t)-1 || op == out) {
				iconv(cd, NULL, NULL, NULL, NULL);
				continue;
			}
			printed_add_mean(fc, faces, out);
		}
	}
	decode_close();
}

static void cjk_add(const char *lang)
{
	struct face fc[3];
	int faces = 0;
	if (!strcmp(lang, "ja")) {
		fc[faces++] = face_open("Klee One");
		fc[faces++] = face_open("Noto Sans CJK JP");
		/* punctuation, the two kanas; and the kanji of the first level */
		encoding_add(fc, faces, "EUC-JP", 1, 1, 1, 12);
		encoding_add(fc, faces, "EUC-JP", 1, 1, 22, 30);
		encoding_add(fc, faces, "EUC-JP", 1, 1, 42, 59);
		encoding_add(fc, faces, "EUC-JP", 4, 5, 1, 86);
		encoding_add(fc, faces, "EUC-JP", 16, 47, 1, 94);
	} else if (!strcmp(lang, "zh")) {
		fc[faces++] = face_open("Noto Sans CJK SC");
		encoding_add(fc, faces, "GB2312", 1, 1, 1, 30);
		encoding_add(fc, faces, "GB2312", 16, 55, 1, 94);
	} else if (!strcmp(lang, "zh-tw")) {
		fc[faces++] = face_open("Noto Sans CJK TC");
		if (decode_open("BIG5")) {
			iconv_t cd = g_decode;
			for (int hi = 0xa4; hi <= 0xc6; hi++) {
				for (int lo = 0x40; lo <= 0xfe; lo++) {
					char in[2] = { (char)hi, (char)lo }, out[8] = { 0 };
					char *ip = in, *op = out;
					size_t il = 2, ol = sizeof out - 1;
					if (iconv(cd, &ip, &il, &op, &ol) != (size_t)-1 && op != out) {
						printed_add_mean(fc, faces, out);
					} else {
						iconv(cd, NULL, NULL, NULL, NULL);
					}
				}
			}
			decode_close();
		}
	} else if (!strcmp(lang, "ko")) {
		fc[faces++] = face_open("Noto Sans CJK KR");
		encoding_add(fc, faces, "EUC-KR", 16, 40, 1, 94);
	}
	for (int i = 0; i < faces; i++) {
		face_close(&fc[i]);
	}
}

static char *table_path(const char *asked)
{
	const char *cache = getenv("XDG_CACHE_HOME");
	const char *home = getenv("HOME");
	char dir[1024];
	if (cache && *cache) {
		snprintf(dir, sizeof dir, "%s/desk", cache);
	} else {
		snprintf(dir, sizeof dir, "%s/.cache/desk", home ? home : ".");
	}
	mkdir(dir, 0700);
	char *path = malloc(1200);
	if (path) {
		snprintf(path, 1200, "%s/handwriting-%d-%s.table", dir, TABLE_VERSION, asked);
	}
	return path;
}

static bool table_read(const char *path)
{
	FILE *f = fopen(path, "rbe");
	if (!f) {
		return false;
	}
	int n = 0;
	bool ok = fread(&n, sizeof n, 1, f) == 1 && n > 0 && n < 1000000;
	struct shape *s = ok ? malloc((size_t)n * sizeof *s) : NULL;
	ok = s && fread(s, sizeof *s, (size_t)n, f) == (size_t)n;
	fclose(f);
	if (!ok) {
		free(s);
		return false;
	}
	made.s = s;
	made.n = n;
	return true;
}

static void table_write(const char *path)
{
	char tmp[1300];
	snprintf(tmp, sizeof tmp, "%s.%d", path, (int)getpid());
	FILE *f = fopen(tmp, "wbe");
	if (!f) {
		return;
	}
	bool ok = fwrite(&made.n, sizeof made.n, 1, f) == 1 && fwrite(made.s, sizeof *made.s, (size_t)made.n, f) == (size_t)made.n;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

static void *table_make(void *data)
{
	(void)data;
	/* (behind everything else: there is nothing waiting for this that cannot wait) */
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 15);
	memset(&made, 0, sizeof made);
	char *path = table_path(g.asked);
	if (!path || !table_read(path)) {
		char extra[256] = "";
		for (size_t i = 0; i < sizeof EXTRA / sizeof *EXTRA; i++) {
			if (strstr(g.asked, EXTRA[i].lang)) {
				strncat(extra, EXTRA[i].extra, sizeof extra - strlen(extra) - 1);
			}
		}
		latin_add(extra);
		char langs[64];
		snprintf(langs, sizeof langs, "%s", g.asked);
		for (char *save = NULL, *l = strtok_r(langs, "+", &save); l; l = strtok_r(NULL, "+", &save)) {
			cjk_add(l);
		}
		if (path) {
			table_write(path);
		}
		fprintf(stderr, "desk: what is written is read from %d shapes (%s)\n", made.n, g.asked);
	}
	free(path);
	g.shapes = made.s;
	g.n = made.n;
	atomic_store(&g.state, 2);
	return NULL;
}

void desk_hw_want(const char *const *langs)
{
	char asked[64] = "";
	for (; langs && *langs; langs++) {
		if (strstr(asked, *langs)) {
			continue;
		}
		if (asked[0]) {
			strncat(asked, "+", sizeof asked - strlen(asked) - 1);
		}
		strncat(asked, *langs, sizeof asked - strlen(asked) - 1);
	}
	int state = atomic_load(&g.state);
	if (state == 1 || (state == 2 && !strcmp(asked, g.asked))) {
		return;
	}
	if (state == 2) {
		free(g.shapes);
		g.shapes = NULL;
		g.n = 0;
	}
	snprintf(g.asked, sizeof g.asked, "%s", asked);
	atomic_store(&g.state, 1);
	pthread_t th;
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th, &a, table_make, NULL) != 0) {
		atomic_store(&g.state, 0);
	}
	pthread_attr_destroy(&a);
}

int desk_hw_ready(void)
{
	return atomic_load(&g.state) == 2;
}

void desk_hw_drop(void)
{
	if (atomic_load(&g.state) == 2) {
		free(g.shapes);
		g.shapes = NULL;
		g.n = 0;
		g.asked[0] = '\0';
		atomic_store(&g.state, 0);
	}
}

/* ---- reading ---------------------------------------------------------------- */

static int read_features(const uint8_t *v, double w, double h, double cy, struct desk_hw_guess *out, int most)
{
	enum { KEEP = 40 };
	struct { int i; float s; } best[KEEP];
	int nb = 0;
	for (int i = 0; i < g.n; i++) {
		const struct shape *s = &g.shapes[i];
		uint32_t dot = 0;
		for (int k = 0; k < DIM; k++) {
			dot += (uint32_t)v[k] * s->vec[k];
		}
		float score = (float)dot / (512.0f * 512.0f);
		/* far too big or small for it, or in the wrong part of the box */
		double sw = s->w / 255.0, sh = s->h / 255.0, scy = s->cy / 255.0;
		/* (by its longer side: its shape is in what it was compared by) */
		double big = fabs(log((fmax(w, h) + 0.03) / (fmax(sw, sh) + 0.03)));
		double off = big - log(1.7);
		/* (a little for any difference, so that of two of one shape the size decides: あ ぁ, o O) */
		score -= (float)(0.15 * big);
		if (off > 0.0) {
			score -= (float)(0.20 * off);
		}
		double low = fabs(cy - scy) - 0.20;
		if (low > 0.0) {
			score -= (float)(0.40 * low);
		}
		if (nb == KEEP && score <= best[KEEP - 1].s) {
			continue;
		}
		int at = nb < KEEP ? nb++ : KEEP - 1;
		while (at > 0 && best[at - 1].s < score) {
			best[at] = best[at - 1];
			at--;
		}
		best[at].i = i;
		best[at].s = score;
	}
	int got = 0;
	for (int i = 0; i < nb && got < most; i++) {
		const char *text = g.shapes[best[i].i].text;
		bool seen = false;
		for (int j = 0; j < got && !seen; j++) {
			seen = !strcmp(out[j].text, text);
		}
		if (!seen) {
			snprintf(out[got].text, sizeof out[got].text, "%s", text);
			out[got].score = best[i].s;
			got++;
		}
	}
	return got;
}

int desk_hw_read(const struct desk_hw_stroke *strokes, int n, double w, double h,
	struct desk_hw_guess *out, int most)
{
	if (!desk_hw_ready() || n < 1 || w <= 0.0 || h <= 0.0) {
		return 0;
	}
	float f[DIM];
	uint8_t v[DIM];
	double x0, y0, x1, y1;
	strokes_features(strokes, n, f, &x0, &y0, &x1, &y1);
	vec_keep(f, v);
	/* the box a pen writes in is a good deal bigger than what it writes, by nature */
	return read_features(v, (x1 - x0) / h * 1.08, (y1 - y0) / h * 1.08, (y0 + y1) * 0.5 / h, out, most);
}
