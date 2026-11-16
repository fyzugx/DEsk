/* Reading what is written with a pen, a character at a time (src/handwriting.c).
 *
 * Nothing is learned and nothing is sent anywhere. What is written is looked at as a picture of
 * which way its edges go, and compared with the same of every character of the languages asked
 * for, as the faces on this machine print them, and with some letters written out by hand. The
 * table of that is made the first time, on a thread, and kept in the cache. */

#ifndef DESK_HANDWRITING_H
#define DESK_HANDWRITING_H

#include <stdbool.h>

struct desk_hw_stroke {
	const float *xy;      /* x0 y0 x1 y1 ..., from the top left of the box written in */
	int n;                /* points */
};

struct desk_hw_guess {
	char text[8];         /* UTF-8 */
	float score;          /* 0 to 1 */
};

/* Which: a language's code each (en, ja, zh, ko, es, fr, de; "zh-tw" too), NULL at the end. Latin
 * letters are always read. Asking again for the same is nothing. */
void desk_hw_want(const char *const *langs);
/* Is its table there? 1 yes, 0 not yet. */
int desk_hw_ready(void);
/* What these strokes, in a box w by h, may be, best first: how many of them. */
int desk_hw_read(const struct desk_hw_stroke *strokes, int n, double w, double h,
	struct desk_hw_guess *out, int most);
void desk_hw_drop(void);

#endif
