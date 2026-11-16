/* What a program kept in the drawer can look like, lying there, instead of
 * the card with its mark on it: one of the things the drawer has always had,
 * one of the desk's own things, its own icon lying there as a thing and not
 * printed on paper, or a look: a folder with a picture of a thing seen from
 * straight above in it, made by anyone (docs/LOOKS.md, desk-look). The
 * desk's own are in share/desk/looks, rendered by art/looks/looks.py with
 * the same light any can be (art/looks/bake.py); more may be put beside them
 * for everyone ($XDG_DATA_DIRS/desk/looks), and the person's own go in
 * $XDG_DATA_HOME/desk/looks, a folder or only a PNG. Which a program has is
 * kept by the drawer (wm.c, $XDG_CONFIG_HOME/desk/drawer-looks).
 *
 * A look's folder:
 *
 *     look.png   the thing from straight above, nothing round it (transparent),
 *                lit from the top right, with no shadow: the drawer gives it its own
 *     look.ini   [Look] Name=, Name[ja]= (and zh ko es fr de), Size= (centimetres,
 *                its longest side as it lies; or PixelsPerCm=, and it is as big as
 *                its picture says; 14 if neither), Thick= (centimetres, how high it
 *                stands, for its shadow; 1), Order= (where among the rest, lower
 *                first), Author=, License=, Comment= */

#ifndef DESK_LOOKS_H
#define DESK_LOOKS_H

#include <cairo.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

enum desk_look_kind {
	DESK_LOOKS_CARD,      /* its card, as it would be */
	DESK_LOOKS_ICON,      /* its icon, lying there as a thing: not printed on a card */
	DESK_LOOKS_TOOL,      /* one of the five the drawer has always had (tool: DESK_ICON_*) */
	DESK_LOOKS_THING,     /* a thing of the desk's own, as it lies in the drawer (thing: its word) */
	DESK_LOOKS_NOTEBOOK,
	DESK_LOOKS_CAMERA,
	DESK_LOOKS_PICTURE,   /* a look's picture: the desk's, anyone's, or the person's own */
};

/* The room's unit, in centimetres, as a look's size is said in them; and a look that does not say
 * how big it is, as big as a card is across, near enough, and as thick as a little box */
#define DESK_LOOK_CM_PER_UNIT 6.4
#define DESK_LOOK_CM 14.0
#define DESK_LOOK_THICK_CM 1.0

struct desk_look {
	char id[96];          /* what the drawer keeps: "card", "compass", "radio", "own/teapot" */
	char name[96];        /* what it is called, in the desk's language */
	enum desk_look_kind kind;
	int tool;
	const char *thing;
	char path[PATH_MAX];  /* a picture's */
	double along;         /* a picture's longest side, in the room's units, lying there */
	double thick;         /* how thick it lies, in the room's units, for its shadow */
	bool own;             /* the person's own */
};

/* All there are, the desk's first, then everyone's, then the person's own as they are now: n of
 * them, kept until asked again. */
int desk_looks_all(const struct desk_look **out);
/* The one with this id, or NULL (an own picture since taken away). */
const struct desk_look *desk_look_find(const char *id);
/* A picture's, read once, or again when it changes: NULL if it will not read. Not to be freed. */
cairo_surface_t *desk_look_picture(const struct desk_look *look);
/* Where the person's own go. */
void desk_looks_own_dir(char *out, size_t n);
/* A look's folder, read (lookfile.c): its picture's path, its name (in the language lang, "ja"
 * and so on, if it has one in it), how long and how thick it lies in the room's units, where it
 * goes among the rest. False if it has no picture. (desk-look checks one with this.) */
bool desk_look_folder(const char *dir, const char *lang, char *path, size_t pn, char *name, size_t nn,
	double *along, double *thick, int *order);
/* How big a PNG is, from its header. */
bool desk_png_size(const char *path, int *w, int *h);

#endif
