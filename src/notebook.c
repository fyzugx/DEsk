/* The notebook desk-notepad's window is the top sheet of (notebook.h): its
 * sheet, as nbink.c lays it, the wire down its side, and shutting it. */

#include "notebook.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <stdio.h>

#include "desk.h"
#include "fonts.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
	/* the shut picture's paper label, as art/notebook/notebook.py has it */
	LABEL_X0 = 168, LABEL_X1 = 510, LABEL_Y0 = 189, LABEL_Y1 = 323,
};

static struct {
	bool tried, ok, shut_tried;
	struct desk_nb_art art;
	cairo_surface_t *shut;
} g;

static cairo_surface_t *picture(const char *file)
{
	char path[1200];
	if (!desk_art_find("notebook", file, path, sizeof path)) {
		fprintf(stderr, "desk: the notebook's picture is not there (%s)\n", path);
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "desk: the notebook's picture would not read (%s: %s)\n", path,
			cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

bool desk_notebook_ready(void)
{
	if (!g.tried) {
		g.tried = true;
		g.art.page = picture("nb-page.png");
		g.art.rings = picture("nb-rings.png");
		g.art.grain = picture("nb-grain.png");
		g.ok = g.art.page && g.art.rings && g.art.grain;
		g.art.blue = desk_nb_ink(NB_BLUE, NB_BLUE_A);
		g.art.blue_faint = desk_nb_ink(NB_BLUE, NB_BLUE_A * 0.62);
		g.art.red = desk_nb_ink(NB_RED, NB_RED_A);
	}
	return g.ok;
}

void desk_draw_notebook(cairo_t *cr, double x, double y, double w, double h)
{
	if (!desk_notebook_ready()) {
		return;
	}
	int ix = (int)lround(x), iy = (int)lround(y), iw = (int)lround(w), ih = (int)lround(h);
	cairo_save(cr);
	desk_nb_lay_page(cr, &g.art, ix, iy, iw, ih);
	desk_nb_lay_print(cr, &g.art, ix, iy, iw, ih);
	desk_nb_lay_tooth(cr, &g.art, ix, iy, iw, ih);
	desk_nb_lay_wire(cr, &g.art, ix, iy, ih);
	cairo_restore(cr);
}

void desk_draw_notebook_next(cairo_t *cr, double x, double y, double size)
{
	if (!desk_notebook_ready()) {
		return;
	}
	int ix = (int)lround(x), iy = (int)lround(y), s = (int)ceil(size);
	cairo_save(cr);
	cairo_rectangle(cr, ix - s, iy, s, s);
	cairo_clip(cr);
	/* the page's own top right corner, printed the same, and the sheet's tooth */
	int iw = cairo_image_surface_get_width(g.art.page);
	cairo_set_source_surface(cr, g.art.page, ix + NB_PAD - iw, iy - NB_PAD);
	cairo_paint(cr);
	for (int ry = iy + NB_HEAD_H - 2; ry < iy + s; ry += 29) {
		desk_nb_line_across(cr, g.art.blue, ix - s, ix - 1, ry);
	}
	desk_nb_lay_tooth(cr, &g.art, ix - s, iy, s, s);
	cairo_restore(cr);
}

/* ---- shutting it ------------------------------------------------------------
 *
 * The cover comes round the wire from the left and down over the sheet: seen from
 * above, it narrows to its edge as it stands up and widens again as it comes down,
 * and what is further from its hinge is nearer the eye, and a little larger. */

/* (read the first time it is shut: most are not) */
static bool shut_ready(void)
{
	if (!g.shut && desk_notebook_ready() && !g.shut_tried) {
		g.shut_tried = true;
		g.shut = picture("nb-shut.png");
	}
	return g.shut != NULL;
}

static void shut_paint(cairo_t *cr, double w, double h)
{
	if (!shut_ready()) {
		return;
	}
	cairo_save(cr);
	cairo_scale(cr, w / cairo_image_surface_get_width(g.shut),
		h / cairo_image_surface_get_height(g.shut));
	cairo_set_source_surface(cr, g.shut, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

double desk_notebook_shut_aspect(void)
{
	if (!shut_ready()) {
		return 1.37;
	}
	return (double)cairo_image_surface_get_height(g.shut) / cairo_image_surface_get_width(g.shut);
}

void desk_notebook_shutting(cairo_t *cr, cairo_surface_t *open, cairo_surface_t *cover,
	double shut)
{
	if (!shut_ready()) {
		return;
	}
	shut = shut < 0.0 ? 0.0 : (shut > 1.0 ? 1.0 : shut);
	int ow = cairo_image_surface_get_width(open), oh = cairo_image_surface_get_height(open);
	double w = ow - NB_PAD * 2, h = oh - NB_PAD * 2;
	double hinge = NB_PAD;
	double e = shut * shut * (3.0 - 2.0 * shut);
	double th = (1.0 - e) * 112.0 * M_PI / 180.0;
	double c = cos(th), sn = sin(th);
	cairo_set_source_surface(cr, open, 0, 0);
	cairo_paint(cr);

	/* what it throws on the sheet as it comes down */
	if (c > 0.0) {
		double reach = hinge + w * c + 46.0 * sn;
		cairo_pattern_t *sh = cairo_pattern_create_linear(hinge, 0, reach, 0);
		cairo_pattern_add_color_stop_rgba(sh, 0.0, 0.04, 0.03, 0.03, 0.46);
		cairo_pattern_add_color_stop_rgba(sh, 0.82, 0.04, 0.03, 0.03, 0.34);
		cairo_pattern_add_color_stop_rgba(sh, 1.0, 0.04, 0.03, 0.03, 0.0);
		cairo_set_source(cr, sh);
		cairo_rectangle(cr, hinge, NB_PAD, reach - hinge, h);
		cairo_fill(cr);
		cairo_pattern_destroy(sh);
	}

	/* the cover, in strips: its outside if it is past standing up, and the inside
	 * of its board before that */
	enum { STRIPS = 30 };
	int iw = cairo_image_surface_get_width(cover), ih = cairo_image_surface_get_height(cover);
	/* (how far its sheet starts, in a picture of it that size) */
	double px = NB_PAD * (double)iw / cairo_image_surface_get_width(g.shut);
	double py = NB_PAD * (double)ih / cairo_image_surface_get_height(g.shut);
	double appear = shut / 0.07;
	appear = appear > 1.0 ? 1.0 : appear;
	/* (lit from above: at its brightest flat, dim on edge) */
	double dim = 0.60 * (1.0 - fabs(c));
	for (int i = 0; i < STRIPS; i++) {
		double u0 = (double)i / STRIPS, u1 = (double)(i + 1) / STRIPS;
		double x0 = hinge + u0 * w * c, x1 = hinge + u1 * w * c;
		if (x1 < x0) {
			double t = x0;
			x0 = x1;
			x1 = t;
		}
		if (x1 - x0 < 0.05) {
			continue;
		}
		double k = 1.0 + 0.5 * (u0 + u1) * sn * 0.14;
		double y0 = NB_PAD + h * 0.5 - h * 0.5 * k, dh = h * k;
		cairo_save(cr);
		cairo_rectangle(cr, floor(x0), y0, ceil(x1) - floor(x0), dh);
		cairo_clip(cr);
		if (c >= 0.0) {
			/* the picture's own cover, the same part of it */
			double su = px + u0 * (iw - px * 2.0), sw = (iw - px * 2.0) / STRIPS;
			cairo_translate(cr, x0, y0);
			cairo_scale(cr, (x1 - x0) / sw, dh / (ih - py * 2.0));
			cairo_translate(cr, -su, -py);
			cairo_set_source_surface(cr, cover, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
			cairo_paint_with_alpha(cr, appear);
		} else {
			cairo_set_source_rgba(cr, 0.052, 0.074, 0.13, appear);
			cairo_paint(cr);
		}
		cairo_restore(cr);
		cairo_set_source_rgba(cr, 0.02, 0.02, 0.03, dim * appear);
		cairo_rectangle(cr, floor(x0), y0, ceil(x1) - floor(x0), dh);
		cairo_fill(cr);
	}
	/* down: the whole of it, wire and stack */
	double last = (shut - 0.84) / 0.16;
	if (last > 0.0) {
		cairo_set_source_surface(cr, cover, 0, 0);
		cairo_paint_with_alpha(cr, last > 1.0 ? 1.0 : last);
	}
}

cairo_surface_t *desk_notebook_shut_named(const char *name, int w, int h)
{
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(out);
	shut_paint(cr, w, h);
	if (name && *name && g.shut) {
		double kx = (double)w / cairo_image_surface_get_width(g.shut);
		double ky = (double)h / cairo_image_surface_get_height(g.shut);
		double x0 = LABEL_X0 * kx, x1 = LABEL_X1 * kx, y0 = LABEL_Y0 * ky, y1 = LABEL_Y1 * ky;
		double pad = (x1 - x0) * 0.07;
		PangoLayout *pl = pango_cairo_create_layout(cr);
		PangoFontDescription *fd = pango_font_description_new();
		pango_font_description_set_family(fd, desk_font_family(DESK_FONT_HAND, name));
		pango_font_description_set_absolute_size(fd, (y1 - y0) * 0.40 * PANGO_SCALE);
		pango_layout_set_font_description(pl, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(pl, name, -1);
		pango_layout_set_single_paragraph_mode(pl, TRUE);
		pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
		pango_layout_set_width(pl, (int)((x1 - x0 - pad * 2.0) * PANGO_SCALE));
		pango_layout_set_alignment(pl, PANGO_ALIGN_CENTER);
		int pw, ph;
		pango_layout_get_pixel_size(pl, &pw, &ph);
		cairo_set_source_rgba(cr, 0.106, 0.165, 0.341, 0.92);
		cairo_move_to(cr, x0 + pad, (y0 + y1 - ph) * 0.5);
		pango_cairo_show_layout(cr, pl);
		g_object_unref(pl);
	}
	cairo_destroy(cr);
	cairo_surface_flush(out);
	return out;
}

static pthread_once_t g_top_once = PTHREAD_ONCE_INIT;
static cairo_surface_t *g_top;

static void top_load(void)
{
	g_top = picture("nb-top.png");
}

cairo_surface_t *desk_notebook_top_lying(double ppu, double turn, double *ax, double *ay)
{
	pthread_once(&g_top_once, top_load);
	if (!g_top) {
		return NULL;
	}
	double pw = cairo_image_surface_get_width(g_top);
	double ph = cairo_image_surface_get_height(g_top);
	/* as long as the pad of paper that lies there for another editor */
	double k = 2.4 * ppu / ph;
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
