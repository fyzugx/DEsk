#pragma once

#include <cairo.h>
#include <stdbool.h>

#include "clips.h"

/* The clipboard's pictures (thing-clipboard.c): a board of brown hardboard with a
 * steel clip at its head, and what has been copied clipped to it, a slip a copy, as
 * a step index is -- the newest at the front, whole, and the foot of each one before
 * it showing below it. Drawn with cairo at the screen's own pixels, k of them to the
 * centimetre. A place is in pixels from the top left of its canvas: the board, and
 * room round it for its shadow. */

struct desk_clip_look {
	const struct desk_clip *const *clips;   /* newest first */
	int n;
	int scroll;      /* how many feet are skipped behind the front one */
	int hover;       /* the one under the hand (its place in clips), or -1 */
	bool current;    /* the front one is what pasting gives now */
	double settle;   /* 0..1: the front one dropping in under the clip, 1 in */
};

enum { DESK_CLIP_OFF = 0, DESK_CLIP_BOARD, DESK_CLIP_JAW, DESK_CLIP_SLIP };

/* the most feet shown behind the front one: past that, the wheel */
#define DESK_CLIP_SHOWN 12

void desk_clip_art_size(double k, int *w, int *h);
/* how far in from the canvas's edge the board is */
double desk_clip_art_pad(double k);
void desk_clip_art_paint(cairo_t *cr, double k, const struct desk_clip_look *lk);
/* What is under a point of the canvas, and which of clips (idx) for a slip. */
int desk_clip_art_at(double k, const struct desk_clip_look *lk, double x, double y, int *idx);
/* Lying in the drawer, its long side `along` room units of ppu pixels, turned. */
cairo_surface_t *desk_clip_art_lying(double ppu, double turn, double *ax, double *ay);
void desk_clip_art_drop(void);
