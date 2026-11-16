#pragma once

#include <stdbool.h>

/* The calculator on the desk, what it does and not what it looks like
 * (calcart.c, wm.c): a pocket calculator's way of working, each key acting as
 * it is pressed and sums going in the order they are keyed (1 + 2 x 3 = 9).
 * What it has worked out it keeps, the last DESK_CALC_LINES sums, with what is
 * in its memory, in $XDG_DATA_HOME/desk/calc: read the first time it is asked
 * for, written when asked to (desk_calc_keep). */

enum desk_calc_key {
	DESK_CK_0, DESK_CK_1, DESK_CK_2, DESK_CK_3, DESK_CK_4,
	DESK_CK_5, DESK_CK_6, DESK_CK_7, DESK_CK_8, DESK_CK_9,
	DESK_CK_DBLZERO, DESK_CK_DOT,
	DESK_CK_ADD, DESK_CK_SUB, DESK_CK_MUL, DESK_CK_DIV, DESK_CK_EQ,
	DESK_CK_PCT, DESK_CK_NEG, DESK_CK_AC,
	DESK_CK_MC, DESK_CK_MR, DESK_CK_MMINUS, DESK_CK_MPLUS,
	DESK_CK_SQRT, DESK_CK_SQ, DESK_CK_POW, DESK_CK_INV,
	DESK_CK_PI, DESK_CK_EUL, DESK_CK_LN, DESK_CK_LOG,
	DESK_CK_SIN, DESK_CK_COS, DESK_CK_TAN, DESK_CK_FACT,
	DESK_CK_COUNT,
	/* (not the workings': the small keys at the head of each face, which shut or open it and put
	 * it away; pressed, they do nothing here) */
	DESK_CK_FOLD, DESK_CK_OFF,
};

#define DESK_CALC_FIGURES 12
#define DESK_CALC_LINES 200

struct desk_calc_line {
	char sum[72];             /* 1,250 + 380 */
	char answer[40];          /* 1,630 */
	double value;
};

void desk_calc_press(enum desk_calc_key k);
/* the last figure keyed taken back; false if nothing is being keyed */
bool desk_calc_rub_out(void);
/* A number from somewhere else, as if keyed: figures, a point or a comma as
 * the decimal mark, a sign, thousands marks; false, and nothing changed, if
 * text is not a number. */
bool desk_calc_paste(const char *text);

/* What the display shows, for the figures: 12 of them, each a digit, '-',
 * 'E', "Error"'s letters or a space, a '.' going with the figure before it.
 * Always has its point, as a calculator's does. */
void desk_calc_shown(char *out, unsigned n);
/* The number shown, to be copied: without the point if it has no decimals;
 * "" for an error. */
void desk_calc_shown_text(char *out, unsigned n);
bool desk_calc_has_memory(void);
/* What is being worked out, for over the figures: the sum pending ("1,250 +"), or the sum whose
 * answer is shown ("1,250 + 380 =", "\xe2\x88\x9a 2 ="); "" when neither. */
void desk_calc_working(char *out, unsigned n);

int desk_calc_lines(void);
/* line i, the oldest first; NULL past the end */
const struct desk_calc_line *desk_calc_line(int i);
/* line i's answer back on the display, as if keyed */
void desk_calc_recall(int i);
/* the paper torn off: all of the lines gone */
void desk_calc_tear(void);

/* changes whenever anything to be shown or kept does */
unsigned desk_calc_generation(void);
void desk_calc_keep(void);
