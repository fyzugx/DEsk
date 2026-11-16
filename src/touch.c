/* Fingers: what they have done (include/touch.h). */

#include <math.h>
#include <stddef.h>

#include "touch.h"

/* A finger is not a pin: this far is still where it was put down. */
#define SLOP_PT 8.0
/* Fingers that come down this soon after the first of them are one hand. */
#define HAND_TOGETHER_SEC 0.30
#define HAND_FINGERS 3
/* Six down together, from the first of them, is two hands asking for the keys (more time than one
 * hand: they are two). */
#define SIX_FINGERS 6
#define SIX_TOGETHER_SEC 0.50
/* This far together, or this much closer or further apart, is where a hand is going. */
#define HAND_OFF_PT 14.0
/* Closer together than this there is nothing to close up. */
#define HAND_MIN_RADIUS_PT 18.0
/* Still for this long before it is lifted, it was not going anywhere. */
#define STILL_SEC 0.08

enum { F_STILL, F_LONG, F_GOING };

static struct desk_touch_finger *finger(struct desk_touch *t, int32_t id)
{
	for (int i = 0; i < DESK_FINGERS; i++) {
		if (t->f[i].on && t->f[i].id == id) {
			return &t->f[i];
		}
	}
	return NULL;
}

static void hand_measure(const struct desk_touch *t, double *x, double *y, double *r)
{
	double sx = 0.0, sy = 0.0, sr = 0.0;
	int n = 0;
	for (int i = 0; i < DESK_FINGERS; i++) {
		if (t->f[i].on) {
			sx += t->f[i].x;
			sy += t->f[i].y;
			n++;
		}
	}
	*x = n ? sx / n : 0.0;
	*y = n ? sy / n : 0.0;
	for (int i = 0; i < DESK_FINGERS; i++) {
		if (t->f[i].on) {
			sr += hypot(t->f[i].x - *x, t->f[i].y - *y);
		}
	}
	*r = n ? sr / n : 0.0;
}

static void hand_base(struct desk_touch *t)
{
	hand_measure(t, &t->hand_x, &t->hand_y, &t->hand_r);
	t->hand_n = t->n;
}

struct desk_touch_said desk_touch_down(struct desk_touch *t, int32_t id, double x, double y,
	double now, double pt)
{
	(void)pt;
	struct desk_touch_said said = { .what = DESK_TOUCH_NOTHING, .id = id, .x = x, .y = y };
	struct desk_touch_finger *f = finger(t, id);
	for (int i = 0; !f && i < DESK_FINGERS; i++) {
		if (!t->f[i].on) {
			f = &t->f[i];
			t->n++;
		}
	}
	if (!f) {
		return said;
	}
	*f = (struct desk_touch_finger){
		.on = true, .id = id, .state = F_STILL, .x0 = x, .y0 = y, .x = x, .y = y, .t0 = now, .t = now,
	};
	if (t->hand) {
		if (!t->hand_off && !t->hand_ended && t->n == SIX_FINGERS) {
			bool together = true;
			for (int i = 0; i < DESK_FINGERS; i++) {
				together = together && !(t->f[i].on && now - t->f[i].t0 > SIX_TOGETHER_SEC);
			}
			if (together) {
				/* the hand goes nowhere after it; the keyboard comes */
				t->hand_ended = true;
				said.what = DESK_TOUCH_SIX;
				hand_measure(t, &said.x, &said.y, &said.spread);
				said.spread = 1.0;
				return said;
			}
		}
		if (!t->hand_off) {
			hand_base(t);
		}
		return said;
	}
	if (t->n < HAND_FINGERS) {
		return said;
	}
	/* (one held down a while, then two more, is a finger and two more) */
	for (int i = 0; i < DESK_FINGERS; i++) {
		if (t->f[i].on && now - t->f[i].t0 > HAND_TOGETHER_SEC) {
			return said;
		}
	}
	t->hand = true;
	t->hand_off = false;
	t->hand_ended = false;
	hand_base(t);
	said.what = DESK_TOUCH_HAND;
	said.x = t->hand_x;
	said.y = t->hand_y;
	return said;
}

struct desk_touch_said desk_touch_move(struct desk_touch *t, int32_t id, double x, double y,
	double now, double pt)
{
	struct desk_touch_said said = { .what = DESK_TOUCH_NOTHING, .id = id, .x = x, .y = y };
	struct desk_touch_finger *f = finger(t, id);
	if (!f) {
		return said;
	}
	double dt = now - f->t;
	if (dt > 0.0005) {
		double vx = (x - f->x) / dt, vy = (y - f->y) / dt;
		bool fresh = dt > STILL_SEC;
		f->vx = fresh ? vx : 0.6 * f->vx + 0.4 * vx;
		f->vy = fresh ? vy : 0.6 * f->vy + 0.4 * vy;
	}
	f->x = x;
	f->y = y;
	f->t = now;
	said.dx = x - f->x0;
	said.dy = y - f->y0;
	if (t->hand) {
		if (t->hand_ended || t->n != t->hand_n) {
			return said;
		}
		double cx, cy, r;
		hand_measure(t, &cx, &cy, &r);
		double dx = cx - t->hand_x, dy = cy - t->hand_y;
		double went = hypot(dx, dy);
		said.x = t->hand_x;
		said.y = t->hand_y;
		said.dx = dx;
		said.dy = dy;
		said.spread = t->hand_r > 1.0 ? r / t->hand_r : 1.0;
		if (t->hand_off) {
			said.what = DESK_TOUCH_HAND_MOVES;
			return said;
		}
		double opened = r - t->hand_r;
		bool round_it = t->hand_r >= HAND_MIN_RADIUS_PT * pt && fabs(opened) > went;
		if (round_it && fabs(opened) >= HAND_OFF_PT * pt) {
			said.what = opened < 0.0 ? DESK_TOUCH_HAND_GATHER : DESK_TOUCH_HAND_SPREAD;
		} else if (!round_it && went >= HAND_OFF_PT * pt) {
			said.what = DESK_TOUCH_HAND_SWIPE;
		}
		t->hand_off = said.what != DESK_TOUCH_NOTHING;
		return said;
	}
	if (f->state == F_STILL && hypot(said.dx, said.dy) >= SLOP_PT * pt) {
		f->state = F_GOING;
		said.what = DESK_TOUCH_SETS_OFF;
	} else if (f->state == F_GOING) {
		said.what = DESK_TOUCH_MOVES;
	}
	return said;
}

struct desk_touch_said desk_touch_up(struct desk_touch *t, int32_t id, double now)
{
	struct desk_touch_said said = { .what = DESK_TOUCH_NOTHING, .id = id };
	struct desk_touch_finger *f = finger(t, id);
	if (!f) {
		return said;
	}
	said.x = f->x;
	said.y = f->y;
	said.dx = f->x - f->x0;
	said.dy = f->y - f->y0;
	if (t->hand) {
		/* the whole hand, as it was going */
		if (t->hand_off && !t->hand_ended && t->n == t->hand_n) {
			double cx, cy, r;
			hand_measure(t, &cx, &cy, &r);
			double vx = 0.0, vy = 0.0, vr = 0.0;
			int n = 0;
			for (int i = 0; i < DESK_FINGERS; i++) {
				const struct desk_touch_finger *g = &t->f[i];
				if (!g->on) {
					continue;
				}
				double ax = g->x - cx, ay = g->y - cy, al = fmax(hypot(ax, ay), 1.0);
				double fvx = now - g->t <= STILL_SEC ? g->vx : 0.0;
				double fvy = now - g->t <= STILL_SEC ? g->vy : 0.0;
				vx += fvx;
				vy += fvy;
				vr += (fvx * ax + fvy * ay) / al;
				n++;
			}
			said.what = DESK_TOUCH_HAND_LIFTED;
			said.x = t->hand_x;
			said.y = t->hand_y;
			said.dx = cx - t->hand_x;
			said.dy = cy - t->hand_y;
			said.spread = t->hand_r > 1.0 ? r / t->hand_r : 1.0;
			said.vx = n ? vx / n : 0.0;
			said.vy = n ? vy / n : 0.0;
			said.vspread = n && t->hand_r > 1.0 ? vr / n / t->hand_r : 0.0;
		}
		t->hand_ended = true;
	} else {
		said.what = f->state == F_STILL ? DESK_TOUCH_TAP : DESK_TOUCH_LIFTED;
		if (f->state == F_GOING && now - f->t <= STILL_SEC) {
			said.vx = f->vx;
			said.vy = f->vy;
		}
	}
	desk_touch_cancel(t, id);
	return said;
}

void desk_touch_cancel(struct desk_touch *t, int32_t id)
{
	struct desk_touch_finger *f = finger(t, id);
	if (!f) {
		return;
	}
	f->on = false;
	t->n--;
	if (t->n == 0) {
		t->hand = false;
		t->hand_off = false;
		t->hand_ended = false;
	}
}

struct desk_touch_said desk_touch_tick(struct desk_touch *t, double now)
{
	struct desk_touch_said said = { .what = DESK_TOUCH_NOTHING };
	if (t->hand) {
		return said;
	}
	for (int i = 0; i < DESK_FINGERS; i++) {
		struct desk_touch_finger *f = &t->f[i];
		if (f->on && f->state == F_STILL && now - f->t0 >= DESK_TOUCH_LONG_SEC) {
			f->state = F_LONG;
			said.what = DESK_TOUCH_LONG;
			said.id = f->id;
			said.x = f->x;
			said.y = f->y;
			return said;
		}
	}
	return said;
}
