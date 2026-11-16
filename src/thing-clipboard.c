/* ================================================================
 * The clipboard: a writing board of brown hardboard with a steel clip at its head,
 * kept in the drawer and put down on the desk among the windows, as the calculator
 * is (owner 2026-11-10). What has been copied (clips.h) is clipped to it, a slip a
 * copy, as a step index is: the newest at the front, whole, and each one before it a
 * little longer than the one in front, so that the foot of each shows below it with
 * the head of what it says (clipart.h). A slip pressed is copied again, and so comes
 * to the front; pressed with the other button it is torn off; the other button on the
 * clip lets all of them go. The wheel over it goes back through more than shows.
 * Carried to the front of the desk, or thrown towards it, it goes back in the drawer; Super+V
 * takes it out and puts it back.
 * ================================================================ */

#include "thing.h"
#include "clipart.h"
#include "clips.h"
#include "desk.h"
#include "i18n.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wlr/util/log.h>

extern const struct desk_thing desk_thing_clipboard;

enum { CB_IN_DRAWER = 0, CB_COMING, CB_LYING, CB_GOING };

#define CB_PT_PER_CM 24.0     /* points to its centimetre, as the calculator's */
#define CB_SLIDE_S 0.42
#define CB_SETTLE_S 0.22

static struct {
	int where;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *pic;
	double k;                 /* screen pixels to its centimetre */
	double x, y;              /* its canvas's top left, in the windows' layer */
	bool placed;
	bool started;
	/* what is shown: drawn again when any of it changes */
	unsigned gen_shown;
	int hover_shown, scroll_shown;
	bool current_shown;
	/* going somewhere by itself */
	bool moving;
	double from_x, from_y, to_x, to_y, move_start, move_dur;
	/* the front slip, just come: dropping in under the clip */
	double settle_at;
	/* a hand on it */
	bool held, held_moved;
	int held_spot, held_idx;
	double px, py, hx, hy;
	/* how fast the hand was going as it carried it, screen pixels a second (a throw towards
	 * the front puts it away as surely as carrying it there) */
	double vx, vy, last_x, last_y, last_t;
	int hover;                /* the slip under the hand (its place among what is kept), or -1 */
	int scroll;               /* how many of the feet are skipped, past the front one */
} b = { .hover = -1, .hover_shown = -2 };

static double clampf_d(double v, double lo, double hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static double cb_k_for(const struct desk_server *server)
{
	return desk_px_per_pt(desk_width(server)) * CB_PT_PER_CM;
}

static void canvas(int *w, int *h)
{
	desk_clip_art_size(b.k, w, h);
}

/* What it shows: what is kept, as it is now. */
static void look_now(struct desk_clip_look *lk, const struct desk_clip **all)
{
	int n = desk_clips_n();
	for (int i = 0; i < n; i++) {
		all[i] = desk_clips_at(i);
	}
	lk->clips = all;
	lk->n = n;
	lk->scroll = b.scroll;
	lk->hover = b.hover;
	lk->current = desk_clips_current();
	lk->settle = b.settle_at > 0.0 ? clampf_d((monotonic_sec() - b.settle_at) / CB_SETTLE_S, 0.0, 1.0)
		: 1.0;
}

static int spot_at(double lx, double ly, int *idx)
{
	const struct desk_clip *all[DESK_CLIPS_MAX];
	struct desk_clip_look lk;
	look_now(&lk, all);
	return desk_clip_art_at(b.k, &lk, lx, ly, idx);
}

static bool cb_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	int idx;
	return spot_at(*sx, *sy, &idx) != DESK_CLIP_OFF;
}

static void cb_lay(void)
{
	if (b.tree) {
		wlr_scene_node_set_position(&b.tree->node, (int)lround(b.x), (int)lround(b.y));
	}
}

static void cb_show(struct desk_server *server)
{
	if (!b.tree) {
		return;
	}
	int w, h;
	canvas(&w, &h);
	const struct desk_clip *all[DESK_CLIPS_MAX];
	struct desk_clip_look lk;
	look_now(&lk, all);
	struct desk_buf *buf = buf_create(w, h);
	desk_clip_art_paint(buf->cr, b.k, &lk);
	scene_put(&b.pic, b.tree, buf, 0, 0, server);
	b.pic->point_accepts_input = cb_accepts;
	b.gen_shown = desk_clips_generation();
	b.hover_shown = b.hover;
	b.scroll_shown = b.scroll;
	b.current_shown = lk.current;
	server_schedule_frames(server);
}

/* The screen it is on, as the windows' layer has it: the one its middle is over, wherever
 * (x, y) is in that layer. */
static struct wlr_box cb_screen(struct desk_server *server, double x, double y)
{
	int w, h;
	canvas(&w, &h);
	double ox = desk_windows(server)->node.x, oy = desk_windows(server)->node.y;
	struct wlr_box sb = desk_screen_box_at(server, x + ox + w * 0.5, y + oy + h * 0.5);
	sb.x -= (int)lround(ox);
	sb.y -= (int)lround(oy);
	return sb;
}

/* (x, y) kept on the screen it is over -- any of them, a screen under the desk's too: the
 * board, not its shadow */
static void cb_on_desk(struct desk_server *server, double *x, double *y)
{
	int w, h;
	canvas(&w, &h);
	double pad = desk_clip_art_pad(b.k);
	struct wlr_box sb = cb_screen(server, *x, *y);
	*x = fmax(fmin(*x, sb.x + sb.width - w + pad - 2.0), sb.x + 2.0 - pad);
	*y = fmax(fmin(*y, sb.y + sb.height - h + pad - 2.0), sb.y + 2.0 - pad);
}

static void cb_drop(void)
{
	if (b.tree) {
		wlr_scene_node_destroy(&b.tree->node);
	}
	b.tree = NULL;
	b.pic = NULL;
}

/* ---- coming and going ---- */

static void cb_take_out(struct desk_server *server)
{
	if (b.where != CB_IN_DRAWER) {
		return;
	}
	desk_clips_start(server);
	b.k = cb_k_for(server);
	int w, h;
	canvas(&w, &h);
	int dw = desk_width(server), dh = desk_height(server);
	if (!b.placed) {
		/* on the right of the desk, by the hand that copies */
		b.x = dw * 0.70 - w * 0.5;
		b.y = dh * 0.47 - h * 0.5;
		b.placed = true;
	}
	cb_on_desk(server, &b.x, &b.y);
	b.tree = wlr_scene_tree_create(desk_windows(server));
	b.scroll = 0;
	b.hover = -1;
	b.held = false;
	/* up from under the front of the desk to where it lay */
	b.to_x = b.from_x = b.x;
	b.to_y = b.y;
	struct wlr_box sb = cb_screen(server, b.x, b.y);
	b.from_y = sb.y + sb.height + 2.0 - desk_clip_art_pad(b.k);
	b.y = b.from_y;
	b.move_start = monotonic_sec();
	b.move_dur = CB_SLIDE_S;
	b.moving = true;
	b.where = CB_COMING;
	cb_lay();
	cb_show(server);
	wlr_scene_node_raise_to_top(&b.tree->node);
	views_above_raise(server);
	drawer_thing_present(server, &desk_thing_clipboard, false);
	snd_at(server, DESK_SND_FELT_PICK, b.x + w * 0.5, 0.9);
}

static void cb_put_away(struct desk_server *server)
{
	if (b.where != CB_LYING) {
		return;
	}
	wlr_log(WLR_INFO, "putting the clipboard back in the drawer");
	b.held = false;
	/* down off the front of the screen it is on */
	struct wlr_box sb = cb_screen(server, b.x, b.y);
	b.from_x = b.to_x = b.x;
	b.from_y = b.y;
	b.to_y = sb.y + sb.height + 2.0 - desk_clip_art_pad(b.k);
	b.move_start = monotonic_sec();
	b.move_dur = CB_SLIDE_S;
	b.moving = true;
	b.where = CB_GOING;
	server_schedule_frames(server);
}

static void cb_tick(struct desk_server *server)
{
	if (!b.started) {
		/* it keeps what is copied from the start, wherever it is */
		b.started = true;
		desk_clips_start(server);
	}
	if (b.where == CB_IN_DRAWER) {
		return;
	}
	double now = monotonic_sec();
	bool busy = false;
	if (b.moving) {
		double u = clampf_d((now - b.move_start) / b.move_dur, 0.0, 1.0);
		double e = u * u * (3.0 - 2.0 * u);
		b.x = b.from_x + (b.to_x - b.from_x) * e;
		b.y = b.from_y + (b.to_y - b.from_y) * e;
		cb_lay();
		busy = true;
		if (u >= 1.0) {
			b.moving = false;
			int w, h;
			canvas(&w, &h);
			if (b.where == CB_COMING) {
				b.where = CB_LYING;
				snd_at(server, DESK_SND_FELT_DROP, b.x + w * 0.5, 0.8);
				wlr_log(WLR_INFO, "the clipboard lies at %d,%d", (int)lround(b.x), (int)lround(b.y));
			} else if (b.where == CB_GOING) {
				/* in the drawer: where it lay is remembered */
				b.where = CB_IN_DRAWER;
				b.x = b.from_x;
				b.y = b.from_y;
				cb_drop();
				drawer_thing_present(server, &desk_thing_clipboard, true);
				snd_at(server, DESK_SND_FELT_DROP, b.x + w * 0.5, 0.7);
				server_schedule_frames(server);
				return;
			}
		}
	}
	bool settling = b.settle_at > 0.0 && now - b.settle_at < CB_SETTLE_S;
	if (b.gen_shown != desk_clips_generation()) {
		/* something new kept, or let go: no further back than there is */
		int most = desk_clips_n() - 1 - DESK_CLIP_SHOWN;
		b.scroll = b.scroll > most ? (most > 0 ? most : 0) : b.scroll;
		if (b.hover >= desk_clips_n()) {
			b.hover = -1;
		}
		cb_show(server);
	} else if (b.settle_at > 0.0 || b.hover != b.hover_shown || b.scroll != b.scroll_shown
		|| b.current_shown != desk_clips_current()) {
		cb_show(server);
	}
	if (b.settle_at > 0.0 && !settling) {
		b.settle_at = 0.0;
	}
	if (busy || b.settle_at > 0.0) {
		server_schedule_frames(server);
	}
}

/* ---- the hand ---- */

/* where the hand is on it, if it is uppermost there */
static int cb_under(struct desk_server *server, double *lx, double *ly, int *idx)
{
	*idx = -1;
	if (!b.tree || !b.pic || b.where != CB_LYING) {
		return DESK_CLIP_OFF;
	}
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&desk_scene(server)->tree.node,
		desk_cursor(server)->x, desk_cursor(server)->y, &sx, &sy);
	if (node != &b.pic->node) {
		return DESK_CLIP_OFF;
	}
	int tx, ty;
	wlr_scene_node_coords(&b.tree->node, &tx, &ty);
	*lx = desk_cursor(server)->x - tx;
	*ly = desk_cursor(server)->y - ty;
	return spot_at(*lx, *ly, idx);
}

static bool cb_here(struct desk_server *server)
{
	double lx, ly;
	int idx;
	return cb_under(server, &lx, &ly, &idx) != DESK_CLIP_OFF;
}

static bool cb_press(struct desk_server *server, uint32_t button)
{
	double lx = 0.0, ly = 0.0;
	int idx = -1;
	int spot = cb_under(server, &lx, &ly, &idx);
	if (spot == DESK_CLIP_OFF) {
		return false;
	}
	/* it comes up over what it was under */
	wlr_scene_node_raise_to_top(&b.tree->node);
	views_above_raise(server);
	wlr_seat_pointer_clear_focus(desk_seat(server));
	if (button == BTN_RIGHT) {
		/* (only the left button is let go of here: the other acts as it goes down) */
		if (spot == DESK_CLIP_SLIP) {
			/* torn off */
			desk_clips_remove(idx);
			snd_at(server, DESK_SND_PAPER_CRUMPLE, desk_cursor(server)->x, 0.6);
			wlr_log(WLR_INFO, "clipboard: a slip torn off");
		} else if (spot == DESK_CLIP_JAW && desk_clips_n() > 0) {
			/* the clip lifted: all of them let go */
			desk_clips_clear();
			b.scroll = 0;
			snd_at(server, DESK_SND_PAPER_TOSS, desk_cursor(server)->x, 0.6);
			wlr_log(WLR_INFO, "clipboard: every slip let go");
		}
		b.hover = -1;
		server_schedule_frames(server);
		return true;
	}
	if (button != BTN_LEFT) {
		return true;
	}
	b.held = true;
	b.held_moved = false;
	b.held_spot = spot;
	b.held_idx = idx;
	b.px = desk_cursor(server)->x;
	b.py = desk_cursor(server)->y;
	b.hx = lx;
	b.hy = ly;
	return true;
}

static enum desk_thing_hold cb_drag(struct desk_server *server)
{
	if (!b.held) {
		/* the hand going over it: the slip under it stands out */
		if (b.where == CB_LYING) {
			double lx, ly;
			int idx = -1;
			int spot = cb_under(server, &lx, &ly, &idx);
			int hover = spot == DESK_CLIP_SLIP ? idx : -1;
			if (hover != b.hover) {
				b.hover = hover;
				server_schedule_frames(server);
			}
		}
		return THING_NOT_HELD;
	}
	if (b.where != CB_LYING || desk_covered(server)) {
		b.held = false;
		return THING_NOT_HELD;
	}
	double cx = desk_cursor(server)->x, cy = desk_cursor(server)->y;
	if (!b.held_moved) {
		double twitch = 5.0 * desk_px_per_pt(desk_width(server));
		if (hypot(cx - b.px, cy - b.py) < twitch) {
			return THING_HELD;
		}
		b.held_moved = true;
		b.hover = -1;
		snd_at(server, DESK_SND_FELT_PICK, cx, 0.35);
		b.vx = b.vy = 0.0;
		b.last_x = cx;
		b.last_y = cy;
		b.last_t = monotonic_sec();
	}
	double now = monotonic_sec(), dt = now - b.last_t;
	if (dt > 0.004) {
		/* the last few hundredths of a second count most */
		double a = fmin(1.0, dt / 0.06);
		b.vx += ((cx - b.last_x) / dt - b.vx) * a;
		b.vy += ((cy - b.last_y) / dt - b.vy) * a;
		b.last_x = cx;
		b.last_y = cy;
		b.last_t = now;
	}
	double ox = desk_windows(server)->node.x, oy = desk_windows(server)->node.y;
	double x = cx - b.hx - ox, y = cy - b.hy - oy;
	cb_on_desk(server, &x, &y);
	b.x = x;
	b.y = y;
	cb_lay();
	server_schedule_frames(server);
	return THING_CARRIED;
}

static bool cb_release(struct desk_server *server)
{
	if (!b.held) {
		return false;
	}
	b.held = false;
	if (b.held_moved) {
		snd_at(server, DESK_SND_FELT_DROP, desk_cursor(server)->x, 0.5);
		/* carried to the front of the desk, or thrown that way: back in the drawer
		 * (2026-11-12: on a touch screen the front edge is a hard place to let go at) */
		double still = monotonic_sec() - b.last_t;
		bool thrown = still < 0.08 && b.vy > 1.1 * desk_height(server) && b.vy > 2.0 * fabs(b.vx);
		if (desk_at_front(server, desk_cursor(server)->x, desk_cursor(server)->y) || thrown) {
			if (thrown) {
				wlr_log(WLR_INFO, "clipboard: thrown towards the front (%.0f px/s down)", b.vy);
			}
			cb_put_away(server);
		}
		return true;
	}
	double lx, ly;
	int idx = -1;
	int spot = cb_under(server, &lx, &ly, &idx);
	if (spot != b.held_spot || idx != b.held_idx) {
		return true;
	}
	if (spot == DESK_CLIP_SLIP) {
		/* copied again, and to the front */
		if (desk_clips_copy(server, idx)) {
			snd_at(server, DESK_SND_CORNER_TAP, desk_cursor(server)->x, 0.5);
			b.settle_at = idx > 0 ? monotonic_sec() : 0.0;
			b.hover = -1;
			wlr_log(WLR_INFO, "clipboard: slip %d copied again", idx);
		}
		server_schedule_frames(server);
	}
	return true;
}

/* The wheel over it goes back through what is kept, if there is more of it than shows. */
static bool cb_wheel(struct desk_server *server, struct wlr_pointer_axis_event *event)
{
	double lx, ly;
	int idx;
	if (cb_under(server, &lx, &ly, &idx) == DESK_CLIP_OFF) {
		return false;
	}
	if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL && event->delta != 0.0) {
		int most = desk_clips_n() - 1 - DESK_CLIP_SHOWN;
		int to = b.scroll + (event->delta > 0.0 ? 1 : -1);
		to = to > most ? most : to;
		to = to < 0 ? 0 : to;
		if (to != b.scroll) {
			b.scroll = to;
			snd_at(server, DESK_SND_LETTER_FLICK, desk_cursor(server)->x, 0.3);
			server_schedule_frames(server);
		}
	}
	return true;
}

/* The screen has changed: it stays on it, at its size in points. */
static void cb_place(struct desk_server *server)
{
	if (b.where == CB_IN_DRAWER) {
		return;
	}
	b.k = cb_k_for(server);
	if (!b.moving) {
		cb_on_desk(server, &b.x, &b.y);
		cb_lay();
	}
	cb_show(server);
}

static bool cb_in_drawer(const struct desk_server *server)
{
	(void)server;
	return b.where == CB_IN_DRAWER;
}

static void cb_quitting(struct desk_server *server)
{
	(void)server;
	desk_clips_stop();
	desk_clip_art_drop();
}

static void cb_drawer_item(struct desk_drawer_item *it)
{
	it->look = DESK_LOOK_THING;
	it->lying = desk_clip_art_lying;
	it->thick = 0.10;
	snprintf(it->name, sizeof it->name, "%s", TR("Clipboard"));
	it->seed = 59;
}

const struct desk_thing desk_thing_clipboard = {
	.name = "clipboard",
	.hand_down = cb_press,
	.hand_here = cb_here,
	.drag = cb_drag,
	.release = cb_release,
	.wheel = cb_wheel,
	.tick = cb_tick,
	.screen_changed = cb_place,
	.quitting = cb_quitting,
	.drawer_word = "clipboard",
	.drawer_item = cb_drawer_item,
	.in_drawer = cb_in_drawer,
	.take_out = cb_take_out,
	.put_away = cb_put_away,
};
