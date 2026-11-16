/* ================================================================
 * Fingers: what they have done (docs/design.md).
 *
 * This knows nothing of the desk. It is told where fingers are, and says when one has been
 * tapped, held, or has set off, and when a whole hand has been put down and gone somewhere.
 * What that means is for src/wm.c.
 * ================================================================ */

#ifndef DESK_TOUCH_H
#define DESK_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#define DESK_FINGERS 10

/* How long a finger held still means something else, for whoever sets a timer. */
#define DESK_TOUCH_LONG_SEC 0.5

enum desk_touch_what {
	DESK_TOUCH_NOTHING,
	DESK_TOUCH_TAP,          /* a finger put down and lifted where it was */
	DESK_TOUCH_LONG,         /* held where it was */
	DESK_TOUCH_SETS_OFF,     /* it has left where it was put down */
	DESK_TOUCH_MOVES,        /* and goes on */
	DESK_TOUCH_LIFTED,       /* and has been lifted; or was, after LONG */
	DESK_TOUCH_HAND,         /* three fingers or more came down together */
	DESK_TOUCH_HAND_SWIPE,   /* and have set off together, one way */
	DESK_TOUCH_HAND_GATHER,  /* or are closing up */
	DESK_TOUCH_HAND_SPREAD,  /* or opening out */
	DESK_TOUCH_HAND_MOVES,   /* and go on */
	DESK_TOUCH_HAND_LIFTED,  /* the first of them lifted: that is the end of it */
	DESK_TOUCH_SIX,          /* six came down together, both hands: the keyboard on the screen
	                          * asked for; the hand is done with, going nowhere */
};

struct desk_touch_said {
	enum desk_touch_what what;
	int32_t id;
	double x, y;      /* where that finger is; for a hand, where its middle was put down */
	double dx, dy;    /* how far it has come */
	double vx, vy;    /* LIFTED: how fast it was going, in pixels a second */
	double spread;    /* a hand: how open it is, against as it was put down */
	double vspread;   /* HAND_LIFTED: how fast that was changing, a second */
};

struct desk_touch_finger {
	bool on;
	int32_t id;
	int state;
	double x0, y0, x, y;
	double t0, t;
	double vx, vy;
};

struct desk_touch {
	struct desk_touch_finger f[DESK_FINGERS];
	int n;
	bool hand;        /* the fingers down are a hand, until the last is lifted */
	bool hand_off;    /* and it has set off */
	bool hand_ended;  /* and one has been lifted, or it was six */
	int hand_n;
	double hand_x, hand_y, hand_r;
};

/* All of them take the time in seconds; down and move the pixels to a point as well. */
struct desk_touch_said desk_touch_down(struct desk_touch *t, int32_t id, double x, double y,
	double now, double pt);
struct desk_touch_said desk_touch_move(struct desk_touch *t, int32_t id, double x, double y,
	double now, double pt);
struct desk_touch_said desk_touch_up(struct desk_touch *t, int32_t id, double now);
/* It was never there: nothing is said of it. */
void desk_touch_cancel(struct desk_touch *t, int32_t id);
/* Time has gone by: one finger that has now been held long enough. */
struct desk_touch_said desk_touch_tick(struct desk_touch *t, double now);

#endif
