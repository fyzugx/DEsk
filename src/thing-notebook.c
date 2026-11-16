/* ================================================================
 * The notebook: the window of desk-notepad is the top sheet of a wire-bound
 * notebook (notebook.h), the wire and the rest of the stack going past its
 * edges. The sheet is ours, and the program has the words on it and what is
 * written with a pen. Its corners are the program's, to turn the sheet by.
 *
 * By it lies its pencil case: a pen of four refills, turned by the hand to
 * the one wanted, a highlighter and a rubber. Each is taken out into the hand
 * and put back with a touch; the pen turns on the finger, and let go it goes on
 * and clicks into the refill it comes to. What is out, the desk keeps in a file
 * the program reads (notebook.h). There is one case, whichever notebook is out:
 * it comes out under the first, and is anyone's to pick up and put down
 * anywhere on the desk, lifted off it on the hand and falling back, sliding on
 * a little if it was let go moving. It stays over the notebooks.
 *
 * Let go of at an edge of the desk, it is done with: the program is asked to
 * go, and as it goes the cover comes over and it slides off that way, the case
 * with it if it was the last out; and the case comes out again with the next,
 * where it lay by this one. If it is still there a moment later (a file not
 * saved, asked about), it comes back to where it was picked up. It shuts where
 * paper folds.
 *
 * In the drawer it is the editor's, when the notebook is the standard editor;
 * and on a machine with a pen, where another editor is, a thing of its own.
 * ================================================================ */

#include "thing.h"
#include "apps.h"
#include "desk.h"
#include "i18n.h"
#include "notebook.h"
#include "case-map.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/util/edges.h>

extern const struct desk_thing desk_thing_notebook;

/* The case's rim this far below the sheet's foot; and room over its picture for what is taken
 * out of it to be lifted into. */
#define CASE_GAP 16
#define CASE_LIFT 18
#define CASE_BUF_H (CASE_H + CASE_LIFT)
/* Out of the case, in the hand: this much higher and to the left, its shadow this much further
 * off and lighter. */
#define OUT_UP 13
#define OUT_LEFT 5
#define OUT_SHADOW_X 8
#define OUT_SHADOW_Y 10
#define OUT_SHADOW_A 0.5
/* The pen turning: one refill is a quarter turn, its top going a quarter of the way round; the
 * air and its fit slow it, and below this speed it clicks into the nearest refill. */
#define PEN_TURN_PX (PEN_RADIUS * 0.974 * 1.5708)
#define PEN_SLOWS 3.2
#define PEN_SETTLES 1.6
#define PEN_SPRING 260.0
#define PEN_DAMP 26.0
/* The case picked up: this much up off the desk and to the left, in this long, its shadow this much
 * further off; let fall, faster as it goes; let go moving, it slides on, slowing by this much a
 * second, and stops below this speed. Its picture has this much room round it for that. */
#define CASE_UP_X 3
#define CASE_UP_Y 7
#define CASE_SHADOW_X 7
#define CASE_SHADOW_Y 11
#define CASE_SHADOW_A 0.24
#define CASE_LIFT_S 0.09
#define CASE_FALL_S 0.15
#define CASE_FRICTION 9.0
#define CASE_STOPS 20.0
#define CASE_M 16
/* The sheet's corners are the program's: a hand there turns the sheet, it does not pull. */
#define CORNER_FREE 34
/* Going: the cover over, then off the desk; let go at an edge and still there this long, back. */
#define SHUT_S 0.32
#define AWAY_S 0.45
#define STAYED_S 0.9
#define BACK_S 0.32

enum { TOOL_PEN, TOOL_MARKER, TOOL_RUBBER, TOOLS, TOOL_BOX = TOOLS, TOOL_NONE };

static const char *const PEN_NAME[NB_PENS] = NB_PEN_NAMES;
static const char *const HAND_NAME[NB_HANDS] = NB_HAND_NAMES;

/* What is in the case, and what it looks like, for every notebook. */
static struct {
	bool tried, ok;
	cairo_surface_t *box, *roll, *rest, *shadow[TOOLS], *marker, *rubber;
	double s;                 /* the pen, turned: 0 its black refill up, 1 blue, .. 4 black again */
	double w;                 /* how fast, refills a second */
	bool turning, aiming;
	double aim;
	int pen, hand;            /* the refill out; what is in the hand (NB_HAND_*) */
	double last;
	/* a hand on the case */
	bool grab;
	int held;
	double y0, x0, s0, ty, vs, hy;
	bool moved;
} g_pc = { .pen = NB_PEN_BLUE, .s = NB_PEN_BLUE, .hand = NB_HAND_NONE, .held = TOOL_NONE };

/* The case, on the desk by itself, while a notebook is out. */
static struct {
	struct desk_server *server;
	struct wlr_scene_buffer *node;
	bool shown, placed;
	double x, y;              /* its picture's top left on the desk */
	int ox, oy;               /* and where in the node's picture that is, as high as it is */
	/* from the sheet's top left of the notebook it came out by: where it was when they went, the
	 * last it was with, which was last here */
	bool have_by;
	int by_x, by_y, book_x, book_y;
	/* on the hand: from where on it, and how fast, when it was last */
	bool carried;
	double hx, hy, vx, vy, tv, px, py;
	/* how high off the desk, 0 to 1, going from lift_from to lift_to since lift_at; and sliding */
	double lift, lift_from, lift_to, lift_at;
	bool sliding;
	double last;
} g_cs;

/* What it keeps for its window. */
struct book {
	/* let go at an edge, it is asked to go: from which side, when, and where it came from */
	int side;
	double side_at;
	bool have_from;
	int from_x, from_y;
	/* and coming back */
	bool back;
	double back_at;
	int back_x0, back_y0;
};

/* A notebook going off the desk, after its window has. */
struct going {
	bool on;
	struct desk_server *server;
	struct wlr_scene_buffer *node, *case_node;
	cairo_surface_t *open, *cover, *shut;
	int x, y, w, h;           /* the picture's top left on the desk, and its size */
	int case_x, case_y;       /* and the case's, if it goes as well */
	int side;
	double at;
	bool sounded;
};
static struct going g_going[4];
static bool g_quitting;

static struct book *book_of(struct desk_toplevel *view)
{
	return view_thing_data(view, &desk_thing_notebook, sizeof(struct book));
}

/* ---- the pictures ----------------------------------------------- */

static cairo_surface_t *case_picture(const char *file)
{
	char path[1200];
	if (!desk_art_find("notebook", file, path, sizeof path)) {
		fprintf(stderr, "desk: the pencil case's picture is not there (%s)\n", path);
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "desk: the pencil case's picture would not read (%s)\n", path);
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

static void case_state_read(void);

static bool case_ready(void)
{
	if (!g_pc.tried) {
		g_pc.tried = true;
		g_pc.box = case_picture("nb-case.png");
		g_pc.roll = case_picture("nb-pen-roll.png");
		g_pc.rest = case_picture("nb-pen-rest.png");
		g_pc.shadow[TOOL_PEN] = case_picture("nb-pen-shadow.png");
		g_pc.shadow[TOOL_MARKER] = case_picture("nb-marker-shadow.png");
		g_pc.shadow[TOOL_RUBBER] = case_picture("nb-rubber-shadow.png");
		g_pc.marker = case_picture("nb-marker.png");
		g_pc.rubber = case_picture("nb-rubber.png");
		g_pc.ok = g_pc.box && g_pc.roll && g_pc.rest && g_pc.shadow[TOOL_PEN] && g_pc.shadow[TOOL_MARKER]
			&& g_pc.shadow[TOOL_RUBBER] && g_pc.marker && g_pc.rubber;
		case_state_read();
	}
	return g_pc.ok;
}

/* ---- what is out of the case, kept for the program ----------------- */

static void case_state_path(char *out, size_t n)
{
	const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	if (state && *state) {
		snprintf(out, n, "%s/desk/%s", state, NB_CASE_FILE);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/state/desk/%s", home, NB_CASE_FILE);
	} else {
		out[0] = '\0';
	}
}

static void case_state_read(void)
{
	char path[600];
	case_state_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	char line[128];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		for (int k = 0; k < NB_PENS; k++) {
			if (!strncmp(line, "pen ", 4) && !strcmp(line + 4, PEN_NAME[k])) {
				g_pc.pen = k;
				g_pc.s = k;
			}
		}
		for (int k = 0; k < NB_HANDS; k++) {
			if (!strncmp(line, "hand ", 5) && !strcmp(line + 5, HAND_NAME[k])) {
				g_pc.hand = k;
			}
		}
		int bx, by;
		if (sscanf(line, "by %d %d", &bx, &by) == 2 && abs(bx) < 20000 && abs(by) < 20000) {
			g_cs.have_by = true;
			g_cs.by_x = bx;
			g_cs.by_y = by;
		}
	}
	fclose(f);
}

/* Written beside it and put in its place: the program watches for that. */
static void case_state_write(void)
{
	char path[600], tmp[640];
	case_state_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = '\0';
		char dir[600];
		snprintf(dir, sizeof dir, "%s", path);
		*slash = '/';
		/* (its parent, then itself) */
		char *up = strrchr(dir, '/');
		if (up) {
			*up = '\0';
			mkdir(dir, 0700);
			*up = '/';
		}
		mkdir(dir, 0700);
	}
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		fprintf(stderr, "desk: cannot keep what is out of the pencil case in %s: %s\n", tmp, strerror(errno));
		return;
	}
	fprintf(f, "# DEsk's pencil case: the pen's refill that is out, and what is in the hand;\n"
		"# and where it lay by the notebook, from the sheet's top left.\n"
		"pen %s\nhand %s\n", PEN_NAME[g_pc.pen], HAND_NAME[g_pc.hand]);
	if (g_cs.have_by) {
		fprintf(f, "by %d %d\n", g_cs.by_x, g_cs.by_y);
	}
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

/* ---- the case, drawn --------------------------------------------- */

static int tool_hand(int tool)
{
	return tool == TOOL_PEN ? NB_HAND_PEN : tool == TOOL_MARKER ? NB_HAND_MARKER : NB_HAND_RUBBER;
}

/* Where a thing in the case is, in the case's picture (the room over it counted in). */
static void tool_box(int tool, bool out, int *x, int *y, int *w, int *h)
{
	switch (tool) {
	case TOOL_PEN: *x = PEN_REST_X; *y = PEN_REST_Y; *w = PEN_REST_W; *h = PEN_REST_H; break;
	case TOOL_MARKER: *x = MARKER_X; *y = MARKER_Y; *w = MARKER_W; *h = MARKER_H; break;
	default: *x = RUBBER_X; *y = RUBBER_Y; *w = RUBBER_W; *h = RUBBER_H; break;
	}
	*y += CASE_LIFT;
	if (out) {
		*x -= OUT_LEFT;
		*y -= OUT_UP;
	}
}

static void paint_part(cairo_t *cr, cairo_surface_t *src, int sx, int sy, int sw, int sh, double x, double y,
	double alpha)
{
	cairo_save(cr);
	cairo_rectangle(cr, x, y, sw, sh);
	cairo_clip(cr);
	cairo_set_source_surface(cr, src, x - sx, y - sy);
	cairo_paint_with_alpha(cr, alpha);
	cairo_restore(cr);
}

static void tool_shadow(cairo_t *cr, int tool, bool out)
{
	static const int SX[TOOLS] = { PEN_SHADOW_X, MARKER_SHADOW_X, RUBBER_SHADOW_X };
	static const int SY[TOOLS] = { PEN_SHADOW_Y, MARKER_SHADOW_Y, RUBBER_SHADOW_Y };
	cairo_surface_t *s = g_pc.shadow[tool];
	double x = SX[tool] + (out ? OUT_SHADOW_X : 0), y = SY[tool] + CASE_LIFT + (out ? OUT_SHADOW_Y : 0);
	cairo_set_source_surface(cr, s, x, y);
	cairo_paint_with_alpha(cr, out ? OUT_SHADOW_A : 1.0);
}

static void tool_paint(cairo_t *cr, int tool, bool out)
{
	int x, y, w, h;
	tool_box(tool, out, &x, &y, &w, &h);
	if (tool == TOOL_MARKER) {
		paint_part(cr, g_pc.marker, 0, 0, w, h, x, y, 1.0);
	} else if (tool == TOOL_RUBBER) {
		paint_part(cr, g_pc.rubber, 0, 0, w, h, x, y, 1.0);
	} else {
		double near = g_pc.s - floor(g_pc.s + 0.5);
		if (g_pc.turning || (g_pc.grab && g_pc.held == TOOL_PEN) || fabs(near) > 0.004) {
			/* turning: its refills in */
			int f = ((int)lround(g_pc.s * PEN_FRAMES / 4.0) % PEN_FRAMES + PEN_FRAMES) % PEN_FRAMES;
			int dx = PEN_X - PEN_REST_X;
			paint_part(cr, g_pc.roll, 0, f * PEN_H, PEN_W, PEN_H, x + dx, y + (PEN_Y - PEN_REST_Y), 1.0);
		} else {
			int k = (((int)lround(g_pc.s)) % NB_PENS + NB_PENS) % NB_PENS;
			paint_part(cr, g_pc.rest, 0, k * PEN_REST_H, w, h, x, y, 1.0);
		}
	}
}

/* The case and what is in it; and what is out of it, in the hand, over it. */
static void case_paint(cairo_t *cr)
{
	cairo_set_source_surface(cr, g_pc.box, 0, CASE_LIFT);
	cairo_paint(cr);
	int out = g_pc.hand == NB_HAND_PEN ? TOOL_PEN : g_pc.hand == NB_HAND_MARKER ? TOOL_MARKER
		: g_pc.hand == NB_HAND_RUBBER ? TOOL_RUBBER : TOOL_NONE;
	for (int t = 0; t < TOOLS; t++) {
		if (t != out) {
			tool_shadow(cr, t, false);
		}
	}
	for (int t = TOOLS - 1; t >= 0; t--) {
		if (t != out) {
			tool_paint(cr, t, false);
		}
	}
	if (out != TOOL_NONE) {
		tool_shadow(cr, out, true);
		tool_paint(cr, out, true);
	}
}

/* What a hand is on, in the case's picture: something to take out, or the case. */
static int case_hit(double x, double y)
{
	int out = g_pc.hand == NB_HAND_PEN ? TOOL_PEN : g_pc.hand == NB_HAND_MARKER ? TOOL_MARKER
		: g_pc.hand == NB_HAND_RUBBER ? TOOL_RUBBER : TOOL_NONE;
	if (out != TOOL_NONE) {
		int bx, by, bw, bh;
		tool_box(out, true, &bx, &by, &bw, &bh);
		if (x >= bx && x < bx + bw && y >= by - 3 && y < by + bh + 3) {
			return out;
		}
	}
	for (int t = 0; t < TOOLS; t++) {
		int bx, by, bw, bh;
		tool_box(t, false, &bx, &by, &bw, &bh);
		if (t != out && x >= bx && x < bx + bw && y >= by - 2 && y < by + bh + 2) {
			return t;
		}
	}
	if (x >= CASE_LEFT - 2 && x <= CASE_RIGHT + 2 && y >= CASE_TOP + CASE_LIFT - 2 && y <= CASE_FOOT + CASE_LIFT + 2) {
		return TOOL_BOX;
	}
	return TOOL_NONE;
}

static bool case_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	return case_hit(*sx - g_cs.ox, *sy - g_cs.oy) != TOOL_NONE;
}

static bool view_book(const struct desk_toplevel *view)
{
	return view_thing(view) == &desk_thing_notebook;
}

/* Where the case first lies by a notebook, from its sheet's top left: under the sheet's foot, a
 * little to the right of its middle. */
static void case_beside(struct desk_toplevel *view, int *x, int *y)
{
	int w = view_w(view), h = view_h(view);
	*x = (w - (CASE_RIGHT - CASE_LEFT)) / 2 + 8 - CASE_LEFT;
	*y = h + CASE_GAP - CASE_TOP - CASE_LIFT;
}

/* The case as it lies, or as high as it is off the desk, its shadow under it. */
static void case_redraw(void)
{
	struct desk_server *server = g_cs.server;
	if (!server || !g_cs.shown || !case_ready()) {
		return;
	}
	double up = g_cs.lift;
	g_cs.ox = CASE_M - (int)lround(CASE_UP_X * up);
	g_cs.oy = CASE_M - (int)lround(CASE_UP_Y * up);
	struct desk_buf *buf = buf_create(CASE_W + 2 * CASE_M, CASE_BUF_H + 2 * CASE_M);
	cairo_t *cr = buf->cr;
	if (up > 0.005) {
		/* off the desk: a shadow of all of it, further off */
		cairo_push_group(cr);
		case_paint(cr);
		cairo_pattern_t *pic = cairo_pop_group(cr);
		cairo_save(cr);
		cairo_translate(cr, CASE_M + CASE_SHADOW_X * up, CASE_M + CASE_SHADOW_Y * up);
		cairo_set_source_rgba(cr, 0.07, 0.05, 0.03, CASE_SHADOW_A * up);
		cairo_mask(cr, pic);
		cairo_restore(cr);
		cairo_translate(cr, g_cs.ox, g_cs.oy);
		cairo_set_source(cr, pic);
		cairo_paint(cr);
		cairo_pattern_destroy(pic);
	} else {
		cairo_translate(cr, g_cs.ox, g_cs.oy);
		case_paint(cr);
	}
	scene_put(&g_cs.node, desk_windows(server), buf, (int)lround(g_cs.x) - CASE_M,
		(int)lround(g_cs.y) - CASE_M, NULL);
	g_cs.node->point_accepts_input = case_accepts;
	wlr_scene_node_set_enabled(&g_cs.node->node, true);
	server_schedule_frames(server);
}

static void case_moved(void)
{
	if (g_cs.node) {
		wlr_scene_node_set_position(&g_cs.node->node, (int)lround(g_cs.x) - CASE_M,
			(int)lround(g_cs.y) - CASE_M);
		server_schedule_frames(g_cs.server);
	}
}

/* All of it on the desk: it does not go past the screens' edges. */
static void case_keep_on(struct desk_server *server)
{
	struct wlr_box box;
	wlr_output_layout_get_box(desk_layout(server), NULL, &box);
	if (wlr_box_empty(&box)) {
		return;
	}
	double x0 = box.x - CASE_LEFT, x1 = box.x + box.width - CASE_RIGHT;
	double y0 = box.y - CASE_TOP - CASE_LIFT, y1 = box.y + box.height - CASE_FOOT - CASE_LIFT;
	if (g_cs.x < x0 || g_cs.x > x1) {
		g_cs.x = g_cs.x < x0 ? x0 : x1;
		g_cs.vx = 0.0;
	}
	if (g_cs.y < y0 || g_cs.y > y1) {
		g_cs.y = g_cs.y < y0 ? y0 : y1;
		g_cs.vy = 0.0;
	}
}

/* Going up off the desk, or down onto it. */
static void case_lift(double to)
{
	g_cs.lift_from = g_cs.lift;
	g_cs.lift_to = to;
	g_cs.lift_at = monotonic_sec();
	server_schedule_frames(g_cs.server);
}

/* Where the hand is, is it on the case, uppermost there? Where on its picture. */
static bool case_under_hand(struct desk_server *server, double *x, double *y)
{
	if (!g_cs.shown || !g_cs.node || !g_cs.node->node.enabled || desk_covered(server)) {
		return false;
	}
	struct wlr_cursor *cursor = desk_cursor(server);
	double sx, sy;
	struct wlr_scene_node *n = wlr_scene_node_at(&desk_scene(server)->tree.node, cursor->x, cursor->y,
		&sx, &sy);
	if (n != &g_cs.node->node) {
		return false;
	}
	*x = sx - g_cs.ox;
	*y = sy - g_cs.oy;
	return true;
}

/* ---- a hand on the case ------------------------------------------ */

static void case_out(struct desk_server *server, int hand, double x)
{
	bool was = g_pc.hand != NB_HAND_NONE;
	g_pc.hand = hand;
	snd_at(server, hand == NB_HAND_NONE ? DESK_SND_FELT_DROP : DESK_SND_FELT_PICK, x, was && hand != NB_HAND_NONE ? 0.5 : 0.7);
	case_state_write();
	case_redraw();
}

static bool notebook_hand_down(struct desk_server *server, uint32_t button)
{
	double x, y;
	if (g_pc.grab || !case_under_hand(server, &x, &y)) {
		return false;
	}
	int what = case_hit(x, y);
	if (what == TOOL_NONE) {
		return false;
	}
	if (button != BTN_LEFT) {
		return true;
	}
	struct wlr_cursor *cursor = desk_cursor(server);
	g_pc.grab = true;
	g_pc.held = what;
	g_pc.x0 = cursor->x;
	g_pc.y0 = cursor->y;
	g_pc.hy = cursor->y;
	g_pc.s0 = g_pc.s;
	g_pc.ty = monotonic_sec();
	g_pc.vs = 0.0;
	g_pc.moved = false;
	if (what == TOOL_PEN) {
		/* held still: no going on by itself */
		g_pc.turning = false;
		g_pc.w = 0.0;
	}
	/* (caught, if it was sliding) */
	g_cs.sliding = false;
	wlr_seat_pointer_clear_focus(desk_seat(server));
	return true;
}

/* Picked up: on the hand from here on, over everything it was under. */
static void case_pick_up(struct desk_server *server)
{
	struct wlr_cursor *cursor = desk_cursor(server);
	g_cs.carried = true;
	/* (from where the hand was put down on it: it comes on to the hand at once) */
	g_cs.hx = g_pc.x0 - g_cs.x;
	g_cs.hy = g_pc.y0 - g_cs.y;
	g_cs.px = cursor->x;
	g_cs.py = cursor->y;
	g_cs.vx = g_cs.vy = 0.0;
	g_cs.tv = monotonic_sec();
	if (g_cs.node) {
		wlr_scene_node_raise_to_top(&g_cs.node->node);
		views_above_raise(server);
	}
	snd_at(server, DESK_SND_FELT_PICK, cursor->x, 0.5);
	case_lift(1.0);
}

/* Where the hand is, 1:1, and how fast it went there. */
static void case_carry(struct desk_server *server)
{
	struct wlr_cursor *cursor = desk_cursor(server);
	double now = monotonic_sec(), dt = now - g_cs.tv;
	if (dt > 0.0005) {
		double a = dt > 0.08 ? 1.0 : 0.45;
		g_cs.vx = (1.0 - a) * g_cs.vx + a * (cursor->x - g_cs.px) / dt;
		g_cs.vy = (1.0 - a) * g_cs.vy + a * (cursor->y - g_cs.py) / dt;
		g_cs.tv = now;
		g_cs.px = cursor->x;
		g_cs.py = cursor->y;
	}
	g_cs.x = cursor->x - g_cs.hx;
	g_cs.y = cursor->y - g_cs.hy;
	case_keep_on(server);
	case_moved();
}

static enum desk_thing_hold notebook_drag(struct desk_server *server)
{
	if (!g_pc.grab) {
		return THING_NOT_HELD;
	}
	struct wlr_cursor *cursor = desk_cursor(server);
	double dx = cursor->x - g_pc.x0, dy = cursor->y - g_pc.y0;
	if (!g_pc.moved && hypot(dx, dy) > 4.0) {
		g_pc.moved = true;
		if (g_pc.held != TOOL_PEN) {
			/* not a touch on what is in it: the case, picked up */
			case_pick_up(server);
		}
	}
	if (g_cs.carried) {
		case_carry(server);
		return THING_CARRIED;
	}
	if (g_pc.held == TOOL_PEN) {
		/* its top goes where the hand goes: up the desk, on to the next refill */
		double was = g_pc.s;
		g_pc.s = g_pc.s0 - dy / PEN_TURN_PX;
		double now = monotonic_sec(), dt = now - g_pc.ty;
		if (dt > 0.0005) {
			double a = dt > 0.08 ? 1.0 : 0.45;
			g_pc.vs = (1.0 - a) * g_pc.vs + a * (-(cursor->y - g_pc.hy) / PEN_TURN_PX) / dt;
			g_pc.ty = now;
			g_pc.hy = cursor->y;
		}
		if (floor(was + 0.5) != floor(g_pc.s + 0.5)) {
			snd_at(server, DESK_SND_KNOB_TICK, cursor->x, 0.45);
		}
		case_redraw();
	}
	return THING_HELD;
}

static bool notebook_release(struct desk_server *server)
{
	if (!g_pc.grab) {
		return false;
	}
	struct wlr_cursor *cursor = desk_cursor(server);
	g_pc.grab = false;
	int what = g_pc.held;
	g_pc.held = TOOL_NONE;
	if (g_cs.carried) {
		/* let fall, and on as fast as it was going, slowing */
		g_cs.carried = false;
		bool still = monotonic_sec() - g_cs.tv > 0.08;
		g_cs.vx = still ? 0.0 : fmax(-2500.0, fmin(2500.0, g_cs.vx));
		g_cs.vy = still ? 0.0 : fmax(-2500.0, fmin(2500.0, g_cs.vy));
		g_cs.sliding = hypot(g_cs.vx, g_cs.vy) > CASE_STOPS;
		g_cs.last = monotonic_sec();
		case_lift(0.0);
		return true;
	}
	if (!g_pc.moved) {
		/* a touch: out into the hand, or back in the case */
		if (what == TOOL_BOX) {
			if (g_pc.hand != NB_HAND_NONE) {
				case_out(server, NB_HAND_NONE, cursor->x);
			}
		} else {
			int hand = tool_hand(what);
			case_out(server, g_pc.hand == hand ? NB_HAND_NONE : hand, cursor->x);
		}
		return true;
	}
	if (what == TOOL_PEN) {
		/* on with the hand's speed, slowing, and into a refill */
		bool still = monotonic_sec() - g_pc.ty > 0.08;
		g_pc.w = still ? 0.0 : fmax(-14.0, fmin(14.0, g_pc.vs));
		g_pc.turning = true;
		g_pc.aiming = false;
		g_pc.last = monotonic_sec();
		server_schedule_frames(server);
	}
	return true;
}

/* A wheel over the pen turns it a refill a notch. */
static bool notebook_wheel(struct desk_server *server, struct wlr_pointer_axis_event *event)
{
	double x, y;
	if (!case_under_hand(server, &x, &y)) {
		return false;
	}
	int what = case_hit(x, y);
	if (what == TOOL_PEN && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL && event->delta != 0.0) {
		double from = g_pc.aiming && g_pc.turning ? g_pc.aim : floor(g_pc.s + 0.5);
		g_pc.aim = from + (event->delta < 0.0 ? 1.0 : -1.0);
		g_pc.aiming = true;
		g_pc.turning = true;
		g_pc.last = monotonic_sec();
		server_schedule_frames(server);
	}
	return what != TOOL_NONE;
}

/* The case there while a notebook is out: by the first, the first time, where it last lay by one;
 * gone with the last, and where it lay by that kept. Over the notebooks, whichever is in front. */
static void case_tick(struct desk_server *server)
{
	g_cs.server = server;
	struct desk_toplevel *book = NULL;
	bool any = false;
	for (struct desk_toplevel *v = view_next(server, NULL); v; v = view_next(server, v)) {
		if (!view_is(v, &desk_thing_notebook)) {
			continue;
		}
		any = true;
		struct wlr_scene_tree *tree = view_tree(v);
		if (!book && view_book(v) && tree && tree->node.enabled && view_w(v) > 0) {
			book = v;
		}
	}
	if (book && case_ready()) {
		struct wlr_scene_tree *tree = view_tree(book);
		g_cs.book_x = tree->node.x;
		g_cs.book_y = tree->node.y;
		if (!g_cs.placed) {
			int dx, dy;
			case_beside(book, &dx, &dy);
			if (g_cs.have_by) {
				dx = g_cs.by_x;
				dy = g_cs.by_y;
			}
			g_cs.x = g_cs.book_x + dx;
			g_cs.y = g_cs.book_y + dy;
			g_cs.lift = g_cs.lift_from = g_cs.lift_to = 0.0;
			g_cs.carried = g_cs.sliding = false;
			case_keep_on(server);
			g_cs.placed = true;
		}
		if (!g_cs.shown) {
			g_cs.shown = true;
			case_redraw();
		}
		/* (over the notebook in front: they are under it, the rest as they come) */
		struct wlr_scene_node *n = &g_cs.node->node;
		if (n->parent == tree->node.parent) {
			struct wlr_scene_node *o;
			wl_list_for_each_reverse(o, &n->parent->children, link) {
				if (o == n) {
					break;
				}
				if (o == &tree->node) {
					wlr_scene_node_place_above(n, o);
					break;
				}
			}
		}
	} else if (g_cs.shown) {
		g_cs.shown = false;
		g_cs.carried = g_cs.sliding = false;
		g_pc.grab = false;
		g_pc.held = TOOL_NONE;
		if (g_cs.node) {
			wlr_scene_node_set_enabled(&g_cs.node->node, false);
		}
	}
	if (!any && g_cs.placed) {
		g_cs.placed = false;
		g_cs.have_by = true;
		g_cs.by_x = (int)lround(g_cs.x) - g_cs.book_x;
		g_cs.by_y = (int)lround(g_cs.y) - g_cs.book_y;
		case_state_write();
	}
	if (!g_cs.shown) {
		return;
	}
	double now = monotonic_sec(), dt = fmax(0.0, fmin(0.05, now - g_cs.last));
	g_cs.last = now;
	if (g_cs.lift != g_cs.lift_to) {
		bool up = g_cs.lift_to > g_cs.lift_from;
		double t = (now - g_cs.lift_at) / (up ? CASE_LIFT_S : CASE_FALL_S);
		if (t >= 1.0) {
			g_cs.lift = g_cs.lift_to;
			if (!up) {
				snd_at(server, DESK_SND_FELT_DROP, g_cs.x + CASE_W * 0.5, 0.55);
			}
		} else {
			/* up quickly, then easing; down, faster as it falls */
			g_cs.lift = up ? g_cs.lift_from + (g_cs.lift_to - g_cs.lift_from) * (1.0 - (1.0 - t) * (1.0 - t))
				: g_cs.lift_from * (1.0 - t * t);
		}
		case_redraw();
	}
	if (g_cs.sliding) {
		double k = exp(-CASE_FRICTION * dt);
		g_cs.vx *= k;
		g_cs.vy *= k;
		g_cs.x += g_cs.vx * dt;
		g_cs.y += g_cs.vy * dt;
		case_keep_on(server);
		g_cs.sliding = hypot(g_cs.vx, g_cs.vy) > CASE_STOPS;
		case_moved();
	}
	if (g_cs.sliding || g_cs.lift != g_cs.lift_to) {
		server_schedule_frames(server);
	}
}

/* The pen going on by itself: slowing, then clicking into the refill it is nearest. */
static void pen_tick(struct desk_server *server)
{
	if (!g_pc.turning || g_pc.grab) {
		return;
	}
	double now = monotonic_sec(), dt = now - g_pc.last;
	g_pc.last = now;
	dt = dt < 0.0 ? 0.0 : dt > 0.05 ? 0.05 : dt;
	int n = (int)ceil(dt * 400.0);
	double h = n > 0 ? dt / n : 0.0;
	double was = g_pc.s;
	for (int i = 0; i < n; i++) {
		if (!g_pc.aiming) {
			g_pc.w -= g_pc.w * PEN_SLOWS * h;
			g_pc.s += g_pc.w * h;
			if (fabs(g_pc.w) < PEN_SETTLES) {
				g_pc.aiming = true;
				g_pc.aim = floor(g_pc.s + 0.5);
			}
		} else {
			g_pc.w += (-PEN_SPRING * (g_pc.s - g_pc.aim) - PEN_DAMP * g_pc.w) * h;
			g_pc.s += g_pc.w * h;
		}
	}
	if (floor(was + 0.5) != floor(g_pc.s + 0.5)) {
		snd_at(server, DESK_SND_KNOB_TICK, desk_cursor(server)->x, 0.4);
	}
	if (g_pc.aiming && fabs(g_pc.s - g_pc.aim) < 0.003 && fabs(g_pc.w) < 0.05) {
		/* (kept small: a whole turn is the same pen) */
		g_pc.s = fmod(g_pc.aim, (double)NB_PENS);
		g_pc.s = g_pc.s < 0.0 ? g_pc.s + NB_PENS : g_pc.s;
		g_pc.turning = false;
		g_pc.aiming = false;
		g_pc.w = 0.0;
		int pen = ((int)lround(g_pc.s)) % NB_PENS;
		if (pen != g_pc.pen) {
			g_pc.pen = pen;
			case_state_write();
		}
	}
	case_redraw();
	server_schedule_frames(server);
}

/* ---- the notebook's window ---------------------------------------- */

/* (its window, not what it asks with: a dialog says the same name, and has a
 * parent) */
static bool notebook_claims(const struct desk_toplevel *view)
{
	const char *id = view_wayland_id(view);
	return id && !view_has_parent(view) && !strcasecmp(id, "desk-notepad");
}

/* As big as a notebook is, if the screen has room for it and for the case under it. */
static void notebook_first_size(const struct wlr_box *screen, int *w, int *h)
{
	int most = screen->height - 300 + NB_HEAD_H;
	*w = NB_WIN_W;
	*h = NB_WIN_H < most ? NB_WIN_H : most;
}

/* (what is written in its heading is the program's: it turns over with the sheet) */
static void notebook_paint(cairo_t *cr, struct desk_toplevel *view, double x, double y, double w,
	double h, const char *title, bool focused, bool resting)
{
	(void)view;
	(void)title;
	(void)focused;
	(void)resting;
	desk_draw_notebook(cr, x, y, w, h);
}

/* The wire is something to take hold of, and so is the sheet where the program does not
 * want the hand: its holes, and its heading but for what is written there. */
static bool notebook_solid(struct desk_toplevel *view, double lx, double ly)
{
	int w = view_w(view), h = view_h(view);
	return !(ly < -6 || ly > h + 6 || lx < -NB_PAD || lx > w + 6);
}

/* Its sheet's corners at the foot are for turning it by: no pulling there. */
static unsigned notebook_edges(struct desk_toplevel *view, double lx, double ly, unsigned edges)
{
	int w = view_w(view), h = view_h(view);
	if (ly > h - CORNER_FREE && (lx > w - CORNER_FREE || (lx >= NB_INSET_L && lx < NB_INSET_L + CORNER_FREE))) {
		return 0;
	}
	return edges;
}

static void notebook_press(struct desk_toplevel *view, struct wlr_surface *surface,
	struct wlr_pointer_button_event *event)
{
	if (surface) {
		view_pass_press(view, event);
		return;
	}
	focus_toplevel(view);
	if (event->button != BTN_LEFT) {
		return;
	}
	struct wlr_cursor *cursor = desk_cursor(view_server(view));
	struct wlr_scene_tree *tree = view_tree(view);
	double lx = cursor->x - tree->node.x, ly = cursor->y - tree->node.y;
	unsigned edges = notebook_edges(view, lx, ly,
		(unsigned)desk_frame_resize_edges(lx, ly, view_w(view), view_h(view)));
	if (edges) {
		view_begin_resize(view, edges);
		return;
	}
	struct book *b = book_of(view);
	b->have_from = true;
	b->from_x = tree->node.x;
	b->from_y = tree->node.y;
	b->side = 0;
	view_begin_move(view);
}

/* A sheet grown over where the case lies (opened out, or pulled bigger) puts it out from under: below
 * the sheet, as far as the desk goes. */
static void notebook_painted(struct desk_toplevel *view)
{
	struct wlr_scene_tree *tree = view_tree(view);
	if (!g_cs.shown || g_cs.carried || !tree) {
		return;
	}
	int x = tree->node.x, y = tree->node.y, w = view_w(view), h = view_h(view);
	double cx0 = g_cs.x + CASE_LEFT, cx1 = g_cs.x + CASE_RIGHT;
	double cy0 = g_cs.y + CASE_LIFT + CASE_TOP, cy1 = g_cs.y + CASE_LIFT + CASE_FOOT;
	if (cx1 <= x || cx0 >= x + w || cy1 <= y || cy0 >= y + h) {
		return;
	}
	g_cs.y = y + h + CASE_GAP - CASE_TOP - CASE_LIFT;
	g_cs.sliding = false;
	case_keep_on(view_server(view));
	case_moved();
}

static void notebook_forget(struct desk_toplevel *view)
{
	struct book *b = book_of(view);
	b->side = 0;
	b->back = false;
}

/* ---- done with: to an edge, and off the desk ------------------------- */

/* Let go at an edge: the program is asked to go (it keeps its pages); going, it goes that way. */
static void notebook_at_edge(struct desk_toplevel *view, int side)
{
	struct book *b = book_of(view);
	b->side = side;
	b->side_at = monotonic_sec();
	view_close(view);
}

static void going_free(struct going *g)
{
	if (g->node) {
		wlr_scene_node_destroy(&g->node->node);
	}
	if (g->case_node) {
		wlr_scene_node_destroy(&g->case_node->node);
	}
	cairo_surface_t *pics[] = { g->open, g->cover, g->shut };
	for (size_t i = 0; i < sizeof pics / sizeof pics[0]; i++) {
		if (pics[i]) {
			cairo_surface_destroy(pics[i]);
		}
	}
	memset(g, 0, sizeof *g);
}

/* Its picture as it goes: the cover come over as far as shut. */
static void going_paint(struct going *g, double shut)
{
	struct desk_buf *buf = buf_create(g->w, g->h);
	if (shut >= 1.0 && g->shut) {
		cairo_set_source_surface(buf->cr, g->shut, 0, 0);
		cairo_paint(buf->cr);
	} else {
		desk_notebook_shutting(buf->cr, g->open, g->cover, shut);
	}
	scene_put(&g->node, desk_windows(g->server), buf, g->x, g->y, NULL);
	g->node->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&g->node->node);
	if (g->case_node) {
		/* (and the case, if it is going too, over it as it was) */
		wlr_scene_node_raise_to_top(&g->case_node->node);
	}
}

static void notebook_goes(struct desk_toplevel *view)
{
	struct desk_server *server = view_server(view);
	struct wlr_scene_tree *tree = view_tree(view);
	if (g_quitting || !tree || !desk_notebook_ready()) {
		return;
	}
	struct going *g = NULL;
	for (size_t i = 0; i < sizeof g_going / sizeof g_going[0] && !g; i++) {
		if (!g_going[i].on) {
			g = &g_going[i];
		}
	}
	if (!g) {
		return;
	}
	struct book *b = book_of(view);
	cairo_surface_t *open = view_picture(view);
	if (!open || cairo_surface_status(open) != CAIRO_STATUS_SUCCESS) {
		if (open) {
			cairo_surface_destroy(open);
		}
		return;
	}
	int pad = NB_PAD;
	int ow = cairo_image_surface_get_width(open), oh = cairo_image_surface_get_height(open);
	char name[128];
	snprintf(name, sizeof name, "%s", view_title(view));
	char *mark = strstr(name, "  \xe2\x80\xa2");
	if (mark) {
		*mark = '\0';
	}
	g->on = true;
	g->server = server;
	g->open = open;
	g->cover = desk_notebook_shut_named(name, ow, oh);
	g->x = tree->node.x - pad;
	g->y = tree->node.y - pad;
	g->w = ow;
	g->h = oh;
	/* the case with it, if it is the last notebook out: where it lay by it, kept for the next */
	bool last = true;
	for (struct desk_toplevel *v = view_next(server, NULL); v; v = view_next(server, v)) {
		last = last && (v == view || !view_is(v, &desk_thing_notebook));
	}
	if (last && g_cs.shown && case_ready()) {
		g->case_x = (int)lround(g_cs.x);
		g->case_y = (int)lround(g_cs.y);
		struct desk_buf *buf = buf_create(CASE_W, CASE_BUF_H);
		case_paint(buf->cr);
		scene_put(&g->case_node, desk_windows(server), buf, g->case_x, g->case_y, NULL);
		g->case_node->point_accepts_input = shadow_accepts;
		g_cs.have_by = true;
		g_cs.by_x = g->case_x - tree->node.x;
		g_cs.by_y = g->case_y - tree->node.y;
		g_cs.placed = g_cs.shown = g_cs.carried = g_cs.sliding = false;
		g_pc.grab = false;
		g_pc.held = TOOL_NONE;
		if (g_cs.node) {
			wlr_scene_node_set_enabled(&g_cs.node->node, false);
		}
		case_state_write();
	}
	g->side = b->side ? b->side : 2;
	g->at = monotonic_sec();
	g->sounded = false;
	b->side = 0;
	going_paint(g, 0.0);
	server_schedule_frames(server);
}

static double smooth(double t)
{
	t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	return t * t * (3.0 - 2.0 * t);
}

static void going_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	for (size_t i = 0; i < sizeof g_going / sizeof g_going[0]; i++) {
		struct going *g = &g_going[i];
		if (!g->on || g->server != server) {
			continue;
		}
		double t = now - g->at;
		if (t < SHUT_S) {
			going_paint(g, smooth(t / SHUT_S));
		} else {
			if (!g->shut) {
				/* shut: one picture from here on, moved */
				g->shut = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g->w, g->h);
				cairo_t *cr = cairo_create(g->shut);
				desk_notebook_shutting(cr, g->open, g->cover, 1.0);
				cairo_destroy(cr);
				going_paint(g, 1.0);
			}
			if (!g->sounded) {
				g->sounded = true;
				snd_at(server, DESK_SND_BINDER_CLOSE, g->x + g->w * 0.5, 0.8);
			}
			/* off the desk that way, faster as it goes, and the case off it too */
			double u = (t - SHUT_S) / AWAY_S;
			int dw = desk_width(server), dh = desk_height(server);
			double far = g->side == 2 ? dh - g->y + 40.0 : g->side < 0 ? g->x + g->w + 40.0 : dw - g->x + 40.0;
			double d = far * u * u;
			int x = g->x + (g->side == -1 ? -(int)d : g->side == 1 ? (int)d : 0);
			int y = g->y + (g->side == 2 ? (int)d : 0);
			wlr_scene_node_set_position(&g->node->node, x, y);
			if (g->case_node) {
				double cfar = g->side == 2 ? dh - g->case_y + 40.0 : g->side < 0 ? g->case_x + CASE_W + 40.0
					: dw - g->case_x + 40.0;
				double cd = fmax(cfar, 0.0) * u * u;
				wlr_scene_node_set_position(&g->case_node->node,
					g->case_x + (g->side == -1 ? -(int)cd : g->side == 1 ? (int)cd : 0),
					g->case_y + (g->side == 2 ? (int)cd : 0));
			}
			if (u >= 1.0) {
				going_free(g);
				continue;
			}
		}
		server_schedule_frames(server);
	}
}

/* Let go at an edge and still here: asked something (a file not saved). Back to where it was. */
static void back_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	for (struct desk_toplevel *v = view_next(server, NULL); v; v = view_next(server, v)) {
		if (!view_book(v)) {
			continue;
		}
		struct book *b = book_of(v);
		struct wlr_scene_tree *tree = view_tree(v);
		if (!tree) {
			continue;
		}
		if (b->side && now - b->side_at > STAYED_S) {
			b->side = 0;
			if (b->have_from) {
				b->back = true;
				b->back_at = now;
				b->back_x0 = tree->node.x;
				b->back_y0 = tree->node.y;
			}
		}
		if (b->back) {
			double u = smooth((now - b->back_at) / BACK_S);
			wlr_scene_node_set_position(&tree->node, (int)lround(b->back_x0 + (b->from_x - b->back_x0) * u),
				(int)lround(b->back_y0 + (b->from_y - b->back_y0) * u));
			if (u >= 1.0) {
				b->back = false;
				view_send_position(v);
			}
			server_schedule_frames(server);
		}
	}
}

static void notebook_tick(struct desk_server *server)
{
	case_tick(server);
	pen_tick(server);
	going_tick(server);
	back_tick(server);
}

static void notebook_quitting(struct desk_server *server)
{
	(void)server;
	g_quitting = true;
}

/* ---- in the drawer ---------------------------------------------------- */

/* Is the notebook the standard editor, and so the drawer's already? (Not known yet: as if it is,
 * so that it is not in the drawer twice.) */
static bool notebook_is_editor(void)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_for_std(l, DESK_ICON_NOTES);
	bool is = l->n == 0 || (app && !strcmp(app->id, "desk-notepad.desktop"));
	desk_apps_release(l);
	return is;
}

/* On a machine with a pen, where another editor is the standard one; or for anyone who asks for it
 * by name (the drawer's file). */
static bool notebook_offered(struct desk_server *server)
{
	return desk_pen_there(server) && !notebook_is_editor();
}

static void notebook_drawer_item(struct desk_drawer_item *it)
{
	it->look = DESK_LOOK_NOTEBOOK;
	it->thick = 0.18;
	snprintf(it->name, sizeof it->name, "%s", TR("Notebook"));
	it->seed = 71;
}

static bool notebook_in_drawer(const struct desk_server *server)
{
	(void)server;
	return true;
}

/* The program shows the notebook it has, if it has it open. */
static void notebook_take_out(struct desk_server *server)
{
	(void)server;
	desk_run("desk-notepad");
}

/* The screen another shape (turned the other way up): the case is not left past its edge. */
static void notebook_screen_changed(struct desk_server *server)
{
	if (g_cs.node) {
		case_keep_on(server);
		case_moved();
	}
}

const struct desk_thing desk_thing_notebook = {
	.name = "notebook",
	.claims = notebook_claims,
	.ready = desk_notebook_ready,
	.desktop = "desk-notepad.desktop",
	/* its menu is under its sheet's foot, and it is not thrown away: its pages are kept */
	.no = THING_NO_CORNER | THING_NO_CRUMPLE | THING_NO_HALF,
	/* the window is all of its top sheet, under the program's rules and over the desk's paper */
	.pad = NB_PAD,
	.first_size = notebook_first_size,
	.paint = notebook_paint,
	.solid = notebook_solid,
	.edges = notebook_edges,
	.press = notebook_press,
	.painted = notebook_painted,
	.forget = notebook_forget,
	.shuts = true,
	.pen_writes = true,
	.program_fingers = true,
	/* (it was rendered with this corner up) */
	.shadow_side = 1.0,
	.shadow_amount = 0.6,
	.shadow_low = true,
	/* opened out, it has its wire on the desk, not past the screen's edge, and its case under it */
	.open_out_left = NB_WIRE,
	.open_out_below = CASE_GAP + (CASE_FOOT - CASE_TOP) + 20,
	.at_edge = notebook_at_edge,
	.goes = notebook_goes,
	.screen_changed = notebook_screen_changed,
	.hand_down = notebook_hand_down,
	.drag = notebook_drag,
	.release = notebook_release,
	.wheel = notebook_wheel,
	.tick = notebook_tick,
	.quitting = notebook_quitting,
	.drawer_word = "notebook",
	.offered = notebook_offered,
	.drawer_item = notebook_drawer_item,
	.in_drawer = notebook_in_drawer,
	.take_out = notebook_take_out,
};
