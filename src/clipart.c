/* ================================================================
 * The clipboard's pictures (clipart.h): the board, its clip and the slips, drawn
 * here with cairo and Pango at the screen's own pixels -- words are never a picture
 * made smaller. The board and the clip are drawn once for a size and kept; the
 * slips each time what is kept, or the hand on it, changes.
 * ================================================================ */

#include "clipart.h"
#include "fonts.h"
#include "i18n.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* In centimetres, from the board's top left. */
#define CB_W 11.5
#define CB_H 17.4
#define CB_R 0.55             /* its corners */
#define CB_PAD 0.70           /* round it, for its shadow */
#define CB_SX0 0.75           /* the slips: their sides */
#define CB_SX1 (CB_W - 0.75)
#define CB_TOP 1.25           /* their heads, under the clip */
#define CB_FRONT 7.6          /* the front one's foot */
#define CB_STEP 0.78          /* how much longer each one behind it is */
#define CB_SHOWN DESK_CLIP_SHOWN
#define CB_JAW_W 7.8          /* the clip */
#define CB_JAW_Y0 0.50
#define CB_JAW_Y1 2.05

static double clampf_d(double v, double lo, double hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static uint32_t cb_hash(uint32_t x, uint32_t y)
{
	uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ 0x2c1b3c6du;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	return h ^ (h >> 15);
}

static double cb_noise(double x, double y)
{
	int xi = (int)floor(x), yi = (int)floor(y);
	double fx = x - xi, fy = y - yi;
	fx = fx * fx * (3.0 - 2.0 * fx);
	fy = fy * fy * (3.0 - 2.0 * fy);
	double v00 = (cb_hash((uint32_t)xi, (uint32_t)yi) & 0xffff) / 65535.0;
	double v10 = (cb_hash((uint32_t)xi + 1, (uint32_t)yi) & 0xffff) / 65535.0;
	double v01 = (cb_hash((uint32_t)xi, (uint32_t)yi + 1) & 0xffff) / 65535.0;
	double v11 = (cb_hash((uint32_t)xi + 1, (uint32_t)yi + 1) & 0xffff) / 65535.0;
	return (v00 * (1 - fx) + v10 * fx) * (1 - fy) + (v01 * (1 - fx) + v11 * fx) * fy;
}

static void cb_rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI * 0.5, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI * 0.5);
	cairo_arc(cr, x + r, y + h - r, r, M_PI * 0.5, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, M_PI * 1.5);
	cairo_close_path(cr);
}

/* The board, as it lies, with its shadow: at k, its top left at (pad, pad) cm. The
 * hardboard's face is pressed fibre, brown, a little lighter where the lamp is and
 * dark at its edge. */
static void paint_board(cairo_t *cr, double k, bool shadow)
{
	cairo_save(cr);
	cairo_scale(cr, k, k);
	cairo_translate(cr, CB_PAD, CB_PAD);
	if (shadow) {
		/* soft, a little down and to the right of the lamp */
		for (int i = 6; i >= 1; i--) {
			double g = i * 0.07;
			cb_rounded(cr, 0.10 - g, 0.22 - g, CB_W + 2 * g, CB_H + 2 * g, CB_R + g);
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.055);
			cairo_fill(cr);
		}
	}
	cb_rounded(cr, 0, 0, CB_W, CB_H, CB_R);
	cairo_clip(cr);
	cairo_restore(cr);

	/* the face, a pixel at a time */
	int x0 = (int)floor(CB_PAD * k), y0 = (int)floor(CB_PAD * k);
	int w = (int)ceil(CB_W * k) + 1, h = (int)ceil(CB_H * k) + 1;
	cairo_surface_t *face = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
	unsigned char *px = cairo_image_surface_get_data(face);
	int stride = cairo_image_surface_get_stride(face);
	for (int y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(px + (size_t)y * stride);
		double cy = y / k;
		for (int x = 0; x < w; x++) {
			double cx = x / k;
			/* fibres laid along the board, and the speckle of the press */
			double fib = cb_noise(cx * 2.2, cy * 22.0) * 0.55 + cb_noise(cx * 6.0, cy * 60.0) * 0.30;
			double spk = (cb_hash((uint32_t)(x + x0), (uint32_t)(y + y0)) & 0xff) / 255.0;
			double lamp = 1.0 - 0.10 * (cx / CB_W) - 0.08 * (cy / CB_H);
			double t = (0.88 + fib * 0.18 + (spk - 0.5) * 0.05) * lamp;
			double r = 0.455 * t, g = 0.305 * t, bl = 0.190 * t;
			/* darker as it nears its edge, where the board was cut and rubbed */
			double ex = fmin(cx, CB_W - cx), ey = fmin(cy, CB_H - cy), e = fmin(ex, ey);
			double edge = e < 0.25 ? 0.80 + 0.20 * (e / 0.25) : 1.0;
			r *= edge;
			g *= edge;
			bl *= edge;
			row[x] = (uint32_t)(clampf_d(r, 0, 1) * 255.0) << 16 | (uint32_t)(clampf_d(g, 0, 1) * 255.0) << 8
				| (uint32_t)(clampf_d(bl, 0, 1) * 255.0);
		}
	}
	cairo_surface_mark_dirty(face);
	cairo_set_source_surface(cr, face, x0, y0);
	cairo_paint(cr);
	cairo_surface_destroy(face);
	cairo_reset_clip(cr);

	/* its cut edge: lit along the top and left, in shade along the foot and right */
	cairo_save(cr);
	cairo_scale(cr, k, k);
	cairo_translate(cr, CB_PAD, CB_PAD);
	cb_rounded(cr, 0.03, 0.03, CB_W - 0.06, CB_H - 0.06, CB_R - 0.03);
	cairo_pattern_t *lg = cairo_pattern_create_linear(0, 0, CB_W, CB_H);
	cairo_pattern_add_color_stop_rgba(lg, 0.0, 1.0, 0.88, 0.70, 0.30);
	cairo_pattern_add_color_stop_rgba(lg, 0.5, 1.0, 0.88, 0.70, 0.04);
	cairo_pattern_add_color_stop_rgba(lg, 1.0, 0.0, 0.0, 0.0, 0.35);
	cairo_set_source(cr, lg);
	cairo_set_line_width(cr, 0.06);
	cairo_stroke(cr);
	cairo_pattern_destroy(lg);
	cairo_restore(cr);
}

/* Steel, as a strip across: dark at its edges, bright where it turns to the lamp. */
static cairo_pattern_t *steel(double y0, double y1, double lift)
{
	cairo_pattern_t *p = cairo_pattern_create_linear(0, y0, 0, y1);
	cairo_pattern_add_color_stop_rgb(p, 0.00, 0.42 + lift, 0.44 + lift, 0.47 + lift);
	cairo_pattern_add_color_stop_rgb(p, 0.18, 0.86 + lift, 0.87 + lift, 0.89 + lift);
	cairo_pattern_add_color_stop_rgb(p, 0.40, 0.66 + lift, 0.68 + lift, 0.71 + lift);
	cairo_pattern_add_color_stop_rgb(p, 0.72, 0.52 + lift, 0.54 + lift, 0.57 + lift);
	cairo_pattern_add_color_stop_rgb(p, 1.00, 0.30 + lift, 0.31 + lift, 0.33 + lift);
	return p;
}

/* The clip: a plate riveted to the board, the jaw over the slips' heads with its
 * ribs, and the wire lever lying back over it. Over the slips, with the shadow it
 * throws on them. In cm, the board's top left at the origin. */
static void paint_clip(cairo_t *cr)
{
	double cx = CB_W * 0.5, jx0 = cx - CB_JAW_W * 0.5, jx1 = cx + CB_JAW_W * 0.5;
	/* its shadow on the paper */
	for (int i = 4; i >= 1; i--) {
		double g = i * 0.05;
		cb_rounded(cr, jx0 + 0.08 - g, CB_JAW_Y0 + 0.16 - g, CB_JAW_W + 2 * g,
			CB_JAW_Y1 - CB_JAW_Y0 + 2 * g, 0.35 + g);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.07);
		cairo_fill(cr);
	}
	/* the plate it turns on, under the jaw's head */
	cb_rounded(cr, cx - 2.6, CB_JAW_Y0 - 0.32, 5.2, 0.62, 0.18);
	cairo_pattern_t *p = steel(CB_JAW_Y0 - 0.32, CB_JAW_Y0 + 0.30, -0.08);
	cairo_set_source(cr, p);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(p);
	cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 0.55);
	cairo_set_line_width(cr, 0.025);
	cairo_stroke(cr);
	for (int s = -1; s <= 1; s += 2) {
		/* its rivets */
		double rx = cx + s * 2.15, ry = CB_JAW_Y0 - 0.02;
		cairo_arc(cr, rx, ry, 0.16, 0, 2 * M_PI);
		cairo_pattern_t *rg = cairo_pattern_create_radial(rx - 0.05, ry - 0.06, 0.01, rx, ry, 0.17);
		cairo_pattern_add_color_stop_rgb(rg, 0.0, 0.96, 0.96, 0.97);
		cairo_pattern_add_color_stop_rgb(rg, 0.6, 0.62, 0.63, 0.66);
		cairo_pattern_add_color_stop_rgb(rg, 1.0, 0.30, 0.31, 0.33);
		cairo_set_source(cr, rg);
		cairo_fill(cr);
		cairo_pattern_destroy(rg);
	}
	/* the jaw: wide at its foot where it bites, its corners rounded off */
	cairo_new_path(cr);
	cairo_move_to(cr, jx0 + 1.10, CB_JAW_Y0);
	cairo_line_to(cr, jx1 - 1.10, CB_JAW_Y0);
	cairo_curve_to(cr, jx1 - 0.55, CB_JAW_Y0, jx1, CB_JAW_Y1 - 0.75, jx1, CB_JAW_Y1 - 0.30);
	cairo_curve_to(cr, jx1, CB_JAW_Y1 - 0.08, jx1 - 0.12, CB_JAW_Y1, jx1 - 0.35, CB_JAW_Y1);
	cairo_line_to(cr, jx0 + 0.35, CB_JAW_Y1);
	cairo_curve_to(cr, jx0 + 0.12, CB_JAW_Y1, jx0, CB_JAW_Y1 - 0.08, jx0, CB_JAW_Y1 - 0.30);
	cairo_curve_to(cr, jx0, CB_JAW_Y1 - 0.75, jx0 + 0.55, CB_JAW_Y0, jx0 + 1.10, CB_JAW_Y0);
	cairo_close_path(cr);
	p = steel(CB_JAW_Y0, CB_JAW_Y1, 0.0);
	cairo_set_source(cr, p);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(p);
	cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, 0.60);
	cairo_set_line_width(cr, 0.03);
	cairo_stroke(cr);
	/* its ribs, pressed into it, each lit above and dark below */
	for (int i = 0; i < 3; i++) {
		double ry = CB_JAW_Y0 + 0.55 + i * 0.28, inset = 1.35 - i * 0.20;
		cairo_move_to(cr, jx0 + inset, ry);
		cairo_line_to(cr, jx1 - inset, ry);
		cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 0.45);
		cairo_set_line_width(cr, 0.05);
		cairo_stroke(cr);
		cairo_move_to(cr, jx0 + inset, ry + 0.05);
		cairo_line_to(cr, jx1 - inset, ry + 0.05);
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.45);
		cairo_set_line_width(cr, 0.03);
		cairo_stroke(cr);
	}
	/* the wire lever, lying back over its head */
	cairo_new_path(cr);
	double wy = CB_JAW_Y0 - 0.05;
	cairo_move_to(cr, cx - 2.35, wy);
	cairo_curve_to(cr, cx - 2.35, wy - 0.42, cx - 2.0, wy - 0.55, cx - 1.5, wy - 0.55);
	cairo_line_to(cr, cx + 1.5, wy - 0.55);
	cairo_curve_to(cr, cx + 2.0, wy - 0.55, cx + 2.35, wy - 0.42, cx + 2.35, wy);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
	cairo_set_line_width(cr, 0.16);
	cairo_save(cr);
	cairo_translate(cr, 0.05, 0.10);
	cairo_stroke_preserve(cr);
	cairo_restore(cr);
	cairo_set_source_rgb(cr, 0.40, 0.41, 0.44);
	cairo_set_line_width(cr, 0.13);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgba(cr, 0.97, 0.97, 0.98, 0.85);
	cairo_set_line_width(cr, 0.04);
	cairo_save(cr);
	cairo_translate(cr, -0.01, -0.025);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static PangoLayout *words_layout(cairo_t *cr, const char *s, double size, bool bold)
{
	PangoLayout *pl = pango_cairo_create_layout(cr);
	/* drawn in centimetres: metrics rounded to whole units of those would put a line
	 * every two of them */
	PangoContext *ctx = pango_layout_get_context(pl);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
	pango_cairo_context_set_font_options(ctx, fo);
	cairo_font_options_destroy(fo);
	pango_context_set_round_glyph_positions(ctx, FALSE);
	pango_layout_context_changed(pl);
	PangoFontDescription *fd = pango_font_description_new();
	pango_font_description_set_family(fd, desk_font_family(DESK_FONT_TYPE, s));
	pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(pl, s, -1);
	return pl;
}

/* How big a layout is, in the units it is drawn in (centimetres: not rounded to
 * whole ones, as pango_layout_get_pixel_size would). */
static void layout_size(PangoLayout *pl, double *w, double *h)
{
	int pw, ph;
	pango_layout_get_size(pl, &pw, &ph);
	*w = (double)pw / PANGO_SCALE;
	*h = (double)ph / PANGO_SCALE;
}

/* The words of one, as they lie on a slip: tabs as spaces, and at most `most` bytes. */
static void words_of(const struct desk_clip *c, char *out, size_t n, bool one_line)
{
	size_t o = 0;
	const unsigned char *s = c->data;
	size_t len = c->len ? c->len - 1 : 0;
	size_t i = 0;
	/* (a line of nothing first is skipped, on a foot) */
	if (one_line) {
		while (i < len && (s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t')) {
			i++;
		}
	}
	for (; i < len && o + 4 < n; i++) {
		unsigned char ch = s[i];
		if (ch == '\r') {
			continue;
		}
		if (ch == '\n' && one_line) {
			break;
		}
		out[o++] = ch == '\t' ? ' ' : (char)ch;
	}
	/* not cut in the middle of a character */
	while (o > 0 && ((unsigned char)out[o - 1] & 0xc0) == 0x80) {
		size_t k = o - 1;
		while (k > 0 && ((unsigned char)out[k] & 0xc0) == 0x80) {
			k--;
		}
		unsigned char lead = (unsigned char)out[k];
		size_t want = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		if (o - k >= want) {
			break;
		}
		o = k;
	}
	out[o] = '\0';
}

static void when_of(const struct desk_clip *c, char *out, size_t n)
{
	struct tm tm;
	localtime_r(&c->at, &tm);
	snprintf(out, n, "%d:%02d", tm.tm_hour, tm.tm_min);
}

/* A slip of paper, its head under the clip and its foot at foot, a little off
 * straight as it was clipped. */
static void slip_paper(cairo_t *cr, double foot, unsigned seed, bool hot)
{
	double off = ((seed & 0xff) / 255.0 - 0.5) * 0.10;
	double x0 = CB_SX0 + off, x1 = CB_SX1 + off;
	/* its shadow on what is behind it */
	cairo_rectangle(cr, x0 + 0.04, CB_TOP, x1 - x0, foot - CB_TOP + 0.07);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.16);
	cairo_fill(cr);
	cairo_rectangle(cr, x0, CB_TOP, x1 - x0, foot - CB_TOP);
	double tone = 0.955 + ((seed >> 8 & 0xff) / 255.0 - 0.5) * 0.03 + (hot ? 0.025 : 0.0);
	cairo_set_source_rgb(cr, tone, tone * 0.985, tone * 0.950);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.30, 0.26, 0.20, 0.30);
	cairo_set_line_width(cr, 0.018);
	cairo_stroke(cr);
}

/* The foot of slip j, behind the front: what it says, from its head, and when. */
static void slip_foot_words(cairo_t *cr, const struct desk_clip *c, double foot, bool hot)
{
	double y0 = foot - CB_STEP, tx = CB_SX0 + 0.35, tw = CB_SX1 - CB_SX0 - 0.70;
	char when[16];
	when_of(c, when, sizeof when);
	PangoLayout *wl = words_layout(cr, when, 0.36, false);
	double wwid, wh;
	layout_size(wl, &wwid, &wh);
	double tsize = 0.44;
	double right = tx + tw;
	cairo_set_source_rgba(cr, 0.40, 0.36, 0.30, 0.85);
	cairo_move_to(cr, right - wwid, y0 + (CB_STEP - wh) * 0.5);
	pango_cairo_show_layout(cr, wl);
	g_object_unref(wl);
	double room = tw - wwid - 0.30;
	double lx = tx;
	char line[512];
	if (c->kind == DESK_CLIP_IMAGE) {
		/* a picture: the least of it, and how big */
		if (c->picture) {
			double ph = CB_STEP * 0.72, pw = ph * cairo_image_surface_get_width(c->picture)
				/ fmax(cairo_image_surface_get_height(c->picture), 1);
			pw = fmin(pw, 1.6);
			cairo_save(cr);
			cairo_rectangle(cr, lx, y0 + (CB_STEP - ph) * 0.5, pw, ph);
			cairo_clip(cr);
			double s = fmax(pw / cairo_image_surface_get_width(c->picture),
				ph / cairo_image_surface_get_height(c->picture));
			cairo_translate(cr, lx, y0 + (CB_STEP - ph) * 0.5);
			cairo_scale(cr, s, s);
			cairo_set_source_surface(cr, c->picture, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_restore(cr);
			lx += pw + 0.20;
			room -= pw + 0.20;
		}
		if (c->img_w > 0) {
			snprintf(line, sizeof line, "%s  %d × %d", TR("Picture"), c->img_w, c->img_h);
		} else {
			snprintf(line, sizeof line, "%s  %s", TR("Picture"), c->mime);
		}
	} else {
		words_of(c, line, sizeof line, true);
	}
	PangoLayout *pl = words_layout(cr, line, tsize, false);
	pango_layout_set_width(pl, (int)(fmax(room, 0.5) * PANGO_SCALE));
	pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
	pango_layout_set_single_paragraph_mode(pl, true);
	cairo_set_source_rgba(cr, 0.14, 0.12, 0.10, hot ? 1.0 : 0.92);
	double lw, lh;
	layout_size(pl, &lw, &lh);
	(void)lw;
	cairo_move_to(cr, lx, y0 + (CB_STEP - lh) * 0.5);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
}

/* The front slip's face: all of what it says that it has room for, or the picture. */
static void slip_front_words(cairo_t *cr, const struct desk_clip *c, bool current)
{
	double tx = CB_SX0 + 0.45, tw = CB_SX1 - CB_SX0 - 0.90;
	double ty = CB_JAW_Y1 + 0.35, th = CB_FRONT - 0.30 - ty;
	if (!c) {
		PangoLayout *pl = words_layout(cr, TR("Nothing has been copied yet"), 0.42, false);
		pango_layout_set_width(pl, (int)(tw * PANGO_SCALE));
		pango_layout_set_alignment(pl, PANGO_ALIGN_CENTER);
		pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
		double pw, ph;
		layout_size(pl, &pw, &ph);
		(void)pw;
		cairo_set_source_rgba(cr, 0.35, 0.31, 0.26, 0.75);
		cairo_move_to(cr, tx, ty + (th - ph) * 0.5);
		pango_cairo_show_layout(cr, pl);
		g_object_unref(pl);
		return;
	}
	/* when, at its head on the right, and a mark when it is what is copied now */
	char when[16];
	when_of(c, when, sizeof when);
	PangoLayout *wl = words_layout(cr, when, 0.38, false);
	double ww, wh;
	layout_size(wl, &ww, &wh);
	(void)wh;
	cairo_set_source_rgba(cr, 0.40, 0.36, 0.30, 0.85);
	cairo_move_to(cr, tx + tw - ww, ty - 0.05);
	pango_cairo_show_layout(cr, wl);
	g_object_unref(wl);
	if (current) {
		/* a tick in pencil: this is what pasting gives */
		cairo_move_to(cr, tx + tw - ww - 0.62, ty + 0.17);
		cairo_line_to(cr, tx + tw - ww - 0.47, ty + 0.33);
		cairo_line_to(cr, tx + tw - ww - 0.20, ty + 0.00);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_set_source_rgba(cr, 0.30, 0.30, 0.32, 0.80);
		cairo_set_line_width(cr, 0.055);
		cairo_stroke(cr);
	}
	ty += 0.55;
	th -= 0.55;
	if (c->kind == DESK_CLIP_IMAGE) {
		/* the picture, as a print with a white border, laid a little turned */
		char cap[96];
		if (c->img_w > 0) {
			snprintf(cap, sizeof cap, "%d × %d", c->img_w, c->img_h);
		} else {
			snprintf(cap, sizeof cap, "%s", c->mime);
		}
		PangoLayout *cl = words_layout(cr, cap, 0.34, false);
		double capw, caph;
		layout_size(cl, &capw, &caph);
		(void)caph;
		double room_h = th - 0.55;
		if (c->picture) {
			double iw = cairo_image_surface_get_width(c->picture);
			double ih = cairo_image_surface_get_height(c->picture);
			double s = fmin((tw - 0.60) / iw, (room_h - 0.40) / ih);
			double pw = iw * s, ph = ih * s;
			double mx = tx + tw * 0.5, my = ty + room_h * 0.5;
			cairo_save(cr);
			cairo_translate(cr, mx, my);
			cairo_rotate(cr, ((c->id * 37u % 100u) / 100.0 - 0.5) * 0.05);
			double bw = 0.18;
			cairo_rectangle(cr, -pw * 0.5 - bw + 0.05, -ph * 0.5 - bw + 0.10, pw + 2 * bw, ph + 2 * bw);
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.20);
			cairo_fill(cr);
			cairo_rectangle(cr, -pw * 0.5 - bw, -ph * 0.5 - bw, pw + 2 * bw, ph + 2 * bw);
			cairo_set_source_rgb(cr, 0.985, 0.98, 0.97);
			cairo_fill(cr);
			cairo_translate(cr, -pw * 0.5, -ph * 0.5);
			cairo_scale(cr, s, s);
			cairo_set_source_surface(cr, c->picture, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_restore(cr);
		} else {
			cairo_rectangle(cr, tx + 0.3, ty + 0.2, tw - 0.6, room_h - 0.4);
			cairo_set_source_rgba(cr, 0.30, 0.26, 0.20, 0.35);
			cairo_set_line_width(cr, 0.03);
			cairo_stroke(cr);
		}
		cairo_set_source_rgba(cr, 0.30, 0.27, 0.22, 0.90);
		cairo_move_to(cr, tx + (tw - capw) * 0.5, ty + th - 0.45);
		pango_cairo_show_layout(cr, cl);
		g_object_unref(cl);
		return;
	}
	char *text = malloc(4096);
	if (!text) {
		return;
	}
	words_of(c, text, 4096, false);
	PangoLayout *pl = words_layout(cr, text, 0.47, false);
	free(text);
	pango_layout_set_width(pl, (int)(tw * PANGO_SCALE));
	pango_layout_set_height(pl, (int)(th * PANGO_SCALE));
	pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
	cairo_set_source_rgba(cr, 0.12, 0.10, 0.09, 0.96);
	cairo_move_to(cr, tx, ty);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
}


/* ---- the slips, for a look ---- */

/* how many slips there are to show: the front one and the feet behind it */
static int slips_shown(const struct desk_clip_look *lk)
{
	int behind = lk->n - 1 - lk->scroll;
	behind = behind < 0 ? 0 : behind > CB_SHOWN ? CB_SHOWN : behind;
	return lk->n > 0 ? 1 + behind : 1;
}

/* the one at place j of those shown (0 the front): its place in clips */
static int slip_clip(const struct desk_clip_look *lk, int j)
{
	return j == 0 ? 0 : j + lk->scroll;
}

static double slip_foot(int j)
{
	return CB_FRONT + j * CB_STEP;
}

static struct {
	cairo_surface_t *board, *clip;
	double k;
} g_art;

void desk_clip_art_drop(void)
{
	if (g_art.board) {
		cairo_surface_destroy(g_art.board);
	}
	if (g_art.clip) {
		cairo_surface_destroy(g_art.clip);
	}
	memset(&g_art, 0, sizeof g_art);
}

void desk_clip_art_size(double k, int *w, int *h)
{
	*w = (int)ceil((CB_W + 2.0 * CB_PAD) * k);
	*h = (int)ceil((CB_H + 2.0 * CB_PAD) * k);
}

double desk_clip_art_pad(double k)
{
	return CB_PAD * k;
}

static void pictures_make(double k)
{
	if (g_art.board && fabs(g_art.k - k) < 1e-9) {
		return;
	}
	desk_clip_art_drop();
	g_art.k = k;
	int w, h;
	desk_clip_art_size(k, &w, &h);
	g_art.board = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(g_art.board);
	paint_board(cr, k, true);
	cairo_destroy(cr);
	g_art.clip = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cr = cairo_create(g_art.clip);
	cairo_scale(cr, k, k);
	cairo_translate(cr, CB_PAD, CB_PAD);
	paint_clip(cr);
	cairo_destroy(cr);
}

void desk_clip_art_paint(cairo_t *cr, double k, const struct desk_clip_look *lk)
{
	pictures_make(k);
	cairo_save(cr);
	cairo_set_source_surface(cr, g_art.board, 0, 0);
	cairo_paint(cr);
	cairo_scale(cr, k, k);
	cairo_translate(cr, CB_PAD, CB_PAD);
	int shown = slips_shown(lk);
	double settle = clampf_d(lk->settle, 0.0, 1.0);
	for (int j = shown - 1; j >= 0; j--) {
		int i = slip_clip(lk, j);
		const struct desk_clip *c = i < lk->n ? lk->clips[i] : NULL;
		bool hot = c && lk->hover == i;
		cairo_save(cr);
		if (j == 0 && settle < 1.0) {
			/* just come: dropping the last of the way in under the clip */
			double e = 1.0 - (1.0 - settle) * (1.0 - settle);
			cairo_translate(cr, 0.0, -0.45 * (1.0 - e));
		}
		slip_paper(cr, slip_foot(j), c ? c->id * 2654435761u : 7u, hot && j > 0);
		if (j == 0) {
			slip_front_words(cr, c, lk->current);
		} else if (c) {
			slip_foot_words(cr, c, slip_foot(j), hot);
		}
		cairo_restore(cr);
	}
	/* more of them than show: the edges of the rest, a line each, under the last foot */
	int more = lk->n - 1 - lk->scroll - CB_SHOWN;
	for (int m = 1; m <= (more > 3 ? 3 : more); m++) {
		double y = slip_foot(shown - 1) + m * 0.06;
		cairo_move_to(cr, CB_SX0 + 0.05, y);
		cairo_line_to(cr, CB_SX1 - 0.05, y);
		cairo_set_source_rgba(cr, 0.92, 0.91, 0.87, 0.85);
		cairo_set_line_width(cr, 0.045);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
	cairo_set_source_surface(cr, g_art.clip, 0, 0);
	cairo_paint(cr);
}

int desk_clip_art_at(double k, const struct desk_clip_look *lk, double lx, double ly, int *idx)
{
	*idx = -1;
	double x = lx / k - CB_PAD, y = ly / k - CB_PAD;
	double cx = CB_W * 0.5;
	if (x >= cx - CB_JAW_W * 0.5 && x <= cx + CB_JAW_W * 0.5 && y >= CB_JAW_Y0 - 0.65 && y <= CB_JAW_Y1) {
		return DESK_CLIP_JAW;
	}
	if (x < 0.0 || x > CB_W || y < 0.0 || y > CB_H) {
		return DESK_CLIP_OFF;
	}
	if (x >= CB_SX0 && x <= CB_SX1 && y >= CB_TOP) {
		int shown = slips_shown(lk);
		if (y <= CB_FRONT) {
			*idx = 0;
			return lk->n > 0 ? DESK_CLIP_SLIP : DESK_CLIP_BOARD;
		}
		int j = (int)ceil((y - CB_FRONT) / CB_STEP);
		if (j >= 1 && j < shown) {
			*idx = slip_clip(lk, j);
			return DESK_CLIP_SLIP;
		}
	}
	return DESK_CLIP_BOARD;
}

/* Lying in the drawer: the board, the clip and a blank slip, its long side 2.25 of
 * the room's units, turned as it happens to lie. */
cairo_surface_t *desk_clip_art_lying(double ppu, double turn, double *ax, double *ay)
{
	double k = 2.25 * ppu / CB_H;
	double cw = (CB_W + 2.0 * CB_PAD) * k, ch = (CB_H + 2.0 * CB_PAD) * k;
	double diag = hypot(cw, ch) + 4.0;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(diag), (int)ceil(diag));
	cairo_surface_t *flat = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(cw), (int)ceil(ch));
	cairo_t *cr = cairo_create(flat);
	paint_board(cr, k, false);
	cairo_scale(cr, k, k);
	cairo_translate(cr, CB_PAD, CB_PAD);
	slip_paper(cr, CB_FRONT + 2 * CB_STEP, 11u, false);
	slip_paper(cr, CB_FRONT + CB_STEP, 23u, false);
	slip_paper(cr, CB_FRONT, 5u, false);
	paint_clip(cr);
	cairo_destroy(cr);
	cr = cairo_create(img);
	cairo_translate(cr, diag * 0.5, diag * 0.5);
	cairo_rotate(cr, turn);
	cairo_translate(cr, -cw * 0.5, -ch * 0.5);
	cairo_set_source_surface(cr, flat, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(flat);
	cairo_surface_flush(img);
	*ax = *ay = diag * 0.5;
	return img;
}

