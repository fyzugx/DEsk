/* The slider phone a picture-in-picture window is the screen of.
 *
 * The window lies in the slab of a phone held on its side. Down its right side are a
 * red light to send it away, a real button to stop and start the picture, and a pale
 * light to open it; and out from under the slab comes a deck with what else a player
 * needs. The pictures are rendered in Blender (art/phone/phone.py); src/phone.c lays
 * them. */

#ifndef DESK_PHONE_H
#define DESK_PHONE_H

#include <cairo.h>
#include <stdbool.h>

/* (src/phone.c lays the pictures by what phone.py says: PH_PAD, PH_L and the rest) */
#include "phone-map.h"

enum {
	PH_DECK_HID = 24,     /* how much of the deck stays under the slab, out */
	PH_DECK_OUT = PH_DECK_H - PH_DECK_HID,   /* how much comes out, at its full size */
};

enum desk_phone_key {
	PH_KEY_NONE = -1,
	PH_KEY_BACK,          /* ten seconds back */
	PH_KEY_FWD,           /* ten seconds on */
	PH_KEY_OPEN,          /* where it is playing from */
	PH_KEY_CLOSE,         /* the red light */
	PH_KEY_DECK,          /* the pale one */
	PH_KEY_PLAY,          /* the button between them: playing or not */
	PH_KEY_SLIDER,        /* the slider, anywhere along it */
};

/* Are the pictures there? Read once, here. */
bool desk_phone_ready(void);

/* The slab at x, y, w by h, and PH_PAD past that on every side. held is a key
 * held down (PH_KEY_NONE: none); deck_out is which way the pale light's arrow goes. */
void desk_draw_phone(cairo_t *cr, double x, double y, double w, double h, int held, bool deck_out);
/* The slab's key under (lx, ly), measured from its top left. */
int desk_phone_slab_hit(double w, double h, double lx, double ly);

/* The deck, under a slab slab_w wide. What it is showing: */
struct desk_phone_deck {
	int held;             /* a key held down */
	bool have;            /* there is a player */
	bool can_seek;        /* and it can be sent somewhere */
	double position;      /* seconds; < 0 unknown */
	double length;        /* seconds; < 0 unknown */
	bool sliding;         /* the slider in the hand, at slide (0..1) */
	double slide;
};
/* The size of its picture, how much a pixel of the full-size deck is in it, and
 * where its left edge and its plate's top are, from the slab's top left corner (the
 * deck all the way out). */
void desk_phone_deck_size(int slab_w, int slab_h, int *pw, int *ph, double *scale, int *x, int *y);
void desk_draw_phone_deck(cairo_t *cr, int slab_w, const struct desk_phone_deck *d);
/* The key or the slider under (x, y) in that picture, or PH_KEY_NONE. */
int desk_phone_deck_hit(int slab_w, double x, double y);
/* Is (x, y) in that picture on the deck, not its shadow? */
bool desk_phone_deck_on(int slab_w, double x, double y);
/* Where along the slider x is: 0..1. */
double desk_phone_slider_at(int slab_w, double x);

#endif
