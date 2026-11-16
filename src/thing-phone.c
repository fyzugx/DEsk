/* ================================================================
 * The phone: a picture-in-picture window is the screen of a slider phone
 * (phone.h has its pictures). What is round the window is ours: keys that speak
 * to the player by MPRIS, and a deck that comes out from under it, a node of its
 * own under the frame, moved rather than drawn again. A phone does not fold, is
 * not opened out and is not crumpled, its shadow is in its pictures, and it
 * leaves the keys where they are.
 * ================================================================ */

#include "thing.h"
#include "desk.h"
#include "phone.h"
#ifdef DESK_HAVE_POST
#include "media.h"
#endif

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <wlr/util/edges.h>

extern const struct desk_thing desk_thing_phone;

#define PH_SLIDE_S 0.30

/* What it keeps for its window. */
struct phone {
	bool begun;
	struct wlr_scene_buffer *deck;  /* the deck that comes out from under it */
	int src;                        /* rows of the deck's picture still under the slab */
	int held;                       /* the key held down on it */
	double t, to, last;             /* the deck: 0 in, 1 out, and where it is going */
	int y0, lift;                   /* gone up by this, from there, to make room for it */
	long said, said_len;            /* what the deck is showing */
	bool said_have;
};

/* A phone with a key in the hand, and its slider, at this. */
static struct desk_toplevel *g_grab;
static bool g_sliding;
static double g_slide;

static struct phone *phone_of(struct desk_toplevel *view)
{
	struct phone *p = view_thing_data(view, &desk_thing_phone, sizeof *p);
	if (!p->begun) {
		p->begun = true;
		p->held = PH_KEY_NONE;
	}
	return p;
}

static bool view_phone(const struct desk_toplevel *view)
{
	return view_thing(view) == &desk_thing_phone;
}

/* Are they windows of one program? (A browser's picture may have no id of its own.) */
static bool phone_same_program(const struct desk_toplevel *a, const struct desk_toplevel *b)
{
	pid_t pa = view_pid(a);
	return pa > 0 && pa == view_pid(b);
}

#ifdef DESK_HAVE_POST
/* What the program calls itself, for finding its player: its window's id or class, or
 * those of another of its windows. */
static void phone_program(struct desk_toplevel *view, char *out, size_t n)
{
	const char *id = view_program_id(view);
	if (!(id && *id)) {
		struct desk_server *server = view_server(view);
		struct desk_toplevel *other;
		for (other = view_next(server, NULL); other; other = view_next(server, other)) {
			if (other != view && phone_same_program(view, other)) {
				id = view_program_id(other);
				break;
			}
		}
	}
	snprintf(out, n, "%s", id ? id : "");
}
#endif

static void phone_read(struct desk_toplevel *view, struct desk_phone_deck *d)
{
	d->have = false;
	d->can_seek = false;
	d->position = -1.0;
	d->length = -1.0;
#ifdef DESK_HAVE_POST
	char id[128] = "";
	phone_program(view, id, sizeof id);
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	struct desk_media m;
	desk_media_read_for(id, &m, ts.tv_sec + ts.tv_nsec / 1e9);
	d->have = m.have;
	d->can_seek = m.have && m.can_seek;
	d->position = m.position;
	d->length = m.length;
#else
	(void)view;
#endif
}

static void phone_command(struct desk_toplevel *view, int key)
{
#ifdef DESK_HAVE_POST
	char id[128] = "";
	phone_program(view, id, sizeof id);
	if (key == PH_KEY_BACK || key == PH_KEY_FWD) {
		desk_media_seek_for(id, key == PH_KEY_BACK ? -10.0 : 10.0);
	} else if (key == PH_KEY_PLAY) {
		desk_media_command_for(id, DESK_MEDIA_PLAY_PAUSE);
	} else if (key == PH_KEY_OPEN) {
		desk_media_command_for(id, DESK_MEDIA_RAISE);
	}
#else
	(void)view;
	(void)key;
#endif
}

static void phone_deck_place(struct desk_toplevel *view)
{
	struct phone *p = phone_of(view);
	if (!p->deck) {
		return;
	}
	if (p->t <= 0.0 || !view_phone(view)) {
		wlr_scene_node_set_enabled(&p->deck->node, false);
		return;
	}
	int pw, ph, x, y;
	double k;
	desk_phone_deck_size(view_w(view), view_h(view), &pw, &ph, &k, &x, &y);
	double u = p->t * p->t * (3.0 - 2.0 * p->t);
	int top = y - (int)lround(PH_DECK_OUT * k * (1.0 - u));
	/* (under a slab shorter than it is, what would come out at the top stays in) */
	int src = top < 6 ? 6 - top : 0;
	if (src >= ph - 1) {
		wlr_scene_node_set_enabled(&p->deck->node, false);
		return;
	}
	struct wlr_fbox box = { 0.0, src, pw, ph - src };
	scene_source(p->deck, &box);
	wlr_scene_buffer_set_dest_size(p->deck, pw, ph - src);
	wlr_scene_node_set_position(&p->deck->node, x, top + src);
	wlr_scene_node_set_enabled(&p->deck->node, true);
	p->src = src;
}

static bool phone_deck_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	struct desk_toplevel *view = buffer->node.data;
	return view && view_phone(view) && desk_phone_deck_on(view_w(view), *sx, *sy + phone_of(view)->src);
}

static void phone_deck_paint(struct desk_toplevel *view)
{
	struct wlr_scene_tree *tree = view_tree(view);
	if (!tree || view_w(view) <= 0) {
		return;
	}
	struct phone *p = phone_of(view);
	int pw, ph, x, y;
	double k;
	desk_phone_deck_size(view_w(view), view_h(view), &pw, &ph, &k, &x, &y);
	struct desk_phone_deck d = { .held = p->held };
	phone_read(view, &d);
	if (g_grab == view && g_sliding) {
		d.sliding = true;
		d.slide = g_slide;
	}
	p->said = d.position < 0.0 ? -1 : (long)d.position;
	p->said_len = d.length < 0.0 ? -1 : (long)d.length;
	p->said_have = d.have;
	struct desk_buf *buf = buf_create(pw, ph);
	desk_draw_phone_deck(buf->cr, view_w(view), &d);
	scene_put(&p->deck, tree, buf, x, y, view);
	p->deck->point_accepts_input = phone_deck_accepts;
	view_lay_under(view, &p->deck->node);
	phone_deck_place(view);
	server_schedule_frames(view_server(view));
}

/* Gone from the desk, or no phone any more: as it was. */
static void phone_forget(struct desk_toplevel *view)
{
	struct phone *p = phone_of(view);
	if (g_grab == view) {
		g_grab = NULL;
		g_sliding = false;
	}
	p->held = PH_KEY_NONE;
	p->t = p->to = 0.0;
	p->lift = 0;
	if (p->deck) {
		wlr_scene_node_destroy(&p->deck->node);
		p->deck = NULL;
	}
}

/* Where a phone is put down: in the corner of the desk, out of the way. */
static void phone_place(struct desk_toplevel *view)
{
	struct wlr_scene_tree *tree = view_tree(view);
	if (!tree || view_w(view) <= 0) {
		return;
	}
	struct wlr_box wa = work_area(view_server(view));
	int x = wa.x + wa.width - view_w(view) - 28;
	int y = wa.y + wa.height - view_h(view) - 28;
	wlr_scene_node_set_position(&tree->node, x > wa.x ? x : wa.x, y > wa.y ? y : wa.y);
	view_send_position(view);
}

/* The pale light: out, or back in. If there is no room for it below, the phone goes up
 * to make it, and comes back with it. */
static void phone_deck_toggle(struct desk_toplevel *view)
{
	struct desk_server *server = view_server(view);
	struct wlr_scene_tree *tree = view_tree(view);
	struct phone *p = phone_of(view);
	bool out = p->to < 0.5;
	p->to = out ? 1.0 : 0.0;
	p->last = monotonic_sec();
	int ny = tree->node.y;
	if (out) {
		int pw, ph, x, y;
		double k;
		desk_phone_deck_size(view_w(view), view_h(view), &pw, &ph, &k, &x, &y);
		struct wlr_output *o = view_screen(view);
		struct wlr_box sc = o ? screen_area(server, o) : work_area(server);
		struct wlr_box wa = work_area(server);
		int over = ny + view_h(view) + (int)ceil((PH_DECK_OUT + 12.0) * k) - (sc.y + sc.height);
		int room = ny - (o == first_output(server) ? wa.y : sc.y);
		p->y0 = ny;
		p->lift = over > 0 ? (over < room ? over : (room > 0 ? room : 0)) : 0;
		phone_deck_paint(view);
	} else if (ny != p->y0 - p->lift) {
		/* moved since: it stays where it has been put */
		p->lift = 0;
		p->y0 = ny;
	}
	snd_at(server, DESK_SND_CARD_SLIDE, tree->node.x + view_w(view) * 0.5, 0.8);
	view_redraw(view);
	server_schedule_frames(server);
}

/* Where it is playing from: the program's own window that was last in front, and the
 * player asked to show the page it is on. */
static void phone_open_source(struct desk_toplevel *view)
{
	struct desk_server *server = view_server(view);
	struct desk_toplevel *it;
	for (it = view_next(server, NULL); it; it = view_next(server, it)) {
		if (it != view && !view_is(it, &desk_thing_phone) && view_on_desk(it)
			&& phone_same_program(view, it)) {
			view_bring_back(it);
			break;
		}
	}
	phone_command(view, PH_KEY_OPEN);
}

static void phone_key_shown(struct desk_toplevel *view, int key)
{
	struct phone *p = phone_of(view);
	p->held = key;
	view_redraw(view);
	if (p->t > 0.0) {
		phone_deck_paint(view);
	}
	server_schedule_frames(view_server(view));
}

/* What is under the hand on it, measured from the slab's top left: is it on the slab? */
static int phone_key_at(struct desk_toplevel *view, double lx, double ly, bool *on_slab)
{
	struct phone *p = phone_of(view);
	int w = view_w(view), h = view_h(view);
	*on_slab = lx >= -6.0 && lx <= w + 6.0 && ly >= -6.0 && ly <= h + 7.0;
	if (*on_slab) {
		return desk_phone_slab_hit(w, h, lx, ly);
	}
	if (!p->deck || !p->deck->node.enabled || p->t < 0.999) {
		return PH_KEY_NONE;
	}
	return desk_phone_deck_hit(w, lx - p->deck->node.x, ly - p->deck->node.y + p->src);
}

static void phone_press(struct desk_toplevel *view, struct wlr_surface *surface,
	struct wlr_pointer_button_event *event)
{
	struct desk_server *server = view_server(view);
	struct wlr_cursor *cursor = desk_cursor(server);
	struct wlr_scene_tree *tree = view_tree(view);
	struct phone *p = phone_of(view);
	if (surface) {
		/* on the picture: the program's */
		view_pass_press(view, event);
		return;
	}
	if (event->button != BTN_LEFT) {
		return;
	}
	double lx = cursor->x - tree->node.x;
	double ly = cursor->y - tree->node.y;
	bool on_slab;
	int key = phone_key_at(view, lx, ly, &on_slab);
	struct desk_phone_deck d = { .held = PH_KEY_NONE };
	phone_read(view, &d);
	/* a player that cannot be sent anywhere, or none at all: those keys do not give */
	if (((key == PH_KEY_BACK || key == PH_KEY_FWD || key == PH_KEY_SLIDER) && !d.can_seek)
		|| (key == PH_KEY_SLIDER && d.length <= 0.0) || (key == PH_KEY_PLAY && !d.have)) {
		key = PH_KEY_NONE;
	}
	if (key != PH_KEY_NONE) {
		g_grab = view;
		wlr_seat_pointer_clear_focus(desk_seat(server));
		if (key == PH_KEY_SLIDER) {
			p->held = PH_KEY_SLIDER;
			g_sliding = true;
			g_slide = desk_phone_slider_at(view_w(view), lx - p->deck->node.x);
			phone_deck_paint(view);
			return;
		}
		snd_at(server, DESK_SND_KEY_PRESS, cursor->x, 0.8);
		phone_key_shown(view, key);
		return;
	}
	unsigned edges = on_slab ? (unsigned)desk_frame_resize_edges(lx, ly, view_w(view), view_h(view)) : 0;
	if (p->to > 0.5) {
		edges &= ~(unsigned)WLR_EDGE_BOTTOM;   /* (the deck is there) */
	}
	if (edges) {
		view_begin_resize(view, edges);
		return;
	}
	view_begin_move(view);
}

static enum desk_thing_hold phone_drag(struct desk_server *server)
{
	struct desk_toplevel *view = g_grab;
	if (!view) {
		return THING_NOT_HELD;
	}
	struct phone *p = phone_of(view);
	if (g_sliding && p->deck) {
		g_slide = desk_phone_slider_at(view_w(view),
			desk_cursor(server)->x - view_tree(view)->node.x - p->deck->node.x);
		phone_deck_paint(view);
	}
	return THING_HELD;
}

static bool phone_release(struct desk_server *server)
{
	struct desk_toplevel *view = g_grab;
	if (!view) {
		return false;
	}
	struct wlr_cursor *cursor = desk_cursor(server);
	struct phone *p = phone_of(view);
	g_grab = NULL;
	int key = p->held;
	if (g_sliding) {
		g_sliding = false;
		p->held = PH_KEY_NONE;
		struct desk_phone_deck d = { .held = PH_KEY_NONE };
		phone_read(view, &d);
#ifdef DESK_HAVE_POST
		if (d.length > 0.0) {
			char id[128] = "";
			phone_program(view, id, sizeof id);
			desk_media_goto_for(id, g_slide * d.length);
		}
#endif
		phone_deck_paint(view);
		return true;
	}
	bool on_slab;
	int over = phone_key_at(view, cursor->x - view_tree(view)->node.x,
		cursor->y - view_tree(view)->node.y, &on_slab);
	snd_at(server, DESK_SND_KEY_RELEASE, cursor->x, 0.8);
	phone_key_shown(view, PH_KEY_NONE);
	if (over != key) {
		return true;   /* taken off it: nothing */
	}
	if (key == PH_KEY_CLOSE) {
		view_close(view);
	} else if (key == PH_KEY_DECK) {
		phone_deck_toggle(view);
	} else if (key == PH_KEY_OPEN) {
		phone_open_source(view);
	} else {
		phone_command(view, key);
		if (p->t > 0.0) {
			phone_deck_paint(view);
		}
	}
	return true;
}

static void phone_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	struct desk_toplevel *view;
	for (view = view_next(server, NULL); view; view = view_next(server, view)) {
		struct wlr_scene_tree *tree = view_tree(view);
		if (!view_is(view, &desk_thing_phone) || !tree) {
			continue;
		}
		struct phone *p = phone_of(view);
		if (p->t == p->to) {
			continue;
		}
		double step = (now - p->last) / PH_SLIDE_S;
		p->last = now;
		p->t = p->t < p->to ? fmin(p->t + step, p->to) : fmax(p->t - step, p->to);
		phone_deck_place(view);
		if (p->lift > 0) {
			double u = p->t * p->t * (3.0 - 2.0 * p->t);
			wlr_scene_node_set_position(&tree->node, tree->node.x, p->y0 - (int)lround(p->lift * u));
		}
		if (p->t == p->to) {
			snd_at(server, DESK_SND_GUARD_STOP, tree->node.x + view_w(view) * 0.5, 0.7);
			view_send_position(view);
		}
		server_schedule_frames(server);
	}
}

/* What the deck says, again, if it has changed: once a second, and when a player says. */
static void phone_say(struct desk_server *server)
{
	struct desk_toplevel *view;
	for (view = view_next(server, NULL); view; view = view_next(server, view)) {
		if (!view_is(view, &desk_thing_phone) || !view_tree(view)) {
			continue;
		}
		struct phone *p = phone_of(view);
		if (p->t <= 0.0 || (g_grab == view && g_sliding)) {
			continue;
		}
		struct desk_phone_deck d = { .held = PH_KEY_NONE };
		phone_read(view, &d);
		long pos = d.position < 0.0 ? -1 : (long)d.position;
		long len = d.length < 0.0 ? -1 : (long)d.length;
		if (pos != p->said || len != p->said_len || d.have != p->said_have) {
			phone_deck_paint(view);
		}
	}
}

static void phone_first_size(const struct wlr_box *screen, int *w, int *h)
{
	(void)screen;
	*w = *h = 0;   /* (a picture knows what size it is) */
}

static void phone_paint(cairo_t *cr, struct desk_toplevel *view, double x, double y, double w,
	double h, const char *title, bool focused, bool resting)
{
	(void)title;
	(void)focused;
	struct phone *p = phone_of(view);
	desk_draw_phone(cr, x, y, w, h, resting ? PH_KEY_NONE : p->held, !resting && p->to > 0.5);
}

static int phone_look(struct desk_toplevel *view)
{
	struct phone *p = phone_of(view);
	return (p->held + 1) * 2 + (p->to > 0.5);
}

/* All of the slab but the screen, and its keys stand a little past nothing. */
static bool phone_solid(struct desk_toplevel *view, double lx, double ly)
{
	int w = view_w(view), h = view_h(view);
	if (lx < -6 || ly < -6 || lx > w + 6 || ly > h + 7) {
		return false;
	}
	return !(lx >= PH_L && lx <= w - PH_R && ly >= PH_T && ly <= h - PH_B);
}

/* Its keys, and with the deck out, no pulling it down. */
static unsigned phone_edges(struct desk_toplevel *view, double lx, double ly, unsigned edges)
{
	int w = view_w(view), h = view_h(view);
	if (desk_phone_slab_hit(w, h, lx, ly) != PH_KEY_NONE || ly > h + 7.0) {
		return 0;
	}
	if (phone_of(view)->to > 0.5) {
		edges &= ~(unsigned)WLR_EDGE_BOTTOM;
	}
	return edges;
}

/* (the size the deck goes with has changed) */
static void phone_painted(struct desk_toplevel *view)
{
	struct phone *p = phone_of(view);
	if (p->t > 0.0 || p->to > 0.0) {
		phone_deck_paint(view);
	}
}

static void phone_hidden(struct desk_toplevel *view)
{
	struct phone *p = phone_of(view);
	if (p->deck) {
		wlr_scene_node_set_enabled(&p->deck->node, false);
	}
}

const struct desk_thing desk_thing_phone = {
	.name = "phone",
	.claims = view_is_pip,
	.by_title = true,
	.ready = desk_phone_ready,
	.no = THING_NO_FOLD | THING_NO_OPEN_OUT | THING_NO_HALF | THING_NO_CORNER | THING_NO_CRUMPLE
		| THING_NO_CREASES | THING_NO_SHADOW | THING_NO_KEYS,
	.frame_always = true,
	.inset_l = PH_L,
	.inset_t = PH_T,
	.inset_r = PH_R,
	.inset_b = PH_B,
	.pad = PH_PAD,
	.first_size = phone_first_size,
	.put_down = phone_place,
	.paint = phone_paint,
	.look = phone_look,
	.solid = phone_solid,
	.edges = phone_edges,
	.press = phone_press,
	.painted = phone_painted,
	.hidden = phone_hidden,
	.forget = phone_forget,
	.drag = phone_drag,
	.release = phone_release,
	.tick = phone_tick,
	.say = phone_say,
};
