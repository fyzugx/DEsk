/* ================================================================
 * The slider phone round a picture-in-picture window (phone.h).
 *
 * The pictures are the phone with nothing on it and its keys one by one, each
 * faded at its edge into what it lies on. A window can be any size, so the slab
 * keeps its corners and stretches between them, and the deck keeps both its ends
 * and stretches its middle, whose polish changes only up and down. On a slab too
 * narrow for the deck, the whole deck is laid smaller.
 * ================================================================ */

#include "phone.h"
#include "desk.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Besides what art/phone/phone.py says (phone-map.h). */
enum {
	SLAB_CORNER_X = PH_PAD + PH_L + 8,   /* (the window's edge is kept with the corner) */
	SLAB_CORNER_Y = PH_PAD + 46,         /* (and all of the round of the corner) */
	KEY_HIT = 22,
	HOME_HALF_W = 18, HOME_HALF_H = 34,  /* what is on the button */
	DECK_INSET = 10,                     /* the plate, in from the slab's edge */
	DECK_END = 110,                      /* what is kept at each end */
	N_KEYS = 3,
};

static const int KEY_WHICH[N_KEYS] = { PH_KEY_BACK, PH_KEY_FWD, PH_KEY_OPEN };

static struct {
	bool tried, ok;
	cairo_surface_t *slab, *ear, *deck, *keys;
} g;

static cairo_surface_t *picture(const char *file)
{
	char path[1200];
	if (!desk_art_find("phone", file, path, sizeof path)) {
		fprintf(stderr, "desk: the phone's picture is not there (%s)\n", path);
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "desk: the phone's picture would not read (%s: %s)\n", path,
			cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

bool desk_phone_ready(void)
{
	if (!g.tried) {
		g.tried = true;
		g.slab = picture("ph-slab.png");
		g.ear = picture("ph-ear.png");
		g.deck = picture("ph-deck.png");
		g.keys = picture("ph-keys.png");
		g.ok = g.slab && g.ear && g.deck && g.keys;
	}
	return g.ok;
}

/* Part of a picture, sw by sh, made dw by dh. */
static void slice(cairo_t *cr, cairo_surface_t *pic, double sx, double sy, double sw, double sh,
	double dx, double dy, double dw, double dh)
{
	if (sw <= 0.0 || sh <= 0.0 || dw <= 0.0 || dh <= 0.0) {
		return;
	}
	cairo_save(cr);
	cairo_rectangle(cr, dx, dy, dw, dh);
	cairo_clip(cr);
	cairo_translate(cr, dx, dy);
	cairo_scale(cr, dw / sw, dh / sh);
	cairo_translate(cr, -sx, -sy);
	cairo_set_source_surface(cr, pic, 0, 0);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_pattern_set_filter(cairo_get_source(cr),
		sw == dw && sh == dh ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* A piece of ph-keys.png, its middle at (cx, cy). */
static void piece(cairo_t *cr, int cell, bool down, double cx, double cy)
{
	cairo_save(cr);
	double x = cx - PH_CELL_W * 0.5, y = cy - PH_CELL_H * 0.5;
	cairo_rectangle(cr, x, y, PH_CELL_W, PH_CELL_H);
	cairo_clip(cr);
	cairo_set_source_surface(cr, g.keys, x - cell * PH_CELL_W, y - (down ? PH_CELL_H : 0));
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* The lights' heights, and how big the button can be between them: on a small window
 * the lights go nearer the corners and the button is made smaller. */
static double slab_keys(double h, double *y0, double *y1)
{
	double k = h >= 190.0 ? PH_LIGHT_Y : (h >= 120.0 ? 24.0 + (h - 120.0) * 20.0 / 70.0 : 24.0);
	*y0 = k;
	*y1 = h - k;
	double room = h - 2.0 * k - 30.0;
	double s = room / (HOME_HALF_H * 2.0);
	return s > 1.0 ? 1.0 : (s < 0.3 ? 0.3 : s);
}

void desk_draw_phone(cairo_t *cr, double x, double y, double w, double h, int held, bool deck_out)
{
	if (!desk_phone_ready()) {
		return;
	}
	int iw = cairo_image_surface_get_width(g.slab), ih = cairo_image_surface_get_height(g.slab);
	double ow = w + PH_PAD * 2, oh = h + PH_PAD * 2, x0 = x - PH_PAD, y0 = y - PH_PAD;
	int cx = SLAB_CORNER_X, cy = SLAB_CORNER_Y;
	double sx[3] = { 0, cx, iw - cx }, sw[3] = { cx, iw - cx * 2.0, cx };
	double sy[3] = { 0, cy, ih - cy }, sh[3] = { cy, ih - cy * 2.0, cy };
	double dx[3] = { x0, x0 + cx, x0 + ow - cx }, dw[3] = { cx, ow - cx * 2.0, cx };
	double dy[3] = { y0, y0 + cy, y0 + oh - cy }, dh[3] = { cy, oh - cy * 2.0, cy };
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) {
			slice(cr, g.slab, sx[i], sy[j], sw[i], sh[j], dx[i], dy[j], dw[i], dh[j]);
		}
	}
	/* the earpiece and the lens, if it is tall enough for them */
	if (h >= 130.0) {
		cairo_save(cr);
		cairo_rectangle(cr, x, y + 8.0, w, h - 16.0);
		cairo_clip(cr);
		cairo_set_source_surface(cr, g.ear, x + PH_EAR_X0, y + floor(h * 0.5) - PH_EAR_ABOVE);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	double k0, k1;
	double s = slab_keys(h, &k0, &k1);
	piece(cr, PH_CELL_CLOSE, held == PH_KEY_CLOSE, x + w - PH_LIGHT_X, y + k0);
	piece(cr, deck_out ? PH_CELL_UP : PH_CELL_DOWN, held == PH_KEY_DECK, x + w - PH_LIGHT_X, y + k1);
	cairo_save(cr);
	cairo_translate(cr, x + w - PH_LIGHT_X, y + floor(h * 0.5));
	if (s != 1.0) {
		cairo_scale(cr, s, s);
		cairo_rectangle(cr, -HOME_HALF_W - 8.0, -HOME_HALF_H - 8.0, HOME_HALF_W * 2.0 + 16.0, HOME_HALF_H * 2.0 + 16.0);
		cairo_clip(cr);
	}
	piece(cr, PH_CELL_HOME, held == PH_KEY_PLAY, 0.0, 0.0);
	cairo_restore(cr);
}

int desk_phone_slab_hit(double w, double h, double lx, double ly)
{
	double k0, k1;
	double s = slab_keys(h, &k0, &k1);
	if (fabs(lx - (w - PH_LIGHT_X)) <= HOME_HALF_W && fabs(ly - floor(h * 0.5)) <= HOME_HALF_H * s) {
		return PH_KEY_PLAY;
	}
	if (hypot(lx - (w - PH_LIGHT_X), ly - k0) <= KEY_HIT) {
		return PH_KEY_CLOSE;
	}
	if (hypot(lx - (w - PH_LIGHT_X), ly - k1) <= KEY_HIT) {
		return PH_KEY_DECK;
	}
	return PH_KEY_NONE;
}

/* ---- the deck ---------------------------------------------------- */

/* Its length at its own scale, and that scale. */
static void deck_scale(int slab_w, double *len, double *scale)
{
	double room = slab_w - DECK_INSET * 2.0;
	if (room < PH_DECK_W) {
		*len = PH_DECK_W;
		*scale = room > 40.0 ? room / PH_DECK_W : 40.0 / PH_DECK_W;
	} else {
		*len = room;
		*scale = 1.0;
	}
}

void desk_phone_deck_size(int slab_w, int slab_h, int *pw, int *ph, double *scale, int *x, int *y)
{
	double len, k;
	deck_scale(slab_w, &len, &k);
	*pw = (int)ceil((len + PH_DECK_SIDE * 2.0) * k);
	*ph = (int)ceil((PH_DECK_H + PH_DECK_FOOT) * k);
	*scale = k;
	*x = (int)lround(DECK_INSET - PH_DECK_SIDE * k);
	*y = (int)lround(slab_h - PH_DECK_HID * k);
}

/* Where each key's left goes on a deck of that length: the gaps take what there is. */
static void key_x(double len, double xs[N_KEYS])
{
	double gap = (len - PH_ROW_LEAD * 2.0 - PH_ROW_WIDE * (double)N_KEYS) / (N_KEYS - 1);
	for (int i = 0; i < N_KEYS; i++) {
		xs[i] = PH_ROW_LEAD + i * (PH_ROW_WIDE + gap);
	}
}

static void time_text(char *s, size_t n, double t)
{
	if (t < 0.0) {
		snprintf(s, n, "--:--");
		return;
	}
	long v = (long)t;
	if (v >= 3600) {
		snprintf(s, n, "%ld:%02ld:%02ld", v / 3600, v / 60 % 60, v % 60);
	} else {
		snprintf(s, n, "%02ld:%02ld", v / 60, v % 60);
	}
}

/* A time in its window of black glass, lit from behind, in the clock's colour. */
static void time_lit(cairo_t *cr, const char *s, double cx, double cy, bool on)
{
	cairo_save(cr);
	cairo_select_font_face(cr, "DejaVu Sans Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	double size = strlen(s) > 5 ? 11.5 : 15.0;
	cairo_set_font_size(cr, size);
	cairo_text_extents_t e;
	cairo_text_extents(cr, s, &e);
	double x = cx - e.x_advance * 0.5, y = cy + size * 0.36;
	double a = on ? 1.0 : 0.35;
	/* the glow in the glass, and then the figures */
	for (int i = 2; i >= 1; i--) {
		cairo_set_source_rgba(cr, 1.0, 0.22, 0.10, a * 0.10);
		for (int dx = -i; dx <= i; dx += i) {
			for (int dy = -i; dy <= i; dy += i) {
				cairo_move_to(cr, x + dx, y + dy);
				cairo_show_text(cr, s);
			}
		}
	}
	cairo_set_source_rgba(cr, 1.0, 0.40, 0.24, a);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, s);
	cairo_restore(cr);
}

void desk_draw_phone_deck(cairo_t *cr, int slab_w, const struct desk_phone_deck *d)
{
	if (!desk_phone_ready()) {
		return;
	}
	double len, k;
	deck_scale(slab_w, &len, &k);
	cairo_save(cr);
	if (k != 1.0) {
		cairo_scale(cr, k, k);
	}
	/* the plate: both ends as they are, the middle drawn out */
	int iw = cairo_image_surface_get_width(g.deck), ih = cairo_image_surface_get_height(g.deck);
	double end = PH_DECK_SIDE + DECK_END, mid = iw - end * 2.0, tot = len + PH_DECK_SIDE * 2.0;
	slice(cr, g.deck, 0, 0, end, ih, 0, 0, end, ih);
	slice(cr, g.deck, end, 0, mid, ih, end, 0, tot - end * 2.0, ih);
	slice(cr, g.deck, iw - end, 0, end, ih, tot - end, 0, end, ih);

	double xs[N_KEYS];
	key_x(len, xs);
	for (int i = 0; i < N_KEYS; i++) {
		piece(cr, i, d->held == KEY_WHICH[i], PH_DECK_SIDE + xs[i] + PH_ROW_WIDE * 0.5, PH_ROW_Y + PH_ROW_H * 0.5);
	}

	/* where it has got to, along the slot */
	double x0 = PH_DECK_SIDE + PH_SLIDER_END, x1 = PH_DECK_SIDE + len - PH_SLIDER_END;
	double f = 0.0;
	bool known = d->have && d->length > 0.0 && d->position >= 0.0;
	if (d->sliding) {
		f = d->slide;
	} else if (known) {
		f = d->position / d->length;
	}
	f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
	/* the knob's middle goes from one end of the slot to the other, less half of
	 * itself at each */
	double kx = x0 + 11.0 + (x1 - x0 - 22.0) * f;
	if (known || d->sliding) {
		cairo_set_source_rgba(cr, 1.0, 0.33, 0.18, 0.92);
		cairo_rectangle(cr, x0 + 3.0, PH_SLIDER_Y - 1.5, kx - x0 - 3.0, 3.0);
		cairo_fill(cr);
	}
	piece(cr, PH_CELL_KNOB, false, kx, PH_SLIDER_Y);

	char a[24], b[24];
	double shown = !d->have ? -1.0 : (d->sliding && d->length > 0.0 ? d->slide * d->length : d->position);
	time_text(a, sizeof a, shown);
	time_text(b, sizeof b, d->have ? d->length : -1.0);
	time_lit(cr, a, PH_DECK_SIDE + PH_TIME_L + PH_TIME_W * 0.5, PH_SLIDER_Y, d->have);
	time_lit(cr, b, PH_DECK_SIDE + len - PH_TIME_L - PH_TIME_W * 0.5, PH_SLIDER_Y, d->have && d->length > 0.0);
	cairo_restore(cr);
}

int desk_phone_deck_hit(int slab_w, double x, double y)
{
	double len, k;
	deck_scale(slab_w, &len, &k);
	x = x / k - PH_DECK_SIDE;
	y = y / k;
	if (y >= PH_SLIDER_Y - 16.0 && y <= PH_SLIDER_Y + 16.0 && x >= PH_SLIDER_END - 6.0
		&& x <= len - PH_SLIDER_END + 6.0) {
		return PH_KEY_SLIDER;
	}
	if (y < PH_ROW_Y || y > PH_ROW_Y + PH_ROW_H) {
		return PH_KEY_NONE;
	}
	double xs[N_KEYS];
	key_x(len, xs);
	for (int i = 0; i < N_KEYS; i++) {
		if (x >= xs[i] && x <= xs[i] + PH_ROW_WIDE) {
			return KEY_WHICH[i];
		}
	}
	return PH_KEY_NONE;
}

bool desk_phone_deck_on(int slab_w, double x, double y)
{
	double len, k;
	deck_scale(slab_w, &len, &k);
	x = x / k - PH_DECK_SIDE;
	y = y / k;
	return x >= 0.0 && x <= len && y >= 0.0 && y <= PH_DECK_H;
}

double desk_phone_slider_at(int slab_w, double x)
{
	double len, k;
	deck_scale(slab_w, &len, &k);
	double x0 = PH_DECK_SIDE + PH_SLIDER_END + 11.0, x1 = PH_DECK_SIDE + len - PH_SLIDER_END - 11.0;
	double f = (x / k - x0) / (x1 - x0);
	return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}
