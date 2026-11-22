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
/* The same for the Control Panel's view: the serial of the room its set is
 * traced in, 0 until that has landed. What is drawn of the set is drawn for
 * this room, so it is drawn again when this changes (DaeTheDruid, #58). */
unsigned desk_console_room(int w, int h);
/* and for any view: the bookcase and the post box are drawn from a stand-in
 * room until theirs lands for a new shape of screen, so they note it too */
unsigned desk_view_room(int w, int h, int view);
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
/* what the day puts on the bookcase's lower board (DESK_DECOR_HALLOWEEN or
 * 0), from then on in every picture of the case */
void desk_shelf_dress(unsigned decor);
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
	/* (2026-11-10) the screens put to sleep, beside LOCK; the machine put to
	 * sleep, under the lid with the three; the four on the lid -- airplane
	 * mode, power saving, the night's warm screen, how the screens are used
	 * -- and the lid itself */
	DESK_CC_SLEEP,
	DESK_CC_PW_SUSPEND,
	DESK_CC_AIR,
	DESK_CC_SAVE,
	DESK_CC_NIGHT,
	DESK_CC_DISPLAY,
	DESK_CC_LID,
	DESK_CC_COUNT,
};

/* How the screens are used, as DISPLAY and Super+P go round them (Windows' Project): the desk
 * going on onto the others, the desk's own on all of them, the desk's only, the others only;
 * and what else the tube may say: there is only one, or none of those would do. */
enum {
	DESK_SCREENS_EXTEND = 0,
	DESK_SCREENS_MIRROR,
	DESK_SCREENS_THIS,
	DESK_SCREENS_OTHER,
	DESK_SCREENS_MODES,
	DESK_SCREENS_ONE = DESK_SCREENS_MODES,
	DESK_SCREENS_REFUSED,
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
	double guard;       /* 0 the lid shut .. 1 swung up */
	int pressed;        /* DESK_CC_PW_* once one is pushed, else -1 */
	double dying;       /* 0 .. 1 the set going out after the push */
	double lock_push;   /* 0 .. 1 the LOCK button pushed in */
	double antenna;     /* the wireless aerial: 0 down .. 1 all the way up */
	/* a button that does a thing and comes back up -- SLEEP, SUSPEND, the
	 * four on the lid -- and how far down it is, 0 .. 1 */
	int push_which;
	double push;
	/* what the four on the lid have on, and whether this machine can */
	bool air_on, save_on, night_on;
	bool save_ok, night_ok;
	/* DISPLAY: how the screens are used (DESK_SCREENS_*), whether there is more than one;
	 * and what the tube says after a press (DESK_SCREENS_*), and how much, 0 .. 1 */
	int screens_mode;
	bool screens_ok;
	int screens_said;
	double screens_word;
};
void desk_draw_console(cairo_t *cr, int w, int h,
	const struct desk_console_state *st, int hover);
/* The traced sheet on its own, which never changes: put up once, and the
 * instruments drawn over it as they change. */
void desk_draw_console_base(cairo_t *cr, int w, int h);
int desk_console_hit(int w, int h, double px, double py);
/* The same with the lid as open as that: up, what is under it; on its way,
 * nothing where it is. */
int desk_console_hit_lid(int w, int h, double px, double py, double open);
/* The lid over the guarded set, as it swings (up, and on its way, it is
 * drawn rather than baked), and whether what is under it has been traced
 * yet -- until it has, the lid stays down. */
bool desk_console_guard_hit(int w, int h, double px, double py, double open);
/* A point down the middle of the lid on the screen, opened that far: along 0 at the hinge, 1 the
 * middle of its free edge. What a finger lifting it holds. */
bool desk_console_lid_point(int w, int h, double open, double along, double *x, double *y);
bool desk_console_lid_ready(int w, int h);
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
 * the row of lenses and the lid as it swings (with what is under it). advance moves the tube's trace on and
 * goes once per frame, before any piece is drawn. key changes exactly when
 * drawing the piece again would come out different. box is where the
 * piece lands on screen; false means it has to be found by drawing it. */
enum {
	DESK_CCP_PLATE = DESK_CC_COUNT,
	DESK_CCP_LENSES,
	DESK_CCP_GUARD,
	DESK_CCP_ANTENNA,
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
	bool flag_hot;      /* the hand on the place for the favourites' flag */
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
/* Is that point on a sheet (desk_binder_local) where its favourites' flag
 * is, or would be stuck? */
bool desk_binder_flag_hit(int w, int h, double lx, double ly);
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
/* A finger round the binder's ring: its angle, as the sheets swing (one turned over goes round
 * -1.05), and how far out; false with no binder to measure. */
bool desk_binder_finger_at(int w, int h, double px, double py, double *angle, double *r);
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
	bool hover_flag;   /* the hand on hover's favourites' flag, in the margin */
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
	DESK_BOOK_HIT_FLAG,     /* *arg: the line whose favourites' flag it is */
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
 * ringed in pencil, or for what the desk is made of a fan of samples of it --
 * and other programs to open, each with what it means
 * under it; the row under the hand and the word in it; how far the sheet is
 * slid up; where it is kept. */
enum { DESK_FORM_SECTION, DESK_FORM_CHECK, DESK_FORM_CHOICE, DESK_FORM_DATE,
	DESK_FORM_ACTION, DESK_FORM_SIGN };
/* A date is laid out as a choice's words are: back a month, the month,
 * on a month; back a day, the day, on a day; and none.
 * An action opens another program. "Open ›" is written after its label in
 * pencil (options[0]); with nothing to open it (value 0) the row is printed
 * faint, and what is written there instead is options[1] -- "not installed
 * -- e.g. a package".
 * A signature is a line to sign an order on (the fans of samples on the sheet
 * are only ticked until it is): value 0 nothing to sign, 1 something ticked
 * and "Sign here ›" (options[1]) pencilled at the line's end, 2 signed, the
 * signature (options[0], a name) written along it in pencil -- as far as
 * sign_t, while it is being written. */
/* the most words a choice has, its NULL included */
#define DESK_FORM_OPTS 32
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
	int swatch;                  /* a choice of what the desk is made of (DESK_SWATCH_*):
	                              * a fan of samples of it, the chosen one taken out of
	                              * it, its name on a brass plate -- not words */
	int swatch_with;             /* and the other of the two, its samples are in: the
	                              * stain the woods are under, the wood the stains are on */
};
enum { DESK_SWATCH_NONE, DESK_SWATCH_WOOD, DESK_SWATCH_STAIN };
struct desk_shell_form {
	const struct desk_form_row *rows;
	int n;
	int hover, hover_opt;        /* -1: none */
	double scroll;
	const char *kept_in;
	bool on_paper;               /* printed over what is there (a sheet of its own) rather
	                              * than on a clear surface over the shell's paper */
	/* a sample just chosen from a fan (DESK_SWATCH_*): its row, the one chosen
	 * before it, and how far it has come out of the fan and that one gone back
	 * in (0 < fan_t < 1; anything else, all where they lie) */
	int fan_row, fan_was;
	double fan_t;
	double sign_t;               /* a signature being written (0 < sign_t < 1); else all of it */
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
/* A sheet gliding onto a half of the screen or off it (wm.c) is drawn again
 * every frame at every size on the way: while w, h is held, the age of its
 * paper is worked out once, at that size -- where it is going -- and laid
 * on stretched to the size it has got to. 0, 0 lets go -- for the next
 * sheet drawn, which is not gliding. (This thread.) */
void desk_paper_age_hold(int w, int h);

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
/* The same for a notebook (notebook.h), which shuts instead. */
/* Folded by a hand coming down it: the top part comes down first, and what is under stays where
 * it was. How far the end of that first flap goes; how far through (t) it is, so much of the way
 * there (0 .. 1); and how far down the sheet's top the stack's is, at t. */
struct desk_fold *desk_fold_new_by_hand(cairo_surface_t *face, unsigned seed, double packet_w,
	cairo_surface_t *icon, const char *name);
double desk_fold_hand_reach(const struct desk_fold *f);
double desk_fold_hand_t(const struct desk_fold *f, double frac);
int desk_fold_hand_y(const struct desk_fold *f, double t);
struct desk_fold *desk_fold_new_book(cairo_surface_t *face, unsigned seed, double packet_w,
	const char *name);
void desk_fold_free(struct desk_fold *f);
void desk_fold_extent(struct desk_fold *f, double t, int *w, int *h, int *margin);
void desk_fold_draw(cairo_t *cr, struct desk_fold *f, double t);
/* Its folds: how many, which is going over at t (stages when it is done),
 * and how much of t they take between them, in equal shares from 0. */
int desk_fold_stages(const struct desk_fold *f);
int desk_fold_stage(const struct desk_fold *f, double t);
double desk_fold_done(const struct desk_fold *f);
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
 * card with a program's mark on it, a sheet folded away, or Print's camera
 * (its picture from art/camera, lying there); or a picture a program has been
 * given to look like (looks.h). */
enum { DESK_LOOK_CARD = DESK_DOCK_COUNT, DESK_LOOK_PACKET, DESK_LOOK_CAMERA, DESK_LOOK_THING,
	DESK_LOOK_NOTEBOOK, DESK_LOOK_PICTURE };
struct desk_drawer_item {
	int look;
	cairo_surface_t *icon;     /* a card: the program's icon, or NULL */
	cairo_surface_t *packet;   /* a packet: the folded sheet as it lies */
	char name[128];            /* printed under a card's mark; chalked under the rest */
	char initial[8];           /* a card with no icon: its letter */
	unsigned seed;             /* how it happens to lie */
	/* a thing kept here (thing.h): its picture lying there, ppu pixels to the room's
	 * unit, turned so, and its middle in the picture; and how thick it lies */
	cairo_surface_t *(*lying)(double ppu, double turn, double *ax, double *ay);
	double thick;
	/* a picture to look like, seen from above, its longest side along the room's units */
	cairo_surface_t *picture;
	double along;
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
/* The card of looks (wm.c, looks.h): n of them to choose from, each as it would lie in the
 * drawer and what it is called, in cols columns, pt pixels to a point; and with more, one more
 * after them, so called, to choose a picture from anywhere with. Its size and which a point on it
 * is on (-1 none), for cells of them, the one more counted in; and it, the program's name at its
 * head, the one chosen ringed, the one under the hand lifted, and a word at its foot. */
void desk_looks_card_size(int cells, int cols, double pt, int *w, int *h);
int desk_looks_card_hit(int cells, int cols, double pt, double x, double y);
void desk_looks_card_paint(cairo_t *cr, const struct desk_drawer_item *items, const char *const *names,
	int n, const char *more, int cols, double pt, const char *title, const char *hint, int chosen, int hover);
/* Alt+Tab (see draw.c): the label on each window spread out, its program's
 * mark (icon, or initial when there is none) and name printed on it; in
 * points (pt pixels each), no wider than max_w; one picture, *w by *h, the
 * label itself pad in from each side of it. */
void desk_switcher_tag_size(const char *name, double pt, double max_w, int *w, int *h,
	int *pad);
void desk_draw_switcher_tag(cairo_t *cr, cairo_surface_t *icon, const char *initial,
	const char *name, double pt, double max_w, unsigned seed);
/* Print, looked at through a camera (wm.c, snip_*): the viewfinder's
 * display, in the dark under the picture, as a reflex camera's is -- green
 * figures and marks, lit only. At its left, what the dial is set to (its
 * picture, as on the dial), the focus light, lit when the shutter would
 * take something (ready), and how big that is, w by h pixels; at its
 * right, the self-timer when it is set (its mark, and its seconds), and
 * the frame the film is at -- or, a tape in the camera, the tape's mark
 * and how long it has run, the red dot before it lit once it is running.
 * As wide as the picture (width pixels): one picture, width by *bh. In
 * points (pt pixels each). And the focusing screen's circle for the
 * middle of what is lit, size square. */
struct desk_finder_line {
	int mode;        /* the dial: 0 an area, 1 a window, 2 a screen */
	bool ready;
	int w, h;
	int timer;       /* the self-timer's seconds; 0, off */
	int frame;
	bool tape;       /* a tape in: its time, not the film's frame */
	bool rec;        /* and running */
	int secs;
};
int desk_finder_line_height(double pt);
void desk_draw_finder_line(cairo_t *cr, int width, const struct desk_finder_line *l, double pt);
int desk_finder_ring_size(double pt);
void desk_draw_finder_ring(cairo_t *cr, double pt);
/* And the shade round the picture, where the light on it falls off: one
 * part of it, as deep as it gets (the node's opacity says how deep that
 * is) a penumbra -- n pixels -- out from the lit area: over its top edge
 * (part 0), right of it (1), under it (2), left of it (3), round its
 * corners (4 top left, 5 top right, 6 bottom right, 7 bottom left). Into
 * cr's own picture: 1 wide and n deep for 0 and 2, n wide and 1 high for
 * 1 and 3 (each to be stretched along its edge), n by n for the corners. */
void desk_draw_finder_penumbra(cairo_t *cr, int n, int part);
/* The glass round the picture, as its edge catches the light: across a
 * band n pixels deep, as for the penumbra (part 0 over the top edge .. 3
 * left of it, 4..7 its corners, rounded off), hardly a breath of white,
 * a bright line at the glass's outer edge, and a fine dark one where it
 * meets the picture. */
void desk_draw_finder_bevel(cairo_t *cr, int n, int part);
/* And the lamp's light on it, a glint: soft, long along the edge it lies
 * on and thin across it, filling cr's own picture, w by h (along x if w
 * is the longer, along y if h is). */
void desk_draw_finder_glint(cairo_t *cr, int w, int h);
/* And the viewfinder's edge, as thick glass: the picture of a screen, src
 * (sw by sh, ARGB32, its rows sstride apart), bent in towards the edge of
 * the window -- a rounded rectangle the size of the screen, its corners r
 * round -- over a band b wide, the colours parted a little, lit from
 * above; past the corners, the eyecup's black; transparent inside the
 * band. Into dst (dw by dh, ARGB32, rows dstride apart): the part of the
 * screen from (dx, dy). Touches nothing else: it may be run on any
 * thread. */
void desk_draw_finder_rim(const unsigned char *src, int sw, int sh, int sstride,
	unsigned char *dst, int dx, int dy, int dw, int dh, int dstride, double b, double r);
/* And the self-timer counting down beside what it is to take: its lamp,
 * lit or not as it blinks, and the seconds left; one picture, *bw by *bh. */
void desk_finder_count_size(double pt, int *bw, int *bh);
void desk_draw_finder_count(cairo_t *cr, int left, bool lit, double pt);
/* The camera Print puts up to the eye (wm.c, snip_*): a reflex camera seen
 * from behind and a little above, as it is held -- rendered in Blender
 * (art/camera), laid together here -- in its own units,
 * DESK_FINDER_CAMERA_W by DESK_FINDER_CAMERA_H, drawn s pixels to the unit
 * -- with what is set on it. Its picture is read the first time it is
 * wanted; desk_finder_camera_load reads it before that (on any thread, as
 * many times as it likes: once), and desk_finder_camera_preload has it
 * read on a thread of its own at the back of the queue. */
void desk_finder_camera_load(void);
void desk_finder_camera_preload(void);
#define DESK_FINDER_CAMERA_W 600
#define DESK_FINDER_CAMERA_H 480
#define DESK_FINDER_TAPE_REACH 0.45
struct desk_finder_camera {
	double dial;    /* the mode dial: 0 an area, 1 a window, 2 a screen (between: turning) */
	double timer;   /* the self-timer lever: 0 off, 1 two seconds, 2 ten (between: moving) */
	double power;   /* the power lever round the shutter button: 0 off (O), 1 on (I) */
	double press;   /* the shutter button, how far down (0..1) */
	double wind;    /* the advance lever, how far swung out (0..1) */
	bool lamp;      /* the self-timer's lamp, lit */
	int count;      /* the frame counter */
	double tape;    /* the tape: 0 none, 1 in its slot (between: brought up to it and
	                 * pushed in, or coming out and taken away -- at the slot's mouth
	                 * at DESK_FINDER_TAPE_REACH) */
	bool see_through;  /* the eyepiece's glass left clear, for what is behind to show */
};
void desk_draw_finder_camera(cairo_t *cr, double s, const struct desk_finder_camera *c);
/* What a hand at (x, y), in the camera's units, would take hold of. */
enum desk_finder_part {
	DESK_FINDER_NOTHING = 0,
	DESK_FINDER_BODY,      /* the camera, but nothing on it to work */
	DESK_FINDER_DIAL,
	DESK_FINDER_TIMER,
	DESK_FINDER_POWER,
	DESK_FINDER_SHUTTER,
	DESK_FINDER_TAPE,      /* the tape's slot, the tape in it, its eject mark */
};
enum desk_finder_part desk_finder_camera_at(double x, double y);
/* The eyepiece's glass, in the camera's units: where the view through it
 * opens out from as it comes up to the eye. */
void desk_finder_camera_eyepiece(double *x, double *y, double *w, double *h);
/* And the middle of the mode dial, likewise: where its clicks are heard. */
void desk_finder_camera_dial(double *x, double *y);
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
/* What is made ahead from now on is shared out to half the machine's threads (true), or all of
 * them (false): calm once the lamp is up and nobody is waiting. */
void desk_bake_calm(bool calm);
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
/* How dark the binders' labels are typed, 0 the tired ribbon they were
 * designed with .. 1 as dark as it goes (shell.conf binder_ink). True if
 * that is a change, and what was drawn with the labels is to be again. */
bool desk_set_binder_ink(double depth);
double desk_ui_scale(void);
/* what a point is on a screen this wide, for things drawn flat over the
 * desk (less than the scale where the screen is too narrow for the desk) */
double desk_px_per_pt(int screen_w);

/* Where a picture of DEsk's is, of a kind (share/desk/<kind>, or art/<kind> by a
 * program not yet installed); false, and path the first place looked, when it
 * is not there. */
bool desk_art_find(const char *kind, const char *file, char *path, size_t n);
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
/* Closed eyes opening, over everything: the lids, open 0 (shut) to 1 (wide,
 * nothing of them left), parted from a line across the middle -- the upper
 * one arched more than the lower, the edges soft, and warm where the light
 * comes through them -- in screen coordinates (cr may be scaled down: it is
 * all soft). */
void desk_draw_lids(cairo_t *cr, int w, int h, double open);
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
enum {
	DESK_DECOR_XMAS = 1, DESK_DECOR_NEWYEAR = 2, DESK_DECOR_BIRTHDAY = 4, DESK_DECOR_HALLOWEEN = 8
};
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
	/* the one move under way, if it is one of those filmed ahead
	 * (desk_lock_films), and how many seconds into it: set by desk_lock_pose */
	int move;
	double move_t;
};
void desk_draw_lock(cairo_t *cr, int w, int h, const struct desk_lock_look *look);

/* How the door's moves go, in seconds from when each begins: the walk up
 * to it; Return, leaning in to the key and trying it (DESK_LOCK_TRY of a
 * quarter turn); told no, the red slot for DESK_LOCK_FAIL_S; let in, the
 * key round, leaning back out, the door swinging in and through it; and
 * locking from in the room, the same backwards. */
#define DESK_LOCK_WALK_S 1.10
#define DESK_LOCK_FAIL_S 1.60
#define DESK_LOCK_LEAN_S 0.45
#define DESK_LOCK_TRY 0.10
#define DESK_LOCK_TRY_AT (DESK_LOCK_LEAN_S * 0.75)   /* the key begins to go round */
#define DESK_LOCK_TRY_S 0.22                         /* and is as far as it goes */
#define DESK_LOCK_TURN_S 0.32
#define DESK_LOCK_BACK0 0.30
#define DESK_LOCK_BACK1 0.95
#define DESK_LOCK_SWING0 0.40
#define DESK_LOCK_SWING1 1.50
#define DESK_LOCK_PUSH0 0.95
#define DESK_LOCK_PUSH1 2.55
#define DESK_LOCK_OUT_WALK 1.55
#define DESK_LOCK_OUT_SHUT0 1.00
#define DESK_LOCK_OUT_SHUT1 2.00
#define DESK_LOCK_OUT_LEAN 1.90
#define DESK_LOCK_OUT_TURN 2.40
#define DESK_LOCK_OUT_BACK 2.80
#define DESK_LOCK_OUT_END 3.45

/* The moves filmed ahead of being made (desk_lock_films). */
enum desk_lock_move {
	DESK_LOCK_MOVE_NONE = 0,
	DESK_LOCK_MOVE_WALK,   /* up to the door from a step away */
	DESK_LOCK_MOVE_LEAN,   /* Return: leaning in to the key, and trying it */
	DESK_LOCK_MOVE_FAIL,   /* told no: the key back, and leaning back out */
	DESK_LOCK_MOVE_OPEN,   /* let in: the key round, the door in, and through it */
	DESK_LOCK_MOVE_OUT,    /* out of the room, the door shut, the key back round */
	DESK_LOCK_MOVES
};

/* Where each of the door's moves has got to, in seconds since it began,
 * as the compositor keeps them; lean0 is how far in Return found you
 * leaning, and inside whether there is a room to go through into. */
struct desk_lock_times {
	bool walking, checking, opening, failing, leaving, inside;
	double walk_t, check_t, lean0, open_t, fail_t, leave_t;
};
/* where that puts the eye, the key and the door (back, lean, turn, open,
 * push, fail of look), and which move it is, if one alone (move, move_t) */
void desk_lock_pose(const struct desk_lock_times *tm, struct desk_lock_look *look);

/* The moves to have filmed ahead, on a thread at the back of the queue,
 * for a w x h screen and the door as look has it -- whose, the weather,
 * the bounce, and for OPEN and OUT the room inside (inside_view, and
 * inside, its picture as it is now, or NULL); moves a mask of
 * 1 << DESK_LOCK_MOVE_*, and 0 to let every film go. Kept, they are what
 * desk_draw_lock shows for those moves. Returns the moves asked for that
 * want the room's picture to be filmed and were not given it. */
unsigned desk_lock_films(int w, int h, const struct desk_lock_look *look, unsigned moves);
/* waits until whatever is being filmed ahead is made (desk-preview) */
void desk_lock_films_settle(void);
/* whether look, on a w x h screen, is drawn from a film: a frame of one is
 * no finer than it was made, and can be drawn smaller and laid larger */
bool desk_lock_filmed(int w, int h, const struct desk_lock_look *look);
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
	DESK_OVL_MAT = 0,     /* the desk mat, under everything else on the desk; under a
	                       * clear one, what is under it */
	DESK_OVL_MAT_SHEET,   /* the clear mat itself, over what is under it */
	DESK_OVL_CASES,       /* the things on the desk, rendered: under all the rest */
	DESK_OVL_CLOCK,       /* the tray clock: a new face every minute */
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

/* ---- the desk mat ----------------------------------------------------
 *
 * A mat laid on the desk under the windows: a plain colour with the grain
 * of felt, or a photograph the user has chosen. It is drawn over the baked
 * room, not traced into it, so changing it costs a redraw and never a new
 * room (shell.conf desk_mat, desk_mat_photo, desk_mat_size).
 *
 * New looks go in as a new kind, or as another preset in the table in
 * draw.c; nothing else has to know. */
enum desk_mat_kind {
	DESK_MAT_NONE = 0,
	DESK_MAT_COLOR,    /* rgb, with felt over it */
	DESK_MAT_LEATHER,  /* rgb, a pebbled leather, tooled round in gold */
	DESK_MAT_LINOLEUM, /* rgb, marbled linoleum, its edge cut square */
	DESK_MAT_PHOTO,    /* a picture, cut to fill it */
	DESK_MAT_CLEAR,    /* a clear sheet over all the open desk, the boards seen through it,
	                    * and what is slipped under it (under) */
};
/* What lies under a clear mat. */
enum desk_mat_under {
	DESK_MAT_UNDER_FREE = 0,   /* prints, each where it was laid (the papers, below) */
	DESK_MAT_UNDER_WHOLE,      /* one picture, photo, over all of it */
};
/* How big: 0, the least, to 1, all the open desk. The words are four places
 * on that, and the sizes between them are in points (draw.c mat_rect). */
#define DESK_MAT_SMALL 0.15
#define DESK_MAT_MEDIUM 0.50
#define DESK_MAT_LARGE 0.80
#define DESK_MAT_FULL 1.00
struct desk_mat {
	int kind;          /* desk_mat_kind */
	float rgb[3];      /* DESK_MAT_COLOR; the cloth a photo that cannot be read falls back to */
	char photo[512];   /* DESK_MAT_PHOTO, and a clear mat's whole picture: the picture's path */
	double size;       /* 0 .. 1, as above; a clear mat is always all the open desk */
	int under;         /* DESK_MAT_CLEAR: desk_mat_under */
};
/* The preset named (green, navy, burgundy, charcoal, cream; leather,
 * linoleum) or a colour written #rrggbb, as a mat: false for anything else.
 * Leaves size alone. */
bool desk_mat_preset(const char *name, struct desk_mat *out);
/* A size as shell.conf writes it: small, medium, large, full, or a
 * percentage (60%). False for anything else. */
bool desk_mat_size_parse(const char *s, double *out);
/* What the mat is now. True when that is a change, in which case what lies
 * over the desk wants drawing again. A photo is read on a thread of its
 * own (a PNG; or by gdk-pixbuf's thumbnailer or ImageMagick, which are not
 * linked, only run; a file that is not a picture of a sane size is not
 * read at all); until it lands the mat is
 * the plain cloth, and desk_overlay_key changes when it does. */
bool desk_set_mat(const struct desk_mat *m);
/* How far it lies unrolled, 0 (rolled up at the back of the desk) to 1
 * (flat, as it was): it is laid by unrolling it toward you, and taken away
 * by rolling it up (wm.c). Only those that roll do (desk_mat_rolls): felt,
 * leather, linoleum, a photo; not a clear sheet. Part of the mat's key. */
void desk_mat_unroll(double t);
bool desk_mat_rolls(const struct desk_mat *m);

/* The prints under a clear mat, laid where you like (DESK_MAT_UNDER_FREE).
 * A print is a picture of one's own, made by desk-mat into the prints'
 * folder ($XDG_DATA_HOME/desk/mat) as a PNG small enough to read here,
 * so that what was chosen can go from where it was and the print stays.
 * Where each lies is kept in $XDG_CONFIG_HOME/desk/desk-mat, the bottom one
 * first:
 *
 *   paper = NAME X Y WIDTH TURN
 *
 * its middle X, Y points from the middle of the mat (right, and down the
 * screen), WIDTH points across, turned TURN degrees clockwise. The desk
 * writes it as they are moved, and reads it again when another (desk-mat)
 * has. */
#define DESK_MAT_PAPERS_MAX 40
#define DESK_MAT_PAPER_MIN_PT 60.0
#define DESK_MAT_PAPER_MAX_PT 900.0
struct desk_mat_paper {
	char name[96];     /* the print's file, in the prints' folder */
	double x, y;       /* its middle from the mat's, in points */
	double w;          /* how wide, in points: as tall as the print is, for that */
	double turn;       /* degrees, clockwise */
	int pw, ph;        /* the print's own size, in pixels (read from it) */
};
/* The prints' folder, and the file of where they lie. */
void desk_mat_prints_dir(char *out, size_t n);
void desk_mat_papers_file(char *out, size_t n);
/* The papers file read again if it has changed since (or first): true when
 * that is a change. Polled, as shell.conf is. */
bool desk_mat_papers_poll(void);
/* The papers as they lie, bottom first: how many. */
int desk_mat_papers(struct desk_mat_paper *out, int max);
/* They lie like this now, and are written to the file (save) as they do. */
void desk_mat_papers_set(const struct desk_mat_paper *p, int n, bool save);
/* Where a paper is on a w x h screen: its middle, and its half-width and
 * half-height across and down it before it is turned, in pixels. False if
 * there is no such paper. */
bool desk_mat_paper_place(int w, int h, const struct desk_mat_paper *p, double *cx, double *cy,
	double *hw, double *hh);
/* The topmost paper at (x, y) on a w x h screen, or -1. */
int desk_mat_paper_at(int w, int h, double x, double y);
/* The mat as it lies on a w x h screen (x, y, w, h): false if there is none. */
bool desk_mat_rect(int w, int h, double r[4]);
/* What (x, y) on a w x h screen lies on: a desk_mat_kind, DESK_MAT_NONE
 * for the bare desk (a sheet put down there sounds of it, wm.c). */
int desk_mat_at(int w, int h, double x, double y);
/* Is the mat a clear one? And what is under it (desk_mat_under). */
bool desk_mat_clear(int *under);
/* The clear mat turned up at a corner -- 0 its back left, 1 back right,
 * 2 front right, 3 front left; -1 lying flat -- by cut points along each
 * edge from it, and lifted off what is under it, to move that, or only
 * turned up under the hand; and the one paper taken up in the hand, which
 * is left out of the mat's picture (-1, none). A change of any is a change
 * of the key. */
void desk_mat_lift(int corner, double cut, bool lifted, int held);
/* The corner of the clear mat (as desk_mat_lift has them) within reach
 * points of (x, y) on a w x h screen, or -1. */
int desk_mat_corner_at(int w, int h, double x, double y, double reach);
/* The corner turned over cut points along each edge, held up off the
 * sheet, and its shadow (folded); or, not folded, only raised off the desk,
 * cut points at its tip, as it is under a hand resting on it: a picture of
 * it, and where its top left goes on the screen. */
cairo_surface_t *desk_mat_curl_picture(int w, int h, int corner, double cut, bool folded, int *ox,
	int *oy);
/* The envelope of photographs that lies by the mat lifted at corner, to
 * take more from or put one back in: where it is on the screen (x, y, w,
 * h), and a picture of it, open with the hand on it, and where that goes. */
bool desk_mat_envelope_box(int w, int h, int corner, double cut, double box[4]);
cairo_surface_t *desk_mat_envelope_picture(int w, int h, int corner, double cut, bool open,
	int *ox, int *oy);
/* One paper by itself as it lies, lit where it lies, lift points off the
 * desk (its shadow longer): a picture of it and where its top left goes on
 * the screen. NULL while its print is not there yet. */
cairo_surface_t *desk_mat_paper_picture(int w, int h, const struct desk_mat_paper *p, double lift,
	int *ox, int *oy);
/* The prints read now, on this thread, for a screen w across: for a picture
 * of the desk taken once (desk-preview), never in the compositor. */
void desk_mat_settle(int w);

/* ---- what the desk is made of -----------------------------------
 *
 * The desk's top, its sides and back and its drawer are of a timber, and the
 * wood is under a stain (shell.conf desk_wood, desk_stain); leather and
 * linoleum lie on it as mats (desk_mat). They are traced
 * into the room, so another is a room traced again; walnut as it was made,
 * unless another is chosen. The bookcase and the console are walnut
 * whatever the desk is: they are other furniture. */
enum desk_wood {
	DESK_WOOD_WALNUT = 0,
	DESK_WOOD_OAK,
	DESK_WOOD_CHERRY,
	DESK_WOOD_MAPLE,
	DESK_WOOD_EBONY,      /* black, the grain showing through */
	DESK_WOOD_COUNT,
};
enum desk_stain {
	DESK_STAIN_NATURAL = 0,
	DESK_STAIN_HONEY,
	DESK_STAIN_BROWN,
	DESK_STAIN_DARK,
	DESK_STAIN_GREY,
	DESK_STAIN_COUNT,
};
/* As shell.conf writes them: false for anything else. */
bool desk_wood_parse(const char *s, int *wood);
bool desk_stain_parse(const char *s, int *stain);
/* Is the stain made for the wood? Ebony is not stained brown or dark
 * brown: on black they are one colour, and nearly its own. A stain not made
 * for a wood is taken as natural on it (desk_set_wood, desk_wood_sample). */
bool desk_stain_offered(int wood, int stain);
/* The desk made of this now: true when that is a change, and the room is
 * to be traced again for it (desk_room_forget). */
bool desk_set_wood(int wood, int stain);
/* A sample of it, as for a fan of them (Shell Settings): a strip of the
 * top, w by h pixels, its grain along its length, lit and toned as the desk
 * is in the room. A new reference to a picture kept for the next asking. */
cairo_surface_t *desk_wood_sample(int wood, int stain, int w, int h);

/* ---- the clock on the back of the desk ----------------------------
 *
 * A red LED display, and beside it a block of four keys, two by two:
 *
 *      | display        | CLOCK | DAY   |
 *      |                | MEDIA | PC    |
 *
 * that light as a hand comes near. What it shows is the compositor's to
 * say; draw.c draws it and says what is under a pointer. */
enum desk_clock_mode {
	DESK_CLOCK_TIME = 0,   /* the time, the volume, the input method; the
	                          * battery and the screen's light, where there are */
	DESK_CLOCK_PC,         /* the machine: its battery, what it draws, how busy
	                          * its processor is, its memory, its line */
	DESK_CLOCK_MEDIA,      /* what is playing, its transport, and the volume */
	DESK_CLOCK_DAY,        /* the date, where the time was */
};
/* the keys, row by row from the top left */
enum {
	DESK_CLOCK_KEY_CLOCK = 0,
	DESK_CLOCK_KEY_DAY,
	DESK_CLOCK_KEY_MEDIA,
	DESK_CLOCK_KEY_PC,
	DESK_CLOCK_KEYS,
};
/* the page a key turns up; CLOCK's is the time, which is also where the
 * others go back to when pressed again */
static inline int desk_clock_key_mode(int key)
{
	static const int MODE[DESK_CLOCK_KEYS] = { DESK_CLOCK_TIME, DESK_CLOCK_DAY,
		DESK_CLOCK_MEDIA, DESK_CLOCK_PC };
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
	int rec;                              /* Print's camera recording: how long it has
	                                       * run, seconds (-1: it is not) */
	int show;                             /* a day being kept (DESK_SHOW_*), or 0 */
	double show_t;                        /* how far into it, seconds */
	int year;                             /* the year it is, to say so */
	struct desk_supply supply;
	double avg_w;                         /* since DEsk started; < 0 unknown */
	double hour_wh;                       /* in the last hour; < 0 unknown */
	struct desk_meters pc;                /* the PC page: the processor, the memory, the
	                                       * line, as last read (a second at a time) */
	bool pc_rates;                        /* and read twice: there are rates to show */
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
/* desk-gputest --tour: the room traced from a camera a line of path, a PNG a frame into dir
 * (the line's form is at desk_room_tour) */
bool desk_room_tour(const char *path, const char *dir, int w, int h);

/* desk-gputest --faces: a trial of the bookcase's view laid from pictures
 * of its faces, against the same view traced, at w x h; the pictures into
 * dir when it is not NULL. */
bool desk_room_faces_test(int w, int h, const char *dir);

/* desk-gputest --faces-bake: a view's faces (layer 3 the bookcase, 4 the
 * console) pictured for every room shape they are for, at w wide, into the
 * file out. */
bool desk_room_faces_bake(const char *out, int w, int layer);

#endif
