/* ================================================================
 * The keyboard: for a desk with no keyboard on it, a typewriter. It comes up over the front of the
 * desk when there is something to type in and only fingers to do it with, or out of the drawer
 * when it is asked for, and lies over everything, the door too, where there is a password to type.
 *
 * It is a keyboard as far as everything else goes (desk_onscreen_keyboard): its keys are real
 * keys, in the keymap of the machine, and go where keys go, through the input method too. What
 * keys it has and what is printed on them are read from that keymap. Its keys are hit by each
 * finger for itself, down at once as a finger lands and typed as it lifts, so that a finger
 * can slide to the key it meant; a second finger coming down types the first; a finger held
 * presses the key for as long as it is held. Shift and the others stay down for the next key,
 * and tapped twice stay down until tapped again, with a light on them.
 *
 * It is taken hold of by its case: it goes where the hand goes, and let go of it slides, or if it
 * is going down, or most of the way off the front of the desk, it goes.
 *
 * And it can be written on. Thrown over to the pen, by its lever or its key, or brought up by a pen
 * in the first place, the machine goes down until only its top row shows and the sheet in it comes
 * up, ruled in squares. What is written in a square is read when the next is begun, or a moment
 * after the pen has stopped (handwriting.h), and the machine types it, where the keys would have
 * gone. What else it might have been is typed along the top of the sheet, to be taken instead.
 * ================================================================ */

#include "thing.h"
#include "desk.h"
#include "i18n.h"
#include "handwriting.h"
#include "keyboardart.h"
#include "keyboard-map.h"
#include "touch.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

extern const struct desk_thing desk_thing_keyboard;

enum { KB_AWAY, KB_READING, KB_COMING, KB_ON, KB_GOING };
enum { ACT_NONE, ACT_PUT_AWAY, ACT_LANGUAGE, ACT_THROW_OVER };
enum { MOD_OFF, MOD_ONCE, MOD_STAYS };
enum { MODE_KEYS, MODE_PEN };

#define KEYS_MOST 96
#define TOUCHES (DESK_FINGERS + 1)
#define DESK_KB_MOUSE (-2)
#define NOT_A_KEY (-1)
#define ON_CASE (-2)
#define ON_PAPER (-3)
#define ON_LEVER (-4)

/* How long a finger is on a key before it presses it, not just taps it, in ms */
#define HELD_MS 320
/* Tapped again this soon, a modifier stays down */
#define TWICE_SEC 0.45
/* Gone from typing this long before it goes itself */
#define NOT_WANTED_MS 600

struct kb_key {
	uint16_t code;
	int act;
	bool sticky;              /* Shift, Ctrl and the like */
	enum desk_kb_kind kind;
	int row;
	int x, y, w, h;           /* in its picture: where it is, in its row */
	struct desk_kb_print print;
	int mod;                  /* MOD_*: how it stays down */
	double mod_at;
	bool typed_while_down;
	bool shown_down;
	bool drawn_alone;         /* no other key was down when down was drawn */
	struct wlr_scene_buffer *down;
};

struct kb_touch {
	bool on;
	int32_t id;
	int at_first;             /* ON_PAPER, ON_LEVER: what it came down on, if not a key */
	int key;                  /* the key under it, or NOT_A_KEY */
	bool pressed;             /* what is on it has been typed */
	bool held;                /* and is still down */
	double at;
};

static struct {
	int where;
	bool made;
	bool auto_shown, wanted, by_pen;
	bool injecting;
	struct wlr_keyboard *kb;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *pic, *rail, *paper, *bend, *bail;
	cairo_surface_t *bare;    /* the case with nothing in its well: what a key goes down into */
	int mode;
	/* The sheet in it: all its picture is paper_all high, and as much as is wanted is out of the
	 * machine, its foot at paper_foot behind the plate. */
	int paper_x, paper_w, paper_all, paper_h, paper_foot;
	double paper_out;
	struct kb_key keys[KEYS_MOST];
	int n;
	double scale;             /* of what was rendered */
	int w, h, body_x, body_y, body_w, body_h;
	int rows_y[6], rows_h[6];
	double rows_x0[6], rows_u[6];
	int screen_foot;
	/* where its top left is, on the desk's layout; where it lies by itself */
	double x, y, vx, vy, at;
	double home_x, home_y;
	bool springing;
	struct kb_touch touch[TOUCHES];
	/* taken hold of by its case */
	bool carried;
	int32_t carry_id;
	double carry_dx, carry_dy, carry_vx, carry_vy, carry_at;
	struct wl_event_source *held_timer, *off_timer;
	int layout_seen;
} k;

/* ---- what is written on the sheet -------------------------------------- */

#define PAD_POINTS 1400
#define PAD_STROKES 60
#define PAD_GUESSES 9
#define PAD_LINE 96
/* Written, and the pen stopped: this long before it is read and typed, in ms */
#define PAD_READ_MS 1100

static struct {
	/* in the sheet's picture, from its top left */
	int line_y, line_h, slips_y, slips_h, slip_w, boxes_y, box_w, box_h, boxes, box_x;
	bool squares;             /* a square for a character; or a letter's narrower place, with its lines */
	int at;                   /* the box being written in, or -1 */
	float xy[PAD_POINTS * 2];
	float press[PAD_STROKES]; /* how hard, on the whole */
	int ends[PAD_STROKES];    /* where each stroke ends */
	int strokes, points;
	int32_t writer;
	bool writing;
	bool ink_changed, slips_changed;
	struct desk_hw_guess guess[PAD_GUESSES];
	int guesses;
	bool typed_last;          /* the guesses are for what has been typed, and one of them can replace it */
	bool typing;              /* the keys going now are its own */
	char line[PAD_LINE * 4];  /* what it has typed, along the top of the sheet */
	struct wlr_scene_buffer *ink, *slips;
	struct wl_event_source *read_timer;
} pad = { .at = -1 };

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint32_t now_ms(void)
{
	return (uint32_t)(now_sec() * 1000.0);
}

static double clampd(double v, double lo, double hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* ---- what keys it has ------------------------------------------------- */

struct cell {
	uint16_t code;
	float w;
	enum desk_kb_kind kind;
	const char *word;
	int act;
	bool sticky;
};

#define LETTER(c) { c, 1.0f, DESK_KB_LETTER, NULL, ACT_NONE, false }
#define DARK(c, w, word) { c, w, DESK_KB_FUNCTION, word, ACT_NONE, false }
#define MOD(c, w, word) { c, w, DESK_KB_FUNCTION, word, ACT_NONE, true }
#define SLIMK(c, word) { c, 1.0f, DESK_KB_SLIM, word, ACT_NONE, false }
#define END { 0, 0.0f, DESK_KB_LETTER, NULL, ACT_NONE, false }
#define SPACE_BAR(wd) { KEY_SPACE, wd, DESK_KB_SPACE, NULL, ACT_NONE, false }

static const struct cell ROW_TOP[] = {
	SLIMK(KEY_ESC, "ESC"), SLIMK(KEY_F1, "F1"), SLIMK(KEY_F2, "F2"), SLIMK(KEY_F3, "F3"),
	SLIMK(KEY_F4, "F4"), SLIMK(KEY_F5, "F5"), SLIMK(KEY_F6, "F6"), SLIMK(KEY_F7, "F7"),
	SLIMK(KEY_F8, "F8"), SLIMK(KEY_F9, "F9"), SLIMK(KEY_F10, "F10"), SLIMK(KEY_F11, "F11"),
	SLIMK(KEY_F12, "F12"), SLIMK(KEY_DELETE, "DEL"),
	{ 0, 1.0f, DESK_KB_SLIM, NULL, ACT_PUT_AWAY, false }, END };

#define FIGURES LETTER(KEY_1), LETTER(KEY_2), LETTER(KEY_3), LETTER(KEY_4), LETTER(KEY_5), \
	LETTER(KEY_6), LETTER(KEY_7), LETTER(KEY_8), LETTER(KEY_9), LETTER(KEY_0), \
	LETTER(KEY_MINUS), LETTER(KEY_EQUAL)
#define QWERTY LETTER(KEY_Q), LETTER(KEY_W), LETTER(KEY_E), LETTER(KEY_R), LETTER(KEY_T), \
	LETTER(KEY_Y), LETTER(KEY_U), LETTER(KEY_I), LETTER(KEY_O), LETTER(KEY_P), \
	LETTER(KEY_LEFTBRACE), LETTER(KEY_RIGHTBRACE)
#define ASDF LETTER(KEY_A), LETTER(KEY_S), LETTER(KEY_D), LETTER(KEY_F), LETTER(KEY_G), \
	LETTER(KEY_H), LETTER(KEY_J), LETTER(KEY_K), LETTER(KEY_L), LETTER(KEY_SEMICOLON), \
	LETTER(KEY_APOSTROPHE)
#define ZXCV LETTER(KEY_Z), LETTER(KEY_X), LETTER(KEY_C), LETTER(KEY_V), LETTER(KEY_B), \
	LETTER(KEY_N), LETTER(KEY_M), LETTER(KEY_COMMA), LETTER(KEY_DOT), LETTER(KEY_SLASH)
#define THROW_OVER { 0, 1.0f, DESK_KB_FUNCTION, NULL, ACT_THROW_OVER, false }
#define ARROWS DARK(KEY_LEFT, 1.0f, "\xe2\x86\x90"), DARK(KEY_UP, 1.0f, "\xe2\x86\x91"), \
	DARK(KEY_DOWN, 1.0f, "\xe2\x86\x93"), DARK(KEY_RIGHT, 1.0f, "\xe2\x86\x92")
#define TAB_ROW DARK(KEY_TAB, 1.5f, "TAB"), QWERTY, LETTER_WIDE(KEY_BACKSLASH, 1.5f), END
#define LETTER_WIDE(c, wd) { c, wd, DESK_KB_LETTER, NULL, ACT_NONE, false }
#define CAPS_ROW DARK(KEY_CAPSLOCK, 1.75f, "SHIFT LOCK"), ASDF, \
	{ KEY_ENTER, 2.25f, DESK_KB_RETURN, "RETURN", ACT_NONE, false }, END
#define LANGUAGE { 0, 1.0f, DESK_KB_FUNCTION, NULL, ACT_LANGUAGE, false }

/* The commonest, and what most of them come to */
/* The top row, with the sheet up: all that shows of the machine. */
static const struct cell ROW_PEN[] = {
	SLIMK(KEY_ESC, "ESC"), SLIMK(KEY_TAB, "TAB"),
	SLIMK(KEY_LEFT, "\xe2\x86\x90"), SLIMK(KEY_RIGHT, "\xe2\x86\x92"),
	{ KEY_SPACE, 4.0f, DESK_KB_SLIM, "SPACE", ACT_NONE, false },
	{ KEY_BACKSPACE, 2.0f, DESK_KB_SLIM, "BACK", ACT_NONE, false },
	{ KEY_ENTER, 2.0f, DESK_KB_SLIM, "RETURN", ACT_NONE, false },
	{ 0, 2.0f, DESK_KB_SLIM, NULL, ACT_THROW_OVER, false },
	{ 0, 1.0f, DESK_KB_SLIM, NULL, ACT_PUT_AWAY, false }, END };

static const struct cell ANSI[5][18] = {
	{ LETTER(KEY_GRAVE), FIGURES, DARK(KEY_BACKSPACE, 2.0f, "BACK"), END },
	{ TAB_ROW },
	{ CAPS_ROW },
	{ MOD(KEY_LEFTSHIFT, 2.25f, "SHIFT"), ZXCV, MOD(KEY_RIGHTSHIFT, 2.75f, "SHIFT"), END },
	{ MOD(KEY_LEFTCTRL, 1.0f, "CTRL"), MOD(KEY_LEFTMETA, 1.0f, "SUPER"), LANGUAGE, THROW_OVER,
		MOD(KEY_LEFTALT, 1.0f, "ALT"), SPACE_BAR(4.75f),
		MOD(KEY_RIGHTALT, 1.25f, "ALT"), ARROWS, END },
};

/* With the key beside the left Shift that a European keyboard has */
static const struct cell ISO_SHIFT_ROW[] = {
	MOD(KEY_LEFTSHIFT, 1.25f, "SHIFT"), LETTER(KEY_102ND), ZXCV,
	MOD(KEY_RIGHTSHIFT, 2.75f, "SHIFT"), END };

/* A Japanese keyboard's */
static const struct cell JIS[5][18] = {
	{ LETTER(KEY_GRAVE), FIGURES, LETTER(KEY_YEN), DARK(KEY_BACKSPACE, 1.0f, "BACK"), END },
	{ TAB_ROW },
	{ CAPS_ROW },
	{ MOD(KEY_LEFTSHIFT, 2.25f, "SHIFT"), ZXCV, LETTER(KEY_RO), MOD(KEY_RIGHTSHIFT, 1.75f, "SHIFT"), END },
	{ MOD(KEY_LEFTCTRL, 1.0f, "CTRL"), MOD(KEY_LEFTMETA, 1.0f, "SUPER"), MOD(KEY_LEFTALT, 1.0f, "ALT"),
		DARK(KEY_MUHENKAN, 1.0f, "\xe7\x84\xa1\xe5\xa4\x89\xe6\x8f\x9b"), THROW_OVER, SPACE_BAR(3.0f),
		DARK(KEY_HENKAN, 1.0f, "\xe5\xa4\x89\xe6\x8f\x9b"),
		DARK(KEY_KATAKANAHIRAGANA, 1.0f, "\xe3\x81\x8b\xe3\x81\xaa"),
		MOD(KEY_RIGHTALT, 1.0f, "ALT"), ARROWS, END },
};

/* A Korean keyboard's last row */
static const struct cell KOREAN_LAST[] = {
	MOD(KEY_LEFTCTRL, 1.0f, "CTRL"), MOD(KEY_LEFTMETA, 1.0f, "SUPER"), MOD(KEY_LEFTALT, 1.0f, "ALT"),
	DARK(KEY_HANJA, 1.0f, "\xed\x95\x9c\xec\x9e\x90"), THROW_OVER, SPACE_BAR(3.75f),
	DARK(KEY_HANGEUL, 1.0f, "\xed\x95\x9c/\xec\x98\x81"), MOD(KEY_RIGHTALT, 1.25f, "ALT"), ARROWS, END };

/* What a keysym prints as: the dead keys print what they put on a letter. */
static void sym_print(xkb_keysym_t sym, char *out, size_t n)
{
	out[0] = '\0';
	const char *dead = NULL;
	switch (sym) {
	case XKB_KEY_dead_acute: dead = "\xc2\xb4"; break;
	case XKB_KEY_dead_grave: dead = "`"; break;
	case XKB_KEY_dead_circumflex: dead = "^"; break;
	case XKB_KEY_dead_tilde: dead = "~"; break;
	case XKB_KEY_dead_diaeresis: dead = "\xc2\xa8"; break;
	case XKB_KEY_dead_cedilla: dead = "\xc2\xb8"; break;
	default: break;
	}
	if (dead) {
		snprintf(out, n, "%s", dead);
		return;
	}
	if (sym == XKB_KEY_NoSymbol || sym < 0x20) {
		return;
	}
	char u[16];
	int len = xkb_keysym_to_utf8(sym, u, sizeof u);
	/* (a control character, or nothing, or something no key has room for) */
	if (len > 1 && (unsigned char)u[0] >= 0x20 && (size_t)len <= n) {
		memcpy(out, u, (size_t)len);
	}
}

static xkb_keysym_t level_sym(struct xkb_keymap *km, xkb_layout_index_t lay, uint32_t code, int level)
{
	const xkb_keysym_t *syms;
	int n = xkb_keymap_key_get_syms_by_level(km, code + 8, lay, (xkb_level_index_t)level, &syms);
	return n > 0 ? syms[0] : XKB_KEY_NoSymbol;
}

/* Printed as a keyboard is printed: a letter once, big; anything else what Shift gives over what
 * it types; and to the side what AltGr gives, if it is something else. */
static void key_print(struct kb_key *key, const struct cell *c, struct xkb_keymap *km, xkb_layout_index_t lay)
{
	struct desk_kb_print *p = &key->print;
	memset(p, 0, sizeof *p);
	if (c->act == ACT_PUT_AWAY) {
		p->icon = DESK_KB_ICON_PUT_AWAY;
		return;
	}
	if (c->act == ACT_LANGUAGE) {
		p->icon = DESK_KB_ICON_GLOBE;
		return;
	}
	if (c->act == ACT_THROW_OVER) {
		p->icon = k.mode == MODE_PEN ? DESK_KB_ICON_KEYS : DESK_KB_ICON_PEN;
		return;
	}
	if (c->word) {
		p->word = c->word;
		if (c->code == KEY_RIGHTALT && level_sym(km, lay, c->code, 0) == XKB_KEY_ISO_Level3_Shift) {
			p->word = "ALT GR";
		}
		return;
	}
	if (c->code == KEY_SPACE) {
		return;
	}
	xkb_keysym_t base = level_sym(km, lay, c->code, 0), up = level_sym(km, lay, c->code, 1);
	xkb_keysym_t third = level_sym(km, lay, c->code, 2);
	if (base == XKB_KEY_Zenkaku_Hankaku || base == XKB_KEY_Hankaku || up == XKB_KEY_Kanji) {
		p->word = "\xe5\x8d\x8a/\xe5\x85\xa8";
		return;
	}
	char b[8], s[8], t[8];
	sym_print(base, b, sizeof b);
	sym_print(up, s, sizeof s);
	sym_print(third, t, sizeof t);
	if (xkb_keysym_to_upper(base) == up && up != base) {
		snprintf(p->base, sizeof p->base, "%s", s);
	} else if (s[0] && strcmp(b, s)) {
		snprintf(p->base, sizeof p->base, "%s", b);
		snprintf(p->shifted, sizeof p->shifted, "%s", s);
	} else {
		snprintf(p->base, sizeof p->base, "%s", b);
	}
	if (t[0] && strcmp(t, b) && strcmp(t, s)) {
		snprintf(p->third, sizeof p->third, "%s", t);
	}
}

static int add_row(int r, const struct cell *cells, xkb_layout_index_t lay, struct xkb_keymap *km)
{
	double x = 0.0;
	for (; cells->w > 0.0f && k.n < KEYS_MOST; cells++) {
		struct kb_key *key = &k.keys[k.n++];
		memset(key, 0, sizeof *key);
		key->code = cells->code;
		key->act = cells->act;
		key->sticky = cells->sticky;
		key->kind = cells->kind;
		key->row = r;
		key->x = (int)lround(k.rows_x0[r] + x * k.rows_u[r]);
		key->w = (int)lround(k.rows_x0[r] + (x + cells->w) * k.rows_u[r]) - key->x;
		key->y = k.rows_y[r];
		key->h = k.rows_h[r];
		key_print(key, cells, km, lay);
		x += cells->w;
	}
	return 0;
}

/* The keys of the keyboard in the keymap as it is now. */
static void keys_lay_out(void)
{
	struct xkb_keymap *km = k.kb->keymap;
	xkb_layout_index_t lay = k.kb->modifiers.group;
	if (km && lay >= xkb_keymap_num_layouts(km)) {
		lay = 0;
	}
	k.layout_seen = (int)lay;
	const char *name = km ? xkb_keymap_layout_get_name(km, lay) : NULL;
	bool japanese = name && strstr(name, "Japanese");
	bool korean = name && strstr(name, "Korean");
	bool american = !name || !strncmp(name, "English (US", 11) || strstr(name, "Chinese") || korean;
	for (int i = 0; i < k.n; i++) {
		if (k.keys[i].down) {
			wlr_scene_node_destroy(&k.keys[i].down->node);
			k.keys[i].down = NULL;
		}
		k.keys[i].shown_down = false;
	}
	k.n = 0;
	add_row(0, k.mode == MODE_PEN ? ROW_PEN : ROW_TOP, lay, km);
	const struct cell (*rows)[18] = japanese ? JIS : ANSI;
	for (int r = 0; r < 5; r++) {
		const struct cell *row = rows[r];
		if (r == 3 && !japanese && !american) {
			row = ISO_SHIFT_ROW;
		}
		if (r == 4 && korean) {
			row = KOREAN_LAST;
		}
		add_row(r + 1, row, lay, km);
	}
}

/* ---- its picture ------------------------------------------------------ */

static void kb_measure(struct desk_server *server)
{
	/* (on the screen the fingers are on) */
	struct wlr_output *out = desk_finger_screen(server);
	struct wlr_box box = { 0 };
	if (out) {
		wlr_output_layout_get_box(desk_layout(server), out, &box);
	}
	double pt = desk_ui_scale();
	/* about half the screen's height at most, all its width if it can have that; and no great
	 * size of key on a great big screen */
	k.scale = desk_kb_art_fit(box.width - 6.0 * pt, box.height * 0.54, 66.0 * pt);
	desk_kb_art_measure(k.scale, &k.w, &k.h, &k.body_x, &k.body_y, &k.body_w, &k.body_h);
	for (int r = 0; r < 6; r++) {
		desk_kb_art_row(k.scale, r, &k.rows_x0[r], &k.rows_u[r], &k.rows_y[r], &k.rows_h[r]);
	}
	k.screen_foot = box.y + box.height;
	k.home_x = box.x + (box.width - k.w) * 0.5;
	/* its front at the edge of the desk; or with the sheet up, off that, as far as its top row */
	k.home_y = k.screen_foot - (k.mode == MODE_PEN ? k.rows_y[1] + (int)lround(2.0 * pt) : desk_kb_art_shows(k.scale));
	/* the sheet, and where things are on it */
	k.paper_foot = desk_kb_art_paper_foot(k.scale);
	k.paper_w = desk_kb_art_paper_w(k.scale);
	k.paper_x = (k.w - k.paper_w) / 2;
	int round_h = k.paper_foot - desk_kb_art_paper_round(k.scale);
	pad.box_h = (int)lround(clampd(box.height * 0.13, 84.0 * pt, 140.0 * pt));
	pad.box_w = pad.squares ? pad.box_h : (int)lround(pad.box_h * 0.70);
	pad.line_y = (int)lround(9.0 * pt);
	pad.line_h = (int)lround(24.0 * pt);
	pad.slips_y = pad.line_y + pad.line_h + (int)lround(3.0 * pt);
	pad.slips_h = (int)lround(38.0 * pt);
	pad.slip_w = (int)lround(42.0 * pt);
	pad.boxes_y = pad.slips_y + pad.slips_h + (int)lround(7.0 * pt);
	int side = (int)lround(12.0 * pt);
	pad.boxes = (k.paper_w - side * 2) / pad.box_w;
	pad.boxes = pad.boxes > 32 ? 32 : pad.boxes < 1 ? 1 : pad.boxes;
	pad.box_x = (k.paper_w - pad.boxes * pad.box_w) / 2;
	k.paper_all = pad.boxes_y + pad.box_h + (int)lround(12.0 * pt) + round_h;
	k.paper_h = k.mode == MODE_PEN ? k.paper_all : round_h + (int)lround(30.0 * pt);
}

static bool kb_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	return desk_kb_art_on_body(k.scale, *sx, *sy);
}

static bool paper_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	double m = 7.0 * desk_ui_scale();
	return *sx >= m && *sy >= 0.0 && *sx < m + k.paper_w;
}

/* What is ruled on the sheet, with the machine thrown over to the pen. In the blue a form is printed
 * in. */
static void pad_rule(cairo_t *cr)
{
	double pt = desk_ui_scale();
	double w = pad.boxes * pad.box_w, h = pad.box_h;
	cairo_save(cr);
	cairo_translate(cr, pad.box_x, pad.boxes_y);
	cairo_set_source_rgba(cr, 0.20, 0.36, 0.52, 0.62);
	cairo_set_line_width(cr, fmax(1.0, 1.0 * pt));
	cairo_rectangle(cr, 0.5, 0.5, w - 1.0, h - 1.0);
	cairo_stroke(cr);
	for (int i = 1; i < pad.boxes; i++) {
		cairo_move_to(cr, floor(i * pad.box_w) + 0.5, 0.0);
		cairo_line_to(cr, floor(i * pad.box_w) + 0.5, h);
	}
	cairo_stroke(cr);
	static const double DASH[] = { 3.0, 4.0 };
	cairo_set_dash(cr, DASH, 2, 0.0);
	cairo_set_source_rgba(cr, 0.20, 0.36, 0.52, 0.28);
	if (pad.squares) {
		/* its middle, each way, to keep a character in proportion */
		cairo_move_to(cr, 0.0, floor(h * 0.5) + 0.5);
		cairo_line_to(cr, w, floor(h * 0.5) + 0.5);
		for (int i = 0; i < pad.boxes; i++) {
			cairo_move_to(cr, floor((i + 0.5) * pad.box_w) + 0.5, 0.0);
			cairo_line_to(cr, floor((i + 0.5) * pad.box_w) + 0.5, h);
		}
	} else {
		/* the top of a small letter, and their feet */
		cairo_move_to(cr, 0.0, floor(h * 0.40) + 0.5);
		cairo_line_to(cr, w, floor(h * 0.40) + 0.5);
		cairo_stroke(cr);
		cairo_set_dash(cr, NULL, 0, 0.0);
		cairo_set_source_rgba(cr, 0.20, 0.36, 0.52, 0.42);
		cairo_move_to(cr, 0.0, floor(h * 0.80) + 0.5);
		cairo_line_to(cr, w, floor(h * 0.80) + 0.5);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A sheet of paper in the machine, up out of the platen under the bail, with its top at the top of the
 * picture: as much of it shows as has come out. */
static void kb_paper(void)
{
	double pt = desk_ui_scale();
	int m = (int)ceil(7.0 * pt);
	int w = k.paper_w, h = k.paper_all;
	struct desk_buf *b = buf_create(w + 2 * m, h + m);
	cairo_t *cr = b->cr;
	/* its shadow on what is behind it */
	for (int i = m; i > 0; i--) {
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.035);
		cairo_rectangle(cr, m - i, m - i, w + 2 * i, h + i);
		cairo_fill(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, m, m);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	desk_draw_pad_paper(cr);
	if (k.mode == MODE_PEN) {
		pad_rule(cr);
	}
	cairo_restore(cr);
	cairo_surface_flush(b->surface);
	scene_put(&k.paper, k.tree, b, k.paper_x - m, 0, NULL);
	k.paper->point_accepts_input = paper_accepts;
	/* bending away out of the light as it goes round the platen, with the shadow of the bail on it,
	 * the same however much has come out */
	int round_h = k.paper_foot - desk_kb_art_paper_round(k.scale) + (int)lround(2.0 * pt);
	struct desk_buf *over = buf_create(w, round_h);
	cairo_pattern_t *bend = cairo_pattern_create_linear(0, 0, 0, round_h);
	cairo_pattern_add_color_stop_rgba(bend, 0.0, 0.0, 0.0, 0.0, 0.0);
	cairo_pattern_add_color_stop_rgba(bend, 0.55, 0.05, 0.03, 0.0, 0.06);
	cairo_pattern_add_color_stop_rgba(bend, 1.0, 0.05, 0.03, 0.0, 0.52);
	cairo_set_source(over->cr, bend);
	cairo_paint(over->cr);
	cairo_pattern_destroy(bend);
	double bail = round_h - desk_kb_art_bail_over_foot(k.scale);
	cairo_pattern_t *under = cairo_pattern_create_linear(0, bail - 2.0 * pt, 0, bail + 7.0 * pt);
	cairo_pattern_add_color_stop_rgba(under, 0.0, 0.0, 0.0, 0.0, 0.0);
	cairo_pattern_add_color_stop_rgba(under, 0.35, 0.0, 0.0, 0.0, 0.34);
	cairo_pattern_add_color_stop_rgba(under, 1.0, 0.0, 0.0, 0.0, 0.0);
	cairo_set_source(over->cr, under);
	cairo_rectangle(over->cr, 0, bail - 2.0 * pt, w, 9.0 * pt);
	cairo_fill(over->cr);
	cairo_pattern_destroy(under);
	cairo_surface_flush(over->surface);
	scene_put(&k.bend, k.tree, over, k.paper_x, k.paper_foot - round_h, NULL);
	k.bend->point_accepts_input = shadow_accepts;
}

/* As much of the sheet as has come out, and what is on it once all of it has. */
static void paper_place(void)
{
	if (!k.paper) {
		return;
	}
	double pt = desk_ui_scale();
	int m = (int)ceil(7.0 * pt);
	int out = (int)lround(clampd(k.paper_out, 1.0, k.paper_all));
	wlr_scene_node_set_position(&k.paper->node, k.paper_x - m, k.paper_foot - out - m);
	struct wlr_fbox box = { 0.0, 0.0, k.paper_w + 2.0 * m, out + m };
	scene_source(k.paper, &box);
	wlr_scene_buffer_set_dest_size(k.paper, k.paper_w + 2 * m, out + m);
	bool all = k.mode == MODE_PEN && out >= k.paper_all - 1;
	if (pad.slips) {
		wlr_scene_node_set_enabled(&pad.slips->node, all);
	}
	if (pad.ink) {
		wlr_scene_node_set_enabled(&pad.ink->node, all && pad.at >= 0);
	}
}

/* What is drawn again to show a key down: all it covers and all that covers it. */
static void key_area(const struct kb_key *key, int *x, int *y, int *w, int *h)
{
	int side, above, below;
	desk_kb_art_key_reach(k.scale, key->row, &side, &above, &below);
	int mid = key->y + key->h / 2;
	*x = key->x - side;
	*w = key->w + side * 2;
	*y = mid - above;
	*h = above + below;
}

static bool keys_overlap(const struct kb_key *a, const struct kb_key *b)
{
	int ax, ay, aw, ah, bx, by, bw, bh;
	key_area(a, &ax, &ay, &aw, &ah);
	key_area(b, &bx, &by, &bw, &bh);
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static bool key_shows_lit(const struct kb_key *key)
{
	return key->sticky || key->code == KEY_CAPSLOCK;
}

static void key_paint(cairo_t *cr, const struct kb_key *key, bool down)
{
	desk_kb_art_paint_key(cr, key->kind, key->row, down, key->x, key->w, k.scale);
	desk_kb_art_paint_print(cr, &key->print, key->kind, key->row, down, key->x, key->w, k.scale,
		down && key_shows_lit(key));
}

static void kb_paint(struct desk_server *server)
{
	(void)server;
	if (k.bare) {
		cairo_surface_destroy(k.bare);
	}
	k.bare = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, k.w, k.h);
	cairo_t *bc = cairo_create(k.bare);
	desk_kb_art_paint_case(bc, k.scale);
	cairo_destroy(bc);
	cairo_surface_flush(k.bare);
	struct desk_buf *b = buf_create(k.w, k.h);
	cairo_set_source_surface(b->cr, k.bare, 0, 0);
	cairo_paint(b->cr);
	/* from the back: a key hides the foot of the stem behind it */
	cairo_save(b->cr);
	desk_kb_art_clip_well(b->cr, k.scale);
	for (int i = 0; i < k.n; i++) {
		key_paint(b->cr, &k.keys[i], false);
	}
	cairo_restore(b->cr);
	cairo_surface_flush(b->surface);
	scene_put(&k.pic, k.tree, b, 0, 0, NULL);
	k.pic->point_accepts_input = kb_accepts;
	wlr_scene_node_lower_to_bottom(&k.pic->node);
	/* a sheet of paper in it */
	kb_paper();
	wlr_scene_node_raise_to_top(&k.paper->node);
	if (pad.slips) {
		wlr_scene_node_raise_to_top(&pad.slips->node);
	}
	if (pad.ink) {
		wlr_scene_node_raise_to_top(&pad.ink->node);
	}
	wlr_scene_node_raise_to_top(&k.bend->node);
	paper_place();
	struct desk_buf *over = buf_create(k.w, desk_kb_art_bail_h(k.scale));
	desk_kb_art_paint_bail(over->cr, k.scale);
	cairo_surface_flush(over->surface);
	scene_put(&k.bail, k.tree, over, 0, 0, NULL);
	k.bail->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&k.bail->node);
}

/* A key seen down, as it is pressed: it goes down behind the key in front of it, so what is drawn
 * is that part of the keyboard over again, the first time. It has to be drawn again if a key it
 * overlaps was down, or is now. */
static void key_draw_down(struct kb_key *key)
{
	int x, y, w, h;
	key_area(key, &x, &y, &w, &h);
	struct desk_buf *b = buf_create(w, h);
	cairo_t *cr = b->cr;
	cairo_translate(cr, -x, -y);
	cairo_set_source_surface(cr, k.bare, 0, 0);
	cairo_paint(cr);
	desk_kb_art_clip_well(cr, k.scale);
	bool alone = true;
	for (int i = 0; i < k.n; i++) {
		struct kb_key *other = &k.keys[i];
		if (other == key) {
			key_paint(cr, key, true);
		} else if (keys_overlap(key, other)) {
			key_paint(cr, other, other->shown_down);
			alone = alone && !other->shown_down;
		}
	}
	cairo_surface_flush(b->surface);
	scene_put(&key->down, k.tree, b, x, y, NULL);
	key->down->point_accepts_input = shadow_accepts;
	key->drawn_alone = alone;
}

static void key_show(struct kb_key *key, bool down)
{
	if (key->shown_down == down) {
		return;
	}
	key->shown_down = down;
	if (!down) {
		if (key->down) {
			wlr_scene_node_set_enabled(&key->down->node, false);
		}
	} else {
		bool neighbours = false;
		for (int i = 0; i < k.n; i++) {
			neighbours = neighbours || (&k.keys[i] != key && k.keys[i].shown_down && keys_overlap(key, &k.keys[i]));
		}
		if (!key->down || neighbours || !key->drawn_alone) {
			key_draw_down(key);
		}
		wlr_scene_node_set_enabled(&key->down->node, true);
	}
	/* those down beside it have it wrong now */
	for (int i = 0; i < k.n; i++) {
		struct kb_key *other = &k.keys[i];
		if (other != key && other->shown_down && keys_overlap(key, other)) {
			key_draw_down(other);
		}
	}
}

static bool caps_on(void)
{
	return k.kb && k.kb->xkb_state
		&& xkb_state_mod_name_is_active(k.kb->xkb_state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_EFFECTIVE) > 0;
}

/* The light on the rail, and the key that says the same. */
static void caps_show(void)
{
	bool on = caps_on();
	if (!k.rail && on && k.tree) {
		struct desk_buf *b = buf_create(k.w, (int)ceil((KB_LIGHT_Y + 40.0) * k.scale));
		desk_kb_art_paint_light(b->cr, 1.0, k.scale);
		cairo_surface_flush(b->surface);
		scene_put(&k.rail, k.tree, b, 0, 0, NULL);
		k.rail->point_accepts_input = shadow_accepts;
	}
	if (k.rail) {
		wlr_scene_node_set_enabled(&k.rail->node, on);
	}
	for (int i = 0; i < k.n; i++) {
		if (k.keys[i].code == KEY_CAPSLOCK) {
			key_show(&k.keys[i], on);
		}
	}
}

/* ---- typing ----------------------------------------------------------- */

static void pad_line_follows(uint16_t code);

/* A character not on the keys, being typed through a key of its own (type_chars). */
#define ODD_STEP_MS 90

static struct {
	uint32_t q[64];
	int n, at;
	int step;                 /* 0 nothing; 1 its keymap given; 2 its key typed; 3 the old one given back */
	struct xkb_keymap *was;
	bool on_x;                /* it went on a spare key of Xwayland's */
	struct desk_server *server;
	struct wl_event_source *timer;
} odd;

static void odd_stop(void);

static void key_send(uint16_t code, bool down)
{
	if (down) {
		pad_line_follows(code);
	}
	/* (a key of the keys, while a character not on them is being typed: that is given up) */
	if (!pad.typing && odd.step) {
		odd_stop();
	}
	struct wlr_keyboard_key_event ev = {
		.time_msec = now_ms(),
		.keycode = code,
		.update_state = true,
		.state = down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
	};
	k.injecting = true;
	wlr_keyboard_notify_key(k.kb, &ev);
	k.injecting = false;
}

/* A key has been typed: what was only down for this one comes up. */
static void key_typed(void)
{
	for (int i = 0; i < k.n; i++) {
		struct kb_key *key = &k.keys[i];
		if (!key->sticky) {
			continue;
		}
		key->typed_while_down = true;
		if (key->mod == MOD_ONCE) {
			bool under_finger = false;
			for (int t = 0; t < TOUCHES; t++) {
				under_finger = under_finger || (k.touch[t].on && k.touch[t].key == i);
			}
			if (!under_finger) {
				key->mod = MOD_OFF;
				key_send(key->code, false);
				key_show(key, false);
			}
		}
	}
}

static struct kb_touch *touch_of(int32_t id)
{
	for (int t = 0; t < TOUCHES; t++) {
		if (k.touch[t].on && k.touch[t].id == id) {
			return &k.touch[t];
		}
	}
	return NULL;
}

/* What is on the key under it is typed, and it has done with it. */
static void touch_types(struct kb_touch *t)
{
	if (t->key < 0 || t->pressed) {
		return;
	}
	struct kb_key *key = &k.keys[t->key];
	if (key->act != ACT_NONE || key->sticky) {
		return;
	}
	key_send(key->code, true);
	key_send(key->code, false);
	t->pressed = true;
	key_typed();
	caps_show();
}

static void held_arm(struct desk_server *server);

static int held_cb(void *data)
{
	struct desk_server *server = data;
	double now = now_sec();
	for (int t = 0; t < TOUCHES; t++) {
		struct kb_touch *tc = &k.touch[t];
		if (tc->on && tc->key >= 0 && !tc->pressed && now - tc->at >= (HELD_MS - 5) / 1000.0) {
			struct kb_key *key = &k.keys[tc->key];
			if (key->act == ACT_NONE && !key->sticky) {
				/* held: pressed, and it goes on until it is lifted */
				key_send(key->code, true);
				tc->pressed = true;
				tc->held = true;
			}
		}
	}
	held_arm(server);
	return 0;
}

static void held_arm(struct desk_server *server)
{
	if (!k.held_timer) {
		k.held_timer = wl_event_loop_add_timer(wl_display_get_event_loop(desk_display(server)),
			held_cb, server);
	}
	double soonest = -1.0, now = now_sec();
	for (int t = 0; t < TOUCHES; t++) {
		struct kb_touch *tc = &k.touch[t];
		if (tc->on && tc->key >= 0 && !tc->pressed) {
			double left = tc->at + HELD_MS / 1000.0 - now;
			soonest = soonest < 0.0 || left < soonest ? left : soonest;
		}
	}
	if (k.held_timer) {
		wl_event_source_timer_update(k.held_timer, soonest < 0.0 ? 0 : (int)fmax(soonest * 1000.0, 1.0));
	}
}

/* Where on it: a key, its case, the sheet, the lever, or not on it. */
static int kb_at(double x, double y)
{
	double lx = x - k.x, ly = y - k.y;
	if (lx >= k.paper_x && lx < k.paper_x + k.paper_w && ly >= k.paper_foot - k.paper_out && ly < k.paper_foot) {
		return k.mode == MODE_PEN ? ON_PAPER : ON_CASE;
	}
	int lx0, ly0, lx1, ly1;
	desk_kb_art_lever(k.scale, &lx0, &ly0, &lx1, &ly1);
	if (lx >= lx0 && lx < lx1 && ly >= ly0 && ly < ly1 && (lx < k.paper_x || ly < k.paper_foot - k.paper_out)) {
		return ON_LEVER;
	}
	if (!desk_kb_art_on_body(k.scale, lx, ly)) {
		return NOT_A_KEY;
	}
	/* a finger on the edge of the well means the key next to it */
	int row = 0;
	for (int r = 0; r < 6; r++) {
		if (ly >= k.rows_y[r]) {
			row = r;
		}
	}
	double m = k.rows_u[row] * 0.15;
	double x0 = k.rows_x0[row], x1 = x0 + k.rows_u[row] * KB_COLS;
	if (ly < k.rows_y[0] - m * 0.4 || lx < x0 - m || lx >= x1 + m || ly >= k.rows_y[5] + k.rows_h[5] + m) {
		return ON_CASE;
	}
	double cx = clampd(lx, x0 + 0.5, x1 - 0.5);
	for (int i = 0; i < k.n; i++) {
		const struct kb_key *key = &k.keys[i];
		if (key->row == row && cx >= key->x && cx < key->x + key->w) {
			return i;
		}
	}
	return ON_CASE;
}

static void kb_place(void)
{
	if (k.tree) {
		wlr_scene_node_set_position(&k.tree->node, (int)lround(k.x), (int)lround(k.y));
	}
}

/* The window with the keys is out from under it, if it lies at the front of the desk. */
static void kb_room(struct desk_server *server)
{
	bool home = k.where == KB_ON || k.where == KB_COMING;
	home = home && !k.carried && fabs(k.y - k.home_y) < k.body_h * 0.5 + k.paper_h;
	desk_keys_room(server, home ? (int)lround(k.home_y) + k.paper_foot - k.paper_h : -1);
}

static void pad_ready(struct desk_server *server);
static void pad_slips_paint(void);
static void pad_forget(void);
static void pad_tick(struct desk_server *server);
static void pad_down(struct desk_server *server, int32_t id, double x, double y);
static void pad_move(struct desk_server *server, int32_t id, double x, double y);
static void pad_up(struct desk_server *server, int32_t id, bool taken_away);
static void pad_read_now(struct desk_server *server);
static void kb_throw_over(struct desk_server *server);

static void kb_appear(struct desk_server *server)
{
	if (!k.tree) {
		k.tree = wlr_scene_tree_create(desk_over_all(server));
	}
	/* by a pen, it comes up to be written on; at the door, there is a password, not for the sheet */
	k.mode = k.by_pen && !desk_door_shut(server) ? MODE_PEN : MODE_KEYS;
	pad_ready(server);
	kb_measure(server);
	keys_lay_out();
	k.paper_out = k.paper_h;
	kb_paint(server);
	pad_slips_paint();
	k.rail = NULL;
	k.x = k.home_x;
	k.y = k.screen_foot + k.paper_h - k.paper_foot;
	k.vx = k.vy = 0.0;
	k.at = now_sec();
	k.springing = true;
	k.where = KB_COMING;
	wlr_scene_node_set_enabled(&k.tree->node, true);
	kb_place();
	caps_show();
	snd_at(server, DESK_SND_SHEET_DOWN, k.home_x + k.w * 0.5, 0.7);
	kb_room(server);
	server_schedule_frames(server);
}

static void kb_show(struct desk_server *server, bool by_itself)
{
	if (k.where != KB_AWAY && k.where != KB_GOING) {
		return;
	}
	k.auto_shown = by_itself;
	k.by_pen = desk_pen_lately(server);
	if (!k.kb) {
		k.kb = desk_onscreen_keyboard(server);
	}
	drawer_thing_present(server, &desk_thing_keyboard, false);
	if (k.where == KB_GOING) {
		/* on its way down: back up */
		k.where = KB_COMING;
		k.springing = true;
		k.at = now_sec();
		server_schedule_frames(server);
		return;
	}
	k.where = KB_READING;
	desk_kb_art_begin();
	server_schedule_frames(server);
}

/* Everything it was holding down, let go of. */
static void kb_let_go_of_all(void)
{
	odd_stop();
	for (int i = 0; i < k.n; i++) {
		struct kb_key *key = &k.keys[i];
		if (key->sticky && key->mod != MOD_OFF) {
			key->mod = MOD_OFF;
			key_send(key->code, false);
		}
		key_show(key, false);
	}
	for (int t = 0; t < TOUCHES; t++) {
		struct kb_touch *tc = &k.touch[t];
		if (tc->on && tc->key >= 0 && tc->held) {
			key_send(k.keys[tc->key].code, false);
		}
		tc->on = false;
	}
	k.carried = false;
}

static void kb_go(struct desk_server *server)
{
	if (k.where != KB_ON && k.where != KB_COMING && k.where != KB_READING) {
		return;
	}
	if (k.where == KB_READING) {
		k.where = KB_AWAY;
		drawer_thing_present(server, &desk_thing_keyboard, true);
		return;
	}
	kb_let_go_of_all();
	k.where = KB_GOING;
	k.springing = false;
	k.at = now_sec();
	k.auto_shown = false;
	kb_room(server);
	server_schedule_frames(server);
}

static void kb_tick(struct desk_server *server)
{
	if (k.where != KB_AWAY && k.where != KB_READING) {
		pad_tick(server);
	}
	if (k.where == KB_READING) {
		int ready = desk_kb_art_ready();
		if (ready > 0) {
			kb_appear(server);
		} else if (ready < 0) {
			k.where = KB_AWAY;
			drawer_thing_present(server, &desk_thing_keyboard, true);
			wlr_log(WLR_ERROR, "the keyboard's pictures could not be read: no keyboard");
		} else {
			server_schedule_frames(server);
		}
		return;
	}
	if (k.where == KB_AWAY || k.carried) {
		return;
	}
	double now = now_sec();
	double dt = clampd(now - k.at, 0.0, 0.05);
	k.at = now;
	double pt = desk_ui_scale();
	if (k.where == KB_GOING) {
		/* it falls off the front of the desk */
		k.vy += 3400.0 * pt * dt;
		k.y += k.vy * dt;
		k.x += k.vx * dt;
		kb_place();
		if (k.y + k.paper_foot - k.paper_h > k.screen_foot) {
			k.where = KB_AWAY;
			for (int i = 0; i < k.n; i++) {
				if (k.keys[i].down) {
					wlr_scene_node_destroy(&k.keys[i].down->node);
					k.keys[i].down = NULL;
				}
				k.keys[i].shown_down = false;
			}
			k.n = 0;
			wlr_scene_node_destroy(&k.tree->node);
			k.tree = NULL;
			k.pic = NULL;
			k.rail = NULL;
			k.paper = NULL;
			k.bend = NULL;
			k.bail = NULL;
			pad.ink = NULL;
			pad.slips = NULL;
			pad_forget();
			if (k.bare) {
				cairo_surface_destroy(k.bare);
				k.bare = NULL;
			}
			drawer_thing_present(server, &desk_thing_keyboard, true);
			return;
		}
		server_schedule_frames(server);
		return;
	}
	if (k.springing) {
		/* to where it lies, a little too far and back */
		const double W = 15.0, Z = 0.62;
		int n = (int)ceil(dt * 480.0);
		double h = n > 0 ? dt / n : 0.0;
		for (int i = 0; i < n; i++) {
			k.vx += (W * W * (k.home_x - k.x) - 2.0 * Z * W * k.vx) * h;
			k.vy += (W * W * (k.home_y - k.y) - 2.0 * Z * W * k.vy) * h;
			k.x += k.vx * h;
			k.y += k.vy * h;
		}
		kb_place();
		if (fabs(k.home_x - k.x) < 0.4 && fabs(k.home_y - k.y) < 0.4 && hypot(k.vx, k.vy) < 12.0) {
			k.x = k.home_x;
			k.y = k.home_y;
			k.vx = k.vy = 0.0;
			k.springing = false;
			k.where = KB_ON;
			kb_place();
			kb_room(server);
			return;
		}
		server_schedule_frames(server);
		return;
	}
	if (fabs(k.vx) + fabs(k.vy) > 0.0) {
		/* let go of where it is: it slides, on the wood */
		double speed = hypot(k.vx, k.vy);
		double lose = 2800.0 * pt * dt;
		if (speed <= lose) {
			k.vx = k.vy = 0.0;
		} else {
			k.vx *= (speed - lose) / speed;
			k.vy *= (speed - lose) / speed;
		}
		k.x += k.vx * dt;
		k.y += k.vy * dt;
		kb_place();
		server_schedule_frames(server);
	}
}

/* ---- what is written on the sheet -------------------------------------- */

/* Typing what is not on any key of the keymap: the machine has that key for as long as it takes. A
 * program has to be told of the new keymap and to have taken it before the key comes, an X one most
 * of all (Xwayland compiles it and tells its programs), so it is done a step at a time, and anything
 * after it waits its turn. */

static int odd_cb(void *data);

static bool key_of(uint32_t cp, uint16_t *code, bool *shifted)
{
	struct xkb_keymap *km = k.kb ? k.kb->keymap : NULL;
	xkb_keysym_t want = xkb_utf32_to_keysym(cp);
	if (!km || want == XKB_KEY_NoSymbol) {
		return false;
	}
	xkb_layout_index_t lay = k.kb->modifiers.group;
	xkb_keycode_t lo = xkb_keymap_min_keycode(km), hi = xkb_keymap_max_keycode(km);
	for (xkb_keycode_t c = lo; c <= hi; c++) {
		for (int level = 0; level < 2; level++) {
			const xkb_keysym_t *syms;
			int n = xkb_keymap_key_get_syms_by_level(km, c, lay, (xkb_level_index_t)level, &syms);
			if (n == 1 && syms[0] == want && c >= 8) {
				*code = (uint16_t)(c - 8);
				*shifted = level == 1;
				return true;
			}
		}
	}
	return false;
}

static void type_key(uint16_t code, bool shifted)
{
	if (shifted) {
		key_send(KEY_LEFTSHIFT, true);
	}
	key_send(code, true);
	key_send(code, false);
	if (shifted) {
		key_send(KEY_LEFTSHIFT, false);
	}
}

/* The keymap back as it was, and nothing more of what was waiting. */
static void odd_stop(void)
{
	if (odd.on_x && odd.server) {
		desk_x_key_done(odd.server);
		odd.on_x = false;
	}
	if (odd.was && k.kb) {
		wlr_keyboard_set_keymap(k.kb, odd.was);
	}
	if (odd.was) {
		xkb_keymap_unref(odd.was);
		odd.was = NULL;
	}
	odd.n = odd.at = 0;
	odd.step = 0;
	if (odd.timer) {
		wl_event_source_timer_update(odd.timer, 0);
	}
}

static void odd_next(void)
{
	pad.typing = true;
	while (odd.at < odd.n) {
		uint32_t cp = odd.q[odd.at];
		uint16_t code;
		bool shifted;
		if (key_of(cp, &code, &shifted)) {
			type_key(code, shifted);
			odd.at++;
			continue;
		}
		/* an X program: on a key of Xwayland's that nothing uses, and off it again in a moment */
		int spare = desk_typing_to_x(odd.server) ? desk_x_key_for(odd.server, cp) : 0;
		if (spare > 0) {
			key_send((uint16_t)spare, true);
			key_send((uint16_t)spare, false);
			odd.on_x = true;
			odd.step = 2;
			wl_event_source_timer_update(odd.timer, ODD_STEP_MS);
			pad.typing = false;
			return;
		}
		char text[512];
		snprintf(text, sizeof text,
			"xkb_keymap {\n"
			"xkb_keycodes \"(unnamed)\" { minimum = 8; maximum = 255; <K1> = 9; };\n"
			"xkb_types \"(unnamed)\" { include \"complete\" };\n"
			"xkb_compatibility \"(unnamed)\" { include \"complete\" };\n"
			"xkb_symbols \"(unnamed)\" { key <K1> {[ U%04X ]}; };\n"
			"};\n", cp);
		struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *one = ctx ? xkb_keymap_new_from_string(ctx, text, XKB_KEYMAP_FORMAT_TEXT_V1, 0) : NULL;
		if (ctx) {
			xkb_context_unref(ctx);
		}
		if (!one || !k.kb->keymap) {
			if (one) {
				xkb_keymap_unref(one);
			}
			odd.at++;
			continue;
		}
		odd.was = xkb_keymap_ref(k.kb->keymap);
		wlr_keyboard_set_keymap(k.kb, one);
		xkb_keymap_unref(one);
		odd.step = 1;
		wl_event_source_timer_update(odd.timer, ODD_STEP_MS);
		pad.typing = false;
		return;
	}
	pad.typing = false;
	odd.n = odd.at = 0;
	odd.step = 0;
}

static int odd_cb(void *data)
{
	(void)data;
	if (odd.step == 1) {
		pad.typing = true;
		key_send(1, true);
		key_send(1, false);
		pad.typing = false;
		odd.step = 2;
		wl_event_source_timer_update(odd.timer, ODD_STEP_MS);
	} else if (odd.step == 2) {
		if (odd.on_x) {
			desk_x_key_done(odd.server);
			odd.on_x = false;
		}
		if (odd.was && k.kb) {
			wlr_keyboard_set_keymap(k.kb, odd.was);
		}
		if (odd.was) {
			xkb_keymap_unref(odd.was);
			odd.was = NULL;
		}
		odd.at++;
		odd.step = 0;
		/* (and what comes after it, once the old keymap has been taken back as well) */
		if (odd.at < odd.n) {
			odd.step = 3;
			wl_event_source_timer_update(odd.timer, ODD_STEP_MS);
		} else {
			odd.n = odd.at = 0;
		}
	} else if (odd.step == 3) {
		odd_next();
	}
	return 0;
}

/* Some characters, in their turn. */
static void type_chars(struct desk_server *server, const uint32_t *cps, int n)
{
	odd.server = server;
	if (!odd.timer) {
		odd.timer = wl_event_loop_add_timer(wl_display_get_event_loop(desk_display(server)), odd_cb, NULL);
	}
	if (!odd.timer) {
		return;
	}
	bool busy = odd.at < odd.n;
	if (!busy) {
		odd.n = odd.at = 0;
	}
	for (int i = 0; i < n && odd.n < (int)(sizeof odd.q / sizeof *odd.q); i++) {
		odd.q[odd.n++] = cps[i];
	}
	if (!busy) {
		odd_next();
	}
}

/* The line along the top of the sheet goes with what the keys on the top row do to what is typed. */
static void pad_line_follows(uint16_t code)
{
	if (k.mode != MODE_PEN || pad.typing) {
		return;
	}
	size_t have = strlen(pad.line);
	if (code == KEY_SPACE && have + 2 < sizeof pad.line) {
		pad.line[have] = ' ';
		pad.line[have + 1] = '\0';
	} else if (code == KEY_BACKSPACE) {
		while (have > 0 && (pad.line[have - 1] & 0xc0) == 0x80) {
			have--;
		}
		pad.line[have > 0 ? have - 1 : 0] = '\0';
	} else if (code == KEY_ENTER) {
		pad.line[0] = '\0';
	} else {
		return;
	}
	pad.guesses = 0;
	pad.typed_last = false;
	pad.slips_changed = true;
}

/* Text, into whatever has the keys: told as text, to a program that asks for it that way; or else
 * key by key. */
static void pad_type(struct desk_server *server, const char *text)
{
	if (desk_type_text(server, text)) {
		return;
	}
	uint32_t cps[16];
	int n = 0;
	for (const unsigned char *p = (const unsigned char *)text; *p && n < 16;) {
		uint32_t cp = *p;
		int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
		cp = more == 3 ? cp & 0x07 : more == 2 ? cp & 0x0f : more == 1 ? cp & 0x1f : cp;
		p++;
		for (int i = 0; i < more && *p; i++, p++) {
			cp = (cp << 6) | (*p & 0x3f);
		}
		cps[n++] = cp;
	}
	type_chars(server, cps, n);
}

static double pad_stroke_width(double press)
{
	return (2.0 + 2.6 * press) * desk_ui_scale();
}

/* A character as it is written: on a picture a little bigger than its box, for what goes over. */
static void pad_ink_paint(void)
{
	if (!k.tree || pad.at < 0) {
		return;
	}
	int over = pad.box_w / 3;
	struct desk_buf *b = buf_create(pad.box_w + over * 2, pad.box_h + over * 2);
	cairo_t *cr = b->cr;
	cairo_translate(cr, over, over);
	cairo_set_source_rgba(cr, 0.106, 0.165, 0.341, 0.97);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	int from = 0;
	for (int s = 0; s < pad.strokes; s++) {
		int to = pad.ends[s];
		cairo_set_line_width(cr, pad_stroke_width(pad.press[s]));
		if (from < to) {
			cairo_move_to(cr, pad.xy[from * 2], pad.xy[from * 2 + 1]);
			if (to - from == 1) {
				cairo_line_to(cr, pad.xy[from * 2] + 0.1, pad.xy[from * 2 + 1]);
			}
			for (int i = from + 1; i + 1 < to; i++) {
				double mx = (pad.xy[i * 2] + pad.xy[i * 2 + 2]) * 0.5, my = (pad.xy[i * 2 + 1] + pad.xy[i * 2 + 3]) * 0.5;
				double cx0, cy0;
				cairo_get_current_point(cr, &cx0, &cy0);
				double qx = pad.xy[i * 2], qy = pad.xy[i * 2 + 1];
				cairo_curve_to(cr, cx0 + (qx - cx0) * 2.0 / 3.0, cy0 + (qy - cy0) * 2.0 / 3.0,
					mx + (qx - mx) * 2.0 / 3.0, my + (qy - my) * 2.0 / 3.0, mx, my);
			}
			if (to - from >= 2) {
				cairo_line_to(cr, pad.xy[(to - 1) * 2], pad.xy[(to - 1) * 2 + 1]);
			}
			cairo_stroke(cr);
		}
		from = to;
	}
	cairo_surface_flush(b->surface);
	scene_put(&pad.ink, k.tree, b,
		k.paper_x + pad.box_x + pad.at * pad.box_w - over, k.paper_foot - k.paper_all + pad.boxes_y - over, NULL);
	pad.ink->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&pad.ink->node);
	if (k.bend) {
		wlr_scene_node_raise_to_top(&k.bend->node);
	}
	if (k.bail) {
		wlr_scene_node_raise_to_top(&k.bail->node);
	}
	wlr_scene_node_set_enabled(&pad.ink->node, true);
}

/* Along the top of the sheet: the line it has typed, and under it what the last character might
 * be, typed each in a little square of its own. */
static void pad_slips_paint(void)
{
	if (!k.tree || k.mode != MODE_PEN) {
		if (pad.slips) {
			wlr_scene_node_set_enabled(&pad.slips->node, false);
		}
		return;
	}
	double pt = desk_ui_scale();
	int h = pad.slips_y + pad.slips_h;
	struct desk_buf *b = buf_create(k.paper_w, h);
	cairo_t *cr = b->cr;
	int side = pad.box_x;
	if (pad.line[0]) {
		int tw = 0;
		const char *show = pad.line;
		/* only as much of it as fits: its end */
		desk_kb_art_type(cr, show, side, pad.line_y + pad.line_h * 0.5, 17.0 * pt, 0.09, 0.09, 0.10, 0.0, &tw, false);
		while (tw > k.paper_w - side * 2 && *show) {
			show++;
			while ((*show & 0xc0) == 0x80) {
				show++;
			}
			desk_kb_art_type(cr, show, side, pad.line_y + pad.line_h * 0.5, 17.0 * pt, 0.09, 0.09, 0.10, 0.0, &tw, false);
		}
		desk_kb_art_type(cr, show, side, pad.line_y + pad.line_h * 0.5, 17.0 * pt, 0.09, 0.09, 0.10, 0.94, NULL, false);
	}
	if (pad.guesses > 0) {
		for (int i = 0; i < pad.guesses; i++) {
			double x = side + i * (pad.slip_w + 6.0 * pt);
			cairo_set_source_rgba(cr, 0.20, 0.36, 0.52, i == 0 && !pad.typed_last ? 0.62 : 0.30);
			cairo_set_line_width(cr, 1.0);
			cairo_rectangle(cr, floor(x) + 0.5, pad.slips_y + 0.5, pad.slip_w - 1.0, pad.slips_h - 1.0);
			cairo_stroke(cr);
			desk_kb_art_type(cr, pad.guess[i].text, x + pad.slip_w * 0.5, pad.slips_y + pad.slips_h * 0.5, 22.0 * pt, 0.09,
				0.09, 0.10, 0.94, NULL, true);
		}
	} else if (!pad.line[0]) {
		desk_kb_art_type(cr, TR("One character in each square"), side, pad.slips_y + pad.slips_h * 0.5, 14.0 * pt, 0.25, 0.25,
			0.27, 0.70, NULL, false);
	}
	cairo_surface_flush(b->surface);
	scene_put(&pad.slips, k.tree, b, k.paper_x, k.paper_foot - k.paper_all, NULL);
	pad.slips->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&pad.slips->node);
	if (pad.ink) {
		wlr_scene_node_raise_to_top(&pad.ink->node);
	}
	if (k.bend) {
		wlr_scene_node_raise_to_top(&k.bend->node);
	}
	if (k.bail) {
		wlr_scene_node_raise_to_top(&k.bail->node);
	}
	paper_place();
}

static void pad_clear_box(void)
{
	pad.at = -1;
	pad.strokes = 0;
	pad.points = 0;
	if (pad.ink) {
		wlr_scene_node_set_enabled(&pad.ink->node, false);
	}
}

/* Typed. What else it might have been is left up, for a moment's second thoughts. */
static void pad_commit(struct desk_server *server, const char *text)
{
	pad_type(server, text);
	size_t have = strlen(pad.line);
	if (have + strlen(text) + 1 < sizeof pad.line) {
		memcpy(pad.line + have, text, strlen(text) + 1);
	}
	double at = k.x + k.paper_x + (pad.at >= 0 ? pad.box_x + pad.at * pad.box_w : 0);
	snd_at(server, DESK_SND_TYPE_STRIKE, at, 0.6);
	pad.typed_last = true;
	pad_clear_box();
	if (pad.read_timer) {
		wl_event_source_timer_update(pad.read_timer, 0);
	}
	pad.slips_changed = true;
	server_schedule_frames(server);
}

static struct desk_hw_stroke pad_strokes_buf[PAD_STROKES];

static void pad_guess(void)
{
	int from = 0;
	for (int s = 0; s < pad.strokes; s++) {
		pad_strokes_buf[s].xy = pad.xy + from * 2;
		pad_strokes_buf[s].n = pad.ends[s] - from;
		from = pad.ends[s];
	}
	pad.guesses = desk_hw_read(pad_strokes_buf, pad.strokes, pad.box_w, pad.box_h, pad.guess, PAD_GUESSES);
	pad.typed_last = false;
	pad.slips_changed = true;
}

static void pad_arm(struct desk_server *server);

/* Written and not yet typed: now. */
static void pad_read_now(struct desk_server *server)
{
	if (pad.strokes == 0 || pad.writing) {
		return;
	}
	if (pad.guesses == 0) {
		pad_guess();
	}
	if (pad.guesses > 0 && !pad.typed_last) {
		pad_commit(server, pad.guess[0].text);
	} else if (desk_hw_ready()) {
		pad_clear_box();
	} else {
		/* (its table is not made yet, the first time: a moment) */
		pad_arm(server);
	}
}

static int pad_read_cb(void *data)
{
	pad_read_now(data);
	return 0;
}

static void pad_arm(struct desk_server *server)
{
	if (!pad.read_timer) {
		pad.read_timer = wl_event_loop_add_timer(wl_display_get_event_loop(desk_display(server)), pad_read_cb, server);
	}
	if (pad.read_timer) {
		wl_event_source_timer_update(pad.read_timer, PAD_READ_MS);
	}
}

/* What to read: the languages of the desk, of the keyboard and of the machine, Latin in any case. */
static void pad_ready(struct desk_server *server)
{
	const char *langs[6];
	int n = 0;
	const char *desk = desk_lang_code(desk_lang_get()), *here = desk_lang_code(desk_lang_from_locale());
	langs[n++] = desk;
	if (strcmp(here, desk)) {
		langs[n++] = here;
	}
	struct xkb_keymap *km = k.kb ? k.kb->keymap : NULL;
	for (xkb_layout_index_t i = 0; km && i < xkb_keymap_num_layouts(km) && n < 5; i++) {
		const char *name = xkb_keymap_layout_get_name(km, i);
		if (name && strstr(name, "Japanese")) {
			langs[n++] = "ja";
		} else if (name && strstr(name, "Korean")) {
			langs[n++] = "ko";
		} else if (name && strstr(name, "Chinese")) {
			langs[n++] = "zh";
		}
	}
	langs[n] = NULL;
	pad.squares = false;
	for (int i = 0; i < n; i++) {
		pad.squares = pad.squares || !strcmp(langs[i], "ja") || !strcmp(langs[i], "ko") || !strcmp(langs[i], "zh");
	}
	if (k.mode == MODE_PEN) {
		desk_hw_want(langs);
	}
	(void)server;
}

static void pad_forget(void)
{
	pad_clear_box();
	pad.writing = false;
	pad.guesses = 0;
	pad.typed_last = false;
	pad.line[0] = '\0';
	if (pad.read_timer) {
		wl_event_source_timer_update(pad.read_timer, 0);
	}
}

static void pad_down(struct desk_server *server, int32_t id, double x, double y)
{
	if (pad.writing) {
		return;
	}
	/* one of what it might have been */
	if (y >= pad.slips_y && y < pad.slips_y + pad.slips_h && pad.guesses > 0) {
		double pt = desk_ui_scale();
		int i = (int)floor((x - pad.box_x) / (pad.slip_w + 6.0 * pt));
		if (i >= 0 && i < pad.guesses) {
			char text[8];
			snprintf(text, sizeof text, "%s", pad.guess[i].text);
			if (pad.typed_last) {
				if (i == 0) {
					return;
				}
				/* instead of what was typed */
				pad.typing = true;
				key_send(KEY_BACKSPACE, true);
				key_send(KEY_BACKSPACE, false);
				pad.typing = false;
				size_t have = strlen(pad.line);
				while (have > 0 && (pad.line[have - 1] & 0xc0) == 0x80) {
					have--;
				}
				if (have > 0) {
					have--;
				}
				pad.line[have] = '\0';
			}
			struct desk_hw_guess chosen = pad.guess[i];
			pad.guess[i] = pad.guess[0];
			pad.guess[0] = chosen;
			pad_commit(server, text);
		}
		return;
	}
	if (y < pad.boxes_y || y >= pad.boxes_y + pad.box_h || x < pad.box_x || x >= pad.box_x + pad.boxes * pad.box_w) {
		return;
	}
	int box = (int)floor((x - pad.box_x) / pad.box_w);
	box = box < 0 ? 0 : box >= pad.boxes ? pad.boxes - 1 : box;
	if (pad.at != box && pad.strokes > 0) {
		pad_read_now(server);
	}
	if (pad.read_timer) {
		wl_event_source_timer_update(pad.read_timer, 0);
	}
	if (pad.strokes >= PAD_STROKES || pad.points >= PAD_POINTS - 1) {
		return;
	}
	pad.at = box;
	pad.writing = true;
	pad.writer = id;
	double lx = x - pad.box_x - box * pad.box_w, ly = y - pad.boxes_y;
	pad.xy[pad.points * 2] = (float)lx;
	pad.xy[pad.points * 2 + 1] = (float)ly;
	pad.points++;
	pad.ends[pad.strokes] = pad.points;
	pad.press[pad.strokes] = desk_pen_lately(server) ? (float)desk_pen_pressure(server) : 0.5f;
	pad.strokes++;
	pad.ink_changed = true;
	server_schedule_frames(server);
}

static void pad_move(struct desk_server *server, int32_t id, double x, double y)
{
	if (!pad.writing || pad.writer != id || pad.points >= PAD_POINTS) {
		return;
	}
	double lx = x - pad.box_x - pad.at * pad.box_w, ly = y - pad.boxes_y;
	float *last = &pad.xy[(pad.points - 1) * 2];
	if (hypot(lx - last[0], ly - last[1]) < 1.3) {
		return;
	}
	pad.xy[pad.points * 2] = (float)lx;
	pad.xy[pad.points * 2 + 1] = (float)ly;
	pad.points++;
	pad.ends[pad.strokes - 1] = pad.points;
	if (desk_pen_lately(server)) {
		float *p = &pad.press[pad.strokes - 1];
		*p += ((float)desk_pen_pressure(server) - *p) * 0.1f;
	}
	pad.ink_changed = true;
	server_schedule_frames(server);
}

static void pad_up(struct desk_server *server, int32_t id, bool taken_away)
{
	if (!pad.writing || pad.writer != id) {
		return;
	}
	pad.writing = false;
	if (taken_away) {
		int from = pad.strokes > 1 ? pad.ends[pad.strokes - 2] : 0;
		pad.points = from;
		pad.strokes--;
		pad.ink_changed = true;
		if (pad.strokes == 0) {
			pad_clear_box();
		}
		server_schedule_frames(server);
		return;
	}
	pad_guess();
	pad_arm(server);
	server_schedule_frames(server);
}

/* Over to the pen, or back to the keys: the sheet winds up out of the machine as the machine goes
 * down, or back. */
static void kb_throw_over(struct desk_server *server)
{
	pad_read_now(server);
	kb_let_go_of_all();
	k.mode = k.mode == MODE_PEN || desk_door_shut(server) ? MODE_KEYS : MODE_PEN;
	if (k.mode == MODE_KEYS) {
		pad_forget();
	}
	pad_ready(server);
	kb_measure(server);
	keys_lay_out();
	kb_paint(server);
	pad.slips_changed = true;
	snd_at(server, DESK_SND_TYPE_RETURN, k.x + k.w * 0.5, 0.5);
	k.springing = true;
	k.where = KB_COMING;
	k.at = now_sec();
	k.vx = k.vy = 0.0;
	kb_room(server);
	server_schedule_frames(server);
}

/* Every frame: the sheet coming out or going back, what has been written since. */
static void pad_tick(struct desk_server *server)
{
	static double was;
	double now = now_sec();
	double dt = clampd(now - was, 0.0, 0.05);
	was = now;
	if (fabs(k.paper_out - k.paper_h) > 0.4) {
		k.paper_out += (k.paper_h - k.paper_out) * (1.0 - exp(-dt * 13.0));
		if (fabs(k.paper_out - k.paper_h) <= 0.4) {
			k.paper_out = k.paper_h;
		}
		paper_place();
		server_schedule_frames(server);
	}
	if (pad.ink_changed) {
		pad.ink_changed = false;
		pad_ink_paint();
	}
	if (pad.slips_changed) {
		pad.slips_changed = false;
		pad_slips_paint();
	}
}

/* ---- a finger on it --------------------------------------------------- */

static void kb_put_by_hand(double x, double y)
{
	k.x = x - k.carry_dx;
	k.y = y - k.carry_dy;
	kb_place();
}

static void kb_carry_begin(int32_t id, double x, double y)
{
	k.carried = true;
	k.carry_id = id;
	k.carry_dx = x - k.x;
	k.carry_dy = y - k.y;
	k.carry_vx = k.carry_vy = 0.0;
	k.carry_at = now_sec();
	k.springing = false;
	k.where = KB_ON;
	k.vx = k.vy = 0.0;
}

static void kb_carry_move(struct desk_server *server, double x, double y)
{
	double was_x = k.x, was_y = k.y;
	kb_put_by_hand(x, y);
	double now = now_sec();
	double dt = now - k.carry_at;
	if (dt > 0.0005) {
		bool fresh = dt > 0.08;
		double a = fresh ? 1.0 : 0.4;
		k.carry_vx = (1.0 - a) * k.carry_vx + a * (k.x - was_x) / dt;
		k.carry_vy = (1.0 - a) * k.carry_vy + a * (k.y - was_y) / dt;
		k.carry_at = now;
	}
	server_schedule_frames(server);
}

/* Let go of. Going down hard, or most of it off the front: it goes. Near where it lies: there.
 * Anywhere else it slides and stays. */
static void kb_carry_end(struct desk_server *server)
{
	k.carried = false;
	double pt = desk_ui_scale();
	double still = now_sec() - k.carry_at > 0.08 ? 0.0 : 1.0;
	k.vx = k.carry_vx * still;
	k.vy = clampd(k.carry_vy * still, -3000.0 * pt, 3000.0 * pt);
	k.at = now_sec();
	double seen = k.y + k.vy * 0.16;
	double shows = k.mode == MODE_PEN ? k.rows_y[1] - k.body_y : k.body_h;
	if (k.vy > 900.0 * pt || seen > k.home_y + shows * 0.45) {
		k.where = KB_COMING;   /* (so that it can go) */
		kb_go(server);
	} else if (fabs(seen - k.home_y) < shows * 0.28) {
		k.springing = true;
		k.where = KB_COMING;
	}
	kb_room(server);
	server_schedule_frames(server);
}

static void touch_on_key(struct desk_server *server, struct kb_touch *t, int i)
{
	struct kb_key *key = &k.keys[i];
	/* what has been written goes first */
	pad_read_now(server);
	t->key = i;
	t->at = now_sec();
	key_show(key, true);
	double at = k.x + key->x + key->w * 0.5;
	if (key->code == KEY_SPACE) {
		snd_at(server, DESK_SND_TYPE_SPACE, at, 0.6);
	} else if (key->code == KEY_LEFTSHIFT || key->code == KEY_RIGHTSHIFT || key->code == KEY_CAPSLOCK) {
		snd_at(server, DESK_SND_TYPE_SHIFT, at, 0.55);
	} else if (key->code == KEY_ENTER) {
		snd_at(server, DESK_SND_TYPE_BELL, at, 0.32);
		snd_at(server, DESK_SND_TYPE_RETURN, at, 0.55);
	} else if (key->kind == DESK_KB_LETTER || key->code == KEY_BACKSPACE || key->code == KEY_TAB) {
		snd_at(server, DESK_SND_TYPE_STRIKE, at, 0.6);
	} else {
		snd_at(server, DESK_SND_KEY_PRESS, at, 0.5);
	}
	if (key->sticky && key->mod == MOD_OFF) {
		key_send(key->code, true);
		key->typed_while_down = false;
		key->mod_at = 0.0;
	}
}

static bool kb_finger_down(struct desk_server *server, int32_t id, double x, double y)
{
	if (k.where == KB_AWAY || k.where == KB_READING || k.where == KB_GOING || !k.tree) {
		return false;
	}
	int at = kb_at(x, y);
	if (at == NOT_A_KEY) {
		return false;
	}
	struct kb_touch *t = NULL;
	for (int i = 0; i < TOUCHES && !t; i++) {
		if (!k.touch[i].on) {
			t = &k.touch[i];
		}
	}
	if (!t) {
		return true;
	}
	*t = (struct kb_touch){ .on = true, .id = id, .key = NOT_A_KEY, .at_first = at };
	if (at == ON_CASE) {
		if (!k.carried) {
			kb_carry_begin(id, x, y);
		}
		return true;
	}
	if (at == ON_LEVER) {
		return true;
	}
	if (at == ON_PAPER) {
		pad_down(server, id, x - k.x - k.paper_x, y - k.y - (k.paper_foot - k.paper_all));
		return true;
	}
	/* a finger coming down types what the last was on: they are typed in the order they land */
	for (int i = 0; i < TOUCHES; i++) {
		if (&k.touch[i] != t && k.touch[i].on) {
			touch_types(&k.touch[i]);
		}
	}
	touch_on_key(server, t, at);
	held_arm(server);
	return true;
}

static void kb_finger_move(struct desk_server *server, int32_t id, double x, double y)
{
	if (k.carried && k.carry_id == id) {
		kb_carry_move(server, x, y);
		return;
	}
	struct kb_touch *t = touch_of(id);
	if (t && t->at_first == ON_PAPER) {
		pad_move(server, id, x - k.x - k.paper_x, y - k.y - (k.paper_foot - k.paper_all));
		return;
	}
	if (!t || t->key < 0 || t->pressed || k.keys[t->key].sticky) {
		return;
	}
	/* slid to the key it meant */
	int at = kb_at(x, y);
	if (at >= 0 && at != t->key && !k.keys[at].sticky) {
		key_show(&k.keys[t->key], false);
		double was = t->at;
		touch_on_key(server, t, at);
		t->at = was;
	}
}

static void sticky_lifted(struct kb_key *key, double now)
{
	if (key->typed_while_down && key->mod == MOD_OFF) {
		/* held for what was typed with it */
		key_send(key->code, false);
		key_show(key, false);
		return;
	}
	if (key->mod == MOD_OFF) {
		key->mod = MOD_ONCE;
		key->mod_at = now;
	} else if (key->mod == MOD_ONCE) {
		if (now - key->mod_at <= TWICE_SEC) {
			key->mod = MOD_STAYS;
		} else {
			key->mod = MOD_OFF;
			key_send(key->code, false);
			key_show(key, false);
		}
	} else {
		key->mod = MOD_OFF;
		key_send(key->code, false);
		key_show(key, false);
	}
}

static void kb_language(struct desk_server *server)
{
	(void)server;
	struct xkb_keymap *km = k.kb->keymap;
	if (km && xkb_keymap_num_layouts(km) > 1) {
		struct wlr_keyboard_modifiers *m = &k.kb->modifiers;
		wlr_keyboard_notify_modifiers(k.kb, m->depressed, m->latched, m->locked,
			(m->group + 1) % xkb_keymap_num_layouts(km));
		return;
	}
	/* one keyboard: the input method's own key */
	key_send(KEY_ZENKAKUHANKAKU, true);
	key_send(KEY_ZENKAKUHANKAKU, false);
}

static void kb_finger_up(struct desk_server *server, int32_t id, bool taken_away)
{
	if (k.carried && k.carry_id == id) {
		struct kb_touch *c = touch_of(id);
		if (c) {
			c->on = false;
		}
		kb_carry_end(server);
		return;
	}
	struct kb_touch *t = touch_of(id);
	if (!t) {
		return;
	}
	int i = t->key;
	bool pressed = t->pressed, held = t->held;
	int first = t->at_first;
	t->on = false;
	if (first == ON_PAPER) {
		pad_up(server, id, taken_away);
		return;
	}
	if (first == ON_LEVER) {
		if (!taken_away) {
			kb_throw_over(server);
		}
		return;
	}
	if (i >= 0 && i < k.n) {
		struct kb_key *key = &k.keys[i];
		snd_at(server, key->kind == DESK_KB_LETTER ? DESK_SND_TYPE_RELEASE : DESK_SND_KEY_RELEASE,
			k.x + key->x + key->w * 0.5, 0.35);
		if (key->sticky) {
			sticky_lifted(key, now_sec());
		} else {
			key_show(key, false);
			if (key->act == ACT_PUT_AWAY) {
				if (!taken_away) {
					kb_go(server);
				}
			} else if (key->act == ACT_THROW_OVER) {
				if (!taken_away) {
					kb_throw_over(server);
				}
				return;
			} else if (key->act == ACT_LANGUAGE) {
				if (!taken_away) {
					kb_language(server);
					int shown = (int)k.kb->modifiers.group;
					if (shown != k.layout_seen) {
						keys_lay_out();
						kb_paint(server);
					}
				}
			} else if (!pressed && !taken_away) {
				key_send(key->code, true);
				key_send(key->code, false);
				key_typed();
			} else if (held) {
				key_send(key->code, false);
				key_typed();
			}
			caps_show();
		}
	}
	held_arm(server);
}

/* ---- the same with a mouse -------------------------------------------- */

static bool kb_hand_down(struct desk_server *server, uint32_t button)
{
	struct wlr_cursor *c = desk_cursor(server);
	if (button != BTN_LEFT) {
		/* (the other buttons do nothing on it, and nothing under it either) */
		bool up = k.where != KB_AWAY && k.where != KB_READING && k.where != KB_GOING && k.tree;
		return up && kb_at(c->x, c->y) != NOT_A_KEY;
	}
	return kb_finger_down(server, DESK_KB_MOUSE, c->x, c->y);
}

static enum desk_thing_hold kb_drag(struct desk_server *server)
{
	if (!touch_of(DESK_KB_MOUSE)) {
		return THING_NOT_HELD;
	}
	struct wlr_cursor *c = desk_cursor(server);
	kb_finger_move(server, DESK_KB_MOUSE, c->x, c->y);
	return k.carried && k.carry_id == DESK_KB_MOUSE ? THING_CARRIED : THING_HELD;
}

static bool kb_release(struct desk_server *server)
{
	if (!touch_of(DESK_KB_MOUSE)) {
		return false;
	}
	kb_finger_up(server, DESK_KB_MOUSE, false);
	return true;
}

/* ---- when it is wanted ------------------------------------------------ */

static int kb_off_cb(void *data)
{
	struct desk_server *server = data;
	if (!k.wanted && k.auto_shown) {
		kb_go(server);
	}
	return 0;
}

/* Something to type in has come up, or gone. With fingers and no keyboard to type on, it comes up
 * by itself, and then it goes by itself, a little after: the keys go from one field to another
 * by way of none. */
static void kb_text_wanted(struct desk_server *server, bool wanted)
{
	k.wanted = wanted;
	if (!k.off_timer) {
		k.off_timer = wl_event_loop_add_timer(wl_display_get_event_loop(desk_display(server)), kb_off_cb,
			server);
	}
	if (wanted) {
		if (k.off_timer) {
			wl_event_source_timer_update(k.off_timer, 0);
		}
		int setting = desk_keys_setting(server);
		if (setting == DESK_KEYS_NEVER || desk_pen_writing(server)) {
			/* (a pen on a notebook is writing in it, not asking for keys) */
			return;
		}
		if (setting == DESK_KEYS_ALWAYS || (desk_fingers_lately(server) && !desk_typing_keyboard(server))) {
			kb_show(server, true);
		}
	} else if (k.auto_shown && k.off_timer) {
		wl_event_source_timer_update(k.off_timer, NOT_WANTED_MS);
	}
}

/* A key on a real keyboard: nobody needs this one for the moment. */
static bool kb_key(struct desk_server *server, uint32_t keycode, xkb_keysym_t sym, uint32_t mods, bool released)
{
	(void)keycode;
	(void)sym;
	(void)mods;
	if (!k.injecting && !released && k.auto_shown && (k.where == KB_ON || k.where == KB_COMING)) {
		kb_go(server);
	}
	return false;
}

static bool kb_in_drawer(const struct desk_server *server)
{
	(void)server;
	return k.where == KB_AWAY;
}

static void kb_take_out(struct desk_server *server)
{
	kb_show(server, false);
}

static void kb_put_away(struct desk_server *server)
{
	kb_go(server);
}

static bool kb_offered(struct desk_server *server)
{
	return desk_touch_there(server) || desk_pen_there(server) || desk_keys_setting(server) == DESK_KEYS_ALWAYS;
}

static void kb_screen_changed(struct desk_server *server)
{
	if (k.where == KB_ON || k.where == KB_COMING) {
		double was = k.scale;
		kb_measure(server);
		if (fabs(was - k.scale) > 1e-5) {
			kb_let_go_of_all();
			keys_lay_out();
			kb_paint(server);
			pad.slips_changed = true;
			if (k.rail) {
				wlr_scene_node_destroy(&k.rail->node);
				k.rail = NULL;
			}
		}
		k.springing = true;
		k.where = KB_COMING;
		k.at = now_sec();
		server_schedule_frames(server);
	}
}

static void kb_quitting(struct desk_server *server)
{
	(void)server;
	if (k.tree) {
		kb_let_go_of_all();
	}
	if (k.held_timer) {
		wl_event_source_remove(k.held_timer);
		k.held_timer = NULL;
	}
	if (k.off_timer) {
		wl_event_source_remove(k.off_timer);
		k.off_timer = NULL;
	}
	if (pad.read_timer) {
		wl_event_source_remove(pad.read_timer);
		pad.read_timer = NULL;
	}
	odd_stop();
	if (odd.timer) {
		wl_event_source_remove(odd.timer);
		odd.timer = NULL;
	}
	desk_hw_drop();
	desk_kb_art_drop();
}

static void kb_drawer_item(struct desk_drawer_item *it)
{
	it->look = DESK_LOOK_THING;
	it->lying = desk_kb_art_top_lying;
	it->thick = 0.22;
	snprintf(it->name, sizeof it->name, "%s", TR("Keyboard"));
	it->seed = 43;
}

const struct desk_thing desk_thing_keyboard = {
	.name = "keyboard",
	.over_all = true,
	.finger_down = kb_finger_down,
	.finger_move = kb_finger_move,
	.finger_up = kb_finger_up,
	.hand_down = kb_hand_down,
	.drag = kb_drag,
	.release = kb_release,
	.key = kb_key,
	.tick = kb_tick,
	.text_wanted = kb_text_wanted,
	.screen_changed = kb_screen_changed,
	.quitting = kb_quitting,
	.drawer_word = "keyboard",
	.drawer_item = kb_drawer_item,
	.offered = kb_offered,
	.in_drawer = kb_in_drawer,
	.take_out = kb_take_out,
	.put_away = kb_put_away,
};
