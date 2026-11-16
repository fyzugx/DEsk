/* ================================================================
 * The clear mat, lifted (draw.c, DESK_MAT_CLEAR): what is under it is moved
 * as under a mat on a real desk, by turning up a corner of it.
 *
 * A hand resting on a corner of the sheet turns it up a little. Taken hold
 * of there, the sheet is lifted, held up at that corner, and what is under
 * it can be moved: a print taken up goes where the hand goes and is laid
 * where it is let go, on top of the rest; the wheel over one turns it, and
 * with Ctrl makes it larger or smaller -- or, on a touch screen, a second
 * finger put down by the one holding it: drawn apart or together it grows
 * or shrinks, and turned round the first, the print turns with it
 * (2026-11-16, the owner on a Surface). By the corner lies the envelope the
 * prints came in: more are taken out of it (desk-mat pick), and a print put
 * into it is gone from under the mat. A hand anywhere else on the desk,
 * Escape, or anything coming up over the desk, and the sheet goes back
 * down, and what is under it stays where it lies.
 *
 * With one picture all over it (desk_mat_under = whole) there is nothing
 * under it to move: lifted, its envelope is for another picture.
 * ================================================================ */

#include "thing.h"
#include "desk.h"
#include "i18n.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern const struct desk_thing desk_thing_mat;

#define REACH_PT 30.0       /* how near its corner, along both edges, the hand has it */
#define PEEK_PT 26.0        /* how far a corner turns up under a hand resting on it */
#define PEEK_AFTER_S 0.14   /* and how long the hand rests there first */
#define LIFT_PT 120.0       /* how far it is turned up, held */
#define TURN_UP_S 0.18      /* how long turning it up or down takes */
#define HELD_LIFT_PT 14.0   /* a print in the hand stands this far off the desk */
#define WHEEL_NOTCH 15.0    /* a wheel's notch, as its axis says it */
#define TURN_STEP 3.0       /* the degrees a notch turns a print */
#define SIZE_STEP 1.06      /* and, with Ctrl, how much larger or smaller it makes it */

static struct {
	int hover;               /* the corner the hand is on, lying flat: -1 */
	double hover_at;         /* since when */
	int corner;              /* the corner turned up: under the hand, or held; -1 */
	bool lifted;             /* held up, what is under it to be moved */
	double cut_from, cut_to, cut_at;   /* how far it is turned up, in points, on its way */
	bool cut_moving;
	bool folded;             /* turned over (lifted), not only raised under the hand */
	struct wlr_scene_buffer *curl, *envelope, *held;
	bool env_open;
	/* the print in the hand: which it was, as it was, and where it was taken */
	int held_i;
	struct desk_mat_paper paper;
	int held_x, held_y;
	double hand_x, hand_y;
	double wheel;            /* what the wheel has turned that is not a notch yet */
	/* a second finger with the print (mat_finger_down): where it is, and how far from the hand
	 * and which way round it, and the print's size and turn, when it came down */
	int32_t pinch_id;
	bool pinching;
	double pinch_x, pinch_y, pinch_d0, pinch_a0, pinch_w0, pinch_turn0;
} m = { .hover = -1, .corner = -1, .held_i = -1 };

static double cut_now(void)
{
	double t = (monotonic_sec() - m.cut_at) / TURN_UP_S;
	if (t >= 1.0) {
		return m.cut_to;
	}
	t = t < 0.0 ? 0.0 : t;
	t = t * t * (3.0 - 2.0 * t);
	return m.cut_from + (m.cut_to - m.cut_from) * t;
}

static void node_gone(struct wlr_scene_buffer **b)
{
	if (*b) {
		wlr_scene_node_destroy(&(*b)->node);
		*b = NULL;
	}
}

/* A picture of draw.c's, put up on the desk under the paper at x, y. */
static void node_put(struct desk_server *server, struct wlr_scene_buffer **slot, cairo_surface_t *s,
	int x, int y)
{
	struct wlr_scene_tree *tree = desk_on_desk(server);
	if (!s) {
		node_gone(slot);
		return;
	}
	if (!tree) {
		cairo_surface_destroy(s);
		return;
	}
	struct desk_buf *b = buf_create(cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
	cairo_set_source_surface(b->cr, s, 0, 0);
	cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(b->cr);
	cairo_surface_destroy(s);
	scene_put(slot, tree, b, x, y, NULL);
	/* only pictures: the hand goes through them to what decides */
	(*slot)->point_accepts_input = shadow_accepts;
}

/* The corner as far up as it is now. */
static void curl_show(struct desk_server *server)
{
	double cut = cut_now();
	int ox = 0, oy = 0;
	/* lifted, it is turned over; under the hand, only raised */
	cairo_surface_t *s = m.corner >= 0 && cut > 0.5 ? desk_mat_curl_picture(desk_width(server),
		desk_height(server), m.corner, cut, m.folded, &ox, &oy) : NULL;
	node_put(server, &m.curl, s, ox, oy);
	if (m.held) {
		wlr_scene_node_raise_to_top(&m.held->node);
	}
}

static void envelope_show(struct desk_server *server, bool open)
{
	int ox = 0, oy = 0;
	cairo_surface_t *s = m.lifted && m.corner >= 0 ? desk_mat_envelope_picture(desk_width(server),
		desk_height(server), m.corner, LIFT_PT, open, &ox, &oy) : NULL;
	node_put(server, &m.envelope, s, ox, oy);
	m.env_open = open && s;
	if (m.held) {
		wlr_scene_node_raise_to_top(&m.held->node);
	}
}

/* A print kept all under the sheet, turned as it is: its middle no nearer
 * the sheet's edge than it reaches. */
static void keep_under(struct desk_server *server, struct desk_mat_paper *q)
{
	int w = desk_width(server), h = desk_height(server);
	double k = desk_px_per_pt(w), r[4];
	if (!desk_mat_rect(w, h, r) || q->pw < 1 || q->ph < 1) {
		return;
	}
	double t = q->turn * M_PI / 180.0, hw = q->w * 0.5, hh = hw * (double)q->ph / q->pw;
	double ex = hw * fabs(cos(t)) + hh * fabs(sin(t)), ey = hw * fabs(sin(t)) + hh * fabs(cos(t));
	double mx = r[2] * 0.5 / k - ex - 6.0, my = r[3] * 0.5 / k - ey - 6.0;
	q->x = mx <= 0.0 ? 0.0 : q->x < -mx ? -mx : q->x > mx ? mx : q->x;
	q->y = my <= 0.0 ? 0.0 : q->y < -my ? -my : q->y > my ? my : q->y;
}

static bool on_envelope(struct desk_server *server, double x, double y)
{
	double box[4];
	return m.lifted && desk_mat_envelope_box(desk_width(server), desk_height(server), m.corner, LIFT_PT,
		box) && x >= box[0] && y >= box[1] && x < box[0] + box[2] && y < box[1] + box[3];
}

/* The corner on its way to cut points up (0: down, and flat once there). */
static void turn_to(struct desk_server *server, double cut)
{
	m.cut_from = cut_now();
	m.cut_to = cut;
	m.cut_at = monotonic_sec();
	m.cut_moving = true;
	server_schedule_frames(server);
}

/* Flat at once, whatever it was doing: nothing turned up or held, nothing
 * on the desk for it. */
static void flat(struct desk_server *server)
{
	m.corner = -1;
	m.lifted = false;
	m.folded = false;
	m.cut_from = m.cut_to = 0.0;
	m.cut_moving = false;
	m.held_i = -1;
	node_gone(&m.curl);
	node_gone(&m.envelope);
	node_gone(&m.held);
	desk_mat_lift(-1, 0.0, false, -1);
	desk_overlays_changed(server);
}

/* The hand has rested on a corner: it comes up a little off the desk. The
 * sheet is not drawn again for it: the corner is a picture over it. */
static void peek(struct desk_server *server, int corner)
{
	if (m.corner >= 0 && m.corner != corner) {
		flat(server);
	}
	m.corner = corner;
	m.folded = false;
	turn_to(server, PEEK_PT);
	snd_at(server, DESK_SND_CORNER_LIFT, desk_cursor(server)->x, 0.35);
}

/* The hand has left it: back down. */
static void unpeek(struct desk_server *server)
{
	if (m.corner >= 0 && !m.lifted) {
		turn_to(server, 0.0);
	}
}

/* Taken hold of at its corner: held up, and the envelope by it. */
static void lift(struct desk_server *server, int corner)
{
	if (m.corner >= 0 && m.corner != corner) {
		flat(server);
	}
	m.corner = corner;
	m.lifted = true;
	m.folded = true;
	m.hover = -1;
	m.cut_from = m.cut_to = 0.0;   /* (turned over from nothing, not from raised) */
	desk_mat_lift(corner, LIFT_PT, true, -1);
	desk_overlays_changed(server);
	turn_to(server, LIFT_PT);
	envelope_show(server, false);
	snd_at(server, DESK_SND_CORNER_LIFT, desk_cursor(server)->x, 0.9);
	wlr_log(WLR_INFO, "the clear mat lifted at corner %d", corner);
}

static void put_down(struct desk_server *server, bool where_held);

/* Let go of: the sheet back down over what is under it. */
static void lay_down(struct desk_server *server)
{
	if (m.held_i >= 0) {
		put_down(server, false);
	}
	if (!m.lifted) {
		return;
	}
	m.lifted = false;
	node_gone(&m.envelope);
	/* (still turned up, as it goes down: flat when it is) */
	desk_mat_lift(m.corner, LIFT_PT, false, -1);
	desk_overlays_changed(server);
	turn_to(server, 0.0);
	snd_at(server, DESK_SND_PAPER_SETTLE, desk_cursor(server)->x, 0.7);
	wlr_log(WLR_INFO, "the clear mat laid down");
}

/* The print the hand is on, taken up. */
static void take(struct desk_server *server, int i)
{
	struct desk_mat_paper p[DESK_MAT_PAPERS_MAX];
	int n = desk_mat_papers(p, DESK_MAT_PAPERS_MAX);
	if (i < 0 || i >= n) {
		return;
	}
	struct wlr_cursor *c = desk_cursor(server);
	int ox = 0, oy = 0;
	cairo_surface_t *s = desk_mat_paper_picture(desk_width(server), desk_height(server), &p[i],
		HELD_LIFT_PT, &ox, &oy);
	if (!s) {
		return;   /* its print not read yet: nothing there to take */
	}
	m.held_i = i;
	m.paper = p[i];
	m.held_x = ox;
	m.held_y = oy;
	m.hand_x = c->x;
	m.hand_y = c->y;
	node_put(server, &m.held, s, ox, oy);
	if (m.held) {
		wlr_scene_node_raise_to_top(&m.held->node);
	}
	desk_mat_lift(m.corner, LIFT_PT, true, i);
	desk_overlays_changed(server);
	snd_at(server, DESK_SND_SHEET_TAKE, c->x, 0.7);
}

/* The print in the hand put down: where the hand is, on top of the rest; or
 * into the envelope, gone; or (where_held false) back where it was. */
static void put_down(struct desk_server *server, bool where_held)
{
	struct desk_mat_paper p[DESK_MAT_PAPERS_MAX];
	int n = desk_mat_papers(p, DESK_MAT_PAPERS_MAX);
	struct wlr_cursor *c = desk_cursor(server);
	int w = desk_width(server);
	int i = m.held_i;
	/* (the file may have been read again since: the same print, by its name) */
	if (i < 0 || i >= n || strcmp(p[i].name, m.paper.name)) {
		i = -1;
		for (int j = 0; j < n && i < 0; j++) {
			i = strcmp(p[j].name, m.paper.name) ? -1 : j;
		}
	}
	m.held_i = -1;
	node_gone(&m.held);
	if (i >= 0 && where_held && on_envelope(server, c->x, c->y)) {
		/* back in its envelope: gone from under the mat, and its print with it,
		 * unless another lies there of the same */
		struct desk_mat_paper gone = p[i];
		memmove(&p[i], &p[i + 1], sizeof *p * (size_t)(n - i - 1));
		n--;
		bool again = false;
		for (int j = 0; j < n; j++) {
			again = again || !strcmp(p[j].name, gone.name);
		}
		char dir[400], path[512];
		desk_mat_prints_dir(dir, sizeof dir);
		if (!again && dir[0]) {
			snprintf(path, sizeof path, "%s/%s", dir, gone.name);
			unlink(path);
		}
		desk_mat_papers_set(p, n, true);
		snd_at(server, DESK_SND_PAPER_SETTLE, c->x, 0.8);
		wlr_log(WLR_INFO, "a print back in its envelope: %s", gone.name);
	} else if (i >= 0 && where_held) {
		/* laid where it is let go, all of it under the sheet; on top */
		double k = desk_px_per_pt(w);
		struct desk_mat_paper q = p[i];
		q.turn = m.paper.turn;
		q.w = m.paper.w;
		q.x += (c->x - m.hand_x) / k;
		q.y += (c->y - m.hand_y) / k;
		keep_under(server, &q);
		memmove(&p[i], &p[i + 1], sizeof *p * (size_t)(n - i - 1));
		p[n - 1] = q;
		desk_mat_papers_set(p, n, true);
		snd_at(server, DESK_SND_SHEET_DOWN, c->x, 0.7);
	}
	desk_mat_lift(m.corner, LIFT_PT, m.lifted, -1);
	desk_overlays_changed(server);
	if (m.lifted) {
		envelope_show(server, on_envelope(server, c->x, c->y));
	}
}

/* More from the envelope: desk-mat chooses them, makes prints of them and
 * lays them under the mat, where the desk sees them come (its file). */
static void more(void)
{
	int under = DESK_MAT_UNDER_FREE;
	desk_mat_clear(&under);
	const char *words[] = { "pick", "--lang", desk_lang_code(desk_lang_get()),
		under == DESK_MAT_UNDER_WHOLE ? "--whole" : NULL, NULL };
	desk_run_with("desk-mat", words);
}

/* The print in the hand, made again at its size and turn now, where the hand has it. */
static void held_again(struct desk_server *server)
{
	struct wlr_cursor *c = desk_cursor(server);
	struct desk_mat_paper at = m.paper;
	int ox = 0, oy = 0;
	cairo_surface_t *s = desk_mat_paper_picture(desk_width(server), desk_height(server), &at,
		HELD_LIFT_PT, &ox, &oy);
	if (s) {
		m.held_x = ox;
		m.held_y = oy;
		node_put(server, &m.held, s, ox + (int)lround(c->x - m.hand_x),
			oy + (int)lround(c->y - m.hand_y));
		wlr_scene_node_raise_to_top(&m.held->node);
	}
	server_schedule_frames(server);
}

/* Two fingers on the print: as far apart and as far round as they are now against when the
 * second came down. Made again only for a change the eye would see. */
#define PINCH_MIN_PX 24.0
static void pinch_apply(struct desk_server *server)
{
	struct wlr_cursor *c = desk_cursor(server);
	double dx = m.pinch_x - c->x, dy = m.pinch_y - c->y;
	double d = hypot(dx, dy);
	if (d < PINCH_MIN_PX) {
		return;
	}
	double w = m.pinch_w0 * d / m.pinch_d0;
	w = w < DESK_MAT_PAPER_MIN_PT ? DESK_MAT_PAPER_MIN_PT
		: w > DESK_MAT_PAPER_MAX_PT ? DESK_MAT_PAPER_MAX_PT : w;
	/* (down the screen is y: round the way atan2 goes is round with the clock, as turn is) */
	double turn = fmod(m.pinch_turn0 + (atan2(dy, dx) - m.pinch_a0) * 180.0 / M_PI + 360.0, 360.0);
	double dt = fabs(turn - m.paper.turn);
	dt = dt > 180.0 ? 360.0 - dt : dt;
	if (fabs(w - m.paper.w) < m.paper.w * 0.01 && dt < 0.5) {
		return;
	}
	m.paper.w = w;
	m.paper.turn = turn;
	held_again(server);
}

/* A finger put down while another, as the hand, has a print (or is on one, not yet taken up): it
 * is a second finger on it, to size and turn it with, and nothing else. */
static bool mat_finger_down(struct desk_server *server, int32_t id, double x, double y)
{
	if (!m.lifted || m.pinching || !desk_hand_finger_on(server, id)) {
		return false;
	}
	struct wlr_cursor *c = desk_cursor(server);
	if (m.held_i < 0) {
		int i = desk_mat_paper_at(desk_width(server), desk_height(server), c->x, c->y);
		if (i < 0) {
			return false;
		}
		take(server, i);
		if (m.held_i < 0) {
			return false;
		}
	}
	double d = hypot(x - c->x, y - c->y);
	m.pinch_id = id;
	m.pinching = true;
	m.pinch_x = x;
	m.pinch_y = y;
	m.pinch_d0 = d > PINCH_MIN_PX ? d : PINCH_MIN_PX;
	m.pinch_a0 = atan2(y - c->y, x - c->x);
	m.pinch_w0 = m.paper.w;
	m.pinch_turn0 = m.paper.turn;
	desk_hand_shape(server, "grabbing");
	return true;
}

static void mat_finger_move(struct desk_server *server, int32_t id, double x, double y)
{
	if (!m.pinching || id != m.pinch_id) {
		return;
	}
	m.pinch_x = x;
	m.pinch_y = y;
	if (m.held_i >= 0) {
		pinch_apply(server);
	}
}

static void mat_finger_up(struct desk_server *server, int32_t id, bool taken_away)
{
	(void)server;
	(void)taken_away;
	if (m.pinching && id == m.pinch_id) {
		m.pinching = false;
	}
}

/* ---- the hand ---------------------------------------------------------- */

static bool buttons_down(struct desk_server *server)
{
	return desk_seat(server)->pointer_state.button_count > 0;
}

static enum desk_thing_hold mat_drag(struct desk_server *server)
{
	struct wlr_cursor *c = desk_cursor(server);
	int w = desk_width(server), h = desk_height(server);
	if (m.held_i >= 0) {
		if (m.pinching) {
			pinch_apply(server);
		}
		/* the print goes where the hand goes */
		if (m.held) {
			wlr_scene_node_set_position(&m.held->node, m.held_x + (int)lround(c->x - m.hand_x),
				m.held_y + (int)lround(c->y - m.hand_y));
		}
		bool env = on_envelope(server, c->x, c->y);
		if (env != m.env_open) {
			envelope_show(server, env);
			/* over its mouth, the print goes into it: in front of the print, and behind it again */
			if (env && m.envelope) {
				wlr_scene_node_raise_to_top(&m.envelope->node);
			} else if (m.held) {
				wlr_scene_node_raise_to_top(&m.held->node);
			}
		}
		desk_hand_shape(server, "grabbing");
		server_schedule_frames(server);
		return THING_CARRIED;
	}
	if (buttons_down(server)) {
		return THING_NOT_HELD;   /* a hand already busy with something else */
	}
	if (!desk_mat_clear(NULL) || desk_busy(server)) {
		m.hover = -1;
		return THING_NOT_HELD;
	}
	bool window = desk_window_under(server);
	if (m.lifted) {
		bool env = !window && on_envelope(server, c->x, c->y);
		if (env != m.env_open) {
			envelope_show(server, env);
		}
		if (!window && (env || desk_mat_paper_at(w, h, c->x, c->y) >= 0)) {
			desk_hand_shape(server, env ? "pointer" : "grab");
			return THING_HELD;
		}
		return THING_NOT_HELD;
	}
	int corner = window ? -1 : desk_mat_corner_at(w, h, c->x, c->y, REACH_PT);
	if (corner != m.hover) {
		if (m.corner >= 0 && m.corner != corner) {
			unpeek(server);
		}
		m.hover = corner;
		m.hover_at = monotonic_sec();
		server_schedule_frames(server);
	}
	if (corner >= 0) {
		desk_hand_shape(server, "pointer");
		return THING_HELD;
	}
	return THING_NOT_HELD;
}

static bool mat_hand_here(struct desk_server *server)
{
	struct wlr_cursor *c = desk_cursor(server);
	int w = desk_width(server), h = desk_height(server);
	if (!desk_mat_clear(NULL) || desk_busy(server) || desk_window_under(server)) {
		return false;
	}
	if (m.lifted) {
		return on_envelope(server, c->x, c->y) || desk_mat_paper_at(w, h, c->x, c->y) >= 0;
	}
	return desk_mat_corner_at(w, h, c->x, c->y, REACH_PT) >= 0;
}

static bool mat_hand_down(struct desk_server *server, uint32_t button)
{
	struct wlr_cursor *c = desk_cursor(server);
	int w = desk_width(server), h = desk_height(server);
	if (!desk_mat_clear(NULL) || desk_busy(server)) {
		return false;   /* (tick lays it down) */
	}
	if (m.held_i >= 0) {
		return true;   /* (taken up already, as a second finger came down with it) */
	}
	bool window = desk_window_under(server);
	if (!m.lifted) {
		int corner = window || button != BTN_LEFT ? -1 : desk_mat_corner_at(w, h, c->x, c->y, REACH_PT);
		if (corner < 0) {
			return false;
		}
		lift(server, corner);
		return true;
	}
	if (window) {
		/* a window taken hold of: the sheet goes down, and the hand is the window's */
		lay_down(server);
		return false;
	}
	if (button == BTN_LEFT && on_envelope(server, c->x, c->y)) {
		envelope_show(server, true);
		more();
		return true;
	}
	int i = button == BTN_LEFT ? desk_mat_paper_at(w, h, c->x, c->y) : -1;
	if (i >= 0) {
		take(server, i);
		return true;
	}
	/* anywhere else on the desk, or the other button: the sheet goes back down */
	lay_down(server);
	return true;
}

static bool mat_release(struct desk_server *server)
{
	if (m.held_i < 0) {
		return false;
	}
	put_down(server, true);
	return true;
}

static bool mat_wheel(struct desk_server *server, struct wlr_pointer_axis_event *event)
{
	if (!m.lifted || event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL
		|| desk_window_under(server)) {
		return false;
	}
	struct wlr_cursor *c = desk_cursor(server);
	int w = desk_width(server), h = desk_height(server);
	int i = m.held_i >= 0 ? m.held_i : desk_mat_paper_at(w, h, c->x, c->y);
	if (i < 0) {
		return false;
	}
	struct desk_mat_paper p[DESK_MAT_PAPERS_MAX];
	int n = desk_mat_papers(p, DESK_MAT_PAPERS_MAX);
	if (i >= n) {
		return true;
	}
	m.wheel += event->delta;
	int steps = (int)(m.wheel / WHEEL_NOTCH);
	if (steps == 0) {
		return true;
	}
	m.wheel -= steps * WHEEL_NOTCH;
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(desk_seat(server));
	bool ctrl = kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_CTRL);
	struct desk_mat_paper *q = m.held_i >= 0 ? &m.paper : &p[i];
	if (ctrl) {
		/* down the page, smaller; up it, larger */
		q->w *= pow(SIZE_STEP, -steps);
		q->w = q->w < DESK_MAT_PAPER_MIN_PT ? DESK_MAT_PAPER_MIN_PT
			: q->w > DESK_MAT_PAPER_MAX_PT ? DESK_MAT_PAPER_MAX_PT : q->w;
	} else {
		/* down the page, round with the clock */
		q->turn = fmod(q->turn + steps * TURN_STEP, 360.0);
	}
	if (m.held_i < 0) {
		keep_under(server, q);
	}
	if (m.held_i >= 0) {
		held_again(server);   /* the print in the hand, made again as it is now */
		return true;
	}
	desk_mat_papers_set(p, n, true);
	desk_overlays_changed(server);
	return true;
}

static bool mat_key(struct desk_server *server, uint32_t keycode, xkb_keysym_t sym, uint32_t mods,
	bool released)
{
	(void)keycode;
	(void)mods;
	/* (a window with the keys has its Escape: the chooser over the desk, say) */
	if (!m.lifted || sym != XKB_KEY_Escape || desk_seat(server)->keyboard_state.focused_surface) {
		return false;
	}
	if (!released) {
		lay_down(server);
	}
	return true;
}

/* ---- every frame ------------------------------------------------------- */

static void mat_tick(struct desk_server *server)
{
	bool there = desk_mat_clear(NULL);
	if (m.corner < 0 && !m.lifted && m.hover < 0) {
		return;
	}
	if (!there || desk_busy(server)) {
		/* gone, or something over the desk: down at once */
		if (m.corner >= 0 || m.lifted) {
			flat(server);
		}
		m.hover = -1;
		return;
	}
	/* the hand resting on a corner turns it up */
	if (!m.lifted && m.hover >= 0 && m.corner != m.hover
		&& monotonic_sec() - m.hover_at >= PEEK_AFTER_S) {
		peek(server, m.hover);
	} else if (!m.lifted && m.hover >= 0 && m.corner == m.hover && m.cut_to < PEEK_PT * 0.5) {
		turn_to(server, PEEK_PT);   /* come back to it on its way down */
	} else if (!m.lifted && m.hover < 0 && m.corner >= 0 && m.cut_to > 0.0) {
		unpeek(server);
	} else if (m.hover >= 0 && m.corner != m.hover) {
		server_schedule_frames(server);   /* (until it has rested long enough) */
	}
	if (m.cut_moving) {
		curl_show(server);
		if (monotonic_sec() - m.cut_at >= TURN_UP_S) {
			m.cut_moving = false;
			if (m.cut_to <= 0.0) {
				/* down: flat, and the sheet whole again (if it was turned over) */
				m.corner = -1;
				node_gone(&m.curl);
				if (m.folded) {
					m.folded = false;
					desk_mat_lift(-1, 0.0, false, -1);
					desk_overlays_changed(server);
				}
			}
		} else {
			server_schedule_frames(server);
		}
	}
}

static void mat_screen_changed(struct desk_server *server)
{
	m.hover = -1;
	if (m.corner >= 0 || m.lifted || m.held_i >= 0) {
		flat(server);
	}
}

static void mat_quitting(struct desk_server *server)
{
	(void)server;
	node_gone(&m.curl);
	node_gone(&m.envelope);
	node_gone(&m.held);
}

const struct desk_thing desk_thing_mat = {
	.name = "mat",
	.hand_down = mat_hand_down,
	.hand_here = mat_hand_here,
	.drag = mat_drag,
	.release = mat_release,
	.wheel = mat_wheel,
	.key = mat_key,
	.finger_down = mat_finger_down,
	.finger_move = mat_finger_move,
	.finger_up = mat_finger_up,
	.tick = mat_tick,
	.screen_changed = mat_screen_changed,
	.quitting = mat_quitting,
};
