/* ================================================================
 * What has been copied, newest first: the clipboard on the desk (thing-clipboard.c)
 * is a picture of this. Words and pictures, the last DESK_CLIPS_MAX of them, kept
 * only for as long as the desk is up -- nothing is written down -- and never what
 * a password manager says is a secret (owner 2026-11-10).
 * ================================================================ */

#ifndef DESK_CLIPS_H
#define DESK_CLIPS_H

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

struct desk_server;

#define DESK_CLIPS_MAX 20

enum { DESK_CLIP_TEXT, DESK_CLIP_IMAGE };

struct desk_clip {
	int kind;
	/* as it was copied: the words (TEXT, with a 0 after them), or the picture's file
	 * (IMAGE), with what it was offered as */
	unsigned char *data;
	size_t len;
	char mime[64];
	/* the picture, made small enough to lie on a slip; NULL if it could not be read
	 * (only PNG can be), and how big it was */
	cairo_surface_t *picture;
	int img_w, img_h;
	time_t at;      /* when */
	unsigned id;    /* one for each, as long as the desk is up */
};

/* Begin keeping what is copied (again, harmlessly). */
void desk_clips_start(struct desk_server *server);
void desk_clips_stop(void);
int desk_clips_n(void);
const struct desk_clip *desk_clips_at(int i);
/* Changes whenever what is kept does. */
unsigned desk_clips_generation(void);
/* Copied again: for whoever pastes it next, and the newest. */
bool desk_clips_copy(struct desk_server *server, int i);
/* Is the newest what is copied now -- ours, or what we read it from? */
bool desk_clips_current(void);
void desk_clips_remove(int i);
void desk_clips_clear(void);

#endif
