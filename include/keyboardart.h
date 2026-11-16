/* The keyboard's pictures, and what DEsk prints on them (art/keyboard/keyboard.py,
 * src/keyboardart.c). Its case and a key of each kind are read on a thread: a key of any
 * width is cut from the one key, and all of it is laid out on the screen's own pixels, so
 * that what is printed on it is sharp. */

#ifndef DESK_KEYBOARD_ART_H
#define DESK_KEYBOARD_ART_H

#include <cairo.h>
#include <stdbool.h>

enum desk_kb_kind {
	DESK_KB_LETTER,    /* letters and figures: glass */
	DESK_KB_FUNCTION,  /* what does something else */
	DESK_KB_RETURN,    /* Enter */
	DESK_KB_SLIM,      /* the row along the top */
	DESK_KB_SPACE,     /* a nickel bar */
	DESK_KB_KINDS,
};

/* What a key has on it. */
struct desk_kb_print {
	const char *word;      /* what it is called, small */
	char base[8];          /* a letter by itself, or what it types */
	char shifted[8];       /* with Shift, over that */
	char third[8];         /* with AltGr, to the side */
	int icon;              /* something drawn */
};
enum { DESK_KB_ICON_NONE, DESK_KB_ICON_PUT_AWAY, DESK_KB_ICON_GLOBE, DESK_KB_ICON_PEN, DESK_KB_ICON_KEYS };

/* Read them; 1 read, 0 not yet, -1 they could not be. */
void desk_kb_art_begin(void);
int desk_kb_art_ready(void);
void desk_kb_art_drop(void);

/* All this is in the pixels of a picture of it at k screen pixels to a pixel of what was rendered.
 * Its rows are not quite the same size, seen from close to: row 0 is the top one, the slim row, and
 * row 5 the front one. */
/* What k fits, for this much of the screen, keys no bigger than most high. */
double desk_kb_art_fit(double room_w, double room_h, double most_high);
/* The whole picture, with its shadow; its body at the front; how far down the picture it shows,
 * lying at the front of the desk. */
void desk_kb_art_measure(double k, int *w, int *h, int *body_x, int *body_y, int *body_w, int *body_h);
int desk_kb_art_shows(double k);
/* A row: where its keys begin, how wide a key is, where the row is. */
void desk_kb_art_row(double k, int row, double *x0, double *u, int *y, int *h);
/* Is this on the machine: with the corners its round ends leave beside the platen? */
bool desk_kb_art_on_body(double k, double x, double y);
/* The machine, and what lies over the paper: the bail and the plate. */
void desk_kb_art_paint_case(cairo_t *cr, double k);
void desk_kb_art_paint_bail(cairo_t *cr, double k);
/* What is painted from now on is in the well (keys and what they throw; the case comes over them). */
void desk_kb_art_clip_well(cairo_t *cr, double k);
/* How much of it goes over the paper; how far down the paper goes in behind it, and how wide it is;
 * the bail's shadow on the paper, up from the paper's foot; where the lever is. */
int desk_kb_art_bail_h(double k);
int desk_kb_art_paper_foot(double k);
int desk_kb_art_paper_round(double k);  /* the top of the platen it goes round */
int desk_kb_art_paper_w(double k);
int desk_kb_art_bail_over_foot(double k);
void desk_kb_art_lever(double k, int *x0, int *y0, int *x1, int *y1);
/* A key, w wide in its row, x being the left of its place, with the shadow it throws. */
void desk_kb_art_paint_key(cairo_t *cr, enum desk_kb_kind kind, int row, bool down, int x, int w, double k);
/* How far past its place a key's picture goes, for what has to be drawn again as it goes down. */
void desk_kb_art_key_reach(double k, int row, int *side, int *above, int *below);
/* What is printed on it, over the middle of its top as it stands. */
void desk_kb_art_paint_print(cairo_t *cr, const struct desk_kb_print *p, enum desk_kb_kind kind, int row,
	bool down, int x, int w, double k, bool light);
/* Typed on the sheet in the machine: its left, or its middle, at x; how wide it came to be. With a of 0
 * it is only measured. */
void desk_kb_art_type(cairo_t *cr, const char *s, double x, double cy, double size, double r, double gr, double b,
	double a, int *width, bool centre);
/* The small light on the rail, 0 out .. 1 on. */
void desk_kb_art_paint_light(cairo_t *cr, double lit, double k);

/* As it lies in the drawer (desk_drawer_item). */
cairo_surface_t *desk_kb_art_top_lying(double ppu, double turn, double *ax, double *ay);

#endif
