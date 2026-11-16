/* The keyboard's pictures, and what DEsk prints on them (keyboardart.h). */

#include "keyboardart.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk.h"
#include "fonts.h"

#include "keyboard-map.h"

static const int ROW_TOP[7] = { KB_ROW_TOP_0, KB_ROW_TOP_1, KB_ROW_TOP_2, KB_ROW_TOP_3, KB_ROW_TOP_4, KB_ROW_TOP_5,
	KB_ROW_TOP_6 };
static const int ROW_MID[6] = { KB_ROW_MID_0, KB_ROW_MID_1, KB_ROW_MID_2, KB_ROW_MID_3, KB_ROW_MID_4, KB_ROW_MID_5 };
static const int ROW_U[6] = { KB_ROW_U_0, KB_ROW_U_1, KB_ROW_U_2, KB_ROW_U_3, KB_ROW_U_4, KB_ROW_U_5 };

enum { ROWS = 6 };

static struct {
	atomic_int state;         /* 0 nothing, 1 being read, 2 read, -1 could not be */
	cairo_surface_t *cas, *bail;
	cairo_surface_t *tile[DESK_KB_KINDS][2];
	/* the keys at the size they are shown, in each row */
	double scaled_for;
	cairo_surface_t *scaled[DESK_KB_KINDS][2][ROWS];
} g;

static cairo_surface_t *picture(const char *file)
{
	char path[1200];
	if (!desk_art_find("keyboard", file, path, sizeof path)) {
		fprintf(stderr, "desk: the keyboard's picture is not there (%s)\n", path);
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "desk: the keyboard's picture would not read (%s: %s)\n", path,
			cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

static void *read_all(void *data)
{
	(void)data;
	static const char *const names[DESK_KB_KINDS] = { "letter", "function", "return", "slim", "space" };
	bool ok = true;
	g.cas = picture("kb-case.png");
	g.bail = picture("kb-bail.png");
	ok = g.cas && g.bail;
	for (int i = 0; i < DESK_KB_KINDS && ok; i++) {
		for (int d = 0; d < 2 && ok; d++) {
			char file[64];
			snprintf(file, sizeof file, "kb-%s-%s.png", names[i], d ? "down" : "up");
			g.tile[i][d] = picture(file);
			ok = g.tile[i][d] != NULL;
		}
	}
	atomic_store(&g.state, ok ? 2 : -1);
	return NULL;
}

static void scaled_drop(void)
{
	for (int i = 0; i < DESK_KB_KINDS; i++) {
		for (int d = 0; d < 2; d++) {
			for (int r = 0; r < ROWS; r++) {
				if (g.scaled[i][d][r]) {
					cairo_surface_destroy(g.scaled[i][d][r]);
					g.scaled[i][d][r] = NULL;
				}
			}
		}
	}
	g.scaled_for = 0.0;
}

void desk_kb_art_begin(void)
{
	if (atomic_load(&g.state) != 0) {
		return;
	}
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

int desk_kb_art_ready(void)
{
	int s = atomic_load(&g.state);
	return s == 2 ? 1 : s < 0 ? -1 : 0;
}

void desk_kb_art_drop(void)
{
	if (atomic_load(&g.state) != 2) {
		return;
	}
	cairo_surface_t **own[] = { &g.cas, &g.bail };
	for (size_t i = 0; i < sizeof own / sizeof *own; i++) {
		if (*own[i]) {
			cairo_surface_destroy(*own[i]);
			*own[i] = NULL;
		}
	}
	for (int i = 0; i < DESK_KB_KINDS; i++) {
		for (int d = 0; d < 2; d++) {
			if (g.tile[i][d]) {
				cairo_surface_destroy(g.tile[i][d]);
				g.tile[i][d] = NULL;
			}
		}
	}
	scaled_drop();
	atomic_store(&g.state, 0);
}

/* How much smaller what was rendered is, in a row: from the row the keys were rendered in. */
static double row_scale(int row, double k)
{
	return k * (ROW_U[row] * 0.1) / KB_U_REF;
}

double desk_kb_art_fit(double room_w, double room_h, double most_high)
{
	double by_h = room_h / (KB_SHOW_FOOT - KB_BODY_TOP);
	double by_w = room_w / (KB_CASE_W - 2.0 * KB_BODY_X);
	double by_key = most_high / KB_ROW_REF;
	return fmin(fmin(by_h, by_w), by_key);
}

void desk_kb_art_measure(double k, int *w, int *h, int *body_x, int *body_y, int *body_w, int *body_h)
{
	*w = (int)ceil(KB_CASE_W * k);
	*h = (int)ceil(KB_CASE_H * k);
	*body_x = (int)lround(KB_BODY_X * k);
	*body_y = (int)lround(KB_BODY_TOP * k);
	*body_w = *w - *body_x * 2;
	*body_h = (int)lround((KB_BODY_FOOT - KB_BODY_TOP) * k);
}

int desk_kb_art_shows(double k)
{
	return (int)lround(KB_SHOW_FOOT * k);
}

void desk_kb_art_row(double k, int row, double *x0, double *u, int *y, int *h)
{
	*u = ROW_U[row] * 0.1 * k;
	*x0 = KB_CASE_W * k * 0.5 - *u * KB_COLS * 0.5;
	*y = (int)lround(ROW_TOP[row] * k);
	*h = (int)lround(ROW_TOP[row + 1] * k) - *y;
}

bool desk_kb_art_on_body(double k, double x, double y)
{
	double px = x / k, py = y / k;
	if (py < KB_BODY_TOP || py > KB_BODY_FOOT) {
		return false;
	}
	/* (the knobs are a little further out than the case) */
	double left = py < KB_WELL_BY ? fmin(KB_KNOB_X, KB_BODY_X) : KB_BODY_X;
	return px >= left && px <= KB_CASE_W - left;
}

/* A picture of the machine's whole size. */
static void paint_whole(cairo_t *cr, cairo_surface_t *img, double k)
{
	if (!img) {
		return;
	}
	double src_k = KB_CASE_W / (double)cairo_image_surface_get_width(img) * k;
	cairo_save(cr);
	cairo_scale(cr, src_k, src_k);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_rectangle(cr, 0, 0, cairo_image_surface_get_width(img), cairo_image_surface_get_height(img));
	cairo_fill(cr);
	cairo_restore(cr);
}

void desk_kb_art_paint_case(cairo_t *cr, double k)
{
	paint_whole(cr, g.cas, k);
}

void desk_kb_art_paint_bail(cairo_t *cr, double k)
{
	paint_whole(cr, g.bail, k);
}

void desk_kb_art_clip_well(cairo_t *cr, double k)
{
	double w = KB_CASE_W * k;
	cairo_move_to(cr, KB_WELL_BX * k, KB_WELL_BY * k);
	cairo_line_to(cr, w - KB_WELL_BX * k, KB_WELL_BY * k);
	cairo_line_to(cr, w - KB_WELL_FX * k, KB_WELL_FY * k);
	cairo_line_to(cr, KB_WELL_FX * k, KB_WELL_FY * k);
	cairo_close_path(cr);
	cairo_clip(cr);
}

int desk_kb_art_bail_h(double k)
{
	return g.bail ? (int)ceil(cairo_image_surface_get_height(g.bail) * (KB_CASE_W / (double)cairo_image_surface_get_width(g.bail)) * k) : 0;
}

int desk_kb_art_paper_foot(double k)
{
	return (int)lround(KB_PLATE_Y * k);
}

int desk_kb_art_paper_round(double k)
{
	return (int)lround(KB_PLATEN_TOP * k);
}

int desk_kb_art_paper_w(double k)
{
	return (int)lround(KB_PAPER_W * k);
}

int desk_kb_art_bail_over_foot(double k)
{
	return (int)lround((KB_PLATE_Y - KB_BAIL_Y) * k);
}

void desk_kb_art_lever(double k, int *x0, int *y0, int *x1, int *y1)
{
	*x0 = (int)lround(KB_LEVER_X0 * k);
	*x1 = (int)lround(KB_LEVER_X1 * k);
	*y0 = (int)lround(KB_LEVER_Y0 * k);
	*y1 = (int)lround(KB_BODY_TOP * k);
}

void desk_kb_art_key_reach(double k, int row, int *side, int *above, int *below)
{
	double ts = row_scale(row, k);
	int tile = g.tile[DESK_KB_LETTER][0] ? cairo_image_surface_get_height(g.tile[DESK_KB_LETTER][0]) : KB_ROW_REF;
	double half = KB_ROW_REF * ts * 0.5;
	*side = (int)ceil(KB_TILE_LEFT * ts);
	*above = (int)ceil(half + KB_TILE_TOP * ts);
	*below = (int)ceil((tile - KB_TILE_TOP - KB_ROW_REF * 0.5) * ts);
}

static cairo_surface_t *scaled_copy(cairo_surface_t *src, double ts)
{
	int sw = cairo_image_surface_get_width(src), sh = cairo_image_surface_get_height(src);
	int w = (int)lround(sw * ts), h = (int)lround(sh * ts);
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w < 1 ? 1 : w, h < 1 ? 1 : h);
	cairo_t *cr = cairo_create(out);
	cairo_scale(cr, (double)w / sw, (double)h / sh);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(out);
	return out;
}

static cairo_surface_t *tile_at(enum desk_kb_kind kind, bool down, int row, double k)
{
	if (fabs(g.scaled_for - k) > 1e-5) {
		scaled_drop();
		g.scaled_for = k;
	}
	cairo_surface_t **slot = &g.scaled[kind][down ? 1 : 0][row];
	if (!*slot && g.tile[kind][down ? 1 : 0]) {
		*slot = scaled_copy(g.tile[kind][down ? 1 : 0], row_scale(row, k));
	}
	return *slot;
}

void desk_kb_art_paint_key(cairo_t *cr, enum desk_kb_kind kind, int row, bool down, int x, int w, double k)
{
	cairo_surface_t *img = tile_at(kind, down, row, k);
	if (!img) {
		return;
	}
	double ts = row_scale(row, k);
	int tw = cairo_image_surface_get_width(img), th = cairo_image_surface_get_height(img);
	double mid_y = ROW_MID[row] * k;
	double half_row = (kind == DESK_KB_SLIM ? KB_SLIM_REF : KB_ROW_REF) * ts * 0.5;
	int top = (int)lround(mid_y - half_row - (kind == DESK_KB_SLIM ? KB_SLIM_TOP : KB_TILE_TOP) * ts);
	int m = (int)lround(KB_TILE_LEFT * ts);
	int half = (tw - 2 * m) / 4;
	int inner_l = w >= half * 2 ? half : w / 2;
	int inner_r = w - inner_l >= half ? half : w - inner_l;
	/* its two ends, each with the margin past it, and what is between drawn out from the one place */
	int left = inner_l + m, right = inner_r + m;
	cairo_save(cr);
	cairo_rectangle(cr, x - m, top, left, th);
	cairo_clip(cr);
	cairo_set_source_surface(cr, img, x - m, top);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_save(cr);
	cairo_rectangle(cr, x + w - inner_r, top, right, th);
	cairo_clip(cr);
	cairo_set_source_surface(cr, img, x + w + m - tw, top);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_paint(cr);
	cairo_restore(cr);
	int mid = w - inner_l - inner_r;
	if (mid > 0) {
		const int strip = 6;
		cairo_save(cr);
		cairo_rectangle(cr, x + inner_l, top, mid, th);
		cairo_clip(cr);
		cairo_translate(cr, x + inner_l, top);
		cairo_scale(cr, (double)mid / strip, 1.0);
		cairo_translate(cr, -(tw - strip) * 0.5, 0.0);
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
		cairo_paint(cr);
		cairo_restore(cr);
	}
}

/* ---- what is printed ------------------------------------------------- */

static void print_text(cairo_t *cr, const char *s, double cx, double cy, double size, bool bold,
	double r, double gr, double b, double glint)
{
	cairo_save(cr);
	PangoLayout *pl = pango_cairo_create_layout(cr);
	/* (laid out as drawn: a scale of its own does not go with metrics rounded to the pixel) */
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
	pango_cairo_context_set_font_options(pango_layout_get_context(pl), fo);
	cairo_font_options_destroy(fo);
	pango_layout_context_changed(pl);
	PangoFontDescription *fd = pango_font_description_new();
	pango_font_description_set_family(fd, desk_font_family(DESK_FONT_ENGRAVE, s));
	pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(pl, s, -1);
	int pw, ph;
	pango_layout_get_pixel_size(pl, &pw, &ph);
	double x = round(cx - pw * 0.5), y = round(cy - ph * 0.5);
	/* the ink lies a little into the plastic: the lower edge catches the light */
	if (glint > 0.0) {
		cairo_set_source_rgba(cr, 1.0, 0.98, 0.92, glint);
		cairo_move_to(cr, x, y + 1.0);
		pango_cairo_show_layout(cr, pl);
	}
	cairo_set_source_rgb(cr, r, gr, b);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
	cairo_restore(cr);
}

static void print_icon(cairo_t *cr, int icon, double cx, double cy, double s, double r, double gr,
	double b)
{
	cairo_save(cr);
	cairo_new_path(cr);
	cairo_set_source_rgb(cr, r, gr, b);
	cairo_set_line_width(cr, fmax(s * 0.11, 1.0));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	if (icon == DESK_KB_ICON_PUT_AWAY) {
		/* down, and away */
		cairo_move_to(cr, cx - s * 0.48, cy - s * 0.2);
		cairo_line_to(cr, cx, cy + s * 0.22);
		cairo_line_to(cr, cx + s * 0.48, cy - s * 0.2);
		cairo_stroke(cr);
	} else if (icon == DESK_KB_ICON_GLOBE) {
		double q = s * 0.46;
		cairo_new_sub_path(cr);
		cairo_arc(cr, cx, cy, q, 0.0, 2.0 * G_PI);
		cairo_stroke(cr);
		cairo_save(cr);
		cairo_translate(cr, cx, cy);
		cairo_scale(cr, 0.42, 1.0);
		cairo_arc(cr, 0.0, 0.0, q, 0.0, 2.0 * G_PI);
		cairo_restore(cr);
		cairo_stroke(cr);
		cairo_move_to(cr, cx - q, cy);
		cairo_line_to(cr, cx + q, cy);
		cairo_stroke(cr);
	} else if (icon == DESK_KB_ICON_PEN) {
		/* a nib: the two shoulders, the point, the slit and its hole */
		double q = s * 0.62;
		cairo_set_line_width(cr, fmax(s * 0.085, 1.0));
		cairo_move_to(cr, cx, cy + q);
		cairo_curve_to(cr, cx - q * 0.26, cy + q * 0.30, cx - q * 0.62, cy, cx - q * 0.42, cy - q * 0.48);
		cairo_line_to(cr, cx + q * 0.42, cy - q * 0.48);
		cairo_curve_to(cr, cx + q * 0.62, cy, cx + q * 0.26, cy + q * 0.30, cx, cy + q);
		cairo_close_path(cr);
		cairo_stroke(cr);
		cairo_move_to(cr, cx, cy + q * 0.92);
		cairo_line_to(cr, cx, cy - q * 0.02);
		cairo_stroke(cr);
		cairo_new_sub_path(cr);
		cairo_arc(cr, cx, cy - q * 0.14, s * 0.075, 0.0, 2.0 * G_PI);
		cairo_fill(cr);
		cairo_move_to(cr, cx - q * 0.40, cy - q * 0.68);
		cairo_line_to(cr, cx + q * 0.40, cy - q * 0.68);
		cairo_stroke(cr);
	} else if (icon == DESK_KB_ICON_KEYS) {
		/* three round keys and a bar */
		double q = s * 0.16;
		for (int row = 0; row < 2; row++) {
			for (int i = -1; i <= 1; i++) {
				cairo_new_sub_path(cr);
				cairo_arc(cr, cx + i * q * 2.9, cy - q * 2.3 + row * q * 2.4, q, 0.0, 2.0 * G_PI);
				cairo_fill(cr);
			}
		}
		cairo_move_to(cr, cx - q * 3.0, cy + q * 2.5);
		cairo_line_to(cr, cx + q * 3.0, cy + q * 2.5);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

void desk_kb_art_paint_print(cairo_t *cr, const struct desk_kb_print *p, enum desk_kb_kind kind, int row,
	bool down, int x, int w, double k, bool light)
{
	if (kind == DESK_KB_SPACE) {
		return;
	}
	double ts = row_scale(row, k);
	/* (going down, it goes a little further off, and out of the light) */
	double key_h = KB_ROW_REF * ts * 0.95 * (down ? 0.965 : 1.0);
	double r = 0.96, gr = 0.94, b = 0.89;
	if (kind == DESK_KB_FUNCTION || kind == DESK_KB_SLIM) {
		r = 0.88;
		gr = 0.84;
		b = 0.72;
	}
	if (down) {
		r *= 0.82;
		gr *= 0.82;
		b *= 0.82;
	}
	/* the middle of its top */
	double cx = x + w * 0.5;
	double cy = ROW_MID[row] * k;
	if (p->icon != DESK_KB_ICON_NONE) {
		print_icon(cr, p->icon, cx, cy, key_h * (kind == DESK_KB_SLIM ? 0.24 : 0.34), r, gr, b);
	} else if (p->word) {
		print_text(cr, p->word, cx, cy, key_h * (kind == DESK_KB_SLIM ? 0.17 : 0.18), true, r, gr, b, 0.0);
	} else if (p->shifted[0]) {
		double gap = key_h * 0.22;
		print_text(cr, p->shifted, cx, cy - gap * 0.80, key_h * 0.21, true, r, gr, b, 0.0);
		print_text(cr, p->base, cx, cy + gap * 0.80, key_h * 0.25, true, r, gr, b, 0.0);
	} else if (p->base[0]) {
		print_text(cr, p->base, cx, cy, key_h * 0.33, true, r, gr, b, 0.0);
	}
	if (p->third[0]) {
		print_text(cr, p->third, x + w * 0.5 + key_h * 0.28, cy + key_h * 0.14, key_h * 0.15, false,
			r * 0.8, gr * 0.8, b * 0.8, 0.0);
	}
	/* a key that stays down says so: a small light on it */
	if (light) {
		double lx = cx, ly = cy - key_h * 0.26;
		cairo_pattern_t *halo = cairo_pattern_create_radial(lx, ly, 0.0, lx, ly, key_h * 0.08);
		cairo_pattern_add_color_stop_rgba(halo, 0.0, 1.0, 0.72, 0.30, 1.0);
		cairo_pattern_add_color_stop_rgba(halo, 0.35, 1.0, 0.55, 0.14, 0.85);
		cairo_pattern_add_color_stop_rgba(halo, 1.0, 1.0, 0.40, 0.05, 0.0);
		cairo_set_source(cr, halo);
		cairo_arc(cr, lx, ly, key_h * 0.08, 0.0, 2.0 * G_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(halo);
	}
}

void desk_kb_art_paint_light(cairo_t *cr, double lit, double k)
{
	double lx = KB_LIGHT_X * k, ly = KB_LIGHT_Y * k;
	double rad = 30.0 * k;
	cairo_pattern_t *halo = cairo_pattern_create_radial(lx, ly, 0.0, lx, ly, rad);
	cairo_pattern_add_color_stop_rgba(halo, 0.0, 1.0, 0.75, 0.42, lit);
	cairo_pattern_add_color_stop_rgba(halo, 0.28, 1.0, 0.25, 0.08, lit * 0.8);
	cairo_pattern_add_color_stop_rgba(halo, 1.0, 1.0, 0.10, 0.02, 0.0);
	cairo_set_source(cr, halo);
	cairo_arc(cr, lx, ly, rad, 0.0, 2.0 * G_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(halo);
}

/* Typed on the sheet. */
void desk_kb_art_type(cairo_t *cr, const char *s, double x, double cy, double size, double r, double gr, double b,
	double a, int *width, bool centre)
{
	PangoLayout *pl = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_new();
	pango_font_description_set_family(fd, desk_font_family(DESK_FONT_TYPE, s));
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(pl, s, -1);
	int pw, ph;
	pango_layout_get_pixel_size(pl, &pw, &ph);
	if (width) {
		*width = pw;
	}
	if (a > 0.0) {
		cairo_set_source_rgba(cr, r, gr, b, a);
		cairo_move_to(cr, centre ? round(x - pw * 0.5) : x, round(cy - ph * 0.5));
		pango_cairo_show_layout(cr, pl);
	}
	g_object_unref(pl);
}

/* ---- as it lies in the drawer ---------------------------------------- */

static cairo_surface_t *g_top;
static pthread_once_t g_top_once = PTHREAD_ONCE_INIT;

static void top_load(void)
{
	g_top = picture("kb-top.png");
}

cairo_surface_t *desk_kb_art_top_lying(double ppu, double turn, double *ax, double *ay)
{
	pthread_once(&g_top_once, top_load);
	if (!g_top) {
		return NULL;
	}
	double pw = cairo_image_surface_get_width(g_top);
	double ph = cairo_image_surface_get_height(g_top);
	double k = 3.3 * ppu / pw;
	double diag = hypot(pw, ph) * k + 4.0;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(diag), (int)ceil(diag));
	cairo_t *cr = cairo_create(img);
	cairo_translate(cr, diag * 0.5, diag * 0.5);
	cairo_rotate(cr, turn);
	cairo_scale(cr, k, k);
	cairo_translate(cr, -pw * 0.5, -ph * 0.5);
	cairo_set_source_surface(cr, g_top, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(img);
	*ax = *ay = diag * 0.5;
	return img;
}
