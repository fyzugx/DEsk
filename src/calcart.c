/* The calculator's pictures, and what DEsk writes on them (calcart.h). */

#include "calcart.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk.h"
#include "fonts.h"

#include "calc-map.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define KEYS ((int)(sizeof CALC_KEYS / sizeof CALC_KEYS[0]))

static struct {
	atomic_int state;         /* 0 nothing, 1 being read, 2 read, -1 could not be */
	double k;                 /* screen pixels to a pixel of the canvas */
	cairo_surface_t *open, *shut;
	int open_x, open_y, shut_x, shut_y;
	cairo_surface_t *film[CALC_FILM_N];
	cairo_surface_t *key[KEYS];
} g;

static cairo_surface_t *picture(const char *file)
{
	char path[1200];
	if (!desk_art_find("calc", file, path, sizeof path)) {
		fprintf(stderr, "desk: the calculator's picture is not there (%s)\n", path);
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "desk: the calculator's picture would not read (%s: %s)\n", path,
			cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

/* Part of a picture, sx, sy, sw, sh, k times the size; dest the screen's place
 * for it, its corner on a whole pixel. */
static cairo_surface_t *scaled(cairo_surface_t *src, int sx, int sy, int sw, int sh, double k,
	double place_x, double place_y, int *dest_x, int *dest_y)
{
	*dest_x = (int)floor(place_x * k);
	*dest_y = (int)floor(place_y * k);
	int w = (int)ceil((place_x + sw) * k) - *dest_x;
	int h = (int)ceil((place_y + sh) * k) - *dest_y;
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w < 1 ? 1 : w, h < 1 ? 1 : h);
	cairo_t *cr = cairo_create(out);
	cairo_translate(cr, -*dest_x, -*dest_y);
	cairo_scale(cr, k, k);
	cairo_translate(cr, place_x - sx, place_y - sy);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_NONE);
	cairo_rectangle(cr, sx, sy, sw, sh);
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_flush(out);
	return out;
}

static cairo_surface_t *rest_scaled(const char *file, double at_x, double at_y, double k, int *x, int *y)
{
	cairo_surface_t *src = picture(file);
	if (!src) {
		return NULL;
	}
	cairo_surface_t *out = scaled(src, 0, 0, cairo_image_surface_get_width(src),
		cairo_image_surface_get_height(src), k, at_x, at_y, x, y);
	cairo_surface_destroy(src);
	return out;
}

static void *read_all(void *arg)
{
	(void)arg;
	double k = g.k;
	bool ok = true;
	g.open = rest_scaled("calc-open.png", CALC_OPEN_X, CALC_OPEN_Y, k, &g.open_x, &g.open_y);
	g.shut = rest_scaled("calc-shut.png", CALC_SHUT_X, CALC_SHUT_Y, k, &g.shut_x, &g.shut_y);
	ok = g.open && g.shut;
	cairo_surface_t *sheet = ok ? picture("calc-keys.png") : NULL;
	ok = ok && sheet;
	for (int i = 0; ok && i < KEYS; i++) {
		int x, y;
		const struct calc_key_map *m = &CALC_KEYS[i];
		g.key[i] = scaled(sheet, m->sx, m->sy, m->at[2], m->at[3], k, m->at[0], m->at[1], &x, &y);
	}
	if (sheet) {
		cairo_surface_destroy(sheet);
	}
	for (int i = 0; ok && i < CALC_FILM_N; i++) {
		char file[32];
		snprintf(file, sizeof file, "calc-lid-%02d.png", i);
		g.film[i] = picture(file);
		ok = g.film[i] != NULL;
	}
	atomic_store(&g.state, ok ? 2 : -1);
	return NULL;
}

void desk_calc_art_begin(double px_per_cm)
{
	if (atomic_load(&g.state) != 0) {
		return;
	}
	g.k = px_per_cm / CALC_BAKE_PX;
	atomic_store(&g.state, 1);
	pthread_t t;
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &a, read_all, NULL) != 0) {
		atomic_store(&g.state, -1);
	}
	pthread_attr_destroy(&a);
}

int desk_calc_art_ready(void)
{
	int s = atomic_load(&g.state);
	return s == 2 ? 1 : s < 0 ? -1 : 0;
}

void desk_calc_art_drop(void)
{
	if (atomic_load(&g.state) == 1) {
		return;       /* (still being read: let go of the next time) */
	}
	if (g.open) {
		cairo_surface_destroy(g.open);
	}
	if (g.shut) {
		cairo_surface_destroy(g.shut);
	}
	g.open = g.shut = NULL;
	for (int i = 0; i < CALC_FILM_N; i++) {
		if (g.film[i]) {
			cairo_surface_destroy(g.film[i]);
		}
		g.film[i] = NULL;
	}
	for (int i = 0; i < KEYS; i++) {
		if (g.key[i]) {
			cairo_surface_destroy(g.key[i]);
		}
		g.key[i] = NULL;
	}
	atomic_store(&g.state, 0);
}

cairo_surface_t *desk_calc_art_rest(bool open, int *x, int *y)
{
	*x = open ? g.open_x : g.shut_x;
	*y = open ? g.open_y : g.shut_y;
	return open ? g.open : g.shut;
}

int desk_calc_art_film_n(void)
{
	return CALC_FILM_N;
}

cairo_surface_t *desk_calc_art_film(int i, int *x, int *y, int *w, int *h)
{
	/* (fewer of the canvas's pixels: it goes by too fast to read) */
	double k = g.k * CALC_FILM_SCALE;
	const short *f = CALC_FILM[i];
	*x = (int)floor(f[0] * k);
	*y = (int)floor(f[1] * k);
	*w = (int)ceil((f[0] + f[2]) * k) - *x;
	*h = (int)ceil((f[1] + f[3]) * k) - *y;
	return g.film[i];
}

int desk_calc_art_keys(void)
{
	return KEYS;
}

cairo_surface_t *desk_calc_art_key_down(int i, int *x, int *y)
{
	const struct calc_key_map *m = &CALC_KEYS[i];
	*x = (int)floor(m->at[0] * g.k);
	*y = (int)floor(m->at[1] * g.k);
	return g.key[i];
}

int desk_calc_art_key_of(enum desk_calc_key k, bool open)
{
	for (int i = 0; i < KEYS; i++) {
		if (CALC_KEYS[i].key == k && CALC_KEYS[i].face == (open ? 0 : 1)) {
			return i;
		}
	}
	return -1;
}

enum desk_calc_key desk_calc_art_key(int i)
{
	return CALC_KEYS[i].key;
}

static bool in_box(const int b[4], double x, double y)
{
	return x >= b[0] && x < b[2] && y >= b[1] && y < b[3];
}

/* The corner a hand pulls to make it bigger or smaller: the box's lower right, this much of it (in
 * the canvas's pixels, a centimetre being CALC_BAKE_PX of them). */
#define CORNER_CM 0.9
/* and the small keys at the head of each face found a little way past themselves */
#define SMALL_PAST_CM 0.22

enum desk_calc_spot desk_calc_art_at(double x, double y, bool open, int *which)
{
	static const int OPEN_BOX[4] = CALC_OPEN_BOX, SHUT_BOX[4] = CALC_SHUT_BOX;
	static const int LID_BOX[4] = CALC_LID_BOX, TRAY[4] = CALC_LID_TRAY;
	static const int HINGE[4] = CALC_HINGE_BOX, PAPER[4] = CALC_PAPER_BOX;
	double k = g.k > 0.0 ? g.k : 0.5;
	x /= k;
	y /= k;
	const int *box = open ? OPEN_BOX : SHUT_BOX;
	if (!in_box(box, x, y)) {
		return DESK_CALC_OFF;
	}
	/* its keys, on the face that is up */
	for (int i = 0; i < KEYS; i++) {
		const struct calc_key_map *m = &CALC_KEYS[i];
		double past = m->key > DESK_CK_COUNT ? SMALL_PAST_CM * CALC_BAKE_PX : 0.0;
		if (m->face == (open ? 0 : 1) && x >= m->hit[0] - past && x < m->hit[2] + past
			&& y >= m->hit[1] - past && y < m->hit[3] + past) {
			if (which) {
				*which = i;
			}
			return DESK_CALC_KEY;
		}
	}
	double corner = CORNER_CM * CALC_BAKE_PX;
	if (x >= box[2] - corner && y >= box[3] - corner) {
		return DESK_CALC_CORNER;
	}
	if (!open) {
		return DESK_CALC_CASE;
	}
	if (in_box(PAPER, x, y)) {
		int row = CALC_RULES - 1;
		for (int r = 0; r < CALC_RULES; r++) {
			if (y < CALC_RULE_Y[r]) {
				row = r;
				break;
			}
		}
		if (which) {
			*which = row;
		}
		return DESK_CALC_PAPER;
	}
	if (in_box(HINGE, x, y) || (in_box(LID_BOX, x, y) && !in_box(TRAY, x, y))) {
		return DESK_CALC_SHUT_IT;
	}
	return DESK_CALC_CASE;
}

static void box_scaled(const int src[4], int box[4])
{
	box[0] = (int)floor(src[0] * g.k);
	box[1] = (int)floor(src[1] * g.k);
	box[2] = (int)ceil(src[2] * g.k);
	box[3] = (int)ceil(src[3] * g.k);
}

void desk_calc_art_box(bool open, int box[4])
{
	static const int OPEN_BOX[4] = CALC_OPEN_BOX, SHUT_BOX[4] = CALC_SHUT_BOX;
	box_scaled(open ? OPEN_BOX : SHUT_BOX, box);
}

int desk_calc_art_hinge_x(void)
{
	static const int HINGE[4] = CALC_HINGE_BOX;
	return (int)lround((HINGE[0] + HINGE[2]) * 0.5 * g.k);
}

void desk_calc_art_figures_box(int box[4])
{
	static const int DISP[4] = CALC_ODISP_BOX;
	box_scaled(DISP, box);
}

void desk_calc_art_dots_box(int box[4])
{
	static const int DISP[4] = CALC_DISP_BOX;
	box_scaled(DISP, box);
}

/* A figure's segments, a to g as a seven-segment figure's always are: its top,
 * round by the right to its bottom and up the left, and its middle. */
static unsigned figure_segments(char c)
{
	static const unsigned char DIGIT[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
	if (c >= '0' && c <= '9') {
		return DIGIT[c - '0'];
	}
	switch (c) {
	case '-': return 0x40;
	case 'E': return 0x79;
	case 'r': return 0x50;
	case 'o': return 0x5c;
	default: return 0;
	}
}

/* A segment as calc.py makes it: a six-sided plate. (u, v) a corner of the
 * figure, v up it; this goes along it. */
static void segment_path(cairo_t *cr, double ox, double oy, double u0, double v0, double u1, double v1,
	double t)
{
	double g2 = t * 0.12, s = t * 0.5;
	double pts[6][2];
	if (v0 == v1) {
		double a = u0 + g2, b = u1 - g2;
		double p[6][2] = { { a, v0 }, { a + s, v0 + s }, { b - s, v0 + s }, { b, v0 }, { b - s, v0 - s },
			{ a + s, v0 - s } };
		memcpy(pts, p, sizeof pts);
	} else {
		double a = v0 + g2, b = v1 - g2;
		double p[6][2] = { { u0, a }, { u0 + s, a + s }, { u0 + s, b - s }, { u0, b }, { u0 - s, b - s },
			{ u0 - s, a + s } };
		memcpy(pts, p, sizeof pts);
	}
	for (int i = 0; i < 6; i++) {
		double x = ox + pts[i][0] + pts[i][1] * CALC_FIG_SLANT;
		double y = oy - pts[i][1];
		if (i == 0) {
			cairo_move_to(cr, x, y);
		} else {
			cairo_line_to(cr, x, y);
		}
	}
	cairo_close_path(cr);
}

void desk_calc_art_paint_figures(cairo_t *cr, const char *shown, bool memory, double lit)
{
	static const int DISP[4] = CALC_ODISP_BOX;
	/* the figures, from the right, a point going with the figure before it */
	struct { char c; bool point; } fig[CALC_FIGURES];
	memset(fig, 0, sizeof fig);
	int n = 0;
	struct { char c; bool point; } in[32];
	for (const char *p = shown; *p && n < (int)(sizeof in / sizeof in[0]); p++) {
		if (*p == '.' && n > 0) {
			in[n - 1].point = true;
			continue;
		}
		in[n].c = *p;
		in[n].point = false;
		n++;
	}
	for (int i = 0; i < CALC_FIGURES && i < n; i++) {
		fig[CALC_FIGURES - 1 - i].c = in[n - 1 - i].c;
		fig[CALC_FIGURES - 1 - i].point = in[n - 1 - i].point;
	}

	cairo_save(cr);
	double k = g.k;
	int box[4];
	box_scaled(DISP, box);
	cairo_translate(cr, -box[0], -box[1]);
	cairo_scale(cr, k, k);
	double w = CALC_FIG_W, h = CALC_FIG_RISE, t = CALC_FIG_T, m = h * 0.5;
	double cm = CALC_BAKE_PX;
	cairo_new_path(cr);
	for (int i = 0; i < CALC_FIGURES; i++) {
		double ox = CALC_FIG_X + i * CALC_FIG_PITCH, oy = CALC_FIG_Y;
		unsigned s = figure_segments(fig[i].c);
		/* a: top, b: upper right, c: lower right, d: bottom, e: lower left,
		 * f: upper left, g: middle */
		if (s & 0x01) segment_path(cr, ox, oy, 0, h, w, h, t);
		if (s & 0x02) segment_path(cr, ox, oy, w, m, w, h, t);
		if (s & 0x04) segment_path(cr, ox, oy, w, 0, w, m, t);
		if (s & 0x08) segment_path(cr, ox, oy, 0, 0, w, 0, t);
		if (s & 0x10) segment_path(cr, ox, oy, 0, 0, 0, m, t);
		if (s & 0x20) segment_path(cr, ox, oy, 0, m, 0, h, t);
		if (s & 0x40) segment_path(cr, ox, oy, 0, m, w, m, t);
		if (fig[i].point) {
			double px = ox + w + 0.125 * cm + 0.05 * cm * CALC_FIG_SLANT, py = oy - 0.05 * cm;
			double d = 0.065 * cm;
			cairo_rectangle(cr, px - d, py - d, d * 2.0, d * 2.0);
		}
	}
	/* lit through smoked glass: brighter than their edges, with a glow round them */
	cairo_path_t *path = cairo_copy_path(cr);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(cr, 1.0, 0.16, 0.08, 0.10 * lit);
	cairo_set_line_width(cr, 11.0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 0.16, 0.08, 0.20 * lit);
	cairo_set_line_width(cr, 4.5);
	cairo_stroke(cr);
	cairo_append_path(cr, path);
	cairo_path_destroy(path);
	cairo_set_source_rgba(cr, 1.0, 0.24, 0.13, lit);
	cairo_fill(cr);
	if (memory) {
		desk_font_select(cr, DESK_FONT_UI, "M", true, false);
		cairo_set_font_size(cr, 15.0);
		cairo_set_source_rgba(cr, 1.0, 0.24, 0.13, lit);
		cairo_move_to(cr, CALC_FIG_X - 2.0, CALC_FIG_Y - CALC_FIG_RISE - 7.0);
		cairo_show_text(cr, "M");
	}
	cairo_restore(cr);
}

/* ---- the inside's display: a matrix of dots ----
 *
 * Like the clock's small print: every dot is there behind the glass, unlit, and
 * the lit ones glow. Over the figures, in five by seven, what is being worked
 * out ("1,250 +", "√ 3,200 ="); under it, big, the figures. */

#define DOTS_W 120
#define DOTS_H 32
#define DOTS_MARGIN_CM 0.22
#define SMALL_ROW 1               /* the top of the small line's letters */
#define BIG_ROW 11                /* and of the figures' room, to the foot */

/* Five columns of seven, bit 0 at the top, for what a sum is written with. */
static const unsigned char *dots_glyph(uint32_t ch)
{
	static const struct {
		uint32_t c;
		unsigned char col[5];
	} FONT[] = {
		{ '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } }, { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
		{ '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } }, { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
		{ '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } }, { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
		{ '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } }, { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
		{ '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } }, { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
		{ 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } }, { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
		{ 'a', { 0x20, 0x54, 0x54, 0x54, 0x78 } }, { 'c', { 0x38, 0x44, 0x44, 0x44, 0x20 } },
		{ 'e', { 0x38, 0x54, 0x54, 0x54, 0x18 } }, { 'g', { 0x0C, 0x52, 0x52, 0x52, 0x3E } },
		{ 'i', { 0x00, 0x44, 0x7D, 0x40, 0x00 } }, { 'l', { 0x00, 0x41, 0x7F, 0x40, 0x00 } },
		{ 'n', { 0x7C, 0x08, 0x04, 0x04, 0x78 } }, { 'o', { 0x38, 0x44, 0x44, 0x44, 0x38 } },
		{ 'r', { 0x7C, 0x08, 0x04, 0x04, 0x08 } }, { 's', { 0x48, 0x54, 0x54, 0x54, 0x20 } },
		{ 't', { 0x04, 0x3F, 0x44, 0x40, 0x20 } },
		{ '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } }, { ',', { 0x00, 0x50, 0x30, 0x00, 0x00 } },
		{ '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } }, { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
		{ '=', { 0x14, 0x14, 0x14, 0x14, 0x14 } }, { '%', { 0x23, 0x13, 0x08, 0x64, 0x62 } },
		{ '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } }, { '^', { 0x04, 0x02, 0x01, 0x02, 0x04 } },
		{ '(', { 0x00, 0x1C, 0x22, 0x41, 0x00 } }, { ')', { 0x00, 0x41, 0x22, 0x1C, 0x00 } },
		{ '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
		{ 0x2212, { 0x08, 0x08, 0x08, 0x08, 0x08 } },   /* − */
		{ 0x00D7, { 0x22, 0x14, 0x08, 0x14, 0x22 } },   /* × */
		{ 0x00F7, { 0x08, 0x08, 0x2A, 0x08, 0x08 } },   /* ÷ */
		{ 0x221A, { 0x10, 0x60, 0x1C, 0x03, 0x01 } },   /* √ */
		{ 0x03C0, { 0x04, 0x7C, 0x04, 0x7C, 0x04 } },   /* π */
		{ 0x00B2, { 0x00, 0x09, 0x0D, 0x0A, 0x00 } },   /* ² */
		{ 0x00B1, { 0x44, 0x44, 0x5F, 0x44, 0x44 } },   /* ± */
	};
	for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++) {
		if (FONT[i].c == ch) {
			return FONT[i].col;
		}
	}
	return NULL;
}

static uint32_t utf8_next(const char **p)
{
	const unsigned char *s = (const unsigned char *)*p;
	uint32_t c = s[0];
	int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
	if (n > 1) {
		c &= 0x3F >> (n - 1);
		for (int i = 1; i < n; i++) {
			if ((s[i] & 0xC0) != 0x80) {
				*p += i;
				return '?';
			}
			c = (c << 6) | (s[i] & 0x3F);
		}
	}
	*p += n;
	return c;
}

/* The small line into the matrix, from its right: as much of the end of it as there is room for. */
static void dots_small(unsigned char lit[DOTS_H][DOTS_W], const char *text, int right)
{
	uint32_t chars[96];
	int n = 0;
	for (const char *p = text; *p && n < (int)(sizeof chars / sizeof chars[0]);) {
		chars[n++] = utf8_next(&p);
	}
	int x = right;
	for (int i = n - 1; i >= 0 && x >= 6; i--) {
		x -= 6;
		const unsigned char *g2 = chars[i] == ' ' ? NULL : dots_glyph(chars[i]);
		for (int col = 0; g2 && col < 5; col++) {
			for (int row = 0; row < 7; row++) {
				if (g2[col] >> row & 1) {
					lit[SMALL_ROW + row][x + col] = 1;
				}
			}
		}
	}
}

/* The figures into the matrix, as big as the room under the small line: drawn in the figures'
 * font with nothing between lit and unlit, a pixel a dot, from its right. */
static void dots_big(unsigned char lit[DOTS_H][DOTS_W], const char *shown, int right)
{
	int rows = DOTS_H - BIG_ROW - 1;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_A8, DOTS_W, rows);
	cairo_t *cr = cairo_create(s);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	cairo_set_font_options(cr, fo);
	cairo_font_options_destroy(fo);
	desk_font_select(cr, DESK_FONT_SCREEN, shown, true, false);
	double size = rows * 1.30;
	cairo_set_font_size(cr, size);
	cairo_text_extents_t te;
	cairo_text_extents(cr, shown, &te);
	/* (narrower, if it is long, before it is cut) */
	double squeeze = te.x_advance > right ? right / te.x_advance : 1.0;
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	cairo_translate(cr, right - te.x_advance * squeeze, rows - 1);
	cairo_scale(cr, squeeze, 1.0);
	cairo_move_to(cr, 0, 0);
	cairo_show_text(cr, shown);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	const unsigned char *px = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < DOTS_W; x++) {
			if (px[y * stride + x] > 127) {
				lit[BIG_ROW + y][x] = 1;
			}
		}
	}
	cairo_surface_destroy(s);
}

void desk_calc_art_paint_dots(cairo_t *cr, const char *working, const char *shown, bool memory, double lit)
{
	static const int DISP[4] = CALC_DISP_BOX;
	unsigned char on[DOTS_H][DOTS_W];
	memset(on, 0, sizeof on);
	dots_small(on, working, DOTS_W);
	/* the figures as they are shown, less the point a calculator always has */
	char figures[64];
	snprintf(figures, sizeof figures, "%s", shown);
	size_t n = strlen(figures);
	if (n > 1 && figures[n - 1] == '.') {
		figures[n - 1] = '\0';
	}
	dots_big(on, figures, DOTS_W - 1);
	const unsigned char *m = memory ? dots_glyph('M') : NULL;
	if (m) {
		for (int col = 0; col < 5; col++) {
			for (int row = 0; row < 7; row++) {
				if (m[col] >> row & 1) {
					on[BIG_ROW + row][col] = 1;
				}
			}
		}
	}

	cairo_save(cr);
	int box[4];
	box_scaled(DISP, box);
	double bw = box[2] - box[0], bh = box[3] - box[1];
	double margin = DOTS_MARGIN_CM * CALC_BAKE_PX * g.k;
	double pitch = fmin((bw - 2.0 * margin) / DOTS_W, (bh - 2.0 * margin) / DOTS_H);
	double ox = (bw - pitch * DOTS_W) * 0.5, oy = (bh - pitch * DOTS_H) * 0.5;
	/* round dots with room round each while they can be told apart; nearer to touching as they
	 * get smaller, till they are what the eye takes a matrix for anyway */
	double r = pitch * fmin(fmax(0.42 + 0.14 * (2.4 - pitch) / 1.2, 0.40), 0.50);
	bool round = pitch >= 2.2;
	for (int pass = 0; pass < 2; pass++) {
		cairo_new_path(cr);
		for (int y = 0; y < DOTS_H; y++) {
			for (int x = 0; x < DOTS_W; x++) {
				if (on[y][x] != pass) {
					continue;
				}
				double cx = ox + (x + 0.5) * pitch, cy = oy + (y + 0.5) * pitch;
				if (round) {
					cairo_new_sub_path(cr);
					cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
				} else {
					cairo_rectangle(cr, cx - r, cy - r, r * 2.0, r * 2.0);
				}
			}
		}
		if (pass == 0) {
			/* every dot there, unlit, behind the smoked glass */
			cairo_set_source_rgba(cr, 1.0, 0.20, 0.10, 0.075);
			cairo_fill(cr);
			continue;
		}
		/* and the lit ones, deep red and full, with a little glow round them */
		cairo_path_t *path = cairo_copy_path(cr);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_set_source_rgba(cr, 1.0, 0.12, 0.05, 0.16 * lit);
		cairo_set_line_width(cr, pitch * 1.4);
		cairo_stroke(cr);
		cairo_append_path(cr, path);
		cairo_path_destroy(path);
		cairo_set_source_rgba(cr, 1.0, 0.20, 0.08, lit);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

void desk_calc_art_paper_box(int box[4])
{
	static const int PAPER[4] = CALC_PAPER_BOX;
	box_scaled(PAPER, box);
}

int desk_calc_art_rows(void)
{
	return CALC_RULES;
}

/* Text as it is set on the paper; most: at most so wide, losing its beginning. */
static PangoLayout *type_layout(cairo_t *cr, const char *s, double size, bool bold, double most)
{
	PangoLayout *pl = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_new();
	pango_font_description_set_family(fd, desk_font_family(DESK_FONT_TYPE, s));
	pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(pl, s, -1);
	if (most > 0.0) {
		pango_layout_set_width(pl, (int)(most * PANGO_SCALE));
		pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_START);
	}
	return pl;
}

static double type_width(cairo_t *cr, const char *s, double size, bool bold)
{
	PangoLayout *pl = type_layout(cr, s, size, bold, 0.0);
	int pw, ph;
	pango_layout_get_pixel_size(pl, &pw, &ph);
	g_object_unref(pl);
	return pw;
}

/* Text on the paper, its baseline at y; right: x is where it ends. */
static void type_on(cairo_t *cr, const char *s, double x, double y, double size, bool bold, bool right,
	double most)
{
	PangoLayout *pl = type_layout(cr, s, size, bold, most);
	int pw, ph;
	pango_layout_get_pixel_size(pl, &pw, &ph);
	double base = pango_layout_get_baseline(pl) / (double)PANGO_SCALE;
	cairo_move_to(cr, right ? x - pw : x, y - base);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
}

void desk_calc_art_paint_paper(cairo_t *cr, int first)
{
	static const int PAPER[4] = CALC_PAPER_BOX;
	int box[4];
	box_scaled(PAPER, box);
	cairo_save(cr);
	cairo_translate(cr, -box[0], -box[1]);
	cairo_scale(cr, g.k, g.k);
	cairo_set_source_rgba(cr, 0.03, 0.06, 0.26, 0.96);
	for (int r = 0; r < CALC_RULES; r++) {
		const struct desk_calc_line *l = desk_calc_line(first + r);
		if (!l) {
			break;
		}
		/* on its rule */
		double y = CALC_RULE_Y[r] - 7.0, size = CALC_TEXT_SIZE;
		char answer[56];
		snprintf(answer, sizeof answer, "= %s", l->answer);
		double aw = type_width(cr, answer, size, true);
		type_on(cr, answer, CALC_TEXT_X1, y, size, true, true, 0.0);
		double room = CALC_TEXT_X1 - CALC_TEXT_X0 - aw - size;
		if (room < size * 3.0) {
			room = size * 3.0;
		}
		/* a long sum is set smaller first, and only then loses its beginning */
		double sw = type_width(cr, l->sum, size, false);
		double fit = sw > room ? room / sw * 0.96 : 1.0;
		type_on(cr, l->sum, CALC_TEXT_X0, y, size * (fit < 0.68 ? 0.68 : fit), false, false, room);
	}
	cairo_restore(cr);
}

static pthread_once_t g_top_once = PTHREAD_ONCE_INIT;
static cairo_surface_t *g_top;

static void top_load(void)
{
	g_top = picture("calc-top.png");
}

cairo_surface_t *desk_calc_art_top_lying(double ppu, double turn, double *ax, double *ay)
{
	pthread_once(&g_top_once, top_load);
	if (!g_top) {
		return NULL;
	}
	double pw = cairo_image_surface_get_width(g_top);
	double ph = cairo_image_surface_get_height(g_top);
	double k = 2.75 * ppu / CALC_TOP_ALONG_PX;
	double diag = hypot(pw, ph) * k + 4.0;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(diag), (int)ceil(diag));
	cairo_t *cr = cairo_create(img);
	cairo_translate(cr, diag * 0.5, diag * 0.5);
	cairo_rotate(cr, turn);
	cairo_scale(cr, k, k);
	cairo_translate(cr, -CALC_TOP_MID_X, -CALC_TOP_MID_Y);
	cairo_set_source_surface(cr, g_top, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(img);
	*ax = *ay = diag * 0.5;
	return img;
}
