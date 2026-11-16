/* The notebook desk-notepad's window is the top sheet of.
 *
 * On DEsk the program writes on glass: the paper under its letters is the
 * desk's, laid under the window and round it like any sheet's frame
 * (notebook.c). The window is the whole sheet, heading and holes, so that the
 * program can turn all of it over, on paper it lays the same way (nbink.c).
 * So this is what the two have to agree on. The pictures are rendered in
 * Blender (art/notebook/notebook.py), whose measures are these. */

#ifndef DESK_NOTEBOOK_H
#define DESK_NOTEBOOK_H

#include <cairo.h>
#include <stdbool.h>

enum {
	NB_PAD = 30,         /* how far the notebook goes past its top sheet: the
	                      * wire to the left, the stack and the cover below */
	NB_WIRE = 26,        /* how far the wire does */
	NB_INSET_L = 31,     /* the writing starts this far into the sheet, clear
	                      * of the holes and the line of perforations */
	NB_HEAD_H = 46,      /* and this far down: the heading, to its rule */
	NB_MARGIN_X = 75,    /* the red margin, from the sheet's left */
	NB_SHEET_R = 10,     /* how round the sheet's corners are */
	NB_WIN_W = 546,      /* the window is all of the sheet, heading and holes, */
	NB_WIN_H = 771,      /* and this is the size it was rendered at */
};

/* The pencil case the desk lays under the notebook (thing-notebook.c): a pen of four refills,
 * turned to the one that is out, a highlighter and a rubber, any of them taken out into the hand
 * to write with a mouse or a finger. The desk says which in a file the notebook reads,
 * $XDG_STATE_HOME/desk/NB_CASE_FILE: a line "pen <one of NB_PEN_NAMES>" and a line
 * "hand <one of NB_HAND_NAMES>". */
enum { NB_PEN_BLACK, NB_PEN_BLUE, NB_PEN_RED, NB_PEN_GREEN, NB_PENS };
enum { NB_HAND_NONE, NB_HAND_PEN, NB_HAND_MARKER, NB_HAND_RUBBER, NB_HANDS };
#define NB_CASE_FILE "pencil-case"
#define NB_PEN_NAMES { "black", "blue", "red", "green" }
#define NB_HAND_NAMES { "none", "pen", "marker", "rubber" }

/* What sheets are printed with, for the rules and the margin. */
#define NB_BLUE 0.36, 0.56, 0.74
#define NB_BLUE_A 0.58
#define NB_RED 0.83, 0.27, 0.26
#define NB_RED_A 0.50

/* A line of print, as a source to fill with: it is not a hairline, and its ink
 * wanders along its length (nbink.c). */
cairo_pattern_t *desk_nb_ink(double r, double g, double b, double a);
/* Fill a printed line across, its middle along row y; or down, along column x. */
void desk_nb_line_across(cairo_t *cr, cairo_pattern_t *ink, int x0, int x1, int y);
void desk_nb_line_down(cairo_t *cr, cairo_pattern_t *ink, int x, int y0, int y1);

/* What a sheet is laid from (art/notebook), as the desk or desk-notepad has read it, and the inks
 * it is printed with (desk_nb_ink). */
struct desk_nb_art {
	cairo_surface_t *page, *rings, *grain;
	cairo_pattern_t *blue, *blue_faint, *red;
};
/* The sheet's paper, x, y, w by h, and the stack and the cover NB_PAD past it on every side. */
void desk_nb_lay_page(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int w, int h);
/* What it comes from the printer with: the margin, the heading's rule, its words. */
void desk_nb_lay_print(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int w, int h);
/* The paper's tooth, over it and what is printed on it. */
void desk_nb_lay_tooth(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int w, int h);
/* The wire, down the sheet's left, through its holes. */
void desk_nb_lay_wire(cairo_t *cr, const struct desk_nb_art *art, int x, int y, int h);

/* Where what is written in the heading goes, on a sheet w wide, from its left: the page's
 * number, its title, and the year, month and day between the dots printed after DATE (on a
 * sheet too narrow for a date, has_date is false). All of it sits on base. */
struct desk_nb_heading {
	double no_x0, no_x1, title_x0, title_x1;
	bool has_date;
	double date_x, part_x0[3], part_x1[3], dot_x[2];
	double base;
};
void desk_nb_heading(int w, struct desk_nb_heading *out);

/* The desk's side (notebook.c). Are the pictures there? Read once, here. */
bool desk_notebook_ready(void);
/* The notebook, its top sheet at x, y, w by h and NB_PAD past that on every
 * side. What is written in its heading is the program's. */
void desk_draw_notebook(cairo_t *cr, double x, double y, double w, double h);
/* The next sheet down, as much as a turned-up corner shows of it: its top right
 * corner at x, y. */
void desk_draw_notebook_next(cairo_t *cr, double x, double y, double size);

/* Shutting it, which is what folding is for a notebook (draw.c's desk_fold). The
 * picture of it open, wire and all (NB_PAD past its sheet on every side), with the
 * cover coming over, shut from 0 to 1; cover is it shut, at the size of that picture
 * (from desk_notebook_shut_named). */
void desk_notebook_shutting(cairo_t *cr, cairo_surface_t *open, cairo_surface_t *cover,
	double shut);
/* It shut, w by h, with name on the label on its cover: a new picture. */
cairo_surface_t *desk_notebook_shut_named(const char *name, int w, int h);
/* The shape of that picture: its height for a width. */
double desk_notebook_shut_aspect(void);
/* It shut with a pencil on it, lying in the drawer, ppu pixels to the room's unit,
 * turned so, and its middle in the picture; NULL if it could not be read. */
cairo_surface_t *desk_notebook_top_lying(double ppu, double turn, double *ax, double *ay);

#endif
