#ifndef DESK_H
#define DESK_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "media.h"
#include "panel.h"

enum {
	DESK_TITLE_H = 46,
	DESK_BORDER = 8,
	DESK_RADIUS = 13,
	DESK_SHADOW = 56,
	DESK_ICON = 112,
	DESK_DOCK_COUNT = 5,
};

enum {
	/* The line's strength used to be a box of its own out here. It is
	 * five dots on the console's nameplate now -- one thing that says
	 * what the machine is doing, in one place. */
	/* The battery went too: most of the machines this runs on have none,
	 * and one that always read 72% was telling the rest a lie. */
	DESK_GADGET_CLOCK = 0,
	DESK_GADGET_DESKCLOCK,
	DESK_GADGET_CALENDAR,
	DESK_GADGET_NOTEPAD,
	DESK_GADGET_COUNT,
};

enum desk_icon {
	DESK_ICON_TERM = 0,
	DESK_ICON_GLOBE,
	DESK_ICON_FOLDER,
	DESK_ICON_NOTES,
	DESK_ICON_GEAR,
};

/* The whole room as one ray traced scene: floor, wall, desk, drawer. */
void desk_draw_room(cairo_t *cr, int w, int h, double drawer_open);
/* Which room desk_draw_room draws for this screen -- the one traced for it
 * or, until that lands, the last one up -- by its serial; 0 before there is
 * any, when it lays the bare boards. Its picture changes only with this,
 * the size and the drawer. */
unsigned desk_room_shown(int screen_w, int screen_h);
/* the bare boards desk_draw_room lays before there is any room, alone:
 * for drawing them smaller than the screen and stretching them */
void desk_draw_room_placeholder(cairo_t *cr, int w, int h);
/* More desk for a screen beside the desk's own: the desk's boards, with
 * nothing on them and no drawer, run on from the desk's screen and lit
 * less further from it. x, y, w, h: that screen, in the coordinates the
 * desk's screen (desk_w by desk_h) has its top left at 0,0 in; cr draws
 * its w by h from 0,0. */
void desk_draw_side_desk(cairo_t *cr, int x, int y, int w, int h, int desk_w, int desk_h);
/* The textures paper, wood and grain are cut from, made on a thread of
 * their own now rather than by the first painter that asks for them: the
 * first window's paper was a third of a second late on an old machine,
 * waiting for its texture. Once; from the compositor's own thread. */
void desk_warm_tiles(void);

/* The room is baked as two sheets: the room with an empty drawer slot, and
 * the drawer alone, fully out. Sliding one over the other is what makes the
 * drawer open in real time when a trace takes most of a minute. */
struct desk_room_sheets {
	cairo_surface_t *background;
	cairo_surface_t *drawer;
	cairo_surface_t *shelf;    /* the bookcase, through the tilted camera */
	int w, h;        /* size both were baked at */
	double slide;    /* pixels the drawer travels, in bake pixels */
	double edge;     /* y of the desk's front edge, in bake pixels */
	double shut_zoom;  /* zoom that fills the frame with just the desk */
	double shut_pan;
	double dr_v0, dr_v1;   /* the band the drawer occupies, in frame units */
};
bool desk_room_sheets(int screen_w, int screen_h, struct desk_room_sheets *out);
/* Where the desk sits for a given drawer position. The desk keeps its size
 * and slides up by exactly the height of the drawer, the way the home
 * screen gets out of the way of the multitasking tray. slide_t is the eased
 * drawer position. */
void desk_room_view(double open, const struct desk_room_sheets *sheets,
	double *zoom, double *pan, double *slide_t);
/* Where the drawer sheet sits on screen for a given drawer position. */
void desk_room_drawer_offset(int screen_h, double open, double scale,
	double oy, double v0, double v1, double sheet_h, double *dy, double *clip_y);
/* The bookcase, with the spine labels printed over it. hover is a binder
 * index or -1. */
void desk_draw_shelf(cairo_t *cr, int w, int h, int hover, double hover_t, int taken);
int desk_shelf_hit(int w, int h, double px, double py);
/* The same view as a compositor keeps it (see draw.c): the rectangle a
 * binder can slide out in, anywhere in its slide; and the three things on
 * the lower board, each with its box and a key that changes exactly when
 * drawing it again would come out different. */
bool desk_shelf_hover_box(int w, int h, int hover, double box[4]);
/* Print the case's labels now, while nobody is looking, rather than on
 * the first look up at it. False while the room has no shelf yet. */
bool desk_shelf_prewarm(int w, int h);
bool desk_shelf_gadget_box(int w, int h, int gadget, double box[4]);
void desk_draw_shelf_gadget(cairo_t *cr, int w, int h, int gadget, time_t now);
uint64_t desk_shelf_gadget_key(int gadget, time_t now);

/* What is on the console, in the order the warm-up lights them. */
enum {
	DESK_CC_WIFI = 0,
	DESK_CC_BT,
	DESK_CC_VOL,
	DESK_CC_BRIGHT,
	DESK_CC_CRT,
	DESK_CC_BATT,
	DESK_CC_NET,
	/* the guarded set along the bottom of the panel */
	DESK_CC_PW_OFF,
	DESK_CC_PW_BOOT,
	DESK_CC_PW_OUT,
	DESK_CC_PW_KEY,
	/* the screen's own light, under the lamp's fader: a laptop's backlight */
	DESK_CC_SCREEN,
	/* the screen lock: out of the house, the door locked behind you */
	DESK_CC_LOCK,
	DESK_CC_COUNT,
};

/* The control console: where its switches have been left, and how far
 * through warming up it is. clock is monotonic seconds, for the tube. */
struct desk_console_state {
	bool wifi_on, bt_on;
	double volume;      /* 0..1, where the knob is turned to */
	double brightness;  /* 0..1, where the lamp's fader sits */
	double screen;      /* 0..1, where the screen's fader sits */
	bool screen_ok;     /* there is a backlight for it to turn */
	double warm;        /* 0..1, the filaments coming up */
	double clock;

	/* The power section. Nothing here is a setting: it is where the key
	 * is, whether the guard is up, and how far through going dark the
	 * machine has got. */
	double key;         /* 0 lying beside the lock .. 1 turned home */
	double guard;       /* 0 the glass shut .. 1 swung up */
	int pressed;        /* DESK_CC_PW_* once one is pushed, else -1 */
	double dying;       /* 0 .. 1 the set going out after the push */
	double lock_push;   /* 0 .. 1 the LOCK button pushed in */
	double antenna;     /* the wireless aerial: 0 down .. 1 all the way up */
	/* the CHOOSE buttons: the one held down, and the one whose tuner is up
	 * (DESK_CC_WIFI, _BT or _VOL; -1 none) */
	int choose_down, choose_open;
};
void desk_draw_console(cairo_t *cr, int w, int h,
	const struct desk_console_state *st, int hover);
/* The traced sheet on its own, which never changes: put up once, and the
 * instruments drawn over it as they change. */
void desk_draw_console_base(cairo_t *cr, int w, int h);
int desk_console_hit(int w, int h, double px, double py);
/* The word stamped under a lever or the knob -- WIRELESS, BLUETOOTH,
 * VOLUME -- under the point: DESK_CC_WIFI, _BT or _VOL, else -1. What is
 * behind the control comes out on a tuner for it. */
int desk_console_legend_hit(int w, int h, double px, double py);
/* The CHOOSE button under the point: DESK_CC_WIFI, _BT or _VOL, else -1. */
int desk_console_choose_hit(int w, int h, double px, double py);

/* The lid on the top of the set's tower, for a tuner: it folds up on its
 * hinges to stand with the tuner in its window, traced a frame at a time
 * after the views (until then it cannot open). zoom is how far the view
 * has pulled back to make room for it, 0 .. 1: everything of the set
 * drawn or picked goes by it (set it before either). open is how far the
 * lid is open, 0 shut .. 1 open. */
#define DESK_LID_FRAMES 16
struct desk_tuner;
bool desk_console_ext_ready(int w, int h);
/* Which room the set is drawn from for this screen, by its serial (0 none
 * yet): everything drawn over it has to be drawn again when this changes,
 * since a room traced for another shape of screen puts the set elsewhere. */
unsigned desk_console_room(int w, int h);
void desk_console_set_zoom(double zoom);
/* What is under a point: DESK_TUNER_* on the tuner in its window (and *at
 * on its dial), DESK_TUNER_PLATE elsewhere on the lid, DESK_TUNER_OUT off
 * it. */
int desk_console_ext_hit(int w, int h, double open, const struct desk_tuner *t, double px,
	double py, double *at);
/* For a compositor to put each up once and move it: open as far as open,
 * the lid is frame of its folding, a picture pw by ph (desk_draw_console_lid)
 * shown at r (x, y, w, h) on the screen; and the tuner, when face says its
 * window is towards you, is drawn on the screen with its window's corners
 * at q (tl, tr, bl, br; desk_draw_console_lid_tuner), inside box. */
struct desk_lid_place {
	int frame;
	double r[4];
	int pw, ph;
	bool face;
	double q[8];
	double box[4];
};
bool desk_console_lid_place(int w, int h, double open, struct desk_lid_place *p);
void desk_draw_console_lid(cairo_t *cr, int w, int h, int frame);
void desk_draw_console_lid_tuner(cairo_t *cr, int w, int h, double open,
	const struct desk_tuner *t);
/* The wide sheet the view pulls back over, in its own pixels (pw by ph),
 * and where it is on the screen now; and how pulling the view back as far
 * as zoom moves what was on the screen: (x, y) to (x z + dx, y z + dy). */
bool desk_console_wide_place(int w, int h, double r[4], int *pw, int *ph);
void desk_draw_console_wide(cairo_t *cr, int w, int h);
bool desk_console_zoom_map(int w, int h, double zoom, double *z, double *dx, double *dy);

/* The tuner: a radio's tuning dial, printed in the window of the lid that
 * folds up off the top of the set's tower, for what is behind a lever or the
 * knob -- the networks in range, the Bluetooth devices, the speakers and
 * microphones. Their names are printed round the dial's scale; turning
 * the knob rolls the scale under a fixed pointer, the readout under the
 * knob spells out the one under it, and pressing the knob takes it. A
 * button on the left looks again (or, for sound, a band switch chooses
 * between playing and recording), and one on the right opens the whole
 * program. Laid out 600 x 400. */
#define DESK_TUNER_MAX 40
struct desk_tuner {
	char title[64];                    /* engraved at its corner */
	int n;
	char name[DESK_TUNER_MAX][128];
	bool lit[DESK_TUNER_MAX];          /* connected, or the one in use */
	double pos;                        /* the entry under the pointer, between two while turning */
	char read1[128], read2[96];        /* the readout: a name, and how it stands */
	char read3[128], read4[96];        /* the other readout: what is in use now */
	const char *left;                  /* the left button's word, or NULL */
	int band;                          /* -1 none; else which of band_word is chosen */
	const char *band_word[2];
	const char *right;                 /* the right button's word */
	int hot;                           /* DESK_TUNER_* under the pointer, or -1 */
	bool busy;                         /* something asked of it, not back yet */
	double clock;                      /* seconds, for the busy lamp */
	bool inset;                        /* set in the lid's window: no case of its own */
};
enum {
	DESK_TUNER_OUT = -3,     /* not on it */
	DESK_TUNER_PLATE = -1,   /* on it, on nothing */
	DESK_TUNER_KNOB = 0,
	DESK_TUNER_DIAL,
	DESK_TUNER_LEFT,
	DESK_TUNER_BAND,
	DESK_TUNER_RIGHT,
};
struct desk_tuner_place {
	double x, y, w, h;       /* where it is drawn */
	double k;                /* its size against the 600 x 400 it is laid out at */
	double travel;           /* how far it has still to come at pull 0 */
};
void desk_draw_tuner(cairo_t *cr, const struct desk_tuner *t, const struct desk_tuner_place *pl,
	double pull);
/* What is under a point on it, out: DESK_TUNER_*. On the dial, *at is the
 * entry printed there (fractional: round it). */
int desk_tuner_hit(const struct desk_tuner *t, const struct desk_tuner_place *pl, double px,
	double py, double *at);
/* the glass over the guarded set, which is drawn rather than baked */
bool desk_console_guard_hit(int w, int h, double px, double py, double open);
/* Is the pointer on the set as it stands on the desk, seen from the desk's
 * own camera? This is what sends you over to it. */
bool desk_console_desk_hit(int w, int h, double px, double py, double drawer_open);
/* Is the pointer on the machine at all, in its own view? Clicking off it is
 * what sends you back to the desk. */
bool desk_console_view_hit(int w, int h, double px, double py);
/* The same two, as seen from up at the bookcase: they stand in the
 * foreground of that view, so they can be reached from there. */
bool desk_console_shelf_hit(int w, int h, double px, double py);
/* Where a hand on the knob or the fader has put it, 0..1. False for
 * anything you throw rather than aim. */
bool desk_console_value_at(int w, int h, int idx, double px, double py, double *out);
/* The live half again, a piece at a time, for a compositor that keeps each
 * as a sprite: the controls, numbered as DESK_CC_*, then the nameplate,
 * the row of lenses and the glass. advance moves the tube's trace on and
 * goes once per frame, before any piece is drawn. key changes exactly when
 * drawing the piece again would come out different. box is where the
 * piece lands on screen; false means it has to be found by drawing it. */
enum {
	DESK_CCP_PLATE = DESK_CC_COUNT,
	DESK_CCP_LENSES,
	DESK_CCP_GUARD,
	DESK_CCP_ANTENNA,
	/* the brass CHOOSE buttons beside the wireless lever, the Bluetooth
	 * lever and the knob, in that order: each brings its tuner up */
	DESK_CCP_CHOOSE,
	DESK_CCP_CHOOSE_LAST = DESK_CCP_CHOOSE + 2,
	DESK_CCP_COUNT,
};
struct desk_meters;
void desk_console_advance(const struct desk_console_state *st,
	const struct desk_meters *m);
uint64_t desk_console_piece_key(int piece, const struct desk_console_state *st,
	int hover, const struct desk_meters *m);
bool desk_console_piece_box(int w, int h, int piece, double box[4]);
void desk_draw_console_piece(cairo_t *cr, int w, int h, int piece,
	const struct desk_console_state *st, int hover, const struct desk_meters *m);
/* The set with nobody at it: its pilot lamps, over the desk view. */
void desk_draw_console_idle(cairo_t *cr, int w, int h, double drawer_open);
/* The post box, opened: the letters standing in it, newest at the front.
 * hover is a letter index or -1; warm runs 0..1 as the camera lands. */
struct desk_note;
/* The turned-down corner of a letter in the box (idx), how far it is
 * turned (s, in the letter's face pixels), how high it is off the paper
 * (lift 0..1), and the instruction on its back under the hand (hot, a
 * DESK_LETTER_ code, or -1). idx -1: every corner is at rest. */
struct desk_post_curl {
	int idx;
	double s, lift;
	int hot;
};
enum { DESK_LETTER_READ, DESK_LETTER_MARK, DESK_LETTER_DISCARD, DESK_LETTER_ACTION };
void desk_draw_post(cairo_t *cr, int w, int h,
	const struct desk_note *notes, int n, int hover, double warm, double pos,
	const struct desk_post_curl *curl);
/* Where a point is on letter i's face (face pixels), and the face's size. */
bool desk_post_on_letter(int w, int h, int i, int n, double pos,
	double px, double py, double *fx, double *fy, double *fw, double *fh);
void desk_post_curl_sizes(double fw, double fh, double *rest, double *open);
int desk_post_curl_hit(const struct desk_note *note, double fw, double fh, double s,
	double fx, double fy);
/* What the sender offered to do about a letter (its key), and the words. */
const char *desk_post_letter_action(const struct desk_note *note, const char **label);
/* What the desk sees of it: the cap, and a letter corner in the slot when
 * something has come that has not been read. */
void desk_draw_post_idle(cairo_t *cr, int w, int h, double drawer_open, int unread);
int desk_post_hit(int w, int h, int n, double pos, double px, double py);
/* where the letters are drawn in the box's own view, x y w h: the opening
 * and the door below it */
bool desk_post_letters_area(int w, int h, double box[4]);
bool desk_post_letter_box(int w, int h, int i, int n, double pos,
	double *x, double *y, double *bw, double *bh);
/* The letter opened out, on a sheet you can put down on the desk. */
cairo_surface_t *desk_letter_sheet(const struct desk_note *note,
	double w, double h);
bool desk_post_desk_hit(int w, int h, double px, double py, double drawer_open);
bool desk_post_view_hit(int w, int h, double px, double py);
bool desk_post_shelf_hit(int w, int h, double px, double py);

/* One binder, taken down and laid open on the desk. first is the sheet at
 * the front of the fan, sel the one being reached for, open_t how far it
 * has come in being taken off the shelf, turned and opened (as the book's,
 * desk_book_look);
 * corner, if not NULL, the turned-down corner of the one reached for;
 * taken, if not -1, a sheet that has been taken out and is not on the ring. */
enum { DESK_BINDER_OPEN = 0, DESK_BINDER_DRAWER };
struct desk_binder_corner {
	int idx;            /* the sheet it is on (it shows only on sel), or -1 */
	double s, lift;     /* how far it is turned down, and how far up off it */
	int hot;            /* the instruction under the hand: DESK_BINDER_, -1 */
	bool in_drawer;     /* the program is in the drawer already */
};
void desk_draw_binder(cairo_t *cr, int w, int h, int shelf, double first,
	int sel, double open_t, const struct desk_binder_corner *corner, int taken);
/* The corner's sizes at rest and turned up, for a screen; where a point on
 * the screen is on sheet idx (hot: lifted, as the one reached for is); and
 * which instruction is there with the corner turned down by s, or -1. */
void desk_binder_curl_sizes(int w, int h, double *rest, double *open);
void desk_binder_local(int w, int h, double first, int idx, bool hot, double px, double py,
	double *lx, double *ly, double *sw, double *sh);
int desk_binder_curl_hit(int w, int h, double s, bool in_drawer, double lx, double ly);
int desk_binder_hit(int w, int h, int count, double first, double px, double py);
int desk_binder_fan(void);
/* The sheets of an open binder are printed once and kept. Warm prints up
 * to `budget` of the ones fanned from `first` and the next either side,
 * ahead of time, and is true while there are more; forget lets them all
 * go, for when it is put away. */
bool desk_binder_warm(int w, int h, int shelf, double first, int budget);
void desk_binder_forget(void);
/* Every kept picture with words in it let go: the desk's language has
 * changed (i18n.h). */
void desk_draw_forget_words(void);
/* The part of the screen an open binder, fanned from `first`, can cover:
 * x, y, w, h -- the whole screen while it is still being taken down and
 * opened (open_t under 1). */
void desk_binder_box(int w, int h, double first, double open_t, double box[4]);
/* What it is drawn from as binder shelf is taken down and opened, printed
 * ahead a piece a call while the pointer is on it: true while there is more. */
bool desk_binder_anim_warm(int w, int h, int shelf);
/* Render one app's sheet to its own surface, so the compositor can fly it
 * off the shelf and down onto the desk. */
cairo_surface_t *desk_app_sheet_surface(const char *id, double w, double h,
	const char *code);
/* A sheet of the open binder's fan as it hangs (place: its index less the
 * first one's): its middle, size and turn round the ring. */
void desk_binder_sheet_pose(int w, int h, double place, bool hot, double *cx, double *cy,
	double *sw, double *sh, double *angle);
/* The book of every program, taken down and held open (DESK_SHELF_BOOK
 * on the shelf): an index, A to Z, of every program on the machine, a line
 * to each, with a line at the head of its left page to write what you are
 * looking for on -- and then only what answers to it is in the index.
 * What is written there (query, caret in bytes) and what the input method
 * has of it not yet written (preedit, its caret in bytes; its candidates,
 * cand_page to a page); the spread it is open at (page, fractional while a
 * leaf is going over); the line under the pointer (hover: a place in the
 * index, -1) and the one chosen with the keys (sel); the thumb-index tab
 * under the pointer (hover_tab: 0 to 25 for A to Z, 26 for the rest, -1);
 * open_t, how far it has come in being taken down and opened: 0 on the
 * shelf, then off it spine first, turned to show its front board, and the
 * board swung over on the spine, to 1 held open (as time goes; the easing
 * is the book's own). The box is the whole screen until it is 1. */
struct desk_book_look {
	const char *query;
	int caret;
	const char *preedit;
	int preedit_caret;
	const char *const *cands;
	int ncand, cand_cursor, cand_page;
	double page;
	int hover, sel, hover_tab;
	double open_t;
	bool ime;
};
#define DESK_BOOK_TABS 27
enum {
	DESK_BOOK_HIT_NONE,
	DESK_BOOK_HIT_BOOK,     /* the book, but nothing on it */
	DESK_BOOK_HIT_ENTRY,    /* *arg: its place in the index */
	DESK_BOOK_HIT_TAB,      /* *arg: the tab */
	DESK_BOOK_HIT_FIND,     /* the line to write on */
	DESK_BOOK_HIT_PREV,     /* the corner of the left page: back a leaf */
	DESK_BOOK_HIT_NEXT,     /* the corner of the right page: on a leaf */
};
void desk_draw_book(cairo_t *cr, int w, int h, const struct desk_book_look *look);
/* The same in two layers, for a compositor that keeps each as a sprite:
 * the book with the leaf going over left out -- the same picture all the
 * way through a turn but for the tabs at its middle -- and the leaf by
 * itself, over it, with its extent (false: no leaf is going over). */
void desk_draw_book_still(cairo_t *cr, int w, int h, const struct desk_book_look *look);
void desk_draw_book_leaf(cairo_t *cr, int w, int h, const struct desk_book_look *look);
bool desk_book_leaf_box(int w, int h, const struct desk_book_look *look, double box[4]);
/* And what the hand on it changes, for a still drawn with hover and
 * hover_tab -1: part 0 the line of the index or the tab it is on, part 1
 * the plate that line puts on an empty right-hand page. Each a patch of
 * the book, cut to where it changed; the look is the still's with the
 * hand put back. Its extent; false: that part has nothing to show. */
void desk_draw_book_hot(cairo_t *cr, int w, int h, const struct desk_book_look *look, int part);
bool desk_book_hot_box(int w, int h, const struct desk_book_look *look, int part, double box[4]);
/* all that is drawn of it, x, y, w, h */
void desk_book_box(int w, int h, const struct desk_book_look *look, double box[4]);
int desk_book_hit(int w, int h, const struct desk_book_look *look, double x, double y,
	int *arg);
/* The index for what is written: the programs that answer to it, as places
 * in the app list, best first (all of them A to Z for nothing written); up
 * to max of them into out; how many there are. */
int desk_book_matches(const char *query, int *out, int max);
/* How many spreads the index takes, which spread place i of it is on, and
 * which spread a thumb-index tab opens it at (-1: none of that letter). */
int desk_book_spreads(int w, int h, const char *query);
int desk_book_spread_of(int w, int h, const char *query, int i);
int desk_book_tab_spread(int w, int h, const char *query, int tab);
/* Where line i of the index is on the screen, open at spread page: its
 * middle, size and turn -- for a sheet to be taken from it. False if it is
 * not on that spread. */
bool desk_book_entry_pose(int w, int h, const struct desk_book_look *look, int i,
	double *cx, double *cy, double *ew, double *eh, double *angle);
/* Where the line to write on is, for the input method's own windows. */
bool desk_book_find_box(int w, int h, const struct desk_book_look *look, double box[4]);
/* What it is drawn from as it is taken down and opened, printed ahead a
 * piece a call while the pointer is on it: true while there is more. */
bool desk_book_warm(int w, int h);

/* A sheet taken out of a binder, in the hand: middle (cx, cy), turned by
 * angle, height 0 (on the desk) to 1 (held up), which sets its shadow. */
void desk_draw_taken_sheet(cairo_t *cr, cairo_surface_t *face, double cx, double cy,
	double w, double h, double angle, double height);
/* And put down: the blank sheet a program's window will be (made once by
 * desk_window_base_sheet, w by h, its name at the head), drawn in its own
 * coordinates with `open` 0 (just the taken sheet, lw by lh, in the middle,
 * its print `face` on it) to 1 (all of it, the paper that came out from
 * behind still creased). */
cairo_surface_t *desk_window_base_sheet(double w, double h, const char *title);
void desk_draw_window_base(cairo_t *cr, cairo_surface_t *blank, cairo_surface_t *face,
	double w, double h, double lw, double lh, double open, unsigned seed);
/* Shell Settings' form, printed on the shell's paper (transparent where
 * there is no ink), w by h: rows of parts (a heading in small capitals over
 * a rule, and what the part is about under it if it says), boxes to tick,
 * choices -- the words in a row, as many rows as they take, the chosen one
 * ringed in pencil -- and other programs to open, each with what it means
 * under it; the row under the hand and the word in it; how far the sheet is
 * slid up; where it is kept. */
enum { DESK_FORM_SECTION, DESK_FORM_CHECK, DESK_FORM_CHOICE, DESK_FORM_DATE,
	DESK_FORM_ACTION };
/* A date is laid out as a choice's words are: back a month, the month,
 * on a month; back a day, the day, on a day; and none.
 * An action opens another program. "Open ›" is written after its label in
 * pencil (options[0]); with nothing to open it (value 0) the row is printed
 * faint, and what is written there instead is options[1] -- "not installed
 * -- e.g. a package". */
/* the most words a choice has, its NULL included */
#define DESK_FORM_OPTS 20
struct desk_form_row {
	int kind;
	const char *label;
	const char *note;            /* may be NULL */
	const char *const *options;  /* a choice's words, NULL-terminated */
	int value;                   /* a box: 0 or 1; a choice: which word; a date: month * 32
	                              * + day, 0 for none; an action: 1 if there is
	                              * something to open */
	cairo_surface_t *icon;       /* an action: the icon of what it opens; may be NULL */
	const char *warn;            /* may be NULL: said before the note, in red */
};
struct desk_shell_form {
	const struct desk_form_row *rows;
	int n;
	int hover, hover_opt;        /* -1: none */
	double scroll;
	const char *kept_in;
	bool on_paper;               /* printed over what is there (a sheet of its own) rather
	                              * than on a clear surface over the shell's paper */
};
void desk_draw_shell_form(cairo_t *cr, double w, double h, const struct desk_shell_form *f);
/* The shell's paper, fresh as a pad's sheet, over all of the clip. */
void desk_draw_pad_paper(cairo_t *cr);
/* A sticky note's colour, c of the colours the pads come in (agenda.h). */
void desk_stick_colour(int c, double *r, double *g, double *b);
/* The row at (x, y) on the form as drawn, and the word in it (opt) if it is
 * a choice, or 0 over an action's "Open ›"; -1. */
int desk_shell_form_hit(double w, double h, const struct desk_shell_form *f, double x,
	double y, int *opt);
/* How tall all of it is, w wide. */
double desk_shell_form_height(double w, const struct desk_shell_form *f);
/* Where row i is on the form, w wide: its top and its height (for keeping
 * the row the keys are on in sight). */
void desk_shell_form_row_span(double w, const struct desk_shell_form *f, int i, double *top,
	double *height);
/* The sheet on its way down: t runs 0 at the binder to 1 on the desk. */
void desk_draw_sheet_flight(cairo_t *cr, cairo_surface_t *sheet,
	double x0, double y0, double w0, double h0,
	double x1, double y1, double w1, double h1, double t);
enum { DESK_VIEW_SHELF = 0, DESK_VIEW_CONSOLE, DESK_VIEW_POST,
	DESK_VIEW_COUNT };

/* The traced move between the desk and one of the things on it, as one
 * strip of frames stacked vertically. Frame 0 is the desk, the last is
 * whatever you were looking at. */
bool desk_tilt_film(int w, int h, int which, cairo_surface_t **strip,
	int *frames, int *fw, int *fh);
/* Where in that film a position in the move lands: draw frame, then
 * frame + 1 over it at alpha mix. */
void desk_tilt_at(double t, int frames, int *frame, double *mix);
/* Where keyframe `frame` of a move's film goes -- the screen box x, y, w, h
 * it is stretched over -- to stand in for the view at t; false to lay it
 * over the screen as it is. And how far the settled view at the end of the
 * move has to be moved to be the view at t, as the move comes in to it,
 * goes past it and comes back. */
bool desk_tilt_place(int screen_w, int screen_h, int which, int frame, double t,
	double box[4]);
void desk_tilt_offset(int screen_w, int screen_h, int which, double t, double *dx, double *dy);
/* Likewise for the desk, at the other end: where the desk view has to be
 * moved to be the view at t, as a move comes down to it or sets off. */
void desk_tilt_desk_offset(int screen_w, int screen_h, int which, double t,
	double *dx, double *dy);
/* What glows behind the desk gadgets' glass; their bodies are in the bake. */
void desk_draw_gadgets(cairo_t *cr, int w, int h, time_t now, double drawer_open);
/* The memo, the clock and the calendar, stood up on the bookcase shelf. */
void desk_draw_shelf_gadgets(cairo_t *cr, int w, int h, time_t now);
/* The desk calendar. Its pages are in four sections, one behind the other
 * -- the year, the months, the weeks, the days -- each with its tab
 * standing out at the right edge; the sections in front of the one being
 * read are turned over the wire to the back. */
enum { DESK_CAL_YEAR, DESK_CAL_MONTH, DESK_CAL_WEEK, DESK_CAL_DAY, DESK_CAL_VIEWS };
/* Taken off the shelf and looked at close up: the section being read and
 * the day its page is about (month 0 to 11); the page turning over the
 * wire -- turn +1 on to the next page of the section, -1 back to the last,
 * turn_t 0 to 1 of the way; the edge of the page the pointer is at (lift
 * +1 its foot, lifted a little, -1 the last page, drawn up a little behind
 * the wire, by lift_t); a bundle of sections going over together on the
 * way to another section (to_view, by switch_t; to_view == view when
 * not); the tab the pointer is on, or -1; and notes (ids, 0 for none) that
 * are off the page just now -- in the air, or in the binder being written
 * on again -- their places left empty. */
#define DESK_CAL_HIDE 8
struct desk_cal_look {
	int view;
	int year, month, day;
	int turn;
	double turn_t;
	int lift;
	double lift_t;
	int to_view;
	double switch_t;
	int hover_tab;
	int hide[DESK_CAL_HIDE];
	/* how it is held: turned, its right side going away by yaw, and its
	 * top going away by pitch (radians), seen by an eye eye_f pixels in
	 * front of the screen over eye_x, eye_y (all in the close up's own
	 * terms; eye_f 0 for one over the middle of the screen) */
	double yaw, pitch;
	double eye_x, eye_y, eye_f;
};
/* How a note looks where it is: being written in the binder (font: the
 * size of its writing), a flag on a month's day, a note on a week's or a
 * day's page (font), or so small it is no more than a spot of its colour.
 * At x, y, w by h as it would lie square, turned by rot about its middle. */
enum { DESK_NOTE_BINDER, DESK_NOTE_FLAG, DESK_NOTE_CARD, DESK_NOTE_DOT };
struct desk_note_pose {
	double x, y, w, h, rot, font;
	int style;
};
/* Where it is drawn: the page, and all of it (box: the board, the tabs,
 * the shadow, and the room above for a page going over), in screen pixels;
 * and the page and its tabs together, against the page alone, across. */
void desk_calendar_rect(int w, int h, double page[4], double box[4]);
/* As it is held (yaw, pitch): all that is drawn of it, turned, and a point
 * of it turned onto the screen or the screen back onto it -- both in the
 * close up's terms, before it is moved aside for the binder. */
void desk_calendar_extent(int w, int h, const struct desk_cal_look *look, double box[4]);
void desk_calendar_turn_point(int w, int h, const struct desk_cal_look *look, double *x,
	double *y);
void desk_calendar_unturn(int w, int h, const struct desk_cal_look *look, double *x,
	double *y);
double desk_calendar_face_ratio(void);
void desk_draw_calendar(cairo_t *cr, int w, int h, const struct desk_cal_look *look,
	time_t now);
/* What a click close up lands on: 0 off it; DESK_CAL_HIT_PREV the head
 * of the page or above (back a page); DESK_CAL_HIT_NEXT its foot (on a
 * page); DESK_CAL_HIT_TAB + v the tab of section v; DESK_CAL_HIT_DATE
 * a day on the page -- or DESK_CAL_HIT_TODO the things to do on a day's
 * page, DESK_CAL_HIT_MONTH the name of a month on the year's -- with the
 * day in *year, *month, *day; DESK_CAL_HIT_PAGE the page, but nothing on
 * it. And whether a point is on the calendar on the shelf. */
#define DESK_CAL_HIT_PREV 1
#define DESK_CAL_HIT_NEXT 2
#define DESK_CAL_HIT_PAGE 3
#define DESK_CAL_HIT_DATE 4
#define DESK_CAL_HIT_TODO 5
#define DESK_CAL_HIT_MONTH 6
#define DESK_CAL_HIT_TAB 10
int desk_calendar_hit(int w, int h, const struct desk_cal_look *look, double x, double y,
	int *year, int *month, int *day);
bool desk_shelf_calendar_hit(int w, int h, double x, double y);

/* The memo pad, taken off the shelf to be written on (memo.h keeps what is
 * written on its sheets). Close up: the sheet it is open at -- sheet, of
 * sheets -- and what is written on it (text, the caret at byte caret, -1
 * for none), with the input method's part being written at the caret
 * (preedit, its own caret at byte preedit_caret) and its candidates, ncand
 * of them, the one picked cand_cursor. A sheet going over the wire: turn
 * +1 this one on to the back, other (sheet other_sheet) under it; -1 that
 * one coming back over onto this; turn_t of the way. Held as the calendar
 * is (yaw, pitch, the eye). */
struct desk_memo_look {
	const char *text;
	int caret;
	const char *preedit;
	int preedit_caret;
	const char *const *cands;
	int ncand, cand_cursor;
	int sheet, sheets;
	int turn;
	double turn_t;
	const char *other;
	int other_sheet;
	double yaw, pitch;
	double eye_x, eye_y, eye_f;
};
/* where it is drawn (as desk_calendar_rect), all that is drawn of it as
 * held, and a point on the screen back onto it */
void desk_memo_rect(int w, int h, double page[4], double box[4]);
void desk_memo_extent(int w, int h, const struct desk_memo_look *look, double box[4]);
void desk_memo_unturn(int w, int h, const struct desk_memo_look *look, double *x,
	double *y);
double desk_memo_face_ratio(void);
void desk_draw_memo(cairo_t *cr, int w, int h, const struct desk_memo_look *look);
/* the caret on the screen: for the input method's own window */
void desk_memo_caret_box(int w, int h, const struct desk_memo_look *look, double box[4]);
/* a click close up (unturned): off the pad, -1; else where in the text
 * the caret goes for it. And whether a point is on the pad on the shelf. */
int desk_memo_hit(int w, int h, const struct desk_memo_look *look, double x, double y);
/* where the caret goes a line up (dir -1) or down (+1), as near straight
 * above or below as the letters there allow */
int desk_memo_caret_step(int w, int h, const struct desk_memo_look *look, int dir);
bool desk_shelf_memo_hit(int w, int h, double x, double y);
/* Where a note is on the calendar close up, drawn as look has it: where it
 * is stuck -- or, if there is no room for it to be seen (one too many for
 * its day), where its day is, as a spot. False if its day is not on the
 * page. The note at x, y on it: its id, 0 for none. */
struct desk_stick;
bool desk_calendar_note_pose(int w, int h, const struct desk_cal_look *look,
	const struct desk_stick *note, struct desk_note_pose *pose);
int desk_calendar_note_at(int w, int h, const struct desk_cal_look *look, double x,
	double y);
/* Whether the page of section view about day y-m-d shows day yy-mm-dd. */
bool desk_calendar_page_has(int view, int y, int m, int d, int yy, int mm, int dd);
/* A note in the air on its way from one place to another: drawn at at (its
 * style not used), looking as it does at from, and at to, by look (0 to
 * 1); lifted off the paper by lift (0 to 1), faded by alpha. */
void desk_draw_note_between(cairo_t *cr, const struct desk_stick *note,
	const struct desk_note_pose *at, const struct desk_note_pose *from,
	const struct desk_note_pose *to, double look, double lift, double alpha);
/* The hour on a day's page close up at x, y (8 to 20), -1 if not on one. */
int desk_calendar_hour_at(int w, int h, const struct desk_cal_look *look, double x,
	double y);
/* The day a page of section view is about, turned dir pages on. */
void desk_calendar_step(int view, int dir, int *year, int *month, int *day);
/* The section the calendar on the shelf is open at. */
void desk_calendar_set_view(int view);

/* The binder of notes that comes in beside the calendar when a day on it is
 * clicked: the day; the pad picked; the note being written -- its id, or
 * DESK_AGENDA_NEW_PLAN or _TODO for one not yet stuck -- its colour now,
 * what is written on it and where the caret is (bytes), and what the
 * input method has of it not yet written; the input method's candidates
 * on show (cand_page to a page); what the pointer is on; a new note on its
 * way from its pad (peel_t) and a note being peeled off (gone_id, gone_t);
 * whether there is an input method (the page says what to type). The time
 * of the plan being written (when, minutes past midnight, -1 for none, and
 * span, how long, 0 for no end), and the ruler it is set on, out by
 * ruler_t, with a pencil mark at ruler_mark (minutes; -1 none) where the
 * pointer is on it; edit_away: the note being written is not in the binder yet,
 * but in the air on its way from the calendar. The notes stuck on the
 * calendar are written down on the page in pencil, a line to each. */
#define DESK_AGENDA_NEW_PLAN (-1)
#define DESK_AGENDA_NEW_TODO (-2)
struct desk_agenda_look {
	int year, month, day;
	int colour;
	int edit;
	int edit_colour;
	const char *text;
	int caret;
	const char *preedit;
	int preedit_caret;
	const char *const *cands;
	int ncand, cand_cursor, cand_page;
	int hover, hover_arg;
	double peel_t;
	int gone_id;
	double gone_t;
	bool ime;
	int when, span;
	double ruler_t;
	int ruler_mark;
	bool edit_away;
	/* how it is held, as the calendar is (yaw, pitch; the eye in the
	 * binder's own terms, eye_f 0 for none: drawn flat) */
	double yaw, pitch;
	double eye_x, eye_y, eye_f;
};
/* What the pointer is on in the binder, with *arg the pad's colour or the
 * note's id. */
enum {
	DESK_AGENDA_HIT_NONE,
	DESK_AGENDA_HIT_BINDER,
	DESK_AGENDA_HIT_PAD,
	DESK_AGENDA_HIT_SLOT_PLAN,
	DESK_AGENDA_HIT_SLOT_TODO,
	DESK_AGENDA_HIT_NOTE,
	DESK_AGENDA_HIT_CHECK,
	DESK_AGENDA_HIT_RULER,     /* *arg the time there, in minutes */
	DESK_AGENDA_HIT_ALLDAY,    /* the end of the ruler: no time */
};
/* Where the binder lies, in, on a screen w by h (x, y, w, h), and where the
 * calendar goes to make room for it: scaled by cal[0] and moved by cal[1],
 * cal[2] (a point of the calendar close up at x, y goes to x * k + dx). */
void desk_agenda_layout(int w, int h, double binder[4], double cal[3]);
/* The binder, drawn at 0, 0 to w by h. */
void desk_draw_agenda(cairo_t *cr, double w, double h, const struct desk_agenda_look *look);
/* The pencil mark on its ruler where the pointer is, and the tag saying
 * when (ruler_mark): drawn as the binder is held, over it -- the binder
 * drawn with ruler_mark -1 leaves it out, so that the pointer going along
 * the ruler draws this and not the whole binder again. Its extent, as
 * desk_agenda_extent's; false if there is no mark. */
void desk_draw_agenda_mark(cairo_t *cr, double w, double h, const struct desk_agenda_look *look);
bool desk_agenda_mark_extent(double w, double h, const struct desk_agenda_look *look,
	double box[4]);
int desk_agenda_hit(double w, double h, const struct desk_agenda_look *look, double x,
	double y, int *arg);
/* The note being written, in the binder (x, y, w, h): false if none is.
 * The same as a pose, to fly from or to. The time on the ruler across the
 * binder at x (minutes past midnight, to a quarter of an hour), the
 * pointer held down and drawn along past its ends too. */
bool desk_agenda_writing(double w, double h, const struct desk_agenda_look *look,
	double r[4]);
bool desk_agenda_note_pose(double w, double h, const struct desk_agenda_look *look,
	struct desk_note_pose *pose);
/* As it is held: all that is drawn of it (from its own 0, 0), and a point
 * of its face turned onto the screen, or back -- in its own terms. */
void desk_agenda_extent(double w, double h, const struct desk_agenda_look *look,
	double box[4]);
void desk_agenda_turn_point(double w, double h, const struct desk_agenda_look *look,
	double *x, double *y);
void desk_agenda_unturn(double w, double h, const struct desk_agenda_look *look,
	double *x, double *y);
int desk_agenda_ruler_minute(double w, double h, double x);
/* The engraved plate at the top right corner of the desk that sends you to
 * the shelf, and whether the pointer is on it. */
void desk_allapps_box(int w, int h, double *x, double *y, double *pw, double *ph);
bool desk_allapps_hit(int w, int h, double px, double py);
/* The tray along the back of the desk -- console, clock, battery, post box,
 * that plate -- and where it ends down the screen with the drawer shut:
 * windows are kept below this. Measured from the traced room; a fair guess
 * until there is one. And the strip along the bottom that is the shut
 * drawer's pull, which windows keep clear of too. */
int desk_tray_bottom(int w, int h);
int desk_drawer_pull_h(int w, int h);
/* The lamp that what is drawn over the room is lit by, for which way
 * shadows fall and which side of a crease catches the light: the room's
 * own over the desk (light_desk, for that size of screen, once the room is
 * traced and again whenever the screen changes), or one above and in
 * front, for a view of something stood up (light_set). And where an
 * off-screen buffer sits on screen, so that shadows drawn into it still
 * lean the right way. */
void desk_light_desk(int screen_w, int screen_h);
void desk_set_paint_origin(double x, double y);

void desk_draw_window_shadow(cairo_t *cr, double x, double y, double w, double h, double r);
/* with_shadow bakes the drop shadow into the frame. A live window passes
 * false and keeps its shadow as a separate scene node, so that dragging it
 * moves the shadow instead of repainting it. */
void desk_draw_window_frame(cairo_t *cr, double x, double y, double w, double h,
	const char *title, bool focused, int hover_btn, bool with_shadow);
/* The paper under a program that draws its own frame and is laid on the
 * shell's paper anyway: a plain margin, no heading (title seeds the paper).
 * DESK_MOUNT round it, DESK_MOUNT_TOP above it. */
enum { DESK_MOUNT = 8, DESK_MOUNT_TOP = 12 };
void desk_draw_window_mount(cairo_t *cr, double x, double y, double w, double h,
	const char *title);

/* A standalone soft shadow, and where to put it. */
int desk_shadow_sprite_pad(void);
void desk_draw_shadow_sprite(cairo_t *cr, double w, double h, double r,
	double side, double amount);
/* Which bottom corner of a sheet is off the desk (-1 left, +1 right) and
 * how far, from the same seed everything else about the sheet comes from. */
void desk_paper_lift(const char *title, double *side, double *amount);
void desk_shadow_offset(double screen_x, double screen_y, double lift,
	double *ox, double *oy);
/* Folding is per sheet: seed picks which way this one goes wrong. */
void desk_paper_fold_size(cairo_surface_t *src, double t, unsigned seed, int *ow, int *oh);
void desk_draw_paper_fold(cairo_t *cr, cairo_surface_t *src, double t, unsigned seed);
/* A sheet being folded away (see draw.c): made once from the sheet as it
 * lay, what it is (icon may be NULL), and how big a packet it should end
 * up -- then drawn at any t from 0, the flat sheet, to 1, the packet. Its
 * extent is the stack's own box at t, top left fixed, and how far the
 * drawing reaches outside it on every side. */
struct desk_fold;
struct desk_fold *desk_fold_new(cairo_surface_t *face, unsigned seed, double packet_w,
	cairo_surface_t *icon, const char *name);
void desk_fold_free(struct desk_fold *f);
void desk_fold_extent(struct desk_fold *f, double t, int *w, int *h, int *margin);
void desk_fold_draw(cairo_t *cr, struct desk_fold *f, double t);
/* Its folds: how many, which is going over at t (stages when it is done),
 * and how much of t they take between them, in equal shares from 0. */
int desk_fold_stages(const struct desk_fold *f);
int desk_fold_stage(const struct desk_fold *f, double t);
double desk_fold_done(void);
/* A sheet being thrown away (see draw.c): screwed up into a ball, u from 0
 * (flat, exactly as it lay) to 1 (a ball), in the sheet's own coordinates,
 * with the box the drawing covers at u; and then the ball in flight,
 * centred on (x, y), at a scale, turned, and `lift` pixels off the desk. */
struct desk_crumple;
struct desk_crumple *desk_crumple_new(cairo_surface_t *sheet, unsigned seed);
void desk_crumple_free(struct desk_crumple *c);
void desk_crumple_extent(struct desk_crumple *c, double u, double *x0, double *y0,
	double *x1, double *y1);
void desk_crumple_draw(cairo_t *cr, struct desk_crumple *c, double u);
void desk_crumple_ball(cairo_t *cr, struct desk_crumple *c, double x, double y,
	double scale, double angle, double lift);
double desk_crumple_radius(const struct desk_crumple *c);
/* One application, printed on a sheet of binder paper. icon may be NULL, in
 * which case the sheet gets a letterpress initial instead. code is the
 * catalogue mark in the top corner and in the running foot. seed decides
 * which way this particular sheet's printing went wrong; wear is 0..1. */
void desk_draw_app_sheet(cairo_t *cr, double w, double h,
	const char *name, const char *generic, const char *code,
	cairo_surface_t *icon, const char *initial, unsigned seed, double wear);

void desk_draw_settings_body(cairo_t *cr, double x, double y, double w, double h);
void desk_draw_desktop(cairo_t *cr, int w, int h, time_t now, bool with_demo_window, double drawer_open);

/* ---- what is in the drawer ----
 * Whatever has been put there, in a row: one of the five things the drawer
 * has always had (look 0..DESK_DOCK_COUNT-1, in DRAWER_ITEMS' order), a
 * card with a program's mark on it, or a sheet folded away. */
enum { DESK_LOOK_CARD = DESK_DOCK_COUNT, DESK_LOOK_PACKET };
struct desk_drawer_item {
	int look;
	cairo_surface_t *icon;     /* a card: the program's icon, or NULL */
	cairo_surface_t *packet;   /* a packet: the folded sheet as it lies */
	char name[128];            /* printed under a card's mark; chalked under the rest */
	char initial[8];           /* a card with no icon: its letter */
	unsigned seed;             /* how it happens to lie */
};
/* The row, in room units across the drawer, 0 its middle: slot k's middle
 * in a row of m, how far apart they are, and how big a thing is drawn in a
 * row that full (1 while they fit at their own size). */
double desk_drawer_slot_x(int k, int m);
double desk_drawer_pitch(int m);
double desk_drawer_fit(int m);
/* The felt they lie on, in the drawer sheet's own pixels (the size
 * desk_room_sheets gives): the box a layer of them over the sheet needs. */
bool desk_drawer_felt_box(int screen_w, int screen_h, double box[4]);
/* Lay them on it, cr in the sheet's pixels: thing i at x[i] along the row,
 * fit[i] its full size; hover stands up off the felt with its name under
 * it. */
void desk_drawer_paint_items(cairo_t *cr, int screen_w, int screen_h,
	const struct desk_drawer_item *items, int n, const double *x, const double *fit,
	int hover);
/* A point on the screen with the drawer out by open: false unless it is
 * over the felt; then where along the row it is, and whether it is level
 * with the things lying there (row). */
bool desk_drawer_at(int screen_w, int screen_h, double open, double px, double py,
	double *x, bool *row);
/* And back: where a place x along the row is on the screen. */
bool desk_drawer_point(int screen_w, int screen_h, double open, double x, double *sx,
	double *sy);
/* A thing as it is carried: a picture at screen scale, held up off the
 * felt with its shadow under it, and its middle in it. */
cairo_surface_t *desk_drawer_item_picture(int screen_w, int screen_h,
	const struct desk_drawer_item *it, double *ax, double *ay);
/* A sheet folded away as just the packet -- no shadow, not turned -- for
 * laying in the drawer. */
cairo_surface_t *desk_fold_packet(struct desk_fold *f);
bool desk_drawer_handle_hit(int screen_w, int screen_h, double px, double py, double open);
/* The turned-down top right corner of a sheet, and what is printed on the
 * back of it (see draw.c). Sizes of the corner at rest and turned up for a
 * screen; the desk under where the paper was, the flap itself; and which
 * instruction, if any, is at a point in the box it is drawn in (top right
 * on the sheet's corner, its size * 1.12 + 18 square: wm.c view_curl_paint). */
enum { DESK_CURL_FOLD, DESK_CURL_UNFOLD, DESK_CURL_DISCARD, DESK_CURL_ITEMS };
void desk_curl_sizes(int screen_w, int screen_h, double *rest, double *open);
/* The traced room in screen coordinates, as the compositor lays it for a
 * drawer position: for drawing what lies under something lifted off it. */
void desk_paint_room_at(cairo_t *cr, int screen_w, int screen_h, double drawer_open);
void desk_draw_curl(cairo_t *cr, double B, double s, double open, double lift,
	int hot, bool maximized, const char *title);
int desk_curl_hit(double B, double s, double open, double lx, double ly);
bool desk_curl_contains(double B, double s, double lx, double ly);
bool desk_frame_title_hit(double lx, double ly, double w, double h);
int desk_frame_resize_edges(double lx, double ly, double w, double h);
/* One edge of a sheet being opened out to the desk, the panel folded under
 * it swinging out from behind -- or, run backwards, folded back under it
 * (see draw.c). u 0 folded, 1 flat. In screen coordinates; the sheet's
 * paper beyond `bend` from the crease is not drawn, the caller has it. */
struct desk_unfold_edge {
	int side;                 /* where the panel goes: 0 right, 1 left, 2 down, 3 up */
	double crease;            /* the fold: x for sides 0 and 1, y for 2 and 3 */
	double t0, t1;            /* its extent along the fold */
	double bend;              /* how much of the sheet by the fold lifts with it */
	double len;               /* how far the panel reaches out from the fold */
	cairo_surface_t *sheet;   /* printed on the sheet, placed at sheet_x, sheet_y */
	double sheet_x, sheet_y;
	cairo_surface_t *panel;   /* on the panel, likewise; NULL: the same as the sheet */
	double panel_x, panel_y;
	double vx, vy, dist;      /* the eye: over vx, vy, dist away */
};
void desk_draw_unfold_edge(cairo_t *cr, const struct desk_unfold_edge *e, double u);
void desk_unfold_edge_extent(const struct desk_unfold_edge *e, double u,
	double *x0, double *y0, double *x1, double *y1);
/* A sheet with nothing on it, and a crease pressed out flat in one. */
void desk_draw_blank_sheet(cairo_t *cr, double x, double y, double w, double h,
	const char *title);
void desk_draw_unfolded_creases(cairo_t *cr, double pw, double ph,
	double x0, double y0, double x1, double y1);
void desk_draw_crease_relief(cairo_t *cr, bool vertical, double at, double from, double to,
	double strength);
/* A sheet being resized (see draw.c), drawn over it: the creases in paper
 * that has come out from under it -- outside the content rectangle cx0..cx1
 * by cy0..cy1 -- and the turn of the paper at the edges being moved
 * (WLR_EDGE_ bits), `lip` 0..1 by how hard it is being pulled. In the
 * sheet's own coordinates, 0,0 to pw,ph. */
void desk_draw_sizing(cairo_t *cr, double pw, double ph,
	double cx0, double cy0, double cx1, double cy1, unsigned edges, double lip,
	unsigned seed);

/* The drawer interior is one ray traced scene, which takes seconds on a cold
 * cache. Pass async from anything that must not block a frame, then repaint
 * once desk_scene_ready() turns true; until then the drawer paints flat. */
void desk_scene_warm(int screen_w, int screen_h, bool async);
/* The warm-up stopped where it is and waited for, before the compositor
 * tears down and returns: a trace still going on the GPU when exit() takes
 * the driver down crashes the process. What it had of the room is neither
 * put up nor kept, and no warm-up starts after it. */
void desk_room_stop(void);
/* The render node the compositor draws the screen with, for the room to be
 * traced on ahead of any other GPU (gpu.h, desk_gpu_prefer). Before the
 * first warm-up. */
void desk_room_gpu_prefer(const char *node);
/* Pixels to a point, as on a Mac: 1 on an ordinary screen, 2 on a Retina
 * one. The desk is as wide as the screen is in points, so what stands on
 * it keeps its size from screen to screen and a bigger screen shows more
 * desk. Set it before the first scene_warm, and again when it changes. */
void desk_set_ui_scale(double scale);
/* How much the desk draws (shell.conf graphics, Shell Settings' Graphics),
 * from 2D -- a machine drawing on its CPU, a VM: the room traced once,
 * small and plain, no films of the moves, no shadows, nothing fading or
 * blurred, nothing that moves, nothing traced as it moves -- up to Ultra,
 * the room traced finer and wider than usual. High is the desk as made.
 * Here: the room (traced again at the next scene_warm, as for a new size)
 * and the shadows; the compositor does the rest. */
enum desk_gfx {
	DESK_GFX_2D,
	DESK_GFX_LOW,
	DESK_GFX_MEDIUM,
	DESK_GFX_HIGH,
	DESK_GFX_ULTRA,
	DESK_GFX_COUNT,
};
void desk_set_gfx(enum desk_gfx g);
enum desk_gfx desk_gfx_level(void);
/* The desk's clock and the times on letters: 24-hour, or 12 with AM/PM. */
void desk_set_clock_12h(bool on);
double desk_ui_scale(void);
/* what a point is on a screen this wide, for things drawn flat over the
 * desk (less than the scale where the screen is too narrow for the desk) */
double desk_px_per_pt(int screen_w);
bool desk_scene_ready(int screen_w, int screen_h);
/* 0..1 through the two desk sheets, for the docket that lies on the bare
 * desk while they are traced. Meaningless once the desk is up. */
double desk_scene_progress(void);
/* whether the bookcase, the console and the post box are traced too */
bool desk_scene_views_ready(int screen_w, int screen_h);
/* One of them (DESK_VIEW_*), there the moment its own sheet is traced: the
 * bookcase first, minutes before the post box on a machine that traces on
 * its processor. */
bool desk_scene_view_ready(int screen_w, int screen_h, int which);
/* 0..1 through tracing those three after the desk, weighed by their
 * pixels, and which of them is being traced (0 the bookcase, 1 the console,
 * 2 the post box; -1 not begun). Meaningless once they are ready. */
double desk_scene_views_progress(int *view);
/* Whether the room the screen wants is being traced from nothing, not read
 * back from the disk -- a first start at this size, the slow one: from when
 * that is known until the desk and the three views are up. */
bool desk_scene_first_run(void);
/* The letter left in the middle of the desk meanwhile: that it takes a while
 * (a minute or two, longer on some machines) and the desk can be used. Its
 * extent in screen pixels, and it, drawn in screen coordinates. */
bool desk_first_letter_box(int screen_w, int screen_h, double box[4]);
void desk_draw_first_letter(cairo_t *cr, int screen_w, int screen_h);

/* The room at login, its lamp coming up: the dark over everything at light
 * 0..1 -- black at 0, the pool under the lamp opening first and the light
 * spreading out to the corners, warm while the filament is -- drawn in
 * screen coordinates (cr may be scaled down: it is all gradient). And how
 * dark that leaves a point. */
void desk_draw_night(cairo_t *cr, int w, int h, double light);
/* how far up the lamp is for how far through the room is, 0..1 */
double desk_boot_light(double progress);
double desk_night_alpha_at(int w, int h, double light, double x, double y);
/* What of the clock shines in the dark: its lit segments and the red
 * light they throw round it, over the night; and where that can draw. */
bool desk_clock_glow_box(int w, int h, double box[4]);
void desk_draw_clock_glow(cairo_t *cr, int w, int h);
/* Two party poppers going off from the bottom corners of the screen, t
 * seconds ago, and their confetti coming down over everything; false once
 * it is all down. */
bool desk_draw_poppers(cairo_t *cr, int w, int h, double t);
/* The size to make a buffer for something drawn over the room in screen
 * coordinates. The same as the screen up to a point, and capped above it:
 * scale the cairo context by ow/screen_w and give the scene node the
 * screen's size as its destination. */
void desk_overlay_size(int screen_w, int screen_h, int *ow, int *oh);
/* The same for what is a scene rather than something read (the door and
 * what moves at it, the confetti, the desk on another screen): no finer
 * than the room at the levels that trace it smaller. */
void desk_ambient_size(int screen_w, int screen_h, int *ow, int *oh);

int desk_write_png(const char *path, int w, int h, bool with_demo_window);

/* What it is like out on the step of the front door: the sun -- how high
 * it is and where round, in degrees, from the north through the east; the
 * door faces south -- how full the moon is, the weather and how hard it is
 * coming down, and what the door is dressed for. Not set, it is a clear
 * night, as it always was. */
enum desk_weather {
	DESK_WX_CLEAR = 0, DESK_WX_CLOUDS, DESK_WX_OVERCAST, DESK_WX_FOG,
	DESK_WX_RAIN, DESK_WX_SNOW, DESK_WX_STORM, DESK_WX_N
};
enum { DESK_DECOR_XMAS = 1, DESK_DECOR_NEWYEAR = 2, DESK_DECOR_BIRTHDAY = 4 };
struct desk_door_mood {
	bool set;
	double sun_el, sun_az;
	double moon;         /* 0 new .. 1 full */
	int weather;         /* DESK_WX_* */
	double heavy;        /* 0..1: drizzle to a downpour, flurries to a blizzard */
	unsigned decor;      /* DESK_DECOR_* */
	int year;            /* the year it is, where it is written */
};

/* The screen lock: the front door of the house the desk is in, seen from
 * its step. Who lives here (name, and picture or NULL), how many letters of
 * the password are in (typed) and whether the caret after them is lit
 * (caret), whether it is being checked (checking; now_s, monotonic seconds,
 * runs the light along the slot), how far through being told no (fail,
 * 0..1; 0 for not), Caps Lock on. Where you stand, each 0..1: a step back
 * from the door (back: 1 as you walk up to it), leaning in to the key
 * (lean), and through the doorway (push); the key turned (turn: 1 is a
 * quarter turn, less is trying it) and the door swung in (open); steady
 * takes the bob out of walking (the camera_bounce setting). inside is
 * the room as it is seen from inside (a picture the size of the screen),
 * which is what fills the screen at push 1. */
struct desk_lock_look {
	const char *name;
	cairo_surface_t *picture;
	int typed;
	bool caret, checking, caps;
	double fail, back, lean, turn, open, push;
	double steady;      /* 0: the head bobs as it walks; 1: not at all */
	cairo_surface_t *inside;
	int inside_view;    /* which of the room's views inside is: DESK_VIEW_SHELF or _CONSOLE */
	double now_s;
	struct desk_door_mood mood;
};
void desk_draw_lock(cairo_t *cr, int w, int h, const struct desk_lock_look *look);
/* What moves out at the door -- rain, snow, lightning, the lights of a
 * garland, balloons' strings -- drawn on a sheet of its own over the door
 * (transparent elsewhere), as often as it is to move; and whether there is
 * any of it for a mood. */
void desk_draw_lock_fx(cairo_t *cr, int w, int h, const struct desk_lock_look *look);
bool desk_lock_fx_wanted(const struct desk_door_mood *m);
/* let go of the kept pictures of the door (a new size, or unlocked) */
void desk_lock_forget(void);
/* whether x, y (in a w x h screen) is on the key, standing at the door */
bool desk_lock_key_at(int w, int h, double x, double y);
/* Whether a fine picture of where the eye has stopped is still being
 * traced (on a thread of its own): while it is, draw the door again each
 * frame, and it will be put in when it is done. settle waits for it. */
bool desk_lock_settling(void);
void desk_lock_settle(void);

/* What lies over the baked desk, one sprite at a time, so the compositor
 * can keep each and redraw one only when what it shows has changed. Each
 * is drawn in screen coordinates as it sits with the drawer shut; key
 * changes exactly when drawing it again would come out different. The
 * ones on_desk move with the desk when the drawer does; the rest stay put.
 * The shaded ones go under the shade the open drawer throws over the desk
 * -- a flat colour, desk_desk_shade's, over everything above clip. */
enum desk_ovl {
	DESK_OVL_CLOCK = 0,   /* the tray clock: a new face every minute */
	DESK_OVL_ALLAPPS,     /* the plate on the screen's edge */
	DESK_OVL_CONSOLE,     /* the console's nameplate and lenses */
	DESK_OVL_POST,        /* the post box's plate, and a letter */
	DESK_OVL_SLIPS,       /* the tray's slips, under the Control Panel */
	DESK_OVL_COUNT,
};
uint64_t desk_overlay_key(int which, time_t now, int unread);
void desk_draw_overlay(cairo_t *cr, int which, int w, int h, time_t now, int unread);
bool desk_overlay_on_desk(int which);
bool desk_overlay_shaded(int which);
/* Where on the screen an overlay element can draw, when that is known
 * without drawing it (x, y, w, h); false to have it found by looking. */
bool desk_overlay_box(int which, int w, int h, double box[4]);

/* ---- the clock on the back of the desk ----------------------------
 *
 * A red LED display, and beside it a block of four keys, two by two:
 *
 *      | display        | CLOCK | DAY   |
 *      |                | MEDIA | POWER |
 *
 * that light as a hand comes near. What it shows is the compositor's to
 * say; draw.c draws it and says what is under a pointer. */
enum desk_clock_mode {
	DESK_CLOCK_TIME = 0,   /* the time, the volume, the input method; the
	                          * battery and the screen's light, where there are */
	DESK_CLOCK_POWER,      /* the battery; off the wall, what the machine draws */
	DESK_CLOCK_MEDIA,      /* what is playing, its transport, and the volume */
	DESK_CLOCK_DAY,        /* the date, where the time was */
};
/* the keys, row by row from the top left */
enum {
	DESK_CLOCK_KEY_CLOCK = 0,
	DESK_CLOCK_KEY_DAY,
	DESK_CLOCK_KEY_MEDIA,
	DESK_CLOCK_KEY_POWER,
	DESK_CLOCK_KEYS,
};
/* the page a key turns up; CLOCK's is the time, which is also where the
 * others go back to when pressed again */
static inline int desk_clock_key_mode(int key)
{
	static const int MODE[DESK_CLOCK_KEYS] = { DESK_CLOCK_TIME, DESK_CLOCK_DAY,
		DESK_CLOCK_MEDIA, DESK_CLOCK_POWER };
	return key >= 0 && key < DESK_CLOCK_KEYS ? MODE[key] : DESK_CLOCK_TIME;
}
struct desk_clock_state {
	int mode;
	double wake;                          /* 0..1: a hand is near, the keys are lit */
	double glow[DESK_CLOCK_KEYS];       /* 0..1: each key's own light */
	int pressed;                          /* the key held down, or -1 */
	bool have_volume;
	double volume;                        /* 0..1 */
	bool muted;
	char ime[16];                         /* the input method's mark, or "" */
	int camera;                           /* -1 no camera, 0 not in use, 1 in use */
	int mic;                              /* and the same for a microphone */
	int show;                             /* a day being kept (DESK_SHOW_*), or 0 */
	double show_t;                        /* how far into it, seconds */
	int year;                             /* the year it is, to say so */
	struct desk_supply supply;
	double avg_w;                         /* since DEsk started; < 0 unknown */
	double hour_wh;                       /* in the last hour; < 0 unknown */
	double hour_span;                     /* how much of that hour there is, 0..1 */
	struct desk_media media;            /* what is playing */
	int media_hot;                        /* the MEDIA button under the hand, or -1 */
	int media_down;                       /* and the one held down, or -1 */
	double now;                           /* monotonic seconds, for what runs on its own */
	/* the room being made at login: how far, 0..1 (< 0: the clock is a
	 * clock), and what is being made -- 0 the desk, 1 the bookcase, 2 the
	 * console, 3 the post box, 4 the lamp coming the last of the way up,
	 * 5 ready */
	double boot;
	int boot_stage;
};
/* What the clock does on a day kept: says so, and the rest of its show */
enum { DESK_SHOW_NONE = 0, DESK_SHOW_BIRTHDAY, DESK_SHOW_XMAS, DESK_SHOW_NEWYEAR,
	DESK_SHOW_MIDNIGHT };
#define DESK_SHOW_BEAT 0.60   /* the birthday's metronome, seconds a beat */
void desk_clock_set(const struct desk_clock_state *st);
/* Changes exactly when the face would come out different. */
uint64_t desk_clock_key(time_t now);
/* In how many milliseconds the face will come out different of itself --
 * a title running along the display, a track's time -- or -1 for never. */
int desk_clock_next_ms(void);
struct desk_clock_pick {
	bool near;            /* within reach: the keys wake */
	bool on;              /* on the face itself */
	int key;              /* the key under the pointer, or -1 */
	bool bar;             /* MEDIA: on the volume, at bar_value */
	double bar_value;
	bool speaker;         /* MEDIA: on the speaker, which mutes */
	int button;           /* MEDIA: a DESK_MEDIA_* under the pointer, or -1 */
};
bool desk_clock_pick(int w, int h, double px, double py, double drawer_open,
	struct desk_clock_pick *out);
/* The bar's value for a pointer anywhere, for a drag that strays off it. */
bool desk_clock_bar_value(int w, int h, double px, double py, double drawer_open,
	double *value);

/* ---- the tray: slips tucked under the Control Panel ----------------
 *
 * Each thing a program has put in the tray is a slip of paper about the
 * size of a sticky note, tucked in under the front of the Control Panel's
 * plinth past the end of its nameplate, with the program's icon printed on
 * the end that shows. Pulled out, a slip is stuck to the end of a card
 * with the program's menu on it. What there is is the compositor's to say;
 * draw.c draws them and says what is under a pointer, in screen pixels
 * with the drawer where it is. */
#define DESK_SLIPS_MAX 16
struct desk_slip {
	cairo_surface_t *icon;   /* NULL: its first letter */
	char title[96];
	unsigned seed;           /* how it happens to lie, and the paper it is */
	bool attention;          /* pushed out further, and marked */
	bool passive;            /* pushed further in */
	double lift;             /* 0..1: slid out a little, under the pointer */
};
struct desk_slips_state {
	int n;
	struct desk_slip slips[DESK_SLIPS_MAX];
	int hot;                 /* on top of the others, or -1 */
	int pulled;              /* its card is out, and it with it; or -1 */
	double scroll;           /* more than there is room for: how many have
	                          * been slid along under the nameplate */
};
/* What to show. The icons are referenced, not taken. */
void desk_slips_set(const struct desk_slips_state *st);
uint64_t desk_slips_key(void);
/* The slip under a point, or -1. */
int desk_slip_at(int w, int h, double px, double py, double drawer_open);
/* How many lie side by side, whole, between the nameplate and the end of
 * the machine; past that the row is slid along. 0 before there is a room. */
int desk_slips_fit(int w, int h);

#define DESK_CARD_MAX 64
struct desk_card_entry {
	int32_t id;
	char label[128];
	bool enabled, separator, submenu;
	int toggle;              /* 0 none, 1 a check box, 2 a radio button */
	int state;               /* 1 on, 0 off, -1 neither */
	cairo_surface_t *icon;   /* referenced, not taken */
};
struct desk_card {
	int slip;                /* the slip it is the end of */
	char title[96];          /* at its head */
	bool back;               /* a submenu's: its head goes back up a level */
	bool loading;
	int n;
	struct desk_card_entry e[DESK_CARD_MAX];
	int hot;                 /* the entry under the pointer; -2 the head; -1 none */
	int top;                 /* the first entry shown */
};
struct desk_card_place {
	double x, y, w, h;       /* the card, pulled all the way out */
	double travel;           /* how far up under the machine it lies before that */
	double tail;             /* how far the slip on its end can stick out below it */
	int rows;                /* how many entries there is room for */
	double head_h, row_h;
};
/* Where the card goes, and whether there is anywhere for it at all. */
bool desk_card_place(int w, int h, double drawer_open, const struct desk_card *c,
	struct desk_card_place *pl);
/* What it covers pulled out as far as `pull`: 0 under the machine, 1 out. */
void desk_card_box(const struct desk_card_place *pl, double pull, double box[4]);
void desk_draw_card(cairo_t *cr, int w, int h, double drawer_open,
	const struct desk_card *c, const struct desk_card_place *pl, double pull);
/* What is under a point on it, out: an entry, -2 its head, -1 the card but
 * nothing on it, -3 not the card. */
int desk_card_hit(const struct desk_card *c, const struct desk_card_place *pl,
	double px, double py);
/* Where the desk is placed for a drawer position, rounded as the
 * compositor rounds it, and the line the drawer's shade stops at. False
 * until there is a room. */
bool desk_desk_place(int w, int h, double open, int *ox, int *oy, double *clip);
/* The shade's colour, and its alpha for a drawer position: 0 for none. */
double desk_desk_shade(double open, float rgb[3]);

/* desk-gputest --room: every kind of sheet the room is baked into, traced
 * on the GPU and by the C tracer at w x h, compared sample by sample and
 * pixel by pixel, and written into dir as PNGs when dir is not NULL. True
 * when the GPU path is there and agrees. */
bool desk_room_selftest(int w, int h, const char *dir);

#endif
