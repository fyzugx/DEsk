#pragma once

#include <cairo.h>
#include <stdbool.h>

#include "calc.h"

/* The calculator's pictures, rendered in Blender (art/calc/calc.py), and what
 * DEsk writes on them. Read and scaled on a thread of their own the first time
 * it comes out of the drawer, and again when it is made bigger or smaller, and
 * let go of when it goes back. It has two faces with keys: the inside, open,
 * and the lid's outside, shut. Everywhere below, a place is in pixels of the
 * screen from the top left of the calculator's canvas, the picture everything
 * is laid in. */

/* Read them, for k pixels of the screen to the centimetre. */
void desk_calc_art_begin(double px_per_cm);
/* 0 still reading, 1 ready, -1 they could not be read */
int desk_calc_art_ready(void);
void desk_calc_art_drop(void);

/* It open or shut, at rest; (x, y) where the picture goes. */
cairo_surface_t *desk_calc_art_rest(bool open, int *x, int *y);
/* The lid on its way: how many pictures there are between shut and open, the
 * first nearly shut; the box it goes in (it is seen through a smaller picture
 * than the box, as it goes by). */
int desk_calc_art_film_n(void);
cairo_surface_t *desk_calc_art_film(int i, int *x, int *y, int *w, int *h);

/* The keys, held down: how many, one's picture and where it goes. */
int desk_calc_art_keys(void);
cairo_surface_t *desk_calc_art_key_down(int i, int *x, int *y);
/* which of them is key k on the face that is up, open or shut; -1 for none */
int desk_calc_art_key_of(enum desk_calc_key k, bool open);

/* What is at a place on the calculator. */
enum desk_calc_spot {
	DESK_CALC_OFF,            /* nothing: the desk, or its shadow */
	DESK_CALC_CASE,           /* its plastic */
	DESK_CALC_KEY,            /* *which is the key (desk_calc_art_keys's numbering) */
	DESK_CALC_PAPER,          /* *which is the row, from the top */
	DESK_CALC_SHUT_IT,        /* the lid's rim or the hinge: what shuts it */
	DESK_CALC_CORNER,         /* its lower right corner: pulled, it is bigger or smaller */
};
enum desk_calc_spot desk_calc_art_at(double x, double y, bool open, int *which);
enum desk_calc_key desk_calc_art_key(int i);

/* What it takes up, open or shut: x0, y0, x1, y1. */
void desk_calc_art_box(bool open, int box[4]);
/* Where the hinge is across. */
int desk_calc_art_hinge_x(void);

/* The outside's display of figures, the inside's of dots, and the paper's writing: the box each
 * goes in, and each drawn with the box's top left at the cairo context's origin. The dots have
 * over the figures what is being worked out (desk_calc_working). */
void desk_calc_art_figures_box(int box[4]);
void desk_calc_art_paint_figures(cairo_t *cr, const char *shown, bool memory, double lit);
void desk_calc_art_dots_box(int box[4]);
void desk_calc_art_paint_dots(cairo_t *cr, const char *working, const char *shown, bool memory, double lit);
void desk_calc_art_paper_box(int box[4]);
int desk_calc_art_rows(void);
/* first is the line (desk_calc_line) on the top row */
void desk_calc_art_paint_paper(cairo_t *cr, int first);

/* It lying in the drawer, along ppu * 2.75, turned by turn; its middle in the
 * picture. NULL if the picture could not be read. */
cairo_surface_t *desk_calc_art_top_lying(double ppu, double turn, double *ax, double *ay);
