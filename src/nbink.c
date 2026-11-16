/* The notebook's sheet and its print, for the desk and for desk-notepad alike (notebook.h).
 *
 * It was rendered once at the size of a B5 page (art/notebook/notebook.py), and
 * a sheet can be any size. Nothing in that picture minds: past its corners it
 * has only long straight edges and slow light, which stretch without anyone
 * being able to tell. What will not stretch is what repeats: the wire, a turn of
 * which is laid again and again down the left side, and the paper's tooth,
 * multiplied over the sheet at its own size. What a sheet is printed with goes
 * on before the tooth, so that it goes into the paper. */

#include "notebook.h"

#include <math.h>

#include "fonts.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
	/* the page's picture: the sheet in it NB_PAD in from every side */
	PAGE_CORNER = NB_PAD + 150,
	/* the wire's: one turn; the sheet's left NB_PAD in */
	RING_P = 24,
	RING_FIRST = 3,      /* the top of a turn has the foot of the turn before */
	RING_TAIL = 4,       /* and so the last goes on this far past its own */
	RING_CLEAR = 15,     /* the wire stops this short of the sheet's top and foot */
	SHEET_R = NB_SHEET_R,
};

/* (a prime, so that no two rules start alike for long) */
enum { INK_LEN = 1021, INK_ROWS = 3 };

cairo_pattern_t *desk_nb_ink(double r, double g, double b, double a)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, INK_LEN, INK_ROWS);
	unsigned char *data = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	/* across a line: mostly its middle, a little bled into the paper, more on
	 * the side the plate left it */
	static const double ACROSS[INK_ROWS] = { 0.10, 1.0, 0.24 };
	unsigned v = 20261106u;
	double slow = 0.0, fine = 0.0;
	for (int x = 0; x < INK_LEN; x++) {
		v = v * 1664525u + 1013904223u;
		double n = ((v >> 12) & 0xffff) / 65535.0 - 0.5;
		slow = slow * 0.985 + n * 0.11;
		fine = fine * 0.55 + n * 0.45;
		double k = 0.86 + slow * 0.55 + fine * 0.34;
		k = k < 0.45 ? 0.45 : (k > 1.0 ? 1.0 : k);
		for (int row = 0; row < INK_ROWS; row++) {
			double al = a * k * ACROSS[row];
			unsigned char *p = data + row * stride + x * 4;
			unsigned pa = (unsigned)lround(al * 255.0);
			unsigned pr = (unsigned)lround(r * al * 255.0);
			unsigned pg = (unsigned)lround(g * al * 255.0);
			unsigned pb = (unsigned)lround(b * al * 255.0);
			*(unsigned *)(void *)p = (pa << 24) | (pr << 16) | (pg << 8) | pb;
		}
	}
	cairo_surface_mark_dirty(s);
	cairo_pattern_t *pat = cairo_pattern_create_for_surface(s);
	cairo_surface_destroy(s);
	cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
	cairo_pattern_set_filter(pat, CAIRO_FILTER_NEAREST);
	return pat;
}

static double ink_phase(int at)
{
	unsigned h = (unsigned)at * 2654435761u;
	return (double)((h >> 8) % INK_LEN);
}

void desk_nb_line_across(cairo_t *cr, cairo_pattern_t *ink, int x0, int x1, int y)
{
	cairo_matrix_t m;
	cairo_matrix_init_translate(&m, ink_phase(y), -(double)(y - 1));
	cairo_pattern_set_matrix(ink, &m);
	cairo_set_source(cr, ink);
	cairo_rectangle(cr, x0, y - 1, x1 - x0, INK_ROWS);
	cairo_fill(cr);
}

void desk_nb_line_down(cairo_t *cr, cairo_pattern_t *ink, int x, int y0, int y1)
{
	cairo_matrix_t m;
	/* along the picture is down the sheet */
	cairo_matrix_init(&m, 0.0, 1.0, 1.0, 0.0, ink_phase(x), -(double)(x - 1));
	cairo_pattern_set_matrix(ink, &m);
	cairo_set_source(cr, ink);
	cairo_rectangle(cr, x - 1, y0, INK_ROWS, y1 - y0);
	cairo_fill(cr);
}

/* ---- the sheet ---------------------------------------------------------------------------------- */

/* Part of the page's picture, sw by sh, made dw by dh: as it is if it is the same
 * size, and smooth if it is not. */
static void slice(cairo_t *cr, cairo_surface_t *page, int sx, int sy, int sw, int sh, int dx, int dy,
	int dw, int dh)
{
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
		return;
	}
	cairo_save(cr);
	cairo_rectangle(cr, dx, dy, dw, dh);
	cairo_clip(cr);
	cairo_translate(cr, dx, dy);
	cairo_scale(cr, (double)dw / sw, (double)dh / sh);
	cairo_translate(cr, -sx, -sy);
	cairo_set_source_surface(cr, page, 0, 0);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_pattern_set_filter(cairo_get_source(cr),
		sw == dw && sh == dh ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);
}

void desk_nb_lay_page(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int w, int h)
{
	int iw = cairo_image_surface_get_width(art->page);
	int ih = cairo_image_surface_get_height(art->page);
	int ow = w + NB_PAD * 2, oh = h + NB_PAD * 2;
	int x0 = x - NB_PAD, y0 = y - NB_PAD;
	/* (the corners as they are and all the rest between them, however little room
	 * there is: left out, the light across the sheet would jump where they meet) */
	int cx = PAGE_CORNER < ow / 3 ? PAGE_CORNER : ow / 3;
	int cy = PAGE_CORNER < oh / 3 ? PAGE_CORNER : oh / 3;
	int sx[3] = { 0, cx, iw - cx }, sw[3] = { cx, iw - cx * 2, cx };
	int sy[3] = { 0, cy, ih - cy }, sh[3] = { cy, ih - cy * 2, cy };
	int dx[3] = { x0, x0 + cx, x0 + ow - cx }, dw[3] = { cx, ow - cx * 2, cx };
	int dy[3] = { y0, y0 + cy, y0 + oh - cy }, dh[3] = { cy, oh - cy * 2, cy };
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) {
			slice(cr, art->page, sx[i], sy[j], sw[i], sh[j], dx[i], dy[j], dw[i], dh[j]);
		}
	}
}

static void small_print(cairo_t *cr, const char *s, double x, double y)
{
	desk_font_select(cr, DESK_FONT_ENGRAVE, s, false, false);
	cairo_set_font_size(cr, 9.5);
	cairo_set_source_rgba(cr, 0.27, 0.44, 0.60, 0.80);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, s);
}

void desk_nb_heading(int w, struct desk_nb_heading *out)
{
	out->base = NB_HEAD_H - 7;
	/* the number after "No.", up to the margin; the title from just past it */
	out->no_x0 = NB_INSET_L + 23;
	out->no_x1 = NB_MARGIN_X - 3;
	out->title_x0 = NB_MARGIN_X + 10;
	out->has_date = w > 430;
	if (!out->has_date) {
		out->title_x1 = w - 62;
		return;
	}
	/* DATE, and room for the year, a dot, the month, a dot, the day */
	out->date_x = w - 172;
	static const double PART[3][2] = { { 31, 67 }, { 74, 98 }, { 105, 129 } };
	for (int i = 0; i < 3; i++) {
		out->part_x0[i] = out->date_x + PART[i][0];
		out->part_x1[i] = out->date_x + PART[i][1];
	}
	out->dot_x[0] = out->date_x + 69;
	out->dot_x[1] = out->date_x + 100;
	out->title_x1 = out->date_x - 12;
}

void desk_nb_lay_print(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int w, int h)
{
	int rule_y = y + NB_HEAD_H - 2;
	/* the margin, from the top of the sheet to its foot */
	desk_nb_line_down(cr, art->red, x + NB_MARGIN_X, y + 2, y + h - 1);
	/* the heading's line, heavier than the rest */
	desk_nb_line_across(cr, art->blue, x + NB_INSET_L, x + w - 1, rule_y);
	desk_nb_line_across(cr, art->blue_faint, x + NB_INSET_L, x + w - 1, rule_y + 1);
	small_print(cr, "No.", x + NB_INSET_L + 3, rule_y - 5);
	struct desk_nb_heading hd;
	desk_nb_heading(w, &hd);
	if (hd.has_date) {
		small_print(cr, "DATE", x + hd.date_x, rule_y - 5);
		small_print(cr, ".", x + hd.dot_x[0], rule_y - 5);
		small_print(cr, ".", x + hd.dot_x[1], rule_y - 5);
	}
}

void desk_nb_lay_tooth(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int w, int h)
{
	cairo_save(cr);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - SHEET_R, y + SHEET_R, SHEET_R, -M_PI / 2.0, 0.0);
	cairo_arc(cr, x + w - SHEET_R, y + h - SHEET_R, SHEET_R, 0.0, M_PI / 2.0);
	cairo_arc(cr, x + SHEET_R, y + h - SHEET_R, SHEET_R, M_PI / 2.0, M_PI);
	cairo_arc(cr, x + SHEET_R, y + SHEET_R, SHEET_R, M_PI, M_PI * 1.5);
	cairo_close_path(cr);
	cairo_clip(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
	cairo_set_source_surface(cr, art->grain, x, y);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* As many turns as there is room for, in the middle of the sheet's side. Each has
 * the shadows of the turns either side of it in its picture, so the two ends are
 * faded out: there is nothing there to cast them. */
void desk_nb_lay_wire(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int h)
{
	int room = h - RING_CLEAR * 2;
	int n = room / RING_P;
	if (n < 1) {
		return;
	}
	int top = y + (h - n * RING_P) / 2 + RING_FIRST;
	int foot = y + (h + n * RING_P) / 2 + RING_TAIL;
	int rw = cairo_image_surface_get_width(art->rings);
	cairo_save(cr);
	cairo_rectangle(cr, x - NB_PAD, top, rw, foot - top);
	cairo_clip(cr);
	cairo_pattern_t *wire = cairo_pattern_create_for_surface(art->rings);
	cairo_matrix_t m;
	cairo_matrix_init_translate(&m, -(double)(x - NB_PAD), -(double)(top - RING_FIRST));
	cairo_pattern_set_matrix(wire, &m);
	cairo_pattern_set_extend(wire, CAIRO_EXTEND_REPEAT);
	cairo_pattern_set_filter(wire, CAIRO_FILTER_NEAREST);
	cairo_set_source(cr, wire);
	cairo_pattern_t *ends = cairo_pattern_create_linear(0, top, 0, foot);
	double span = foot - top;
	cairo_pattern_add_color_stop_rgba(ends, 0.0, 0, 0, 0, 0.0);
	cairo_pattern_add_color_stop_rgba(ends, 3.0 / span, 0, 0, 0, 1.0);
	cairo_pattern_add_color_stop_rgba(ends, (span - 4.0) / span, 0, 0, 0, 1.0);
	cairo_pattern_add_color_stop_rgba(ends, 1.0, 0, 0, 0, 0.0);
	cairo_mask(cr, ends);
	cairo_pattern_destroy(ends);
	cairo_pattern_destroy(wire);
	cairo_restore(cr);
}
