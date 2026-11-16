/* ================================================================
 * Things: what is on the desk and is not a sheet of paper (docs/things.md).
 *
 * A window is paper unless a thing claims it, and paper has its rules: it folds, it
 * opens out, it has a corner to turn, it is crumpled up when it goes. A thing has the
 * rules that suit what it is: a phone does not fold, a notebook shuts, a calculator is
 * no window at all.
 *
 * A thing is one file: a struct desk_thing filled in, and its name in src/things.c.
 * What is left out of it is as paper has it, or nothing. The second half of this
 * header is all a thing needs to ask of the desk (src/wm.c).
 * ================================================================ */

#ifndef DESK_THING_H
#define DESK_THING_H

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/box.h>
#include <xkbcommon/xkbcommon.h>

#include "sound.h"

struct desk_server;
struct desk_toplevel;
struct desk_drawer_item;

/* What paper does that this does not. */
enum {
	THING_NO_FOLD = 1u << 0,      /* fold into a packet */
	THING_NO_OPEN_OUT = 1u << 1,  /* open out to the whole desk */
	THING_NO_HALF = 1u << 2,      /* lie over half of the screen */
	THING_NO_CORNER = 1u << 3,    /* have a corner to turn */
	THING_NO_CRUMPLE = 1u << 4,   /* get crumpled up as it goes */
	THING_NO_CREASES = 1u << 5,   /* crease while it is pulled to another size */
	THING_NO_SHADOW = 1u << 6,    /* have a shadow from us: it has one in its picture */
	THING_NO_KEYS = 1u << 7,      /* take the keyboard as it comes onto the desk */
};

/* A hand on it, moving (drag). */
enum desk_thing_hold {
	THING_NOT_HELD,
	THING_HELD,
	THING_CARRIED,                /* and the hand shows that it is carrying something */
};

struct desk_thing {
	const char *name;

	/* ---- round a window: the program's window is part of it ---- */

	/* Is this window one? Asked as it comes onto the desk, and asked before that, for the
	 * size to offer it. */
	bool (*claims)(const struct desk_toplevel *view);
	/* Its title is what says so: asked again whenever that changes. */
	bool by_title;
	/* Can it be drawn? If not, the window is paper: nothing breaks for a lost picture. */
	bool (*ready)(void);
	/* The desktop file of the program, if the window can be known before it is there: a
	 * blank one is put down for it as the program is taken off the shelf. */
	const char *desktop;
	unsigned no;                  /* THING_NO_* */
	/* It is the frame whatever the program would draw: the program is told it is tiled,
	 * so it leaves out its own shadow and round corners. */
	bool frame_always;
	/* The window lies this far in from the thing's edge, and the thing's picture goes
	 * pad past that edge on every side, for its shadow and what stands out from it. */
	int inset_l, inset_t, inset_r, inset_b;
	int pad;
	/* The size to offer a new window on a screen of this size (0 by 0 leaves it to the
	 * program). */
	void (*first_size)(const struct wlr_box *screen, int *w, int *h);
	/* Where it goes: once, when it has come and has a size. */
	void (*put_down)(struct desk_toplevel *view);
	/* Its picture, its edge at x, y, w by h, with pad to spare round that. Resting, it is
	 * the thing as it lies, whatever a hand is doing. view is NULL for the blank that is
	 * put down before the program is there (desktop). */
	void (*paint)(cairo_t *cr, struct desk_toplevel *view, double x, double y, double w, double h,
		const char *title, bool focused, bool resting);
	/* Whatever else its picture shows: it is painted again when this changes. */
	int (*look)(struct desk_toplevel *view);
	/* Does a hand find it here? From its edge's top left; the window in it has the hand
	 * first, and past this the hand reaches what is under it. Without this, all of it
	 * does but the window. */
	bool (*solid)(struct desk_toplevel *view, double lx, double ly);
	/* The edges a hand here would pull, of those that it would pull on paper. */
	unsigned (*edges)(struct desk_toplevel *view, double lx, double ly, unsigned edges);
	/* A hand put down on it (surface NULL) or on the window in it: all of what happens
	 * then. Without this, its edges pull as paper's do, and the rest picks it up. */
	void (*press)(struct desk_toplevel *view, struct wlr_surface *surface,
		struct wlr_pointer_button_event *event);
	/* Its picture has been painted again, for a new size; or is not being shown, for it
	 * has the whole screen. */
	void (*painted)(struct desk_toplevel *view);
	void (*hidden)(struct desk_toplevel *view);
	/* It has gone from the desk, or is not this any more: what it put in view_tree
	 * goes now, and a window that comes again starts afresh. */
	void (*forget)(struct desk_toplevel *view);
	/* It folds as a notebook shuts: from a picture of it, pad and all (src/draw.c). */
	bool shuts;
	/* A pen on it writes: the keyboard does not come up for a pen there. */
	bool pen_writes;
	/* Fingers on the window in it are the program's, as they are on paper, though it has a press
	 * of its own for the rest (without this, the hand's: clicks). */
	bool program_fingers;
	/* The corner it was rendered with lifted, and that it lies low: for its shadow. */
	double shadow_side, shadow_amount;
	bool shadow_low;
	/* Opened out, it keeps this much of the desk on its left, and under it. */
	int open_out_left, open_out_below;
	/* What is under its corner when that is turned up, instead of the desk. */
	void (*under_corner)(cairo_t *cr, double x, double y, double size);
	/* Let go of with the hand at an edge of the desk with no screen past it (side -1 the left, 1
	 * the right, 2 the front): what it does then, instead of lying over half of the screen. */
	void (*at_edge)(struct desk_toplevel *view, int side);
	/* Its program has let it go, and what it last drew can still be read (view_picture): how it
	 * goes, instead of being crumpled up. */
	void (*goes)(struct desk_toplevel *view);

	/* ---- over everything, the door too, and asked first ---- */

	bool over_all;
	/* Fingers, each for itself: true if this one is on it, and then the rest of it comes here,
	 * and it is no part of what fingers do together. taken_away: nothing is to come of it. */
	bool (*finger_down)(struct desk_server *server, int32_t id, double x, double y);
	void (*finger_move)(struct desk_server *server, int32_t id, double x, double y);
	void (*finger_up)(struct desk_server *server, int32_t id, bool taken_away);
	/* Something to type in has come up, or gone. */
	void (*text_wanted)(struct desk_server *server, bool wanted);

	/* ---- at any time, wherever the hand is ---- */

	/* A hand put down, before anything else is asked: true if on this. */
	bool (*hand_down)(struct desk_server *server, uint32_t button);
	/* Is the hand on it? Only asked: a finger that has set off from here is carrying it,
	 * and one over nothing is going somewhere. */
	bool (*hand_here)(struct desk_server *server);
	/* The hand has moved: does this have it? */
	enum desk_thing_hold (*drag)(struct desk_server *server);
	/* The left button let go: true if this had the hand. */
	bool (*release)(struct desk_server *server);
	bool (*wheel)(struct desk_server *server, struct wlr_pointer_axis_event *event);
	/* A key of the keyboard: true if this took it. */
	bool (*key)(struct desk_server *server, uint32_t keycode, xkb_keysym_t sym, uint32_t mods,
		bool released);
	/* Have the keys gone to a window? */
	void (*keys_gone)(struct desk_server *server);
	void (*tick)(struct desk_server *server);          /* every frame */
	void (*say)(struct desk_server *server);           /* every second, and when a player says */
	void (*screen_changed)(struct desk_server *server);
	void (*quitting)(struct desk_server *server);

	/* ---- kept in the drawer, and put down on the desk by itself ---- */

	const char *drawer_word;      /* what the drawer's file calls it */
	/* Is it to be kept there at all, on this machine? Without this, it is. */
	bool (*offered)(struct desk_server *server);
	/* How it lies in the drawer (look, lying, thick, name, seed). */
	void (*drawer_item)(struct desk_drawer_item *it);
	bool (*in_drawer)(const struct desk_server *server);
	void (*take_out)(struct desk_server *server);
	/* Is what is typed for this? Then Super+Q puts it away, not a window. */
	bool (*has_keys)(const struct desk_server *server);
	void (*put_away)(struct desk_server *server);
};

/* Every thing there is (src/things.c), to the NULL. */
extern const struct desk_thing *const desk_things[];

/* ---- what a thing can ask of the desk ---------------------------- */

struct wl_display *desk_display(const struct desk_server *server);
struct wlr_seat *desk_seat(const struct desk_server *server);
struct wlr_cursor *desk_cursor(const struct desk_server *server);  /* where the hand is */
struct wlr_scene *desk_scene(const struct desk_server *server);
struct wlr_scene_tree *desk_windows(const struct desk_server *server);  /* the windows' layer */
int desk_width(const struct desk_server *server);
int desk_height(const struct desk_server *server);
/* Is the door shut, or the shelf, the console or the post in the way of the desk? */
bool desk_covered(const struct desk_server *server);
struct wlr_box work_area(struct desk_server *server);
struct wlr_box screen_area(struct desk_server *server, struct wlr_output *out);
struct wlr_output *first_output(struct desk_server *server);
struct wlr_output_layout *desk_layout(const struct desk_server *server);
/* The screen a point is on (or the nearest to it): where it is among them all. */
struct wlr_box desk_screen_box_at(struct desk_server *server, double x, double y);
/* Is a hand there at the front edge of its screen, with no screen past it -- where a thing
 * carried goes back into the drawer? (On a machine with a screen under the desk's, the
 * desk's front edge is not one: the hand goes on to that screen.) */
bool desk_at_front(struct desk_server *server, double x, double y);

/* The desk's own time, in seconds: it runs as slowly as the desk does. */
double monotonic_sec(void);
/* Something has moved: draw the next frame. */
void server_schedule_frames(struct desk_server *server);
/* A sound, from where along the desk it comes. */
int snd_at(const struct desk_server *server, enum desk_sound s, double x, double gain);

/* A picture to draw on, and to put in the scene: scene_put takes it over. */
struct desk_buf {
	struct wlr_buffer base;
	cairo_surface_t *surface;
	cairo_t *cr;
};
struct desk_buf *buf_create(int w, int h);
void scene_put(struct wlr_scene_buffer **slot, struct wlr_scene_tree *parent,
	struct desk_buf *buffer, int x, int y, void *data);
void scene_source(struct wlr_scene_buffer *b, const struct wlr_fbox *src);
/* For a node's point_accepts_input: a hand goes through it. */
bool shadow_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy);

/* Some text for whoever pastes it; and text from whoever has copied some, when they
 * send it. */
void desk_copy_text(struct desk_server *server, const char *text);
void desk_paste_text(struct desk_server *server, void (*got)(struct desk_server *server, const char *text));

/* The keys are taken from whichever window has them, for this; or given to the next. */
void desk_take_keys(struct desk_server *server);
void focus_next(struct desk_server *server);
/* A tree of its own has been brought up over the windows: what stays in front, stays. */
void views_above_raise(struct desk_server *server);
/* A thing in the drawer has gone from it, or come back. */
void drawer_thing_present(struct desk_server *server, const struct desk_thing *thing, bool present);

/* A keyboard that is nothing but its keys, for typing with what is not one. */
struct wlr_keyboard *desk_onscreen_keyboard(struct desk_server *server);
/* Over everything but the dark of a screen gone off. */
struct wlr_scene_tree *desk_over_all(const struct desk_server *server);
/* Has a finger or a pen been the last thing used? A pen? A real keyboard to type on, and not folded
 * away? A touch screen at all? A pen at all, and how hard it was last pressed, 0 to 1? */
bool desk_fingers_lately(const struct desk_server *server);
bool desk_pen_lately(const struct desk_server *server);
/* Is what has the keys a thing a pen writes on, and the pen the last thing used on it? */
bool desk_pen_writing(struct desk_server *server);
bool desk_pen_there(const struct desk_server *server);
double desk_pen_pressure(const struct desk_server *server);
/* The screen a finger was last on, or the desk's. */
struct wlr_output *desk_finger_screen(struct desk_server *server);
/* Is a finger on the screen as the hand (the pointer, moved unseen to it), other than this one? */
bool desk_hand_finger_on(const struct desk_server *server, int32_t besides);
bool desk_typing_keyboard(const struct desk_server *server);
bool desk_touch_there(const struct desk_server *server);
/* Is the door shut? Text, to what has the keys: false if that is not asking for text as text. */
bool desk_door_shut(const struct desk_server *server);
bool desk_type_text(struct desk_server *server, const char *text);
/* Is what has the keys an X program? Then a character with no key of its own goes on a key nothing
 * uses, for the moment: which (an evdev code), or 0; and back off it. */
bool desk_typing_to_x(struct desk_server *server);
int desk_x_key_for(struct desk_server *server, uint32_t cp);
void desk_x_key_done(struct desk_server *server);
/* What Shell Settings has to say about it. */
enum { DESK_KEYS_AUTO, DESK_KEYS_ALWAYS, DESK_KEYS_NEVER };
int desk_keys_setting(const struct desk_server *server);
/* Keep what is being typed in above this line of the screen, or -1, nothing is in the way. */
void desk_keys_room(struct desk_server *server, int top);

/* Start one of the desk's own programs, by itself: the one beside this desk's binary (as built, or
 * installed with it), else the one on the PATH; with these words after its name (NULL-ended, or
 * NULL for none). */
void desk_run(const char *program);
void desk_run_with(const char *program, const char *const *words);

/* ---- for what lies on the desk itself, under the windows ---- */

/* Is the desk out of the hand's reach: covered (desk_covered), or the drawer out, the windows
 * spread out to choose from, the camera up? */
bool desk_busy(const struct desk_server *server);
/* Is there a window under the hand? */
bool desk_window_under(struct desk_server *server);
/* A tree over the desk's own pictures (the mat among them) and under all the paper: what is put
 * in it moves with the desk. NULL until the desk has been drawn once. */
struct wlr_scene_tree *desk_on_desk(const struct desk_server *server);
/* The hand's picture, as the cursor theme names them ("pointer", "grab", "grabbing", "default"). */
void desk_hand_shape(struct desk_server *server, const char *name);
/* What lies over the desk (draw.c's overlays, the mat among them) has changed: drawn again now,
 * where it has. */
void desk_overlays_changed(struct desk_server *server);

/* The windows, top first: from NULL, to NULL. */
struct desk_toplevel *view_next(struct desk_server *server, struct desk_toplevel *view);
struct desk_server *view_server(const struct desk_toplevel *view);
/* The thing's edge: how big it is, and the tree it is in (NULL off the desk). */
int view_w(const struct desk_toplevel *view);
int view_h(const struct desk_toplevel *view);
struct wlr_scene_tree *view_tree(const struct desk_toplevel *view);
/* In that tree, under the frame and the window. */
void view_lay_under(struct desk_toplevel *view, struct wlr_scene_node *node);
/* The thing it is at the moment, if it is one: not while it has the whole screen. */
const struct desk_thing *view_thing(const struct desk_toplevel *view);
/* It is this thing, at the moment or not. */
bool view_is(const struct desk_toplevel *view, const struct desk_thing *thing);
/* What this thing keeps for this window: zeros the first time, and the desk frees it
 * with the window, or when another thing claims it. */
void *view_thing_data(struct desk_toplevel *view, const struct desk_thing *thing, size_t size);
/* Its frame again, whether or not it thinks anything has changed. */
void view_redraw(struct desk_toplevel *view);
/* The thing as it lies, what is in it and all, its pad past its edge: a new picture. */
cairo_surface_t *view_picture(struct desk_toplevel *view);
bool view_on_desk(const struct desk_toplevel *view);
const char *view_title(const struct desk_toplevel *view);
bool view_is_pip(const struct desk_toplevel *view);
/* A Wayland window's id (NULL for an X one), and whether it is some window's dialog. */
const char *view_wayland_id(const struct desk_toplevel *view);
bool view_has_parent(const struct desk_toplevel *view);
/* What the program calls itself: that id, or an X window's class. */
const char *view_program_id(const struct desk_toplevel *view);
pid_t view_pid(const struct desk_toplevel *view);
struct wlr_output *view_screen(struct desk_toplevel *view);
void view_send_position(struct desk_toplevel *view);
void view_close(struct desk_toplevel *view);
void focus_toplevel(struct desk_toplevel *view);
/* In front, and opened back out if it was folded away. */
void view_bring_back(struct desk_toplevel *view);
/* Picked up by the hand, or pulled by these edges (WLR_EDGE_*). */
void view_begin_move(struct desk_toplevel *view);
void view_begin_resize(struct desk_toplevel *view, unsigned edges);
/* A hand on the window: it is the program's, as it would be on paper. */
void view_pass_press(struct desk_toplevel *view, struct wlr_pointer_button_event *event);

#endif
