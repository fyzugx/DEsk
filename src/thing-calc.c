/* ================================================================
 * The calculator: a thing that lies on the desk, like a letter: among the windows,
 * in their layer, so that one can lie over it, and so asked about before a click is
 * taken for a window's. It comes up out of the drawer shut and opens by itself, if it
 * was open when it went; its lid is pictures rendered ahead (calcart.h), and what it
 * shows on its displays and its paper is written here over them. Shut, it is still a
 * calculator: the lid's outside has a display and the keys for a sum. The small keys
 * at the head of each face shut or open it (FOLD, OPEN) and put it away (OFF). Its
 * lower right corner, pulled, makes it bigger or smaller, and so does the wheel with
 * Ctrl. It has the keys when it was the last thing clicked on: then no window has them.
 * ================================================================ */

#include "thing.h"
#include "calc.h"
#include "calcart.h"
#include "desk.h"
#include "i18n.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

extern const struct desk_thing desk_thing_calc;

enum { CALC_IN_DRAWER = 0, CALC_READING, CALC_COMING, CALC_LYING, CALC_GOING, CALC_RESIZING };

/* the most pieces of keys held down there can be (calcart.h's keys) */
#define CALC_PIECES 64
/* how much bigger or smaller it can be made than it comes */
#define CALC_SCALE_LEAST 0.6
#define CALC_SCALE_MOST 2.2

/* What it is: in the drawer, or lying on the desk among the windows, in a tree of its
 * own that a window can lie over. Its picture, with the rest laid on that: the keys held
 * down (each its own small piece), the display's figures and the paper's writing. lid is
 * how far open it is, 0 shut, 1 open; (x, y) where the top left of its canvas is, in the
 * windows' layer. */
static struct {
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *pic, *figures, *paper;
	struct wlr_scene_buffer *down[CALC_PIECES];
	int where;
	bool placed;              /* it has lain somewhere: it goes back there */
	bool keys;                /* what is typed is keyed on it */
	bool away;                /* going back to the drawer once shut */
	double k;                 /* screen pixels to its centimetre: as it comes, times scale */
	double scale;             /* how much bigger it has been made, 1 as it comes */
	bool open_wanted;         /* it was open when it went: it opens when it comes */
	/* made bigger or smaller: by how much, as it is being, from where its box's top left is;
	 * what each of its pictures was before; and to be read again at that size, soon */
	double resize, resize_ax, resize_ay, resize_d0;
	struct { struct wlr_scene_buffer *b; int x, y, w, h; } was[CALC_PIECES + 4];
	int n_was;
	struct wl_event_source *resize_soon;
	double lid, lid_from, lid_to, lid_start;
	bool lid_moving;
	int shown;                /* which picture is up: 0 shut, n + 1 open */
	double x, y, from_x, from_y, to_x, to_y;
	double move_start, move_dur;
	bool moving;
	/* a hand on it */
	bool held, held_moved;
	int held_spot, held_which;
	int held_key;             /* the key under the hand; -1 none */
	double hx, hy, px, py;
	/* keys of the keyboard holding down keys of the calculator */
	struct { uint32_t keycode; int key; } typed[8];
	int scroll;               /* lines back up the paper from the last */
	unsigned gen;             /* what is on the display and the paper is this */
	bool lit_keys;            /* and whether it was lit as having the keys */
	bool lit_open;            /* and on which face's display */
	struct wl_event_source *keep;
} c;

#define CALC_PX_PER_CM_PT 24.0     /* points to its centimetre */
#define CALC_LID_S 0.46
#define CALC_SLIDE_S 0.32

static double clampf_d(double v, double lo, double hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- how big it was made, and whether it was open: kept ($XDG_STATE_HOME/desk/calculator) ---- */

static void calc_state_path(char *out, size_t n)
{
	const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	if (state && *state) {
		snprintf(out, n, "%s/desk/calculator", state);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/state/desk/calculator", home);
	} else {
		out[0] = '\0';
	}
}

static void calc_state_read(void)
{
	char path[600], line[64];
	calc_state_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	double v;
	int o;
	while (f && fgets(line, sizeof line, f)) {
		if (sscanf(line, "scale %lf", &v) == 1 && isfinite(v)) {
			c.scale = clampf_d(v, CALC_SCALE_LEAST, CALC_SCALE_MOST);
		} else if (sscanf(line, "open %d", &o) == 1) {
			c.open_wanted = o != 0;
		}
	}
	if (f) {
		fclose(f);
	}
}

static void calc_state_write(void)
{
	char path[600], tmp[640];
	calc_state_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = '\0';
		char *up = strrchr(path, '/');
		if (up) {
			*up = '\0';
			mkdir(path, 0700);
			*up = '/';
		}
		mkdir(path, 0700);
		*slash = '/';
	}
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		return;
	}
	fprintf(f, "# DEsk's calculator: how much bigger it was made, and whether it was open.\n"
		"scale %.3f\nopen %d\n", c.scale, c.open_wanted ? 1 : 0);
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

/* Screen pixels to its centimetre, as big as it has been made, on this screen. */
static double calc_k_for(const struct desk_server *server)
{
	return desk_px_per_pt(desk_width(server)) * CALC_PX_PER_CM_PT * c.scale;
}

static bool calc_has_keys(const struct desk_server *server)
{
	return c.keys && c.where == CALC_LYING
		&& !desk_seat(server)->keyboard_state.focused_surface && !desk_covered(server);
}

/* A hand reaches it only where it is: not its shadow, not the desk round it. */
static bool calc_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	struct desk_server *server = buffer->node.data;
	return server && desk_calc_art_at(buffer->node.x + *sx, buffer->node.y + *sy,
		c.lid >= 0.5, NULL) != DESK_CALC_OFF;
}

static void calc_lay(void)
{
	if (c.tree) {
		wlr_scene_node_set_position(&c.tree->node, (int)lround(c.x),
			(int)lround(c.y));
	}
}

/* The screen it is over, as the windows' layer has it: the one the middle of its box is on, with
 * (x, y) where it would be. */
static struct wlr_box calc_screen(struct desk_server *server, bool open, double x, double y)
{
	int box[4];
	desk_calc_art_box(open, box);
	double ox = desk_windows(server)->node.x, oy = desk_windows(server)->node.y;
	struct wlr_box sb = desk_screen_box_at(server, x + ox + (box[0] + box[2]) * 0.5,
		y + oy + (box[1] + box[3]) * 0.5);
	sb.x -= (int)lround(ox);
	sb.y -= (int)lround(oy);
	return sb;
}

/* (x, y), kept on the screen it is over -- any of them, one under the desk's too -- as it lies
 * open or shut. */
static void calc_on_desk(struct desk_server *server, bool open, double *x, double *y)
{
	int box[4];
	desk_calc_art_box(open, box);
	struct wlr_box sb = calc_screen(server, open, *x, *y);
	*x = fmax(fmin(*x, sb.x + sb.width - 2.0 - box[2]), sb.x + 2.0 - box[0]);
	*y = fmax(fmin(*y, sb.y + sb.height - 2.0 - box[3]), sb.y + 2.0 - box[1]);
}

/* A picture of it: at rest, or the lid on its way, which is drawn smaller
 * than it is seen. */
static void calc_show_picture(struct desk_server *server, cairo_surface_t *src, int x, int y,
	int dw, int dh)
{
	struct desk_buf *buf = buf_create(cairo_image_surface_get_width(src),
		cairo_image_surface_get_height(src));
	cairo_set_operator(buf->cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(buf->cr, src, 0, 0);
	cairo_paint(buf->cr);
	scene_put(&c.pic, c.tree, buf, x, y, server);
	c.pic->point_accepts_input = calc_accepts;
	wlr_scene_buffer_set_dest_size(c.pic, dw, dh);
}

/* The picture for how far open the lid is. */
static void calc_show_lid(struct desk_server *server)
{
	int n = desk_calc_art_film_n();
	int at = (int)lround(clampf_d(c.lid, 0.0, 1.0) * (n + 1));
	if (at == c.shown && c.pic) {
		return;
	}
	c.shown = at;
	int x, y, w, h;
	cairo_surface_t *src;
	if (at <= 0 || at >= n + 1) {
		src = desk_calc_art_rest(at > 0, &x, &y);
		w = cairo_image_surface_get_width(src);
		h = cairo_image_surface_get_height(src);
	} else {
		src = desk_calc_art_film(at - 1, &x, &y, &w, &h);
	}
	calc_show_picture(server, src, x, y, w, h);
}

/* The display of the face that is up and the paper's writing, if they are what is to be seen: the
 * lid at rest, open or shut. The display dims when something else has the keys. */
static void calc_refresh(struct desk_server *server)
{
	if (!c.tree) {
		return;
	}
	bool open = !c.lid_moving && c.lid >= 1.0, shut = !c.lid_moving && c.lid <= 0.0;
	if ((!open && !shut) || c.where == CALC_RESIZING) {
		if (c.figures) {
			wlr_scene_node_set_enabled(&c.figures->node, false);
		}
		if (c.paper) {
			wlr_scene_node_set_enabled(&c.paper->node, false);
		}
		return;
	}
	unsigned gen = desk_calc_generation();
	bool keys = calc_has_keys(server);
	bool was_hidden = !c.figures || !c.figures->node.enabled;
	if (gen == c.gen && keys == c.lit_keys && open == c.lit_open && !was_hidden) {
		return;
	}
	bool paper = open && (gen != c.gen || was_hidden || open != c.lit_open);
	c.gen = gen;
	c.lit_keys = keys;
	c.lit_open = open;

	int box[4];
	char shown[64];
	desk_calc_shown(shown, sizeof shown);
	struct desk_buf *buf;
	if (open) {
		/* the inside's dots: what is being worked out over the figures */
		char working[96];
		desk_calc_working(working, sizeof working);
		desk_calc_art_dots_box(box);
		buf = buf_create(box[2] - box[0], box[3] - box[1]);
		desk_calc_art_paint_dots(buf->cr, working, shown, desk_calc_has_memory(), keys ? 1.0 : 0.62);
	} else {
		desk_calc_art_figures_box(box);
		buf = buf_create(box[2] - box[0], box[3] - box[1]);
		desk_calc_art_paint_figures(buf->cr, shown, desk_calc_has_memory(), keys ? 1.0 : 0.62);
	}
	scene_put(&c.figures, c.tree, buf, box[0], box[1], NULL);
	c.figures->point_accepts_input = shadow_accepts;
	wlr_scene_node_set_enabled(&c.figures->node, true);

	if (paper) {
		int lines = desk_calc_lines(), rows = desk_calc_art_rows();
		int first = lines > rows ? lines - rows - c.scroll : 0;
		desk_calc_art_paper_box(box);
		buf = buf_create(box[2] - box[0], box[3] - box[1]);
		desk_calc_art_paint_paper(buf->cr, first < 0 ? 0 : first);
		scene_put(&c.paper, c.tree, buf, box[0], box[1], NULL);
		c.paper->point_accepts_input = shadow_accepts;
		wlr_scene_node_set_enabled(&c.paper->node, true);
	} else if (!open && c.paper) {
		wlr_scene_node_set_enabled(&c.paper->node, false);
	}
	server_schedule_frames(server);
}

static int calc_keep_cb(void *data)
{
	(void)data;
	desk_calc_keep();
	return 0;
}

/* What it has worked out is written down a moment after the last key. */
static void calc_keep_soon(struct desk_server *server)
{
	if (!c.keep) {
		c.keep = wl_event_loop_add_timer(wl_display_get_event_loop(desk_display(server)),
			calc_keep_cb, server);
	}
	if (c.keep) {
		wl_event_source_timer_update(c.keep, 1000);
	}
}

/* Its lid, to open (1) or shut (0): on its hinge, and it moves over first if
 * opened where the lid would come down off the screen. */
static void calc_lid_go(struct desk_server *server, double to)
{
	if (c.lid == to && !c.lid_moving) {
		return;
	}
	double now = monotonic_sec();
	/* what is held down comes up */
	for (int i = 0; i < desk_calc_art_keys(); i++) {
		if (c.down[i]) {
			wlr_scene_node_set_enabled(&c.down[i]->node, false);
		}
	}
	c.held_key = -1;
	memset(c.typed, 0, sizeof c.typed);
	c.lid_from = c.lid;
	c.lid_to = to;
	c.lid_start = now;
	c.lid_moving = true;
	snd_at(server, DESK_SND_GUARD_LIFT, c.x + desk_calc_art_hinge_x(), 0.6);
	if (to > 0.5) {
		double x = c.x, y = c.y;
		calc_on_desk(server, true, &x, &y);
		if (x != c.x || y != c.y) {
			c.from_x = c.x;
			c.from_y = c.y;
			c.to_x = x;
			c.to_y = y;
			c.move_start = now;
			c.move_dur = CALC_LID_S;
			c.moving = true;
		}
	}
	calc_refresh(server);
	server_schedule_frames(server);
}

static void calc_drop_all(void)
{
	if (c.tree) {
		wlr_scene_node_destroy(&c.tree->node);
	}
	c.tree = NULL;
	c.pic = c.figures = c.paper = NULL;
	memset(c.down, 0, sizeof c.down);
	desk_calc_art_drop();
}

/* Its pictures in a tree of their own, as they are read, the lid as it is. */
static void calc_build(struct desk_server *server)
{
	c.tree = wlr_scene_tree_create(desk_windows(server));
	c.shown = -1;
	c.held = false;
	c.held_key = -1;
	c.gen = 0;
	c.n_was = 0;
	calc_show_lid(server);
	for (int i = 0; i < desk_calc_art_keys() && i < CALC_PIECES; i++) {
		int x, y;
		cairo_surface_t *src = desk_calc_art_key_down(i, &x, &y);
		struct desk_buf *buf = buf_create(cairo_image_surface_get_width(src),
			cairo_image_surface_get_height(src));
		cairo_set_operator(buf->cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(buf->cr, src, 0, 0);
		cairo_paint(buf->cr);
		scene_put(&c.down[i], c.tree, buf, x, y, NULL);
		c.down[i]->point_accepts_input = shadow_accepts;
		wlr_scene_node_set_enabled(&c.down[i]->node, false);
	}
}

/* It has come out of the drawer: its pictures are read. Up from under the
 * front of the desk, shut, to where it lay, or the middle of the desk. */
static void calc_appear(struct desk_server *server)
{
	int w = desk_width(server), h = desk_height(server);
	c.lid = 0.0;
	c.lid_moving = false;
	calc_build(server);
	int box[4];
	if (!c.placed) {
		desk_calc_art_box(true, box);
		c.x = w * 0.5 - desk_calc_art_hinge_x();
		c.y = (h - (box[3] - box[1])) * 0.42 - box[1];
		c.placed = true;
	}
	calc_on_desk(server, false, &c.x, &c.y);
	desk_calc_art_box(false, box);
	c.to_x = c.from_x = c.x;
	c.to_y = c.y;
	/* up from the front of the screen it is to lie on */
	struct wlr_box sb = calc_screen(server, false, c.x, c.y);
	c.from_y = sb.y + sb.height - box[1] + 2.0;
	c.y = c.from_y;
	c.move_start = monotonic_sec();
	c.move_dur = CALC_SLIDE_S;
	c.moving = true;
	c.where = CALC_COMING;
	calc_lay();
	wlr_scene_node_raise_to_top(&c.tree->node);
	views_above_raise(server);
	snd_at(server, DESK_SND_FELT_PICK, c.x + (box[0] + box[2]) * 0.5, 0.9);
}

static void calc_take_out(struct desk_server *server)
{
	if (c.where != CALC_IN_DRAWER) {
		return;
	}
	static bool read_once;
	if (!read_once) {
		read_once = true;
		c.scale = 1.0;
		c.open_wanted = true;
		calc_state_read();
	}
	c.k = calc_k_for(server);
	c.where = CALC_READING;
	c.away = false;
	desk_calc_art_begin(c.k);
	drawer_thing_present(server, &desk_thing_calc, false);
	server_schedule_frames(server);
}

/* Shut, if it is open, and back in the drawer. */
static void calc_put_away(struct desk_server *server)
{
	if (c.where != CALC_LYING) {
		return;
	}
	wlr_log(WLR_INFO, "putting the calculator back in the drawer");
	desk_calc_keep();
	bool had_keys = calc_has_keys(server);
	c.keys = false;
	c.held = false;
	c.away = true;
	c.where = CALC_GOING;
	if (c.lid > 0.0) {
		calc_lid_go(server, 0.0);
	}
	if (had_keys) {
		focus_next(server);
	}
	server_schedule_frames(server);
}

/* Put it back: it goes down off the front of the desk, into the drawer. */
static void calc_slide_home(struct desk_server *server)
{
	int box[4];
	desk_calc_art_box(false, box);
	c.from_x = c.to_x = c.x;
	c.from_y = c.y;
	/* (it comes out where it lay when it is next wanted); down off the front of the screen it
	 * is on */
	struct wlr_box sb = calc_screen(server, c.lid >= 0.5, c.x, c.y);
	c.to_y = sb.y + sb.height - box[1] + 2.0;
	c.move_start = monotonic_sec();
	c.move_dur = CALC_SLIDE_S;
	c.moving = true;
}

/* ---- bigger or smaller ----
 *
 * As a hand pulls its corner (or the wheel turns with Ctrl), its pictures as they are, made that
 * much bigger, its box's top left where it was; let go, they are read again at that size, and are
 * as sharp as ever. */

static void calc_resize_begin(void)
{
	if (c.n_was > 0 || !c.tree) {
		return;
	}
	struct wlr_scene_buffer *all[CALC_PIECES + 3] = { c.pic, c.figures, c.paper };
	int n = 3;
	for (int i = 0; i < CALC_PIECES; i++) {
		all[n++] = c.down[i];
	}
	for (int i = 0; i < n; i++) {
		struct wlr_scene_buffer *b = all[i];
		if (!b || !b->buffer) {
			continue;
		}
		c.was[c.n_was].b = b;
		c.was[c.n_was].x = b->node.x;
		c.was[c.n_was].y = b->node.y;
		c.was[c.n_was].w = b->dst_width > 0 ? b->dst_width : b->buffer->width;
		c.was[c.n_was].h = b->dst_height > 0 ? b->dst_height : b->buffer->height;
		c.n_was++;
	}
	int box[4];
	desk_calc_art_box(c.lid >= 0.5, box);
	c.resize = 1.0;
	c.resize_ax = box[0];
	c.resize_ay = box[1];
}

/* r times as big as it is, its box's top left where it is. */
static void calc_resize_show(struct desk_server *server, double r)
{
	r = clampf_d(r, CALC_SCALE_LEAST / c.scale, CALC_SCALE_MOST / c.scale);
	c.resize = r;
	for (int i = 0; i < c.n_was; i++) {
		struct wlr_scene_buffer *b = c.was[i].b;
		wlr_scene_node_set_position(&b->node, (int)lround(c.was[i].x * r), (int)lround(c.was[i].y * r));
		wlr_scene_buffer_set_dest_size(b, (int)lround(c.was[i].w * r), (int)lround(c.was[i].h * r));
	}
	if (c.tree) {
		wlr_scene_node_set_position(&c.tree->node, (int)lround(c.x + c.resize_ax * (1.0 - r)),
			(int)lround(c.y + c.resize_ay * (1.0 - r)));
	}
	server_schedule_frames(server);
}

/* Let go at that: read again at its size, and lying where it is now. */
static void calc_resize_done(struct desk_server *server)
{
	if (c.n_was == 0) {
		return;
	}
	double r = c.resize;
	c.n_was = 0;
	if (fabs(r - 1.0) < 0.01 || c.where != CALC_LYING) {
		calc_resize_show(server, 1.0);
		c.n_was = 0;
		return;
	}
	c.x += c.resize_ax * (1.0 - r);
	c.y += c.resize_ay * (1.0 - r);
	c.scale = clampf_d(c.scale * r, CALC_SCALE_LEAST, CALC_SCALE_MOST);
	c.k = calc_k_for(server);
	calc_state_write();
	wlr_log(WLR_INFO, "the calculator made %.2f times as big as it comes", c.scale);
	c.where = CALC_RESIZING;
	calc_refresh(server);
	desk_calc_art_drop();
	desk_calc_art_begin(c.k);
	server_schedule_frames(server);
}

static int calc_resize_soon_cb(void *data)
{
	calc_resize_done(data);
	return 0;
}

static void calc_tick(struct desk_server *server)
{
	if (c.where == CALC_IN_DRAWER) {
		return;
	}
	if (c.where == CALC_READING) {
		int ready = desk_calc_art_ready();
		if (ready < 0) {
			wlr_log(WLR_ERROR, "the calculator's pictures could not be read");
			desk_calc_art_drop();
			c.where = CALC_IN_DRAWER;
			drawer_thing_present(server, &desk_thing_calc, true);
			return;
		}
		if (ready == 0) {
			server_schedule_frames(server);
			return;
		}
		calc_appear(server);
	}
	if (c.where == CALC_RESIZING) {
		int ready = desk_calc_art_ready();
		if (ready == 0) {
			server_schedule_frames(server);
			return;
		}
		/* read again at its new size: its pictures again, where it lies */
		wlr_scene_node_destroy(&c.tree->node);
		c.tree = NULL;
		c.pic = c.figures = c.paper = NULL;
		memset(c.down, 0, sizeof c.down);
		if (ready < 0) {
			wlr_log(WLR_ERROR, "the calculator's pictures could not be read again");
			desk_calc_art_drop();
			c.where = CALC_IN_DRAWER;
			drawer_thing_present(server, &desk_thing_calc, true);
			return;
		}
		c.where = CALC_LYING;
		calc_build(server);
		calc_lay();
		wlr_log(WLR_INFO, "the calculator lies at %d,%d, %.3f pixels to the centimetre",
			(int)lround(c.x), (int)lround(c.y), c.k);
		wlr_scene_node_raise_to_top(&c.tree->node);
		views_above_raise(server);
		calc_refresh(server);
		server_schedule_frames(server);
	}
	double now = monotonic_sec();
	bool busy = false;
	if (c.moving) {
		double u = clampf_d((now - c.move_start) / c.move_dur, 0.0, 1.0);
		double e = u * u * (3.0 - 2.0 * u);
		c.x = c.from_x + (c.to_x - c.from_x) * e;
		c.y = c.from_y + (c.to_y - c.from_y) * e;
		calc_lay();
		busy = true;
		if (u >= 1.0) {
			c.moving = false;
			if (c.where == CALC_COMING) {
				/* down on the desk, and open */
				c.where = CALC_LYING;
				c.keys = true;
				desk_take_keys(server);
				snd_at(server, DESK_SND_FELT_DROP, c.x + desk_calc_art_hinge_x(), 0.8);
				if (c.open_wanted) {
					calc_lid_go(server, 1.0);
				}
				wlr_log(WLR_INFO, "the calculator lies at %d,%d, %.3f pixels to the centimetre",
					(int)lround(c.x), (int)lround(c.y), c.k);
			} else if (c.where == CALC_GOING) {
				/* in the drawer: only where it lay is remembered */
				c.where = CALC_IN_DRAWER;
				c.x = c.from_x;
				c.y = c.from_y;
				calc_drop_all();
				drawer_thing_present(server, &desk_thing_calc, true);
				snd_at(server, DESK_SND_FELT_DROP, c.x + 80.0, 0.7);
				server_schedule_frames(server);
				return;
			}
		}
	}
	if (c.lid_moving) {
		double u = clampf_d((now - c.lid_start) / CALC_LID_S, 0.0, 1.0);
		/* easing up over the top, and falling the rest of the way */
		double e = u * u * (3.0 - 2.0 * u);
		c.lid = c.lid_from + (c.lid_to - c.lid_from) * e;
		busy = true;
		if (u >= 1.0) {
			c.lid = c.lid_to;
			c.lid_moving = false;
			snd_at(server, DESK_SND_GUARD_STOP, c.x + desk_calc_art_hinge_x(), 0.7);
		}
		calc_show_lid(server);
		if (!c.lid_moving && c.where == CALC_GOING && c.lid <= 0.0) {
			calc_slide_home(server);
		}
	}
	if (c.where == CALC_GOING && !c.moving && !c.lid_moving) {
		calc_slide_home(server);
		busy = true;
	}
	calc_refresh(server);
	if (busy) {
		server_schedule_frames(server);
	}
}

/* Where the hand is on it, if it is uppermost there. */
static enum desk_calc_spot calc_under(struct desk_server *server, double *lx, double *ly, int *which)
{
	if (!c.tree || !c.pic) {
		return DESK_CALC_OFF;
	}
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&desk_scene(server)->tree.node,
		desk_cursor(server)->x, desk_cursor(server)->y, &sx, &sy);
	if (node != &c.pic->node) {
		return DESK_CALC_OFF;
	}
	int tx, ty;
	wlr_scene_node_coords(&c.tree->node, &tx, &ty);
	*lx = desk_cursor(server)->x - tx;
	*ly = desk_cursor(server)->y - ty;
	return desk_calc_art_at(*lx, *ly, c.lid >= 0.5, which);
}

/* A key of it going down, or coming up. */
static void calc_key_shown(struct desk_server *server, int key, bool down)
{
	if (key >= 0 && key < desk_calc_art_keys() && c.down[key]) {
		wlr_scene_node_set_enabled(&c.down[key]->node, down);
		server_schedule_frames(server);
	}
}

/* Pressed: a key acts as it goes down. */
static void calc_key_acts(struct desk_server *server, int key)
{
	desk_calc_press(desk_calc_art_key(key));
	c.scroll = 0;
	snd_at(server, DESK_SND_KEY_PRESS, c.x + desk_calc_art_hinge_x(), 0.5);
	calc_keep_soon(server);
	calc_refresh(server);
}

/* A hand put down on the calculator: true if that is what it was put on. */
static bool calc_here(struct desk_server *server)
{
	double lx = 0.0, ly = 0.0;
	int which = -1;
	return calc_under(server, &lx, &ly, &which) != DESK_CALC_OFF;
}

static bool calc_press(struct desk_server *server, uint32_t button)
{
	double lx = 0.0, ly = 0.0;
	int which = -1;
	enum desk_calc_spot spot = calc_under(server, &lx, &ly, &which);
	if (spot == DESK_CALC_OFF) {
		return false;
	}
	/* it comes up over what it was under, and has the keys */
	wlr_scene_node_raise_to_top(&c.tree->node);
	views_above_raise(server);
	desk_take_keys(server);
	c.keys = true;
	wlr_seat_pointer_clear_focus(desk_seat(server));
	calc_refresh(server);
	if (c.where != CALC_LYING || c.lid_moving) {
		return true;
	}
	if (button == BTN_RIGHT) {
		/* the paper torn off */
		if (spot == DESK_CALC_PAPER && desk_calc_lines() > 0) {
			desk_calc_tear();
			c.scroll = 0;
			snd_at(server, DESK_SND_PAPER_CRUMPLE, desk_cursor(server)->x, 0.6);
			calc_keep_soon(server);
			calc_refresh(server);
		}
		return true;
	}
	if (button != BTN_LEFT) {
		return true;
	}
	c.held = true;
	c.held_moved = false;
	c.held_spot = spot;
	c.held_which = which;
	c.held_key = -1;
	c.px = desk_cursor(server)->x;
	c.py = desk_cursor(server)->y;
	c.hx = lx;
	c.hy = ly;
	if (spot == DESK_CALC_KEY) {
		c.held_key = which;
		calc_key_shown(server, which, true);
		/* (a small key at the head acts as it comes up: it shuts it, or puts it away) */
		if (desk_calc_art_key(which) < DESK_CK_COUNT) {
			calc_key_acts(server, which);
		} else {
			snd_at(server, DESK_SND_KEY_PRESS, c.x + desk_calc_art_hinge_x(), 0.4);
		}
	} else if (spot == DESK_CALC_CORNER) {
		calc_resize_begin();
		int tx = c.tree->node.x, ty = c.tree->node.y;
		c.resize_d0 = fmax(hypot(desk_cursor(server)->x - (tx + c.resize_ax),
			desk_cursor(server)->y - (ty + c.resize_ay)), 1.0);
	}
	return true;
}

/* The calculator in the hand follows it, once it has moved more than a
 * twitch; a hand on a key stays on that key. */
static enum desk_thing_hold calc_drag(struct desk_server *server)
{
	if (!c.held) {
		return THING_NOT_HELD;
	}
	if (c.where != CALC_LYING || desk_covered(server)) {
		calc_key_shown(server, c.held_key, false);
		c.held_key = -1;
		c.held = false;
		return THING_NOT_HELD;
	}
	if (c.held_spot == DESK_CALC_KEY) {
		return THING_HELD;
	}
	double cx = desk_cursor(server)->x, cy = desk_cursor(server)->y;
	if (c.held_spot == DESK_CALC_CORNER) {
		/* as far from its top left as the hand is, as much bigger */
		double ax = c.x + c.resize_ax, ay = c.y + c.resize_ay;
		calc_resize_show(server, hypot(cx - ax, cy - ay) / c.resize_d0);
		c.held_moved = true;
		return THING_HELD;
	}
	if (!c.held_moved) {
		double twitch = 5.0 * desk_px_per_pt(desk_width(server));
		if (hypot(cx - c.px, cy - c.py) < twitch) {
			return THING_HELD;
		}
		c.held_moved = true;
		snd_at(server, DESK_SND_FELT_PICK, cx, 0.35);
	}
	double ox = desk_windows(server)->node.x, oy = desk_windows(server)->node.y;
	double x = cx - c.hx - ox, y = cy - c.hy - oy;
	calc_on_desk(server, c.lid >= 0.5, &x, &y);
	c.x = x;
	c.y = y;
	calc_lay();
	wlr_log(WLR_DEBUG, "the calculator lies at %d,%d, %.3f pixels to the centimetre",
		(int)lround(c.x), (int)lround(c.y), c.k);
	server_schedule_frames(server);
	return THING_CARRIED;
}

/* Let go of it. A click on it shut opens it; on the lid's rim or the hinge,
 * shuts it; on the paper, brings that sum's answer back. Carried to the
 * front of the desk, it goes in the drawer. */
static bool calc_release(struct desk_server *server)
{
	if (!c.held) {
		return false;
	}
	c.held = false;
	if (c.held_spot == DESK_CALC_CORNER) {
		calc_resize_done(server);
		return true;
	}
	if (c.held_key >= 0) {
		int key = c.held_key;
		calc_key_shown(server, key, false);
		snd_at(server, DESK_SND_KEY_RELEASE, c.x + desk_calc_art_hinge_x(), 0.35);
		c.held_key = -1;
		enum desk_calc_key k = desk_calc_art_key(key);
		double lx, ly;
		int now = -1;
		/* a small key let go over itself: what it is for */
		if (k > DESK_CK_COUNT && calc_under(server, &lx, &ly, &now) == DESK_CALC_KEY && now == key) {
			if (k == DESK_CK_FOLD) {
				c.open_wanted = c.lid < 0.5;
				calc_state_write();
				calc_lid_go(server, c.open_wanted ? 1.0 : 0.0);
			} else {
				calc_put_away(server);
			}
		}
		return true;
	}
	if (c.held_moved) {
		snd_at(server, DESK_SND_FELT_DROP, desk_cursor(server)->x, 0.5);
		if (desk_at_front(server, desk_cursor(server)->x, desk_cursor(server)->y)) {
			calc_put_away(server);
		}
		return true;
	}
	switch (c.held_spot) {
	case DESK_CALC_SHUT_IT:
		c.open_wanted = false;
		calc_state_write();
		calc_lid_go(server, 0.0);
		break;
	case DESK_CALC_PAPER: {
		int lines = desk_calc_lines(), rows = desk_calc_art_rows();
		int first = lines > rows ? lines - rows - c.scroll : 0;
		int line = (first < 0 ? 0 : first) + c.held_which;
		if (line < lines) {
			desk_calc_recall(line);
			snd_at(server, DESK_SND_CORNER_TAP, desk_cursor(server)->x, 0.5);
			calc_refresh(server);
		}
		break;
	}
	case DESK_CALC_CASE:
		if (c.lid < 0.5) {
			c.open_wanted = true;
			calc_state_write();
			calc_lid_go(server, 1.0);
		}
		break;
	default:
		break;
	}
	return true;
}

/* The wheel over the paper goes up and down what has been worked out; over the
 * rest of it, nothing: it is not for what lies under it. */
static bool calc_wheel(struct desk_server *server, struct wlr_pointer_axis_event *event)
{
	double lx, ly;
	int which;
	enum desk_calc_spot spot = calc_under(server, &lx, &ly, &which);
	if (spot == DESK_CALC_OFF) {
		return false;
	}
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(desk_seat(server));
	if (kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_CTRL) && c.where == CALC_LYING && !c.lid_moving
		&& !c.held && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL && event->delta != 0.0) {
		/* with Ctrl, bigger or smaller a step a notch, read again at that size a moment after */
		calc_resize_begin();
		calc_resize_show(server, c.resize * (event->delta < 0.0 ? 1.1 : 1.0 / 1.1));
		if (!c.resize_soon) {
			c.resize_soon = wl_event_loop_add_timer(wl_display_get_event_loop(desk_display(server)),
				calc_resize_soon_cb, server);
		}
		if (c.resize_soon) {
			wl_event_source_timer_update(c.resize_soon, 300);
		}
		return true;
	}
	if (spot == DESK_CALC_PAPER && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL
		&& event->delta != 0.0) {
		int most = desk_calc_lines() - desk_calc_art_rows();
		int to = c.scroll + (event->delta < 0.0 ? 1 : -1);
		to = to > most ? most : to;
		to = to < 0 ? 0 : to;
		if (to != c.scroll) {
			c.scroll = to;
			c.gen = 0;   /* (written again) */
			calc_refresh(server);
		}
	}
	return true;
}

/* A number from whoever has copied one. */
static void calc_pasted(struct desk_server *server, const char *text)
{
	char number[80];
	snprintf(number, sizeof number, "%s", text);
	if (desk_calc_paste(number)) {
		calc_refresh(server);
	}
}

/* The keyboard's key for a key of it, or -1. */
static int calc_key_for(xkb_keysym_t sym)
{
	if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
		return DESK_CK_0 + (int)(sym - XKB_KEY_0);
	}
	if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9) {
		return DESK_CK_0 + (int)(sym - XKB_KEY_KP_0);
	}
	switch (sym) {
	case XKB_KEY_period: case XKB_KEY_comma: case XKB_KEY_KP_Decimal: case XKB_KEY_KP_Separator:
		return DESK_CK_DOT;
	case XKB_KEY_plus: case XKB_KEY_KP_Add:
		return DESK_CK_ADD;
	case XKB_KEY_minus: case XKB_KEY_KP_Subtract:
		return DESK_CK_SUB;
	case XKB_KEY_asterisk: case XKB_KEY_KP_Multiply: case XKB_KEY_x: case XKB_KEY_X: case XKB_KEY_multiply:
		return DESK_CK_MUL;
	case XKB_KEY_slash: case XKB_KEY_KP_Divide: case XKB_KEY_division:
		return DESK_CK_DIV;
	case XKB_KEY_equal: case XKB_KEY_Return: case XKB_KEY_KP_Enter: case XKB_KEY_KP_Equal:
		return DESK_CK_EQ;
	case XKB_KEY_percent:
		return DESK_CK_PCT;
	case XKB_KEY_Delete: case XKB_KEY_KP_Delete: case XKB_KEY_Clear:
		return DESK_CK_AC;
	case XKB_KEY_n: return DESK_CK_NEG;
	case XKB_KEY_r: return DESK_CK_SQRT;
	case XKB_KEY_q: return DESK_CK_SQ;
	case XKB_KEY_asciicircum: return DESK_CK_POW;
	case XKB_KEY_i: return DESK_CK_INV;
	case XKB_KEY_p: return DESK_CK_PI;
	case XKB_KEY_e: return DESK_CK_EUL;
	case XKB_KEY_l: return DESK_CK_LN;
	case XKB_KEY_g: return DESK_CK_LOG;
	case XKB_KEY_s: return DESK_CK_SIN;
	case XKB_KEY_c: return DESK_CK_COS;
	case XKB_KEY_t: return DESK_CK_TAN;
	case XKB_KEY_exclam: return DESK_CK_FACT;
	default:
		return -1;
	}
}

/* A key of the keyboard, if the calculator has the keys: true if it took it.
 * Escape shuts it, and again puts it back in the drawer. */
static bool calc_key(struct desk_server *server, uint32_t keycode, xkb_keysym_t sym, uint32_t mods,
	bool released)
{
	const int typed_n = (int)(sizeof c.typed / sizeof c.typed[0]);
	if (released) {
		for (int i = 0; i < typed_n; i++) {
			if (c.typed[i].keycode == keycode + 1) {
				calc_key_shown(server, c.typed[i].key, false);
				c.typed[i].keycode = 0;
				snd_at(server, DESK_SND_KEY_RELEASE, c.x + desk_calc_art_hinge_x(), 0.3);
				return true;
			}
		}
		return false;
	}
	if (!calc_has_keys(server) || (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT))) {
		return false;
	}
	if (mods & WLR_MODIFIER_CTRL) {
		if (sym == XKB_KEY_c || sym == XKB_KEY_C) {
			char text[64];
			desk_calc_shown_text(text, sizeof text);
			if (text[0]) {
				desk_copy_text(server, text);
			}
			return true;
		}
		if (sym == XKB_KEY_v || sym == XKB_KEY_V) {
			desk_paste_text(server, calc_pasted);
			return true;
		}
		return false;
	}
	if (sym == XKB_KEY_Escape) {
		if (c.lid >= 0.5) {
			c.open_wanted = false;
			calc_state_write();
			calc_lid_go(server, 0.0);
		} else {
			calc_put_away(server);
		}
		return true;
	}
	if (c.lid_moving) {
		return calc_key_for(sym) >= 0 || sym == XKB_KEY_BackSpace;
	}
	if (sym == XKB_KEY_BackSpace) {
		if (desk_calc_rub_out()) {
			snd_at(server, DESK_SND_KEY_PRESS, c.x + desk_calc_art_hinge_x(), 0.3);
			calc_refresh(server);
		}
		return true;
	}
	int ck = calc_key_for(sym);
	if (ck < 0) {
		return false;
	}
	bool open = c.lid >= 0.5;
	int key = desk_calc_art_key_of(ck, open);
	if (key < 0) {
		/* shut, and no such key on the outside (sin, MR): it opens, and that is worked out */
		desk_calc_press(ck);
		c.scroll = 0;
		calc_keep_soon(server);
		c.open_wanted = true;
		calc_state_write();
		calc_lid_go(server, 1.0);
		return true;
	}
	for (int i = 0; i < typed_n; i++) {
		if (!c.typed[i].keycode) {
			c.typed[i].keycode = keycode + 1;
			c.typed[i].key = key;
			break;
		}
	}
	calc_key_shown(server, key, true);
	calc_key_acts(server, key);
	return true;
}

/* The screen has changed: it stays on it, at its size in points. */
static void calc_place(struct desk_server *server)
{
	if (c.where == CALC_IN_DRAWER || c.where == CALC_READING) {
		return;
	}
	if (c.where == CALC_RESIZING) {
		return;
	}
	if (fabs(calc_k_for(server) - c.k) > 1e-6) {
		/* another size altogether: read again, and it comes out again */
		calc_drop_all();
		c.where = CALC_IN_DRAWER;
		calc_take_out(server);
		return;
	}
	if (c.where == CALC_LYING && !c.moving) {
		calc_on_desk(server, c.lid >= 0.5, &c.x, &c.y);
		calc_lay();
	}
}

static bool calc_in_drawer(const struct desk_server *server)
{
	(void)server;
	return c.where == CALC_IN_DRAWER;
}

/* (the display dims) */
static void calc_keys_gone(struct desk_server *server)
{
	if (c.where != CALC_IN_DRAWER) {
		server_schedule_frames(server);
	}
}

static void calc_quitting(struct desk_server *server)
{
	(void)server;
	desk_calc_keep();
}

static void calc_drawer_item(struct desk_drawer_item *it)
{
	it->look = DESK_LOOK_THING;
	it->lying = desk_calc_art_top_lying;
	it->thick = 0.26;
	snprintf(it->name, sizeof it->name, "%s", TR("Calculator"));
	it->seed = 41;
}

const struct desk_thing desk_thing_calc = {
	.name = "calculator",
	.hand_down = calc_press,
	.hand_here = calc_here,
	.drag = calc_drag,
	.release = calc_release,
	.wheel = calc_wheel,
	.key = calc_key,
	.keys_gone = calc_keys_gone,
	.tick = calc_tick,
	.screen_changed = calc_place,
	.quitting = calc_quitting,
	.drawer_word = "calculator",
	.drawer_item = calc_drawer_item,
	.in_drawer = calc_in_drawer,
	.take_out = calc_take_out,
	.has_keys = calc_has_keys,
	.put_away = calc_put_away,
};
