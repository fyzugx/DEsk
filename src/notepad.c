/* desk-notepad: DEsk's notebook, to type in and to write in with a pen.
 *
 * On the desk the window is the top sheet of a wire-bound notebook, and the
 * desk lays the notebook round it and under it (notebook.h): all this draws is
 * the rules, which go with the lines, what is typed, in blue-black ink, what
 * is written with a pen, and what is written in the sheet's heading -- the
 * page's number, a title and a date. Off the desk, or with the whole screen to
 * itself, it has a plain ruled page of its own. There is no menu bar and no
 * tool bar: the sheet's foot is turned up, and lifted there it shows, on the
 * sheet under it, what can be done; turned right over, heading, holes and all,
 * the next page is open, and after the last one a clean one. The typing
 * itself -- input methods, right-to-left, selections, the clipboard -- is
 * GTK's.
 *
 * A pen writes, as hard as it is pressed, and its other end, or the pen with
 * its side button held, rubs out. What it writes with is in the pencil case the
 * desk lays under the notebook (thing-notebook.c): a pen of four refills,
 * turned to the one wanted, a highlighter and a rubber; taken out of the case,
 * they write for a mouse and a finger too. What is written hangs from the line
 * it was written by, and goes where that line goes as the words round it change.
 * A finger turns the sheet, on the finger, and let go it goes on or falls back;
 * two fingers tapped take back what was done last.
 *
 * The notebook's own pages are kept as they are written, in
 * $XDG_DATA_HOME/desk/notebook: the words of a page in one file, what is
 * written on it in another. There is one notebook, and a second one started
 * shows the first. A file it is given is opened on a notebook of its own, as it
 * finds it, and saved the same way: the encoding, a byte order mark and the
 * kind of line ending are kept, and what is written over it is kept by the
 * notebook's pages. A copy of what has not been saved is kept every little
 * while in $XDG_STATE_HOME/desk/notepad, and offered back the next time, if the
 * machine or the program went down before it could be saved.
 *
 * It does what a notebook does. Syntax colours, tabs and plugins are what the
 * other editors are for; shell.conf's app.editor, or Default programs, still
 * chooses one. */

#include <cairo-pdf.h>
#include <errno.h>
#include <fcntl.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#include "fonts.h"
#include "i18n.h"
#include "notebook.h"

#define PAPER_CSS "#f4efe2"
#define INK_CSS "#1b2a57"

/* the red margin and the writing, in the window */
#define MARGIN_AT (NB_MARGIN_X - NB_INSET_L)
#define LEFT_MARGIN (MARGIN_AT + 10)
#define RIGHT_MARGIN 24
#define SIZE_LEAST 9
#define SIZE_MOST 40
#define KEEP_EVERY_S 15
#define TOO_BIG (64 * 1024 * 1024)
/* a page of the notebook's own is kept this long after it was last changed */
#define SAVE_AFTER_MS 700
/* what is written by hand in the heading is in units of this many pixels: a page's, at its usual size */
#define HEAD_PITCH 24.0

/* Turning a sheet over. A finger gone this far has hold of it, in pixels */
#define TAKES_HOLD 10.0
/* how far a corner reaches, in pixels */
#define CORNER 30.0
#define GRAVITY 16.0
#define AIR 1.1
/* Taken this far over, it goes on by itself: the wire holds a sheet up a little past where it would
 * stand on a hinge, in radians (the free edge a little past the middle of the sheet) */
#define BALANCE 1.0
/* Lifted less than this and let go, it falls back; further, short of going over, it stays up off
 * the sheet under it, which has on it what can be done */
#define PEEK 0.16
/* thrown faster than this, in radians a second, it goes the way it was thrown */
#define THROWN 5.0
/* the sheet in the air is drawn in this many strips */
#define STRIPS 26

enum { PAPER_LINED, PAPER_SQUARED, PAPER_DOTTED, PAPER_PLAIN, PAPER_KINDS };

/* ---- what writes: the pens, and the case they are in ----------------------- */

enum { INK_BLUE, INK_BLACK, INK_RED, INK_PENCIL, INK_MARKER, INK_GREEN, INKS };

static const struct {
	double r, g, b, a;
	double width;             /* in line pitches, at the middle of how hard a pen is pressed */
	double firm;              /* how much of that goes with how hard, 0 to 1 */
} INK[INKS] = {
	[INK_BLUE] = { 0.11, 0.21, 0.53, 1.0, 0.070, 0.70 },
	[INK_BLACK] = { 0.075, 0.075, 0.085, 1.0, 0.070, 0.70 },
	[INK_RED] = { 0.72, 0.11, 0.11, 1.0, 0.070, 0.70 },
	[INK_PENCIL] = { 0.27, 0.27, 0.29, 0.90, 0.056, 0.45 },
	[INK_MARKER] = { 1.0, 0.86, 0.14, 0.46, 0.66, 0.0 },
	[INK_GREEN] = { 0.07, 0.43, 0.23, 1.0, 0.070, 0.70 },
};

/* the pen's refills, as they go round it (notebook.h) */
static const int PEN_INK[NB_PENS] = { INK_BLACK, INK_BLUE, INK_RED, INK_GREEN };
static const char *const PEN_NAME[NB_PENS] = NB_PEN_NAMES;
static const char *const HAND_NAME[NB_HANDS] = NB_HAND_NAMES;

/* What the desk's pencil case says: which of the pen's refills is out, and what has been taken out
 * of the case into the hand. With no case -- off the desk, or with the whole screen to itself -- the
 * sheet under a turned-up one has the pens on it instead, and what is chosen there is ours. */
static struct {
	int pen, hand;
	int own_pen, own_hand;
	GFileMonitor *watch;
} g_case = { NB_PEN_BLUE, NB_HAND_NONE, NB_PEN_BLUE, NB_HAND_NONE, NULL };

/* ---- what is written ------------------------------------------------------- */

struct pt {
	float x, y, p;            /* from where it hangs, in line pitches; how hard, 0 to 1 */
};

struct stroke {
	int ink;
	int n, cap;
	struct pt *pts;
	float x0, y0, x1, y1;     /* how far it goes, in pitches */
	/* the start of the line it hangs from: it goes where that goes; or in the heading, 1 its title
	 * from the sheet's left, 2 its date from the sheet's right (at NULL) */
	GtkTextMark *at;
	int head;
	/* as it was last drawn, and from where it hangs */
	cairo_surface_t *pic;
	double pic_x, pic_y;
	int pic_pitch, pic_scale;
};

/* ---- the pad's own view: paper under the text, writing over it ------------- */

#define PAD_TYPE_VIEW (pad_view_get_type())
G_DECLARE_FINAL_TYPE(PadView, pad_view, PAD, VIEW, GtkTextView)

struct _PadView {
	GtkTextView parent;
	int pitch;                /* the rules' spacing where there is no text */
	int sink;                 /* how far above a line's foot its rule is: under the
	                           * letters, with the tails going through it */
	bool own_paper;           /* no notebook under it: the margin is ours as well */
	cairo_pattern_t *blue, *red;
	struct stroke *wet;       /* being written, not on the page yet */
};

G_DEFINE_TYPE(PadView, pad_view, GTK_TYPE_TEXT_VIEW)

enum eol { EOL_LF, EOL_CRLF, EOL_CR };

/* A change of the typing, to be taken back: text put in at, or taken out from, a place. */
struct edit {
	bool put;
	int at;
	char *text;
	int chars;
	unsigned group;
	unsigned serial;
};

/* A thing done with a pen, to be taken back: strokes that came and strokes that went. */
struct action {
	GPtrArray *came, *went;
	unsigned serial;
};

struct pad;

/* A page: what is typed on it and written on it, and where it is kept. */
struct doc {
	GtkTextBuffer *buf;
	struct pad *pad;
	int number;               /* one of the notebook's own pages: which, from 1; else 0 */
	char *path;               /* a file's, or NULL: nothing yet to save it to */
	char *encoding;           /* NULL: UTF-8 */
	const char *bom;          /* what it began with, kept */
	size_t bom_len;
	enum eol eol;
	guint64 file_time;        /* when the file was last as we know it */
	bool have_time;

	GQueue undo, redo;        /* the typing, the last first */
	GQueue ink_undo, ink_redo;  /* the writing, the last last */
	bool replaying;
	unsigned group, in_group, serial, saved_serial;

	char *keep_path;          /* the copy in case of the worst */
	bool keep_dirty;
	unsigned untitled_id;

	GPtrArray *strokes;       /* on it (and they are the actions' as well: not freed with it) */
	int paper;
	/* written in its heading: a title, and a date's year, month and day (NULL: nothing) */
	char *title, *date[3];
	bool unkept;              /* a page of the notebook's, changed since it was kept */
	GtkTextTag *found_tag;
};

/* What can be done, on the sheet under one turned up: each where it is. */
enum {
	DO_OPEN, DO_SAVE, DO_SAVE_AS, DO_PDF, DO_PICTURE, DO_FIND, DO_REPLACE, DO_WRAP, DO_TYPED,
	DO_LINED, DO_SQUARED, DO_DOTTED, DO_PLAIN, DO_RUB_ALL, DO_SHUT, DO_OPEN_OUT, DO_PUT_AWAY,
	DO_PEN, DO_PEN_LAST = DO_PEN + NB_PENS - 1, DO_MARKER, DO_RUBBER, DO_NO_PEN,
};

struct item {
	int what;
	const char *label, *keys;
	bool ticked;
	int row;                  /* those in a row of pictures share it */
	GdkRectangle r;
};

#define ITEMS_MOST 24

/* A window: a notebook, and the pages it has. */
struct pad {
	GtkWidget *win, *overlay, *scroll, *view, *sheet;
	GPtrArray *docs;          /* its pages in order (one of the notebook's not read yet: NULL) */
	int at;
	bool notebook;            /* the notebook's own pages; else files, and pages begun after them */
	bool whole;               /* the whole screen to itself: no case, a page of its own */
	bool opened_out;          /* over the whole desk */
	guint keep_timer, save_timer;

	/* the note for finding things */
	GtkWidget *note, *find_entry, *replace_row, *replace_entry, *count;

	/* what is written in the heading: the page's title, and its date's year, month and day */
	GtkWidget *head[4];
	bool head_filling;        /* (by the program, from the page: not a hand's) */
	bool head_stamping;       /* (today's date, all of it at once) */
	bool head_wide;           /* room for a date */

	/* writing and rubbing out */
	GdkDevice *writing_with;
	bool rubbing;
	struct action *rub;
	bool rubbing_head;
	/* written in the heading, not on it yet; and at what, a part with nothing written there yet
	 * being come to (by a tap, it is not written on: it is to be typed in) */
	struct stroke *head_wet;
	bool head_pen, head_to_type;
	double head_x0, head_y0;

	/* the sheet being turned: it is the sheet of page sheet, over page under */
	struct {
		bool on, held, propped, can_go;
		bool retaken;         /* taken hold of again, up: let go where it was, it falls */
		bool by_corner;       /* taken by a corner: let go where it was, it goes over */
		int dir;              /* 1 on to the next, -1 back */
		int sheet, under;     /* under -1: a clean page, after the last */
		double a, w;          /* how far over it is, 0 flat to pi/2 gone; how fast */
		double prop;          /* propped up this far, what can be done showing */
		double edge0, x0;     /* its free edge and the hand, when it was taken hold of */
		double vx, tx, x;     /* the hand: how fast, when last */
		cairo_surface_t *pic, *under_pic;
		guint tick;
		gint64 last;
		int hot;              /* the item under the hand */
	} turn;
	struct item items[ITEMS_MOST];
	int n_items;

	/* a pen or a mouse, and fingers */
	GtkGesture *pen_g, *head_g, *swipe, *pinch;
	bool finger_turns, finger_writes, finger_head, finger_moved;
	int finger_corner;        /* down on a corner: the way a tap there turns, or 0 */
	double finger_x0, finger_y0;
	gint64 two_at;

	GdkCursor *dot;
	bool dot_shown;
};

static struct pad *pad_new(bool notebook, const char *path);
static void pad_show(struct pad *p, int i);
static void pad_title(struct pad *p);
static void head_fill(struct pad *p);
static struct doc *pad_doc(struct pad *p);
static void pad_close_ask(struct pad *p);
static void pad_changed(struct pad *p, struct doc *d);
static bool doc_save(struct doc *d);
static bool doc_save_as(struct doc *d);
static void doc_keep_drop(struct doc *d);
static void turn_drop(struct pad *p);
static void case_say(void);

/* ---- the settings, the same for every pad ----------------------------------- */

static struct {
	int size;                 /* points */
	bool wrap;
	bool typed;               /* typewriter letters, for what has to line up */
	int paper;                /* how a file's page is ruled */
	int page;                 /* the notebook's page it was open at */
	GtkCssProvider *css;
} g_set = { 13, true, false, PAPER_LINED, 0, NULL };

/* Is it DEsk it is on, which lays a notebook under the window? */
static bool g_on_desk;

static GList *g_pads;
/* the notebook's own: the pad with its pages, and the lock that says it is ours */
static struct pad *g_book;
static int g_book_lock = -1;

static bool on_the_desk(void)
{
	const char *d = getenv("XDG_CURRENT_DESKTOP");
	return d && strstr(d, "DEsk")
		&& !strcmp(G_OBJECT_TYPE_NAME(gdk_display_get_default()), "GdkWaylandDisplay");
}

static char *conf_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "desk", "notepad.conf", NULL);
}

static int conf_int(GKeyFile *k, const char *group, const char *key, int least, int most, int dflt)
{
	GError *e = NULL;
	int v = g_key_file_get_integer(k, group, key, &e);
	if (e) {
		g_error_free(e);
		return dflt;
	}
	return v >= least && v <= most ? v : dflt;
}

static int conf_name(GKeyFile *k, const char *group, const char *key, const char *const *names, int n,
	int dflt)
{
	char *v = g_key_file_get_string(k, group, key, NULL);
	int got = dflt;
	for (int i = 0; v && i < n; i++) {
		if (!strcmp(v, names[i])) {
			got = i;
		}
	}
	g_free(v);
	return got;
}

static void conf_load(void)
{
	char *path = conf_path();
	GKeyFile *k = g_key_file_new();
	if (g_key_file_load_from_file(k, path, G_KEY_FILE_NONE, NULL)) {
		g_set.size = conf_int(k, "pad", "size", SIZE_LEAST, SIZE_MOST, g_set.size);
		GError *e = NULL;
		gboolean b = g_key_file_get_boolean(k, "pad", "wrap", &e);
		if (!e) {
			g_set.wrap = b;
		}
		g_clear_error(&e);
		b = g_key_file_get_boolean(k, "pad", "typewriter", &e);
		if (!e) {
			g_set.typed = b;
		}
		g_clear_error(&e);
		g_set.paper = conf_int(k, "pad", "paper", 0, PAPER_KINDS - 1, g_set.paper);
		g_set.page = conf_int(k, "notebook", "page", 0, 99999, 0);
		g_case.own_pen = conf_name(k, "pens", "pen", PEN_NAME, NB_PENS, g_case.own_pen);
		g_case.own_hand = conf_name(k, "pens", "hand", HAND_NAME, NB_HANDS, g_case.own_hand);
	}
	g_key_file_unref(k);
	g_free(path);
}

static void conf_save(void)
{
	char *path = conf_path();
	char *dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0755);
	GKeyFile *k = g_key_file_new();
	g_key_file_set_integer(k, "pad", "size", g_set.size);
	g_key_file_set_boolean(k, "pad", "wrap", g_set.wrap);
	g_key_file_set_boolean(k, "pad", "typewriter", g_set.typed);
	g_key_file_set_integer(k, "pad", "paper", g_set.paper);
	g_key_file_set_integer(k, "notebook", "page", g_set.page);
	g_key_file_set_string(k, "pens", "pen", PEN_NAME[g_case.own_pen]);
	g_key_file_set_string(k, "pens", "hand", HAND_NAME[g_case.own_hand]);
	g_key_file_save_to_file(k, path, NULL);
	g_key_file_unref(k);
	g_free(dir);
	g_free(path);
}

/* The desk's language, as Shell Settings has it. */
static void language_load(void)
{
	enum desk_lang lang = desk_lang_from_locale();
	char *path = g_build_filename(g_get_user_config_dir(), "desk", "shell.conf", NULL);
	FILE *f = fopen(path, "re");
	g_free(path);
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			char *eq = strchr(line, '=');
			if (!eq) {
				continue;
			}
			*eq = '\0';
			if (!strcmp(g_strstrip(line), "language")) {
				const char *v = g_strstrip(eq + 1);
				if (strcmp(v, "auto") != 0) {
					lang = desk_lang_parse(v);
				}
			}
		}
		fclose(f);
	}
	desk_lang_set(lang);
	desk_font_set_wide_default(desk_lang_wide(lang));
	desk_font_set_region(lang);
}

/* Where the notebook's pictures are: with DEsk's data, wherever that is from here (or under it,
 * in the tree it was built in). */
static char *art_picture(const char *file)
{
	const char *art = g_getenv("DESK_ART_DIR");
	if (art && *art) {
		return g_build_filename(art, "notebook", file, NULL);
	}
	char *exe = g_file_read_link("/proc/self/exe", NULL);
	char *dir = exe ? g_path_get_dirname(exe) : g_strdup(".");
	g_free(exe);
	const char *const places[] = { "../share/desk/notebook", "../art/notebook" };
	char *found = NULL;
	for (size_t i = 0; !found && i < G_N_ELEMENTS(places); i++) {
		char *p = g_build_filename(dir, places[i], file, NULL);
		if (g_file_test(p, G_FILE_TEST_EXISTS)) {
			found = g_canonicalize_filename(p, NULL);
		}
		g_free(p);
	}
	g_free(dir);
#ifdef DESK_DATADIR
	if (!found) {
		char *p = g_build_filename(DESK_DATADIR, "desk", "notebook", file, NULL);
		if (g_file_test(p, G_FILE_TEST_EXISTS)) {
			found = p;
		} else {
			g_free(p);
		}
	}
#endif
	return found;
}

/* The desk's own paper and wire, for a sheet turned over on DEsk: read the first time one is. */
static struct {
	bool tried, ok;
	struct desk_nb_art art;
} g_paper;

static cairo_surface_t *art_read(const char *file)
{
	char *path = art_picture(file);
	cairo_surface_t *img = path ? cairo_image_surface_create_from_png(path) : NULL;
	g_free(path);
	if (img && cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		img = NULL;
	}
	return img;
}

static bool paper_ready(void)
{
	if (!g_paper.tried) {
		g_paper.tried = true;
		g_paper.art.page = art_read("nb-page.png");
		g_paper.art.rings = art_read("nb-rings.png");
		g_paper.art.grain = art_read("nb-grain.png");
		g_paper.ok = g_paper.art.page && g_paper.art.rings && g_paper.art.grain;
		g_paper.art.blue = desk_nb_ink(NB_BLUE, NB_BLUE_A);
		g_paper.art.blue_faint = desk_nb_ink(NB_BLUE, NB_BLUE_A * 0.62);
		g_paper.art.red = desk_nb_ink(NB_RED, NB_RED_A);
	}
	return g_paper.ok;
}

/* The look: the ink, the letters at their size, a highlighter for what is
 * selected, the note for finding things; and on the desk nothing of the program's
 * under any of it, or a page if it is on its own. */
static void style_apply(void)
{
	if (!g_set.css) {
		g_set.css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(g_set.css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	}
	const char *family = desk_font_family(g_set.typed ? DESK_FONT_TYPE : DESK_FONT_HAND, NULL);
	/* (the heading is written by hand, whatever the page is typed in) */
	const char *hand = desk_font_family(DESK_FONT_HAND, NULL);
	char *note_path = art_picture("nb-note.png");
	char *note_uri = note_path ? g_filename_to_uri(note_path, NULL, NULL) : NULL;
	char *note_look = note_uri
		? g_strdup_printf("background: url(\"%s\"); background-size: 100%% 100%%; border: none; box-shadow: none;", note_uri)
		: g_strdup("background-color: #f4e58e; border: 1px solid #b9a64b; box-shadow: 0 2px 5px rgba(40, 30, 10, 0.35);");
	g_free(note_path);
	g_free(note_uri);
	char *css = g_strdup_printf(
		"window.pad-win { background: %s; }\n"
		"window.pad-win.whole { background: " PAPER_CSS "; }\n"
		"window.pad-win scrolledwindow undershoot, window.pad-win scrolledwindow overshoot {"
		" background: none; border: none; box-shadow: none; }\n"
		"window.pad-win scrollbar, window.pad-win scrollbar trough {"
		" background: none; border: none; box-shadow: none; }\n"
		"window.pad-win scrollbar slider { background-color: rgba(27, 42, 87, 0.30);"
		" border: 1px solid transparent; border-radius: 4px; min-width: 4px; min-height: 28px; }\n"
		"window.pad-win scrollbar slider:hover { background-color: rgba(27, 42, 87, 0.45); }\n"
		"textview.pad, textview.pad text {"
		" background-color: transparent; color: rgba(27, 42, 87, 0.94); caret-color: " INK_CSS ";"
		" font-family: \"%s\"; font-weight: %d; font-size: %dpt; }\n"
		"textview.pad text selection { background-color: rgba(255, 214, 40, 0.58); color: " INK_CSS "; }\n"
		"box.note { %s color: #3a3320; padding: 19px 22px 23px 22px; margin: 4px 6px 2px 6px; }\n"
		"box.note label { color: #3a3320; }\n"
		"box.note entry { background: none; border: none; border-bottom: 1px solid rgba(58, 51, 32, 0.55);"
		" border-radius: 0; box-shadow: none; outline: none; color: " INK_CSS "; caret-color: " INK_CSS ";"
		" font-family: \"%s\"; font-size: 12pt; min-height: 22px; padding: 0 2px; }\n"
		"box.note entry placeholder { color: rgba(58, 51, 32, 0.55); }\n"
		"box.note button { background: none; border: none; box-shadow: none; color: #3a3320;"
		" padding: 1px 7px; min-height: 22px; min-width: 14px; border-radius: 3px; text-shadow: none; }\n"
		"box.note button:hover { background: rgba(58, 51, 32, 0.13); }\n"
		"box.note button:active { background: rgba(58, 51, 32, 0.24); }\n"
		"entry.head, entry.head:focus { background: none; border: none; border-radius: 0; box-shadow: none;"
		" outline: none; padding: 0; margin: 0; min-height: 0; min-width: 0;"
		" color: rgba(27, 42, 87, 0.94); caret-color: " INK_CSS ";"
		" font-family: \"%s\"; font-weight: %d; font-size: 18px; }\n"
		"entry.head.date { font-size: 15px; }\n"
		"entry.head selection { background-color: rgba(255, 214, 40, 0.58); color: " INK_CSS "; }\n",
		g_on_desk ? "transparent" : PAPER_CSS, family, strstr(family, "Klee") ? 600 : 400, g_set.size,
		note_look, family, hand, strstr(hand, "Klee") ? 600 : 400);
	gtk_css_provider_load_from_data(g_set.css, css, -1, NULL);
	g_free(css);
	g_free(note_look);
}

/* ---- strokes ---------------------------------------------------------------- */

static double ink_width(int ink, double p, double pitch)
{
	double firm = INK[ink].firm;
	double w = INK[ink].width * (1.0 - firm + firm * (0.25 + 1.5 * pow(p, 0.8))) * pitch;
	return w < 0.7 ? 0.7 : w;
}

static struct stroke *stroke_new(int ink)
{
	struct stroke *s = g_new0(struct stroke, 1);
	s->ink = ink;
	return s;
}

static void stroke_free(gpointer data)
{
	struct stroke *s = data;
	if (s->at) {
		GtkTextBuffer *buf = gtk_text_mark_get_buffer(s->at);
		if (buf) {
			gtk_text_buffer_delete_mark(buf, s->at);
		}
		g_object_unref(s->at);
	}
	if (s->pic) {
		cairo_surface_destroy(s->pic);
	}
	g_free(s->pts);
	g_free(s);
}

static void stroke_add(struct stroke *s, double x, double y, double p)
{
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 64;
		s->pts = g_renew(struct pt, s->pts, s->cap);
	}
	s->pts[s->n++] = (struct pt){ (float)x, (float)y, (float)p };
	if (s->n == 1) {
		s->x0 = s->x1 = (float)x;
		s->y0 = s->y1 = (float)y;
	} else {
		s->x0 = fminf(s->x0, (float)x);
		s->x1 = fmaxf(s->x1, (float)x);
		s->y0 = fminf(s->y0, (float)y);
		s->y1 = fmaxf(s->y1, (float)y);
	}
}

/* It hangs from here: the start of a line of the buffer's, kept as the words change. */
static void stroke_hang(struct stroke *s, GtkTextBuffer *buf, const GtkTextIter *at)
{
	s->at = gtk_text_buffer_create_mark(buf, NULL, at, TRUE);
	g_object_ref(s->at);
}

/* Where it hangs from, in a view's buffer coordinates. */
static void stroke_hook(GtkTextView *tv, const struct stroke *s, double *x, double *y)
{
	GtkTextIter it;
	gtk_text_buffer_get_iter_at_mark(gtk_text_view_get_buffer(tv), &it, s->at);
	GdkRectangle r;
	gtk_text_view_get_iter_location(tv, &it, &r);
	*x = LEFT_MARGIN;
	*y = r.y;
}

/* A stretch of it, from middle to middle of the lines between the points, so that it goes round them
 * and not from one to the next: as many short lines as make that smooth, each as wide as it should
 * be there. Up to the middle before point `to`, or the last point if that is the end. From ox, oy,
 * a pitch to its unit. */
static void stroke_paint_range(cairo_t *cr, const struct stroke *s, int from, int to, bool ending,
	double pitch, double ox, double oy)
{
	cairo_set_source_rgba(cr, INK[s->ink].r, INK[s->ink].g, INK[s->ink].b, INK[s->ink].a);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
#define PX(v) (ox + (v) * pitch)
#define PY(v) (oy + (v) * pitch)
	if (s->n == 1) {
		double w = ink_width(s->ink, s->pts[0].p, pitch);
		cairo_new_sub_path(cr);
		cairo_arc(cr, PX(s->pts[0].x), PY(s->pts[0].y), w * 0.6, 0.0, 2.0 * G_PI);
		cairo_fill(cr);
		return;
	}
	for (int i = from; i < to && i + 1 < s->n; i++) {
		const struct pt *a = &s->pts[i], *b = &s->pts[i + 1];
		const struct pt *c = i + 2 < s->n ? &s->pts[i + 2] : NULL;
		/* from the middle of a..b, round b, to the middle of b..c */
		double x0 = i == 0 ? PX(a->x) : PX((a->x + b->x) * 0.5), y0 = i == 0 ? PY(a->y) : PY((a->y + b->y) * 0.5);
		double p0 = i == 0 ? a->p : (a->p + b->p) * 0.5;
		double cx = PX(b->x), cy = PY(b->y);
		double x1, y1, p1;
		if (c) {
			x1 = PX((b->x + c->x) * 0.5);
			y1 = PY((b->y + c->y) * 0.5);
			p1 = (b->p + c->p) * 0.5;
		} else {
			x1 = PX(b->x);
			y1 = PY(b->y);
			p1 = b->p;
			if (!ending) {
				break;
			}
		}
		double len = hypot(cx - x0, cy - y0) + hypot(x1 - cx, y1 - cy);
		int steps = (int)ceil(len / 1.4);
		steps = steps < 1 ? 1 : steps;
		double px = x0, py = y0;
		for (int k = 1; k <= steps; k++) {
			double t = (double)k / steps, m = 1.0 - t;
			double x = m * m * x0 + 2.0 * m * t * cx + t * t * x1;
			double y = m * m * y0 + 2.0 * m * t * cy + t * t * y1;
			cairo_set_line_width(cr, ink_width(s->ink, p0 + (p1 - p0) * t, pitch));
			cairo_move_to(cr, px, py);
			cairo_line_to(cr, x, y);
			cairo_stroke(cr);
			px = x;
			py = y;
		}
	}
}

/* All of a stroke. A highlighter's is one wide line, for it goes over itself no darker. */
static void stroke_paint(cairo_t *cr, const struct stroke *s, double pitch, double ox, double oy)
{
	if (s->ink == INK_MARKER && s->n > 1) {
		cairo_set_source_rgba(cr, INK[s->ink].r, INK[s->ink].g, INK[s->ink].b, INK[s->ink].a);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_set_line_width(cr, INK[s->ink].width * pitch);
		cairo_move_to(cr, PX(s->pts[0].x), PY(s->pts[0].y));
		for (int i = 1; i < s->n; i++) {
			cairo_line_to(cr, PX(s->pts[i].x), PY(s->pts[i].y));
		}
		cairo_stroke(cr);
		return;
	}
	stroke_paint_range(cr, s, 0, s->n, true, pitch, ox, oy);
}
#undef PX
#undef PY

/* How far past its points the ink of it goes, in pixels. */
static double stroke_reach(const struct stroke *s, double pitch)
{
	return (s->ink == INK_MARKER ? INK[s->ink].width * 0.5 : INK[s->ink].width * 1.4) * pitch + 2.0;
}

/* A picture of it as it is at this pitch, for drawing it again and again: from where it hangs,
 * pic_x, pic_y. */
static void stroke_picture(struct stroke *s, int pitch, int scale)
{
	if (s->pic && s->pic_pitch == pitch && s->pic_scale == scale) {
		return;
	}
	if (s->pic) {
		cairo_surface_destroy(s->pic);
	}
	double reach = stroke_reach(s, pitch);
	double x0 = floor(s->x0 * pitch - reach), y0 = floor(s->y0 * pitch - reach);
	int w = (int)ceil(s->x1 * pitch + reach - x0), h = (int)ceil(s->y1 * pitch + reach - y0);
	w = w < 1 ? 1 : w;
	h = h < 1 ? 1 : h;
	s->pic = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * scale, h * scale);
	cairo_surface_set_device_scale(s->pic, scale, scale);
	cairo_t *cr = cairo_create(s->pic);
	stroke_paint(cr, s, pitch, -x0, -y0);
	cairo_destroy(cr);
	s->pic_x = x0;
	s->pic_y = y0;
	s->pic_pitch = pitch;
	s->pic_scale = scale;
}

/* ---- the view ------------------------------------------------------------------ */

static struct doc *view_doc(GtkTextView *tv)
{
	return g_object_get_data(G_OBJECT(gtk_text_view_get_buffer(tv)), "desk-doc");
}

/* The foot of every line of writing, however tall a line is that has letters from another font in
 * it, and on down the sheet at the usual spacing: where the rules go. */
static int pad_view_feet(GtkTextView *tv, const GdkRectangle *vis, int *feet, int most)
{
	PadView *self = PAD_VIEW(tv);
	int bottom = vis->y + vis->height - 3;
	int n = 0;
	GtkTextIter it;
	gtk_text_view_get_line_at_y(tv, &it, vis->y, NULL);
	gtk_text_view_backward_display_line_start(tv, &it);
	int last = vis->y;
	GdkRectangle at;
	for (;;) {
		gtk_text_view_get_iter_location(tv, &it, &at);
		last = at.y + at.height - self->sink;
		if (last >= vis->y - 2 && last <= bottom && n < most) {
			feet[n++] = last;
		}
		if (last > bottom) {
			break;
		}
		if (!gtk_text_view_forward_display_line(tv, &it)) {
			break;
		}
	}
	int pitch = self->pitch > 4 ? self->pitch : 24;
	for (int y = last + pitch; y <= bottom && n < most; y += pitch) {
		if (y >= vis->y) {
			feet[n++] = y;
		}
	}
	return n;
}

/* The page's ruling, under every line and on down the sheet: rules, squares, dots or nothing. */
static void pad_view_rules(GtkTextView *tv, cairo_t *cr, const GdkRectangle *vis, int paper)
{
	PadView *self = PAD_VIEW(tv);
	int x0 = vis->x, x1 = vis->x + vis->width;
	/* (none in the last of the sheet: its corner is off the page under it) */
	int bottom = vis->y + vis->height - 3;
	if (!self->blue) {
		self->blue = desk_nb_ink(NB_BLUE, NB_BLUE_A);
		self->red = desk_nb_ink(NB_RED, NB_RED_A);
	}
	if (self->own_paper) {
		desk_nb_line_down(cr, self->red, MARGIN_AT, vis->y, bottom + 3);
	}
	if (paper == PAPER_PLAIN) {
		return;
	}
	int feet[512];
	int n = pad_view_feet(tv, vis, feet, G_N_ELEMENTS(feet));
	int pitch = self->pitch > 4 ? self->pitch : 24;
	if (paper == PAPER_DOTTED) {
		cairo_set_source_rgba(cr, NB_BLUE, 0.72);
		for (int i = 0; i < n; i++) {
			for (int x = MARGIN_AT + pitch; x < x1 - 2; x += pitch) {
				if (x >= x0) {
					cairo_new_sub_path(cr);
					cairo_arc(cr, x, feet[i], 0.95, 0.0, 2.0 * G_PI);
				}
			}
		}
		cairo_fill(cr);
		return;
	}
	for (int i = 0; i < n; i++) {
		desk_nb_line_across(cr, self->blue, x0, x1, feet[i]);
	}
	if (paper == PAPER_SQUARED) {
		/* down the sheet from the margin, as far apart as the lines */
		for (int x = MARGIN_AT + pitch; x < x1 - 2; x += pitch) {
			if (x >= x0) {
				desk_nb_line_down(cr, self->blue, x, vis->y, bottom);
			}
		}
		for (int x = MARGIN_AT - pitch; x > 2; x -= pitch) {
			if (x <= x1) {
				desk_nb_line_down(cr, self->blue, x, vis->y, bottom);
			}
		}
	}
}

/* And, down in the corner the page is lifted by, as somebody would pencil it on a new notebook's
 * first page: what the corner is for, and an arrow to it (2026-11-12, the owner: ノートがわかりにくい
 * ので、何も書き込んでないときは右下をクリックして詳細を表示みたいな文字を書いておく（手書き風の
 * フォント、矢印込）). Gone with the first thing typed or written, as the hint above is. */
static void pad_view_corner_hint(GtkTextView *tv, cairo_t *cr)
{
	GdkRectangle vis;
	gtk_text_view_get_visible_rect(tv, &vis);
	if (vis.width < 160 || vis.height < 120) {
		return;
	}
	const char *say = TR("Click this corner for more");
	PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(tv), say);
	PangoFontDescription *fd = pango_font_description_from_string(desk_font_family(DESK_FONT_HAND, say));
	double size = fmin(22.0, fmax(14.0, vis.width / 26.0));
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	/* no wider than most of the page: a long language goes onto two lines */
	pango_layout_set_alignment(pl, PANGO_ALIGN_RIGHT);
	PangoRectangle lg;
	pango_layout_get_pixel_extents(pl, NULL, &lg);
	if (lg.width > vis.width * 0.62) {
		pango_layout_set_width(pl, (int)(vis.width * 0.62 * PANGO_SCALE));
		pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
		pango_layout_get_pixel_extents(pl, NULL, &lg);
	}
	double tw = lg.width, th = lg.height;
	/* the corner, and the words up and to the left of it, with room for the arrow between */
	double cx = vis.x + vis.width - 10.0, cy = vis.y + vis.height - 10.0;
	double tx = cx - size * 2.6 - tw, ty = cy - size * 3.4 - th;
	if (ty < vis.y + vis.height * 0.40) {
		g_object_unref(pl);
		return;   /* (a page too short for it under the first line's hint) */
	}
	/* in pencil, a little darker than the first line's, slightly turned as handwriting is */
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.18, 0.20, 0.30, 0.55);
	cairo_translate(cr, tx + tw * 0.5, ty + th * 0.5);
	cairo_rotate(cr, -0.035);
	cairo_move_to(cr, -tw * 0.5 - lg.x, -th * 0.5 - lg.y);
	pango_cairo_show_layout(cr, pl);
	cairo_restore(cr);
	g_object_unref(pl);
	/* the arrow: from under the end of the words, a curve down to the corner, drawn in one go */
	double ax = tx + tw - size * 0.3, ay = ty + th + size * 0.35;
	double ex = cx - size * 0.55, ey = cy - size * 0.55;
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.18, 0.20, 0.30, 0.55);
	cairo_set_line_width(cr, fmax(1.4, size * 0.09));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, ax, ay);
	cairo_curve_to(cr, ax + size * 0.4, ay + size * 1.6, ex - size * 1.7, ey + size * 0.15, ex, ey);
	cairo_stroke(cr);
	/* its head, along the way it comes in */
	double ang = atan2(ey - (ey + size * 0.15), ex - (ex - size * 1.7));
	double hl = size * 0.7;
	cairo_move_to(cr, ex - hl * cos(ang - 0.45), ey - hl * sin(ang - 0.45));
	cairo_line_to(cr, ex, ey);
	cairo_line_to(cr, ex - hl * cos(ang + 0.5), ey - hl * sin(ang + 0.5));
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* On a blank sheet, faintly, what there is to do. */
static void pad_view_hint(GtkTextView *tv, cairo_t *cr)
{
	GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
	struct doc *d = view_doc(tv);
	if (gtk_text_buffer_get_char_count(buf) > 0 || (d && d->strokes->len > 0) || PAD_VIEW(tv)->wet) {
		return;
	}
	GtkTextIter it;
	gtk_text_buffer_get_start_iter(buf, &it);
	GdkRectangle at;
	gtk_text_view_get_iter_location(tv, &it, &at);
	const char *say = d && d->number ? TR("Type here, or write with a pen")
		: TR("Ctrl+O open    Ctrl+S save    Ctrl+F find");
	PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(tv), say);
	PangoFontDescription *fd = pango_font_description_copy(
		pango_context_get_font_description(pango_layout_get_context(pl)));
	pango_font_description_set_size(fd, pango_font_description_get_size(fd) * 3 / 4);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	/* in pencil, on the first line */
	cairo_set_source_rgba(cr, 0.20, 0.20, 0.22, 0.42);
	double base = pango_layout_get_baseline(pl) / (double)PANGO_SCALE;
	cairo_move_to(cr, at.x + 2, at.y + at.height - PAD_VIEW(tv)->sink - 3 - base);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
	pad_view_corner_hint(tv, cr);
}

/* What is written on the page that shows, the highlighter's or the rest: from pictures of each, or,
 * for a picture that is not on a screen, as lines. */
static void pad_view_ink(GtkTextView *tv, cairo_t *cr, bool marker)
{
	PadView *self = PAD_VIEW(tv);
	struct doc *d = view_doc(tv);
	int pitch = self->pitch > 4 ? self->pitch : 24;
	int scale = gtk_widget_get_scale_factor(GTK_WIDGET(tv));
	bool lines = cairo_surface_get_type(cairo_get_target(cr)) != CAIRO_SURFACE_TYPE_IMAGE
		&& cairo_surface_get_type(cairo_get_target(cr)) != CAIRO_SURFACE_TYPE_XLIB;
	double cx0, cy0, cx1, cy1;
	cairo_clip_extents(cr, &cx0, &cy0, &cx1, &cy1);
	for (guint i = 0; d && i < d->strokes->len; i++) {
		struct stroke *s = g_ptr_array_index(d->strokes, i);
		if ((s->ink == INK_MARKER) != marker || !s->at) {
			continue;
		}
		double hx, hy;
		stroke_hook(tv, s, &hx, &hy);
		double reach = stroke_reach(s, pitch);
		if (hx + s->x1 * pitch + reach < cx0 || hx + s->x0 * pitch - reach > cx1
			|| hy + s->y1 * pitch + reach < cy0 || hy + s->y0 * pitch - reach > cy1) {
			continue;
		}
		if (lines) {
			stroke_paint(cr, s, pitch, hx, hy);
			continue;
		}
		stroke_picture(s, pitch, scale);
		cairo_set_source_surface(cr, s->pic, hx + s->pic_x, hy + s->pic_y);
		cairo_paint(cr);
	}
	struct stroke *w = self->wet;
	if (w && w->at && (w->ink == INK_MARKER) == marker) {
		double hx, hy;
		stroke_hook(tv, w, &hx, &hy);
		stroke_paint(cr, w, pitch, hx, hy);
	}
}

static void pad_view_draw_layer(GtkTextView *tv, GtkTextViewLayer layer, cairo_t *cr)
{
	struct doc *d = view_doc(tv);
	if (layer == GTK_TEXT_VIEW_LAYER_BELOW_TEXT) {
		GdkRectangle vis;
		gtk_text_view_get_visible_rect(tv, &vis);
		cairo_save(cr);
		pad_view_rules(tv, cr, &vis, d ? d->paper : PAPER_LINED);
		pad_view_hint(tv, cr);
		/* the highlighter under the words, as it is on paper under print */
		pad_view_ink(tv, cr, true);
		cairo_restore(cr);
	}
	/* (GTK's own has none) */
	if (GTK_TEXT_VIEW_CLASS(pad_view_parent_class)->draw_layer) {
		GTK_TEXT_VIEW_CLASS(pad_view_parent_class)->draw_layer(tv, layer, cr);
	}
	if (layer == GTK_TEXT_VIEW_LAYER_ABOVE_TEXT) {
		cairo_save(cr);
		pad_view_ink(tv, cr, false);
		cairo_restore(cr);
	}
}

static void pad_view_finalize(GObject *o)
{
	PadView *self = PAD_VIEW(o);
	if (self->blue) {
		cairo_pattern_destroy(self->blue);
		cairo_pattern_destroy(self->red);
	}
	if (self->wet) {
		stroke_free(self->wet);
	}
	G_OBJECT_CLASS(pad_view_parent_class)->finalize(o);
}

static void pad_view_class_init(PadViewClass *klass)
{
	GTK_TEXT_VIEW_CLASS(klass)->draw_layer = pad_view_draw_layer;
	G_OBJECT_CLASS(klass)->finalize = pad_view_finalize;
}

static void pad_view_init(PadView *self)
{
	self->pitch = 24;
}

/* ---- a page ------------------------------------------------------------------ */

static const char *doc_name(const struct doc *d, char *out, size_t n)
{
	if (d->path) {
		char *b = g_filename_display_basename(d->path);
		snprintf(out, n, "%s", b);
		g_free(b);
	} else if (d->number) {
		snprintf(out, n, "%s  %d", TR("Notebook"), d->number);
	} else {
		snprintf(out, n, "%s", TR("Untitled"));
	}
	return out;
}

static void say(struct pad *p, const char *text)
{
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(p->win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
		GTK_BUTTONS_OK, "%s", text);
	gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
}

/* Anything written in its heading? */
static bool doc_headed(const struct doc *d)
{
	return d->title || d->date[0] || d->date[1] || d->date[2];
}

static bool doc_blank(const struct doc *d)
{
	return gtk_text_buffer_get_char_count(d->buf) == 0 && d->strokes->len == 0 && !doc_headed(d);
}

/* What was done last to it, of the typing and the writing. */
static unsigned doc_top(struct doc *d)
{
	const struct edit *e = g_queue_peek_head(&d->undo);
	const struct action *a = g_queue_peek_tail(&d->ink_undo);
	unsigned t = e ? e->serial : 0, k = a ? a->serial : 0;
	return t > k ? t : k;
}

/* Back as it was saved, it is not changed any more; a page of the notebook's is kept anyway. */
static void doc_settle(struct doc *d)
{
	if (d->number) {
		gtk_text_buffer_set_modified(d->buf, FALSE);
		return;
	}
	gtk_text_buffer_set_modified(d->buf, doc_top(d) != d->saved_serial);
}

/* ---- taking things back ------------------------------------------------------- */

static void edit_free(struct edit *e)
{
	g_free(e->text);
	g_free(e);
}

static void edits_clear(GQueue *q)
{
	struct edit *e;
	while ((e = g_queue_pop_head(q))) {
		edit_free(e);
	}
}

static void action_free(gpointer data)
{
	struct action *a = data;
	g_ptr_array_free(a->came, TRUE);
	g_ptr_array_free(a->went, TRUE);
	g_free(a);
}

static struct action *action_new(void)
{
	struct action *a = g_new0(struct action, 1);
	a->came = g_ptr_array_new_with_free_func(NULL);
	a->went = g_ptr_array_new_with_free_func(stroke_free);
	return a;
}

static void actions_clear(GQueue *q)
{
	struct action *a;
	while ((a = g_queue_pop_head(q))) {
		action_free(a);
	}
}

/* Something new done: what had been taken back cannot be put back now. */
static void doc_redo_clear(struct doc *d)
{
	edits_clear(&d->redo);
	actions_clear(&d->ink_redo);
}

static void doc_edit_add(struct doc *d, bool put, int at, const char *text, int bytes)
{
	doc_redo_clear(d);
	char *t = g_strndup(text, (gsize)bytes);
	int chars = (int)g_utf8_strlen(t, -1);
	struct edit *last = g_queue_peek_head(&d->undo);
	const struct action *ink = g_queue_peek_tail(&d->ink_undo);
	/* one letter after another is one thing to take back, up to the end of a
	 * word; and so is rubbing out (unless something was written in between) */
	if (last && chars == 1 && last->put == put && !g_unichar_isspace(g_utf8_get_char(t))
		&& (!ink || ink->serial < last->serial)
		&& (put ? last->at + last->chars == at : at + 1 == last->at)) {
		char *joined = put ? g_strconcat(last->text, t, NULL) : g_strconcat(t, last->text, NULL);
		g_free(last->text);
		g_free(t);
		last->text = joined;
		last->chars += 1;
		last->at = put ? last->at : at;
		last->serial = ++d->serial;
		return;
	}
	struct edit *e = g_new0(struct edit, 1);
	e->put = put;
	e->at = at;
	e->text = t;
	e->chars = chars;
	e->group = d->in_group ? d->group : 0;
	e->serial = ++d->serial;
	g_queue_push_head(&d->undo, e);
	/* (a long way back is far enough) */
	if (g_queue_get_length(&d->undo) > 4000) {
		edit_free(g_queue_pop_tail(&d->undo));
	}
}

static void on_insert(GtkTextBuffer *buf, GtkTextIter *at, gchar *text, gint len, gpointer data)
{
	struct doc *d = data;
	(void)buf;
	if (!d->replaying) {
		doc_edit_add(d, true, gtk_text_iter_get_offset(at), text, len);
	}
}

static void on_delete(GtkTextBuffer *buf, GtkTextIter *a, GtkTextIter *b, gpointer data)
{
	struct doc *d = data;
	if (d->replaying) {
		return;
	}
	char *t = gtk_text_buffer_get_text(buf, a, b, TRUE);
	doc_edit_add(d, false, gtk_text_iter_get_offset(a), t, (int)strlen(t));
	g_free(t);
}

static void on_begin_action(GtkTextBuffer *buf, gpointer data)
{
	struct doc *d = data;
	(void)buf;
	if (d->in_group++ == 0) {
		d->group++;
	}
}

static void on_end_action(GtkTextBuffer *buf, gpointer data)
{
	struct doc *d = data;
	(void)buf;
	if (d->in_group > 0) {
		d->in_group--;
	}
}

/* One edit, or its opposite. */
static void edit_apply(struct doc *d, const struct edit *e, bool forward)
{
	GtkTextIter a, b;
	gtk_text_buffer_get_iter_at_offset(d->buf, &a, e->at);
	if (e->put == forward) {
		gtk_text_buffer_insert(d->buf, &a, e->text, -1);
		gtk_text_buffer_get_iter_at_offset(d->buf, &a, e->at + e->chars);
	} else {
		gtk_text_buffer_get_iter_at_offset(d->buf, &b, e->at + e->chars);
		gtk_text_buffer_delete(d->buf, &a, &b);
		gtk_text_buffer_get_iter_at_offset(d->buf, &a, e->at);
	}
	gtk_text_buffer_place_cursor(d->buf, &a);
}

static void edits_replay(struct doc *d, GQueue *from, GQueue *to, bool forward)
{
	struct edit *e = g_queue_peek_head(from);
	if (!e) {
		return;
	}
	unsigned group = e->group;
	d->replaying = true;
	do {
		e = g_queue_pop_head(from);
		edit_apply(d, e, forward);
		g_queue_push_head(to, e);
		e = g_queue_peek_head(from);
	} while (e && group != 0 && e->group == group);
	d->replaying = false;
}

/* Move strokes on or off the page, by what the action says, and turn it round. */
static void action_apply(struct doc *d, struct action *a)
{
	for (guint i = 0; i < a->came->len; i++) {
		g_ptr_array_remove_fast(d->strokes, g_ptr_array_index(a->came, i));
	}
	for (guint i = 0; i < a->went->len; i++) {
		g_ptr_array_add(d->strokes, g_ptr_array_index(a->went, i));
	}
	GPtrArray *t = a->came;
	a->came = a->went;
	a->went = t;
	/* (what went is on no page, and goes with the action) */
	g_ptr_array_set_free_func(a->came, NULL);
	g_ptr_array_set_free_func(a->went, stroke_free);
}

/* Back a step, or on one: the typing's or the writing's, whichever was last. */
static void doc_undo(struct doc *d, bool back)
{
	const struct edit *e = g_queue_peek_head(back ? &d->undo : &d->redo);
	struct action *a = g_queue_peek_tail(back ? &d->ink_undo : &d->ink_redo);
	if (!e && !a) {
		return;
	}
	/* back: the later of the two; on: the earlier, as it was the last taken back */
	bool ink = a && (!e || (back ? a->serial > e->serial : a->serial < e->serial));
	if (ink) {
		g_queue_pop_tail(back ? &d->ink_undo : &d->ink_redo);
		action_apply(d, a);
		g_queue_push_tail(back ? &d->ink_redo : &d->ink_undo, a);
	} else {
		edits_replay(d, back ? &d->undo : &d->redo, back ? &d->redo : &d->undo, !back);
	}
	doc_settle(d);
	if (d->pad) {
		gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(d->pad->view), gtk_text_buffer_get_insert(d->buf));
		pad_changed(d->pad, d);
		gtk_widget_queue_draw(d->pad->view);
		gtk_widget_queue_draw(d->pad->sheet);
	}
}

/* Something written or rubbed out, to be taken back. */
static void doc_action_done(struct doc *d, struct action *a)
{
	doc_redo_clear(d);
	a->serial = ++d->serial;
	g_queue_push_tail(&d->ink_undo, a);
	doc_settle(d);
}

/* ---- the copy in case of the worst -------------------------------------------- */

static char *keep_dir(void)
{
	return g_build_filename(g_get_user_state_dir(), "desk", "notepad", NULL);
}

/* Where the copy of this page goes: by the name of the file, or, for what has
 * none, a number of its own. */
static void doc_keep_where(struct doc *d)
{
	g_free(d->keep_path);
	char *dir = keep_dir();
	char *name;
	if (d->path) {
		char *h = g_compute_checksum_for_string(G_CHECKSUM_SHA1, d->path, -1);
		name = g_strconcat(h, ".keep", NULL);
		g_free(h);
	} else {
		/* (with who has it: a program still running keeps its own) */
		if (!d->untitled_id) {
			static unsigned next;
			d->untitled_id = (unsigned)time(NULL) ^ ++next;
		}
		name = g_strdup_printf("untitled-%d-%08x.keep", (int)getpid(), d->untitled_id);
	}
	d->keep_path = g_build_filename(dir, name, NULL);
	g_free(name);
	g_free(dir);
}

static void doc_keep_drop(struct doc *d)
{
	if (d->number) {
		return;
	}
	doc_keep_where(d);
	g_unlink(d->keep_path);
	d->keep_dirty = false;
}

/* The file's first line says what it is a copy of (nothing, for a page with no
 * file), the rest is what was typed. */
static void doc_keep_now(struct doc *d)
{
	if (!d->keep_dirty || d->number) {
		return;
	}
	d->keep_dirty = false;
	if (!gtk_text_buffer_get_modified(d->buf)) {
		doc_keep_drop(d);
		return;
	}
	char *dir = keep_dir();
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	doc_keep_where(d);
	GtkTextIter a, b;
	gtk_text_buffer_get_bounds(d->buf, &a, &b);
	char *text = gtk_text_buffer_get_text(d->buf, &a, &b, TRUE);
	char *all = g_strconcat(d->path ? d->path : "", "\n", text, NULL);
	g_file_set_contents(d->keep_path, all, -1, NULL);
	g_chmod(d->keep_path, 0600);
	g_free(all);
	g_free(text);
}

/* ---- reading and writing ------------------------------------------------------ */

static char *line_ends_in(const char *text, size_t len, enum eol *eol)
{
	/* as the first line does */
	*eol = EOL_LF;
	const char *n = memchr(text, '\n', len);
	const char *r = memchr(text, '\r', len);
	if (r && n && n == r + 1 && r < n) {
		*eol = EOL_CRLF;
	} else if (r && (!n || r < n)) {
		*eol = EOL_CR;
	}
	char *out = g_malloc(len + 1);
	size_t w = 0;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\r') {
			out[w++] = '\n';
			if (i + 1 < len && text[i + 1] == '\n') {
				i++;
			}
		} else {
			out[w++] = text[i];
		}
	}
	out[w] = '\0';
	return out;
}

static char *line_ends_out(const char *text, enum eol eol, size_t *out_len)
{
	GString *s = g_string_sized_new(strlen(text) + 64);
	for (const char *p = text; *p; p++) {
		if (*p == '\n' && eol == EOL_CRLF) {
			g_string_append_len(s, "\r\n", 2);
		} else if (*p == '\n' && eol == EOL_CR) {
			g_string_append_c(s, '\r');
		} else {
			g_string_append_c(s, *p);
		}
	}
	*out_len = s->len;
	return g_string_free(s, FALSE);
}

/* What the bytes are written in: a mark at the start says; UTF-8 if they are
 * that; otherwise the encodings of the desk's language, then Western. */
static char *decode(const char *bytes, size_t len, char **encoding, const char **bom, size_t *bom_len,
	bool *not_text)
{
	*encoding = NULL;
	*bom = NULL;
	*bom_len = 0;
	*not_text = false;
	if (len >= 3 && !memcmp(bytes, "\xef\xbb\xbf", 3)) {
		*bom = "\xef\xbb\xbf";
		*bom_len = 3;
		bytes += 3;
		len -= 3;
	} else if (len >= 2 && (!memcmp(bytes, "\xff\xfe", 2) || !memcmp(bytes, "\xfe\xff", 2))) {
		bool le = bytes[0] == '\xff';
		*bom = le ? "\xff\xfe" : "\xfe\xff";
		*bom_len = 2;
		char *t = g_convert(bytes + 2, (gssize)len - 2, "UTF-8", le ? "UTF-16LE" : "UTF-16BE", NULL, NULL, NULL);
		if (t) {
			*encoding = g_strdup(le ? "UTF-16LE" : "UTF-16BE");
			return t;
		}
		*bom = NULL;
		*bom_len = 0;
	}
	if (memchr(bytes, '\0', len)) {
		*not_text = true;
		return NULL;
	}
	if (g_utf8_validate(bytes, (gssize)len, NULL)) {
		return g_strndup(bytes, len);
	}
	static const char *const JA[] = { "CP932", "EUC-JP", NULL };
	static const char *const ZH[] = { "GB18030", "BIG5", NULL };
	static const char *const KO[] = { "CP949", NULL };
	static const char *const OTHERS[] = { "WINDOWS-1252", NULL };
	const char *const *lists[3] = { OTHERS, NULL, NULL };
	switch (desk_lang_get()) {
	case DESK_LANG_JA: lists[0] = JA; lists[1] = OTHERS; break;
	case DESK_LANG_ZH: lists[0] = ZH; lists[1] = OTHERS; break;
	case DESK_LANG_KO: lists[0] = KO; lists[1] = OTHERS; break;
	default: break;
	}
	for (int l = 0; l < 3 && lists[l]; l++) {
		for (int i = 0; lists[l][i]; i++) {
			char *t = g_convert(bytes, (gssize)len, "UTF-8", lists[l][i], NULL, NULL, NULL);
			if (t) {
				*encoding = g_strdup(lists[l][i]);
				return t;
			}
		}
	}
	*not_text = true;
	return NULL;
}

static guint64 file_mtime(const char *path)
{
	GFile *f = g_file_new_for_path(path);
	GFileInfo *i = g_file_query_info(f, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	guint64 t = i ? g_file_info_get_attribute_uint64(i, G_FILE_ATTRIBUTE_TIME_MODIFIED) : 0;
	if (i) {
		g_object_unref(i);
	}
	g_object_unref(f);
	return t;
}

/* Nothing written on it, nothing to take back. */
static void doc_ink_clear(struct doc *d)
{
	actions_clear(&d->ink_undo);
	actions_clear(&d->ink_redo);
	for (guint i = 0; i < d->strokes->len; i++) {
		stroke_free(g_ptr_array_index(d->strokes, i));
	}
	g_ptr_array_set_size(d->strokes, 0);
}

/* Read in: the page is what it is now, nothing to take back. */
static void doc_set_text(struct doc *d, const char *text)
{
	d->replaying = true;
	gtk_text_buffer_set_text(d->buf, text, -1);
	d->replaying = false;
	edits_clear(&d->undo);
	edits_clear(&d->redo);
	d->saved_serial = 0;
	GtkTextIter a;
	gtk_text_buffer_get_start_iter(d->buf, &a);
	gtk_text_buffer_place_cursor(d->buf, &a);
	gtk_text_buffer_set_modified(d->buf, FALSE);
}

/* ---- what is written, kept -----------------------------------------------------
 *
 * "DEsk ink 2", the page's ruling, what is typed in its heading, then a stroke to a line: its
 * ink, the character it hangs from, and its points, in line pitches from the start of that line
 * and the margin, with how hard each was pressed ("s"). One written in the heading ("h") has the
 * part it is in, 1 the title or 2 the date, for the character, and its points are in HEAD_PITCH
 * pixels from the sheet's top left, or for the date its top right. The journal's pages before (1) had no words to
 * hang from: a thousandth of the sheet's width from its top left, a sheet as wide as a notebook's
 * window opened then (515). */

static char *notebook_dir(void)
{
	return g_build_filename(g_get_user_data_dir(), "desk", "notebook", NULL);
}

/* The heading is kept with what is written: "title ", "year ", "month ", "day " and the rest of
 * the line. */
static const char *const HEAD_KEY[4] = { "title ", "year ", "month ", "day " };

static char **head_field(struct doc *d, int i)
{
	return i == 0 ? &d->title : &d->date[i - 1];
}

static bool head_line(struct doc *d, const char *line)
{
	for (int i = 0; i < 4; i++) {
		size_t n = strlen(HEAD_KEY[i]);
		if (!strncmp(line, HEAD_KEY[i], n) && line[n] && g_utf8_validate(line + n, -1, NULL)) {
			g_free(*head_field(d, i));
			*head_field(d, i) = g_strdup(line + n);
			return true;
		}
	}
	return false;
}

static void ink_read(struct doc *d, const char *path)
{
	char *all = NULL;
	if (!g_file_get_contents(path, &all, NULL, NULL)) {
		return;
	}
	bool old = g_str_has_prefix(all, "DEsk ink 1");
	/* (as the journal's sheet was to the notebook's lines, near enough) */
	const double OLD_PITCH = 25.0, OLD_UNIT = 515 / 1000.0;
	int most = gtk_text_buffer_get_char_count(d->buf);
	char *line = all;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) {
			*nl = '\0';
		}
		if (!strncmp(line, "paper ", 6)) {
			int v = atoi(line + 6);
			d->paper = v >= 0 && v < PAPER_KINDS ? v : PAPER_LINED;
		} else if (head_line(d, line)) {
			/* (the heading's) */
		} else if ((line[0] == 's' || (line[0] == 'h' && !old)) && line[1] == ' ') {
			bool head = line[0] == 'h';
			char *q = line + 2, *end;
			long ink = strtol(q, &end, 10);
			long at = 0;
			if (end != q && !old) {
				q = end;
				at = strtol(q, &end, 10);
			}
			if (end != q && ink >= 0 && ink < INKS && (!head || at == 1 || at == 2)) {
				struct stroke *s = stroke_new((int)ink);
				s->head = head ? (int)at : 0;
				q = end;
				for (;;) {
					double x = g_ascii_strtod(q, &end);
					if (end == q) {
						break;
					}
					q = end;
					double y = g_ascii_strtod(q, &end);
					if (end == q) {
						break;
					}
					q = end;
					double pr = g_ascii_strtod(q, &end);
					if (end == q) {
						break;
					}
					q = end;
					if (old) {
						x = (x * OLD_UNIT - LEFT_MARGIN) / OLD_PITCH;
						y = y * OLD_UNIT / OLD_PITCH;
					}
					stroke_add(s, x, y, pr);
				}
				if (s->n > 0) {
					if (!head) {
						GtkTextIter it;
						gtk_text_buffer_get_iter_at_offset(d->buf, &it, at < 0 ? 0 : at > most ? most : (int)at);
						stroke_hang(s, d->buf, &it);
					}
					g_ptr_array_add(d->strokes, s);
				} else {
					stroke_free(s);
				}
			}
		}
		line = nl ? nl + 1 : NULL;
	}
	g_free(all);
}

/* Kept, or, with nothing written and the usual ruling, no file at all. */
static bool ink_write(struct doc *d, const char *path, int usual_paper)
{
	if (d->strokes->len == 0 && d->paper == usual_paper && !doc_headed(d)) {
		return g_unlink(path) == 0 || errno == ENOENT;
	}
	GString *out = g_string_new("DEsk ink 2\n");
	g_string_append_printf(out, "paper %d\n", d->paper);
	for (int i = 0; i < 4; i++) {
		const char *v = *head_field(d, i);
		if (v) {
			g_string_append_printf(out, "%s%s\n", HEAD_KEY[i], v);
		}
	}
	char num[G_ASCII_DTOSTR_BUF_SIZE];
	for (guint k = 0; k < d->strokes->len; k++) {
		const struct stroke *s = g_ptr_array_index(d->strokes, k);
		if (s->head) {
			g_string_append_printf(out, "h %d %d", s->ink, s->head);
		} else {
			GtkTextIter it;
			gtk_text_buffer_get_iter_at_mark(d->buf, &it, s->at);
			g_string_append_printf(out, "s %d %d", s->ink, gtk_text_iter_get_offset(&it));
		}
		for (int j = 0; j < s->n; j++) {
			g_string_append_c(out, ' ');
			g_string_append(out, g_ascii_formatd(num, sizeof num, "%.3f", s->pts[j].x));
			g_string_append_c(out, ' ');
			g_string_append(out, g_ascii_formatd(num, sizeof num, "%.3f", s->pts[j].y));
			g_string_append_c(out, ' ');
			g_string_append(out, g_ascii_formatd(num, sizeof num, "%.2f", s->pts[j].p));
		}
		g_string_append_c(out, '\n');
	}
	char *dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	GError *err = NULL;
	bool ok = g_file_set_contents(path, out->str, (gssize)out->len, &err);
	if (!ok) {
		fprintf(stderr, "desk-notepad: could not keep what is written in %s: %s\n", path, err ? err->message : "");
		g_clear_error(&err);
	}
	g_string_free(out, TRUE);
	return ok;
}

/* What is written over a file is kept by the notebook's pages, by the file's name. */
static char *doc_over_path(const struct doc *d)
{
	char *h = g_compute_checksum_for_string(G_CHECKSUM_SHA1, d->path, -1);
	char *name = g_strconcat(h, ".ink", NULL);
	char *dir = notebook_dir();
	char *path = g_build_filename(dir, "over", name, NULL);
	g_free(dir);
	g_free(name);
	g_free(h);
	return path;
}

/* A copy not saved of this file, newer than the file: offered back. */
static void doc_offer_copy(struct pad *p, struct doc *d)
{
	doc_keep_where(d);
	GStatBuf st;
	if (g_stat(d->keep_path, &st) != 0) {
		return;
	}
	if (d->have_time && (guint64)st.st_mtime <= d->file_time) {
		doc_keep_drop(d);
		return;
	}
	char when[64];
	struct tm tm;
	time_t t = st.st_mtime;
	localtime_r(&t, &tm);
	strftime(when, sizeof when, "%x %H:%M", &tm);
	char msg[512];
	snprintf(msg, sizeof msg, TR("There is a copy of this that was not saved, from %s. Bring it back?"), when);
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(p->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_NONE, "%s", msg);
	gtk_dialog_add_buttons(GTK_DIALOG(dlg), TR("Throw Away"), GTK_RESPONSE_REJECT, TR("Bring Back"),
		GTK_RESPONSE_ACCEPT, NULL);
	int r = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	if (r != GTK_RESPONSE_ACCEPT) {
		if (r == GTK_RESPONSE_REJECT) {
			doc_keep_drop(d);
		}
		return;
	}
	char *all = NULL;
	if (g_file_get_contents(d->keep_path, &all, NULL, NULL)) {
		const char *nl = strchr(all, '\n');
		if (nl) {
			gtk_text_buffer_set_text(d->buf, nl + 1, -1);
			gtk_text_buffer_set_modified(d->buf, TRUE);
			edits_clear(&d->undo);
			edits_clear(&d->redo);
			d->saved_serial = (unsigned)-1;
		}
		g_free(all);
	}
}

static bool doc_read(struct pad *p, struct doc *d, const char *path)
{
	char msg[600];
	char *shown = g_filename_display_name(path);
	char *bytes = NULL;
	gsize len = 0;
	GError *e = NULL;
	GStatBuf st;
	if (g_stat(path, &st) == 0 && st.st_size > TOO_BIG) {
		snprintf(msg, sizeof msg, TR("%s is too big for a notepad."), shown);
		say(p, msg);
		g_free(shown);
		return false;
	}
	if (!g_file_get_contents(path, &bytes, &len, &e)) {
		snprintf(msg, sizeof msg, TR("Could not open %s."), shown);
		char *full = g_strdup_printf("%s\n\n%s", msg, e->message);
		say(p, full);
		g_free(full);
		g_error_free(e);
		g_free(shown);
		return false;
	}
	char *encoding;
	const char *bom;
	size_t bom_len;
	bool not_text;
	char *text = decode(bytes, len, &encoding, &bom, &bom_len, &not_text);
	g_free(bytes);
	if (!text) {
		snprintf(msg, sizeof msg, TR("%s does not look like text."), shown);
		say(p, msg);
		g_free(shown);
		return false;
	}
	g_free(shown);
	enum eol eol;
	char *clean = line_ends_in(text, strlen(text), &eol);
	g_free(text);
	/* a file read over a page with no copy of its own any more */
	doc_keep_drop(d);
	g_free(d->path);
	d->path = g_canonicalize_filename(path, NULL);
	g_free(d->encoding);
	d->encoding = encoding;
	d->bom = bom;
	d->bom_len = bom_len;
	d->eol = eol;
	d->file_time = file_mtime(d->path);
	d->have_time = d->file_time != 0;
	doc_ink_clear(d);
	doc_set_text(d, clean);
	g_free(clean);
	d->paper = g_set.paper;
	char *over = doc_over_path(d);
	ink_read(d, over);
	g_free(over);
	doc_offer_copy(p, d);
	return true;
}

/* What it says on the page, in the file's own encoding; NULL if it cannot be
 * said in it. */
static char *doc_bytes(struct doc *d, size_t *out_len)
{
	GtkTextIter a, b;
	gtk_text_buffer_get_bounds(d->buf, &a, &b);
	char *text = gtk_text_buffer_get_text(d->buf, &a, &b, TRUE);
	size_t len;
	char *ended = line_ends_out(text, d->eol, &len);
	g_free(text);
	char *body = ended;
	gsize body_len = len;
	if (d->encoding) {
		gsize wrote = 0;
		body = g_convert(ended, (gssize)len, d->encoding, "UTF-8", NULL, &wrote, NULL);
		g_free(ended);
		if (!body) {
			return NULL;
		}
		body_len = wrote;
	}
	char *out = g_malloc(d->bom_len + body_len + 1);
	if (d->bom_len) {
		memcpy(out, d->bom, d->bom_len);
	}
	memcpy(out + d->bom_len, body, body_len);
	out[d->bom_len + body_len] = '\0';
	*out_len = d->bom_len + body_len;
	g_free(body);
	return out;
}

/* Written to path: as a file of its own, or, for a page of the notebook's, a copy of its words. */
static bool doc_write(struct doc *d, const char *path)
{
	struct pad *p = d->pad;
	size_t len = 0;
	char *bytes = doc_bytes(d, &len);
	if (!bytes) {
		/* a letter its encoding has not got */
		char msg[500];
		snprintf(msg, sizeof msg, TR("Some of this cannot be written in %s. Save it as UTF-8?"), d->encoding);
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(p->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
			GTK_BUTTONS_NONE, "%s", msg);
		gtk_dialog_add_buttons(GTK_DIALOG(dlg), TR("Cancel"), GTK_RESPONSE_CANCEL, TR("Save"), GTK_RESPONSE_ACCEPT,
			NULL);
		int r = gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		if (r != GTK_RESPONSE_ACCEPT) {
			return false;
		}
		g_free(d->encoding);
		d->encoding = NULL;
		d->bom = NULL;
		d->bom_len = 0;
		bytes = doc_bytes(d, &len);
		if (!bytes) {
			return false;
		}
	}
	GError *e = NULL;
	GFile *f = g_file_new_for_path(path);
	/* written beside it and put in its place, so that a machine going down
	 * cannot leave half a file */
	bool ok = g_file_replace_contents(f, bytes, len, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &e);
	g_object_unref(f);
	g_free(bytes);
	if (!ok) {
		char msg[600];
		char *shown = g_filename_display_name(path);
		snprintf(msg, sizeof msg, TR("Could not save %s."), shown);
		char *full = g_strdup_printf("%s\n\n%s", msg, e->message);
		say(p, full);
		g_free(full);
		g_free(shown);
		g_error_free(e);
		return false;
	}
	if (d->number) {
		/* the page stays the notebook's: what was saved is a copy */
		return true;
	}
	/* the copy goes, under the name it had */
	doc_keep_drop(d);
	if (!d->path || strcmp(d->path, path) != 0) {
		g_free(d->path);
		d->path = g_strdup(path);
	}
	d->file_time = file_mtime(d->path);
	d->have_time = d->file_time != 0;
	char *over = doc_over_path(d);
	ink_write(d, over, g_set.paper);
	g_free(over);
	d->saved_serial = doc_top(d);
	gtk_text_buffer_set_modified(d->buf, FALSE);
	pad_title(p);
	return true;
}

static bool doc_save_as(struct doc *d)
{
	struct pad *p = d->pad;
	GtkFileChooserNative *c = gtk_file_chooser_native_new(TR("Save As"), GTK_WINDOW(p->win),
		GTK_FILE_CHOOSER_ACTION_SAVE, TR("Save"), TR("Cancel"));
	GtkFileChooser *fc = GTK_FILE_CHOOSER(c);
	gtk_file_chooser_set_do_overwrite_confirmation(fc, TRUE);
	if (d->path) {
		gtk_file_chooser_set_filename(fc, d->path);
	} else {
		char name[300], with[320];
		doc_name(d, name, sizeof name);
		snprintf(with, sizeof with, "%s.txt", name);
		gtk_file_chooser_set_current_name(fc, with);
	}
	bool ok = false;
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		char *path = gtk_file_chooser_get_filename(fc);
		if (path) {
			ok = doc_write(d, path);
			g_free(path);
		}
	}
	g_object_unref(c);
	return ok;
}

static bool doc_save(struct doc *d)
{
	if (!d->path) {
		return doc_save_as(d);
	}
	/* somebody else has written it since: not over that without asking */
	guint64 now = file_mtime(d->path);
	if (d->have_time && now != 0 && now != d->file_time) {
		char name[300], msg[500];
		doc_name(d, name, sizeof name);
		snprintf(msg, sizeof msg, TR("%s has been changed by something else since it was opened. Save over it?"),
			name);
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(d->pad->win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
			GTK_BUTTONS_NONE, "%s", msg);
		gtk_dialog_add_buttons(GTK_DIALOG(dlg), TR("Cancel"), GTK_RESPONSE_CANCEL, TR("Save"), GTK_RESPONSE_ACCEPT,
			NULL);
		int r = gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		if (r != GTK_RESPONSE_ACCEPT) {
			return false;
		}
	}
	return doc_write(d, d->path);
}

/* ---- a page, made and gone ----------------------------------------------------- */

static void on_modified(GtkTextBuffer *buf, gpointer data);
static void on_changed(GtkTextBuffer *buf, gpointer data);

static struct doc *doc_new(struct pad *p, int number)
{
	struct doc *d = g_new0(struct doc, 1);
	d->pad = p;
	d->number = number;
	d->paper = g_set.paper;
	d->strokes = g_ptr_array_new();
	g_queue_init(&d->undo);
	g_queue_init(&d->redo);
	g_queue_init(&d->ink_undo);
	g_queue_init(&d->ink_redo);
	d->buf = gtk_text_buffer_new(NULL);
	g_object_set_data(G_OBJECT(d->buf), "desk-doc", d);
	/* (the highlighter again, more lightly, for every place it is found) */
	GdkRGBA found = { 1.0, 0.91, 0.36, 0.42 };
	d->found_tag = gtk_text_buffer_create_tag(d->buf, "found", "background-rgba", &found, NULL);
	g_signal_connect(d->buf, "insert-text", G_CALLBACK(on_insert), d);
	g_signal_connect(d->buf, "delete-range", G_CALLBACK(on_delete), d);
	g_signal_connect(d->buf, "begin-user-action", G_CALLBACK(on_begin_action), d);
	g_signal_connect(d->buf, "end-user-action", G_CALLBACK(on_end_action), d);
	g_signal_connect(d->buf, "modified-changed", G_CALLBACK(on_modified), d);
	g_signal_connect(d->buf, "changed", G_CALLBACK(on_changed), d);
	return d;
}

static void doc_free(struct doc *d)
{
	if (!d) {
		return;
	}
	edits_clear(&d->undo);
	edits_clear(&d->redo);
	doc_ink_clear(d);
	g_ptr_array_free(d->strokes, TRUE);
	g_signal_handlers_disconnect_by_data(d->buf, d);
	g_object_set_data(G_OBJECT(d->buf), "desk-doc", NULL);
	g_object_unref(d->buf);
	g_free(d->path);
	g_free(d->encoding);
	g_free(d->keep_path);
	g_free(d->title);
	for (int i = 0; i < 3; i++) {
		g_free(d->date[i]);
	}
	g_free(d);
}

/* ---- the notebook's own pages ----------------------------------------------------- */

static char *note_file(int number, const char *ext)
{
	char name[40];
	snprintf(name, sizeof name, "page-%05d.%s", number, ext);
	char *dir = notebook_dir();
	char *path = g_build_filename(dir, name, NULL);
	g_free(dir);
	return path;
}

static void note_read(struct doc *d)
{
	char *txt = note_file(d->number, "txt"), *ink = note_file(d->number, "ink");
	char *all = NULL;
	if (g_file_get_contents(txt, &all, NULL, NULL) && g_utf8_validate(all, -1, NULL)) {
		doc_set_text(d, all);
	}
	g_free(all);
	d->paper = PAPER_LINED;
	ink_read(d, ink);
	g_free(txt);
	g_free(ink);
}

/* Kept, as it is: no files for a page that is blank. */
static void note_keep(struct doc *d)
{
	if (!d || !d->number || !d->unkept) {
		return;
	}
	char *txt = note_file(d->number, "txt"), *ink = note_file(d->number, "ink");
	char *dir = notebook_dir();
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	bool ok = true;
	if (gtk_text_buffer_get_char_count(d->buf) == 0) {
		ok = g_unlink(txt) == 0 || errno == ENOENT;
	} else {
		GtkTextIter a, b;
		gtk_text_buffer_get_bounds(d->buf, &a, &b);
		char *text = gtk_text_buffer_get_text(d->buf, &a, &b, TRUE);
		GError *err = NULL;
		ok = g_file_set_contents(txt, text, -1, &err);
		if (!ok) {
			fprintf(stderr, "desk-notepad: could not keep page %d: %s\n", d->number, err ? err->message : "");
			g_clear_error(&err);
		}
		g_free(text);
	}
	ok = ink_write(d, ink, PAPER_LINED) && ok;
	if (ok) {
		d->unkept = false;
	}
	g_free(txt);
	g_free(ink);
}

/* The journal's pages, from before there was one notebook, are the notebook's: once, if it has
 * none of its own. */
static void notes_from_journal(const char *dir)
{
	GDir *mine = g_dir_open(dir, 0, NULL);
	if (mine) {
		const char *name;
		while ((name = g_dir_read_name(mine))) {
			if (g_str_has_prefix(name, "page-")) {
				g_dir_close(mine);
				return;
			}
		}
		g_dir_close(mine);
	}
	char *old = g_build_filename(g_get_user_data_dir(), "desk", "journal", NULL);
	GDir *d = g_dir_open(old, 0, NULL);
	if (d) {
		const char *name;
		while ((name = g_dir_read_name(d))) {
			int n = 0;
			if (sscanf(name, "page-%d.ink", &n) == 1 && n > 0) {
				char *from = g_build_filename(old, name, NULL), *to = g_build_filename(dir, name, NULL);
				if (g_rename(from, to) != 0) {
					fprintf(stderr, "desk-notepad: could not bring %s over: %s\n", from, g_strerror(errno));
				}
				g_free(from);
				g_free(to);
			}
		}
		g_dir_close(d);
	}
	g_free(old);
}

/* How many pages there are: those written on, and a clean one after them. */
static int notes_count(void)
{
	char *dir = notebook_dir();
	g_mkdir_with_parents(dir, 0700);
	notes_from_journal(dir);
	int most = 0;
	GDir *d = g_dir_open(dir, 0, NULL);
	if (d) {
		const char *name;
		while ((name = g_dir_read_name(d))) {
			int n = 0;
			if ((sscanf(name, "page-%d.txt", &n) == 1 || sscanf(name, "page-%d.ink", &n) == 1) && n > most) {
				most = n;
			}
		}
		g_dir_close(d);
	}
	g_free(dir);
	return most + 1;
}

/* ---- a pad and its pages ------------------------------------------------------------- */

static struct doc *pad_doc_at(struct pad *p, int i)
{
	if (i < 0 || i >= (int)p->docs->len) {
		return NULL;
	}
	struct doc *d = g_ptr_array_index(p->docs, i);
	if (!d && p->notebook) {
		d = doc_new(p, i + 1);
		/* (in its place before it is read: what it says as its words go in asks for it) */
		g_ptr_array_index(p->docs, i) = d;
		note_read(d);
	}
	return d;
}

static struct doc *pad_doc(struct pad *p)
{
	return pad_doc_at(p, p->at);
}

static void pad_title(struct pad *p)
{
	struct doc *d = pad_doc(p);
	if (!d) {
		return;
	}
	char name[300], title[320];
	doc_name(d, name, sizeof name);
	snprintf(title, sizeof title, "%s%s", name, gtk_text_buffer_get_modified(d->buf) ? "  \xe2\x80\xa2" : "");
	gtk_window_set_title(GTK_WINDOW(p->win), title);
	if (!d->number && p->head[0]) {
		/* (a file's name, in its heading) */
		head_fill(p);
	}
}

static gboolean pad_keep_due(gpointer data)
{
	struct pad *p = data;
	p->save_timer = 0;
	for (guint i = 0; i < p->docs->len; i++) {
		note_keep(g_ptr_array_index(p->docs, i));
	}
	return G_SOURCE_REMOVE;
}

/* The words or the writing on a page have changed: a page of the notebook's is kept soon. */
static void pad_changed(struct pad *p, struct doc *d)
{
	if (d->number) {
		d->unkept = true;
		if (p->save_timer) {
			g_source_remove(p->save_timer);
		}
		p->save_timer = g_timeout_add(SAVE_AFTER_MS, pad_keep_due, p);
	} else {
		d->keep_dirty = true;
	}
}

static void pads_keep_all(struct pad *p)
{
	if (p->save_timer) {
		g_source_remove(p->save_timer);
		p->save_timer = 0;
	}
	for (guint i = 0; i < p->docs->len; i++) {
		note_keep(g_ptr_array_index(p->docs, i));
	}
}

static gboolean pad_keep_tick(gpointer data)
{
	struct pad *p = data;
	for (guint i = 0; i < p->docs->len; i++) {
		struct doc *d = g_ptr_array_index(p->docs, i);
		if (d) {
			doc_keep_now(d);
		}
	}
	return G_SOURCE_CONTINUE;
}

static void on_modified(GtkTextBuffer *buf, gpointer data)
{
	struct doc *d = data;
	if (d->number && gtk_text_buffer_get_modified(buf)) {
		/* a page of the notebook's is kept as it is written: never waiting to be saved */
		gtk_text_buffer_set_modified(buf, FALSE);
		return;
	}
	if (d->pad && pad_doc(d->pad) == d) {
		pad_title(d->pad);
	}
}

static void on_changed(GtkTextBuffer *buf, gpointer data)
{
	struct doc *d = data;
	(void)buf;
	if (d->pad) {
		pad_changed(d->pad, d);
	}
}

/* ---- finding --------------------------------------------------------------------------- */

static void find_mark_all(struct pad *p)
{
	struct doc *d = pad_doc(p);
	GtkTextIter a, b;
	gtk_text_buffer_get_bounds(d->buf, &a, &b);
	gtk_text_buffer_remove_tag(d->buf, d->found_tag, &a, &b);
	const char *what = gtk_entry_get_text(GTK_ENTRY(p->find_entry));
	GtkTextIter cur;
	gtk_text_buffer_get_iter_at_mark(d->buf, &cur, gtk_text_buffer_get_selection_bound(d->buf));
	GtkTextIter ins;
	gtk_text_buffer_get_iter_at_mark(d->buf, &ins, gtk_text_buffer_get_insert(d->buf));
	if (gtk_text_iter_compare(&cur, &ins) > 0) {
		cur = ins;
	}
	int n = 0, here = 0;
	if (what[0]) {
		GtkTextIter from = a, m0, m1;
		while (n < 9999 && gtk_text_iter_forward_search(&from, what,
				GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY, &m0, &m1, NULL)) {
			gtk_text_buffer_apply_tag(d->buf, d->found_tag, &m0, &m1);
			n++;
			if (gtk_text_iter_compare(&m0, &cur) <= 0) {
				here = n;
			}
			from = m1;
		}
	}
	char s[64];
	if (!what[0]) {
		s[0] = '\0';
	} else if (n == 0) {
		snprintf(s, sizeof s, "%s", TR("not found"));
	} else {
		snprintf(s, sizeof s, TR("%d of %d"), here > 0 ? here : 1, n);
	}
	gtk_label_set_text(GTK_LABEL(p->count), s);
}

static void find_go(struct pad *p, bool back)
{
	struct doc *d = pad_doc(p);
	const char *what = gtk_entry_get_text(GTK_ENTRY(p->find_entry));
	if (!what[0]) {
		return;
	}
	GtkTextIter a, b, m0, m1;
	gtk_text_buffer_get_selection_bounds(d->buf, &a, &b);
	GtkTextSearchFlags fl = GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY;
	bool got = back ? gtk_text_iter_backward_search(&a, what, fl, &m0, &m1, NULL)
		: gtk_text_iter_forward_search(&b, what, fl, &m0, &m1, NULL);
	if (!got) {
		/* round from the other end */
		if (back) {
			gtk_text_buffer_get_end_iter(d->buf, &a);
			got = gtk_text_iter_backward_search(&a, what, fl, &m0, &m1, NULL);
		} else {
			gtk_text_buffer_get_start_iter(d->buf, &b);
			got = gtk_text_iter_forward_search(&b, what, fl, &m0, &m1, NULL);
		}
	}
	if (got) {
		gtk_text_buffer_select_range(d->buf, &m0, &m1);
		gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(p->view), &m0, 0.1, FALSE, 0.0, 0.0);
	}
	find_mark_all(p);
}

static void find_changed(GtkEditable *e, gpointer data)
{
	struct pad *p = data;
	struct doc *d = pad_doc(p);
	(void)e;
	/* from where the selection starts, so that typing on stays on the same one */
	GtkTextIter a, b, m0, m1;
	gtk_text_buffer_get_selection_bounds(d->buf, &a, &b);
	const char *what = gtk_entry_get_text(GTK_ENTRY(p->find_entry));
	GtkTextSearchFlags fl = GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY;
	if (what[0]) {
		bool got = gtk_text_iter_forward_search(&a, what, fl, &m0, &m1, NULL);
		if (!got) {
			gtk_text_buffer_get_start_iter(d->buf, &a);
			got = gtk_text_iter_forward_search(&a, what, fl, &m0, &m1, NULL);
		}
		if (got) {
			gtk_text_buffer_select_range(d->buf, &m0, &m1);
			gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(p->view), &m0, 0.1, FALSE, 0.0, 0.0);
		}
	}
	find_mark_all(p);
}

static void find_replace_one(struct pad *p)
{
	struct doc *d = pad_doc(p);
	const char *what = gtk_entry_get_text(GTK_ENTRY(p->find_entry));
	const char *with = gtk_entry_get_text(GTK_ENTRY(p->replace_entry));
	GtkTextIter a, b;
	if (what[0] && gtk_text_buffer_get_selection_bounds(d->buf, &a, &b)) {
		char *sel = gtk_text_buffer_get_text(d->buf, &a, &b, FALSE);
		char *x = g_utf8_casefold(sel, -1), *y = g_utf8_casefold(what, -1);
		if (!strcmp(x, y)) {
			gtk_text_buffer_begin_user_action(d->buf);
			gtk_text_buffer_delete(d->buf, &a, &b);
			gtk_text_buffer_insert(d->buf, &a, with, -1);
			gtk_text_buffer_end_user_action(d->buf);
		}
		g_free(x);
		g_free(y);
		g_free(sel);
	}
	find_go(p, false);
}

static void find_replace_all(struct pad *p)
{
	struct doc *d = pad_doc(p);
	const char *what = gtk_entry_get_text(GTK_ENTRY(p->find_entry));
	const char *with = gtk_entry_get_text(GTK_ENTRY(p->replace_entry));
	if (!what[0]) {
		return;
	}
	GtkTextIter from, m0, m1;
	gtk_text_buffer_get_start_iter(d->buf, &from);
	GtkTextMark *mk = gtk_text_buffer_create_mark(d->buf, NULL, &from, FALSE);
	gtk_text_buffer_begin_user_action(d->buf);
	while (gtk_text_iter_forward_search(&from, what, GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY,
			&m0, &m1, NULL)) {
		gtk_text_buffer_delete(d->buf, &m0, &m1);
		gtk_text_buffer_insert(d->buf, &m0, with, -1);
		gtk_text_buffer_move_mark(d->buf, mk, &m0);
		gtk_text_buffer_get_iter_at_mark(d->buf, &from, mk);
	}
	gtk_text_buffer_end_user_action(d->buf);
	gtk_text_buffer_delete_mark(d->buf, mk);
	find_mark_all(p);
}

static void note_show(struct pad *p, bool replacing)
{
	struct doc *d = pad_doc(p);
	gtk_widget_show_all(p->note);
	gtk_widget_set_visible(p->replace_row, replacing);
	/* what is selected is what is looked for */
	GtkTextIter a, b;
	if (gtk_text_buffer_get_selection_bounds(d->buf, &a, &b)
		&& gtk_text_iter_get_line(&a) == gtk_text_iter_get_line(&b)) {
		char *sel = gtk_text_buffer_get_text(d->buf, &a, &b, FALSE);
		gtk_entry_set_text(GTK_ENTRY(p->find_entry), sel);
		g_free(sel);
	}
	gtk_widget_grab_focus(p->find_entry);
	find_mark_all(p);
}

static void note_hide(struct pad *p)
{
	struct doc *d = pad_doc(p);
	if (d) {
		GtkTextIter a, b;
		gtk_text_buffer_get_bounds(d->buf, &a, &b);
		gtk_text_buffer_remove_tag(d->buf, d->found_tag, &a, &b);
	}
	gtk_widget_hide(p->note);
	gtk_widget_grab_focus(p->view);
}

static void on_find_next(GtkButton *b, gpointer d) { (void)b; find_go(d, false); }
static void on_find_prev(GtkButton *b, gpointer d) { (void)b; find_go(d, true); }
static void on_replace(GtkButton *b, gpointer d) { (void)b; find_replace_one(d); }
static void on_replace_all(GtkButton *b, gpointer d) { (void)b; find_replace_all(d); }
static void on_note_close(GtkButton *b, gpointer d) { (void)b; note_hide(d); }

static void on_find_activate(GtkEntry *e, gpointer d)
{
	(void)e;
	find_go(d, false);
}

static GtkWidget *note_build(struct pad *p)
{
	GtkWidget *note = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(note), "note");
	gtk_widget_set_halign(note, GTK_ALIGN_END);
	gtk_widget_set_valign(note, GTK_ALIGN_END);

	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	p->find_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(p->find_entry), TR("Find"));
	gtk_entry_set_width_chars(GTK_ENTRY(p->find_entry), 8);
	gtk_entry_set_max_width_chars(GTK_ENTRY(p->find_entry), 22);
	g_signal_connect(p->find_entry, "changed", G_CALLBACK(find_changed), p);
	g_signal_connect(p->find_entry, "activate", G_CALLBACK(on_find_activate), p);
	gtk_box_pack_start(GTK_BOX(row), p->find_entry, TRUE, TRUE, 0);
	p->count = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(row), p->count, FALSE, FALSE, 0);
	GtkWidget *prev = gtk_button_new_with_label("\xe2\x80\xb9");
	GtkWidget *next = gtk_button_new_with_label("\xe2\x80\xba");
	GtkWidget *close = gtk_button_new_with_label("\xc3\x97");
	g_signal_connect(prev, "clicked", G_CALLBACK(on_find_prev), p);
	g_signal_connect(next, "clicked", G_CALLBACK(on_find_next), p);
	g_signal_connect(close, "clicked", G_CALLBACK(on_note_close), p);
	gtk_box_pack_start(GTK_BOX(row), prev, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), next, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), close, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note), row, FALSE, FALSE, 0);

	p->replace_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	p->replace_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(p->replace_entry), TR("Replace with"));
	gtk_entry_set_width_chars(GTK_ENTRY(p->replace_entry), 8);
	gtk_entry_set_max_width_chars(GTK_ENTRY(p->replace_entry), 22);
	gtk_box_pack_start(GTK_BOX(p->replace_row), p->replace_entry, TRUE, TRUE, 0);
	GtkWidget *one = gtk_button_new_with_label(TR("Replace"));
	GtkWidget *all = gtk_button_new_with_label(TR("All"));
	g_signal_connect(one, "clicked", G_CALLBACK(on_replace), p);
	g_signal_connect(all, "clicked", G_CALLBACK(on_replace_all), p);
	gtk_box_pack_start(GTK_BOX(p->replace_row), one, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(p->replace_row), all, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note), p->replace_row, FALSE, FALSE, 0);
	return note;
}

/* ---- the sheet's spacing ---------------------------------------------------------------- */

/* The letters have changed size: the rules go on under them at the same
 * proportion, and what is written with them. */
static void pad_measure(struct pad *p)
{
	PangoLayout *pl = gtk_widget_create_pango_layout(p->view, "Xg");
	int pw, ph;
	pango_layout_get_pixel_size(pl, &pw, &ph);
	int descent = ph - PANGO_PIXELS(pango_layout_get_baseline(pl));
	g_object_unref(pl);
	int gap = ph / 5;
	gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(p->view), gap);
	gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(p->view), 0);
	gtk_text_view_set_pixels_inside_wrap(GTK_TEXT_VIEW(p->view), gap);
	PAD_VIEW(p->view)->pitch = ph + gap;
	/* the line's rule a little under its letters' feet, and the first of them
	 * one line down from the heading's */
	PAD_VIEW(p->view)->sink = descent > 3 ? descent - 3 : 0;
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(p->view), PAD_VIEW(p->view)->sink);
	gtk_widget_queue_draw(p->view);
}

static void pads_restyle(void)
{
	style_apply();
	for (GList *l = g_pads; l; l = l->next) {
		struct pad *p = l->data;
		gtk_widget_reset_style(p->view);
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(p->view), g_set.wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->scroll),
			g_set.wrap ? GTK_POLICY_EXTERNAL : GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		pad_measure(p);
	}
	conf_save();
}

/* ---- the pens in the hand -----------------------------------------------------------------
 *
 * The desk's pencil case says what is out of it, unless there is no case for this window. */

static bool pad_has_case(const struct pad *p)
{
	return g_on_desk && !p->whole;
}

static int pad_pen(const struct pad *p)
{
	return pad_has_case(p) ? g_case.pen : g_case.own_pen;
}

static int pad_hand(const struct pad *p)
{
	return pad_has_case(p) ? g_case.hand : g_case.own_hand;
}

/* What a pen's point writes with, as the hand has it: the pen's refill that is out, or the
 * highlighter; -1 for the rubber. */
static int pad_ink(const struct pad *p)
{
	int hand = pad_hand(p);
	return hand == NB_HAND_MARKER ? INK_MARKER : hand == NB_HAND_RUBBER ? -1 : PEN_INK[pad_pen(p)];
}

/* With something out of the case, the pointer over the sheet is its point. */
static void pad_pointer(struct pad *p)
{
	GdkWindow *text = gtk_text_view_get_window(GTK_TEXT_VIEW(p->view), GTK_TEXT_WINDOW_TEXT);
	if (!text) {
		return;
	}
	bool dot = pad_hand(p) != NB_HAND_NONE;
	if (dot == p->dot_shown && p->dot) {
		return;
	}
	if (!p->dot) {
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 9, 9);
		cairo_t *cr = cairo_create(s);
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.75);
		cairo_arc(cr, 4.5, 4.5, 2.6, 0.0, 2.0 * G_PI);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.05, 0.08, 0.20, 0.95);
		cairo_arc(cr, 4.5, 4.5, 1.5, 0.0, 2.0 * G_PI);
		cairo_fill(cr);
		cairo_destroy(cr);
		p->dot = gdk_cursor_new_from_surface(gdk_window_get_display(text), s, 4.5, 4.5);
		cairo_surface_destroy(s);
	}
	p->dot_shown = dot;
	if (dot) {
		gdk_window_set_cursor(text, p->dot);
	} else {
		GdkCursor *c = gdk_cursor_new_from_name(gdk_window_get_display(text), "text");
		gdk_window_set_cursor(text, c);
		if (c) {
			g_object_unref(c);
		}
	}
}

static char *case_path(void)
{
	return g_build_filename(g_get_user_state_dir(), "desk", NB_CASE_FILE, NULL);
}

/* What the desk says is out of its pencil case. */
static void case_read(void)
{
	char *path = case_path(), *all = NULL;
	if (g_file_get_contents(path, &all, NULL, NULL)) {
		char **lines = g_strsplit(all, "\n", -1);
		for (int i = 0; lines[i]; i++) {
			char *l = g_strstrip(lines[i]);
			for (int k = 0; k < NB_PENS; k++) {
				if (g_str_has_prefix(l, "pen ") && !strcmp(g_strstrip(l + 4), PEN_NAME[k])) {
					g_case.pen = k;
				}
			}
			for (int k = 0; k < NB_HANDS; k++) {
				if (g_str_has_prefix(l, "hand ") && !strcmp(g_strstrip(l + 5), HAND_NAME[k])) {
					g_case.hand = k;
				}
			}
		}
		g_strfreev(lines);
		g_free(all);
	}
	g_free(path);
}

static void case_say(void)
{
	for (GList *l = g_pads; l; l = l->next) {
		struct pad *p = l->data;
		pad_pointer(p);
		gtk_widget_queue_draw(p->sheet);
	}
}

static void on_case_changed(GFileMonitor *m, GFile *file, GFile *other, GFileMonitorEvent ev, gpointer data)
{
	(void)m;
	(void)data;
	/* (put in its place, it is the other file of a rename) */
	bool ours = false;
	GFile *files[2] = { file, other };
	for (int i = 0; i < 2; i++) {
		char *base = files[i] ? g_file_get_basename(files[i]) : NULL;
		ours = ours || (base && !strcmp(base, NB_CASE_FILE));
		g_free(base);
	}
	if (ours && (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_CREATED
			|| ev == G_FILE_MONITOR_EVENT_MOVED_IN || ev == G_FILE_MONITOR_EVENT_RENAMED
			|| ev == G_FILE_MONITOR_EVENT_CHANGED)) {
		case_read();
		case_say();
	}
}

/* The desk writes it beside and puts it in its place: so it is the folder that is watched. */
static void case_watch(void)
{
	char *dir = g_build_filename(g_get_user_state_dir(), "desk", NULL);
	g_mkdir_with_parents(dir, 0700);
	GFile *f = g_file_new_for_path(dir);
	g_case.watch = g_file_monitor_directory(f, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (g_case.watch) {
		g_signal_connect(g_case.watch, "changed", G_CALLBACK(on_case_changed), NULL);
	}
	g_object_unref(f);
	g_free(dir);
	case_read();
}

/* ---- writing and rubbing out ---------------------------------------------------------------- */

static double pad_pitch(struct pad *p)
{
	int pitch = PAD_VIEW(p->view)->pitch;
	return pitch > 4 ? pitch : 24;
}

/* The view again, about a stretch of the buffer. */
static void view_dirty(GtkTextView *tv, double x0, double y0, double x1, double y1, double pad)
{
	int wx0, wy0, wx1, wy1;
	gtk_text_view_buffer_to_window_coords(tv, GTK_TEXT_WINDOW_WIDGET, (int)floor(fmin(x0, x1) - pad),
		(int)floor(fmin(y0, y1) - pad), &wx0, &wy0);
	gtk_text_view_buffer_to_window_coords(tv, GTK_TEXT_WINDOW_WIDGET, (int)ceil(fmax(x0, x1) + pad),
		(int)ceil(fmax(y0, y1) + pad), &wx1, &wy1);
	gtk_widget_queue_draw_area(GTK_WIDGET(tv), wx0, wy0, wx1 - wx0, wy1 - wy0);
}

/* A stroke begun at bx, by: it hangs from the start of the line there. */
static void write_begin(struct pad *p, double bx, double by, double pressure, int ink)
{
	GtkTextView *tv = GTK_TEXT_VIEW(p->view);
	PadView *pv = PAD_VIEW(p->view);
	struct doc *d = pad_doc(p);
	if (pv->wet) {
		stroke_free(pv->wet);
	}
	GtkTextIter it;
	gtk_text_view_get_iter_at_location(tv, &it, LEFT_MARGIN, (int)by);
	gtk_text_view_backward_display_line_start(tv, &it);
	struct stroke *s = stroke_new(ink);
	stroke_hang(s, d->buf, &it);
	pv->wet = s;
	double hx, hy, pitch = pad_pitch(p);
	stroke_hook(tv, s, &hx, &hy);
	stroke_add(s, (bx - hx) / pitch, (by - hy) / pitch, pressure);
	view_dirty(tv, bx, by, bx, by, ink_width(ink, 1.0, pitch) + 3.0);
}

static void write_on(struct pad *p, double bx, double by, double pressure)
{
	GtkTextView *tv = GTK_TEXT_VIEW(p->view);
	struct stroke *s = PAD_VIEW(p->view)->wet;
	if (!s) {
		return;
	}
	double hx, hy, pitch = pad_pitch(p);
	stroke_hook(tv, s, &hx, &hy);
	double x = (bx - hx) / pitch, y = (by - hy) / pitch;
	const struct pt *last = &s->pts[s->n - 1];
	/* (not points on top of points: they would make a knot of it) */
	if (hypot(x - last->x, y - last->y) * pitch < 0.8) {
		return;
	}
	/* what the hand shook out of it, not what it meant: how hard goes smoothly */
	double smooth = last->p + (pressure - last->p) * 0.45;
	const struct pt *before = &s->pts[s->n > 1 ? s->n - 2 : 0];
	stroke_add(s, x, y, smooth);
	if (s->ink == INK_MARKER) {
		gtk_widget_queue_draw(p->view);
		return;
	}
	view_dirty(tv, hx + before->x * pitch, hy + before->y * pitch, bx, by, ink_width(s->ink, 1.0, pitch) + 3.0);
}

static void write_end(struct pad *p)
{
	PadView *pv = PAD_VIEW(p->view);
	struct stroke *s = pv->wet;
	if (!s) {
		return;
	}
	pv->wet = NULL;
	struct doc *d = pad_doc(p);
	g_ptr_array_add(d->strokes, s);
	struct action *a = action_new();
	g_ptr_array_add(a->came, s);
	doc_action_done(d, a);
	pad_changed(p, d);
	gtk_widget_queue_draw(p->view);
}

/* Lifted off before it was done as a stroke: a hand, not a pen. */
static void write_forget(struct pad *p)
{
	PadView *pv = PAD_VIEW(p->view);
	if (pv->wet) {
		stroke_free(pv->wet);
		pv->wet = NULL;
		gtk_widget_queue_draw(p->view);
	}
}

/* Where a stroke in the heading hangs from, in the window: the title's from the sheet's left, the
 * date's from its right, as they are printed. */
static double head_from(const struct stroke *s, int w)
{
	return s->head == 2 ? w : 0.0;
}

/* Rubbing: points the rubber goes over come out of what they were in, which is two strokes then, or
 * one less long. On the page, bx, by in its buffer; in the heading, in the window. */
static void rub_at(struct pad *p, double bx, double by, bool head)
{
	GtkTextView *tv = GTK_TEXT_VIEW(p->view);
	struct doc *d = pad_doc(p);
	double pitch = head ? HEAD_PITCH : pad_pitch(p), rad = 0.30;
	int w = gtk_widget_get_allocated_width(p->overlay);
	bool any = false;
	for (guint i = 0; i < d->strokes->len;) {
		struct stroke *s = g_ptr_array_index(d->strokes, i);
		if ((s->head != 0) != head || (s->head == 2 && !p->head_wide)) {
			i++;
			continue;
		}
		double hx = head_from(s, w), hy = 0.0;
		if (!head) {
			stroke_hook(tv, s, &hx, &hy);
		}
		double rx = (bx - hx) / pitch, ry = (by - hy) / pitch;
		bool hit = false;
		if (rx >= s->x0 - rad && rx <= s->x1 + rad && ry >= s->y0 - rad && ry <= s->y1 + rad) {
			for (int j = 0; j < s->n && !hit; j++) {
				hit = hypot(s->pts[j].x - rx, s->pts[j].y - ry) < rad;
			}
		}
		if (!hit) {
			i++;
			continue;
		}
		any = true;
		g_ptr_array_remove_index_fast(d->strokes, i);
		g_ptr_array_add(p->rub->went, s);
		/* what is left of it, on either side, hanging where it did */
		GtkTextIter at;
		if (!head) {
			gtk_text_buffer_get_iter_at_mark(d->buf, &at, s->at);
		}
		struct stroke *part = NULL;
		for (int j = 0; j <= s->n; j++) {
			bool gone = j == s->n || hypot(s->pts[j].x - rx, s->pts[j].y - ry) < rad;
			if (gone) {
				if (part) {
					if (part->n >= 2) {
						g_ptr_array_add(d->strokes, part);
						g_ptr_array_add(p->rub->came, part);
					} else {
						stroke_free(part);
					}
					part = NULL;
				}
				continue;
			}
			if (!part) {
				part = stroke_new(s->ink);
				part->head = s->head;
				if (!head) {
					stroke_hang(part, d->buf, &at);
				}
			}
			stroke_add(part, s->pts[j].x, s->pts[j].y, s->pts[j].p);
		}
	}
	if (any) {
		gtk_widget_queue_draw(head ? p->sheet : p->view);
	}
}

static void rub_begin(struct pad *p, double bx, double by, bool head)
{
	p->rubbing = true;
	p->rubbing_head = head;
	p->rub = action_new();
	rub_at(p, bx, by, head);
}

static void rub_end(struct pad *p)
{
	if (!p->rubbing) {
		return;
	}
	p->rubbing = false;
	struct doc *d = pad_doc(p);
	if (p->rub->went->len == 0) {
		action_free(p->rub);
	} else {
		/* (a part made and rubbed out in the same go is in both: it was on no page before, nor after) */
		for (guint i = 0; i < p->rub->came->len;) {
			gpointer part = g_ptr_array_index(p->rub->came, i);
			guint at;
			if (g_ptr_array_find(p->rub->went, part, &at)) {
				g_ptr_array_remove_index_fast(p->rub->came, i);
				g_ptr_array_set_free_func(p->rub->went, NULL);
				g_ptr_array_remove_index_fast(p->rub->went, at);
				g_ptr_array_set_free_func(p->rub->went, stroke_free);
				stroke_free(part);
			} else {
				i++;
			}
		}
		doc_action_done(d, p->rub);
		pad_changed(p, d);
	}
	p->rub = NULL;
}

/* All that is written on the page, rubbed out, to be taken back in one go. */
static void ink_rub_all(struct pad *p)
{
	struct doc *d = pad_doc(p);
	if (d->strokes->len == 0) {
		return;
	}
	struct action *a = action_new();
	while (d->strokes->len > 0) {
		g_ptr_array_add(a->went, g_ptr_array_index(d->strokes, d->strokes->len - 1));
		g_ptr_array_remove_index_fast(d->strokes, d->strokes->len - 1);
	}
	doc_action_done(d, a);
	pad_changed(p, d);
	gtk_widget_queue_draw(p->view);
	gtk_widget_queue_draw(p->sheet);
}

/* ---- the heading: the page's number, its title, its date --------------------------------------------
 *
 * On the line printed across the top of the sheet: written in by hand, as the page's words are, and
 * turned over with them. On the notebook's own pages the title and the date are anyone's to write;
 * the date, put down empty, is today's, to be written over. A file's page has its name there. */

#define HEAD_INK 27 / 255.0, 42 / 255.0, 87 / 255.0, 0.94

static bool head_ours(struct doc *d)
{
	return d && d->number;
}

/* Where what is written there goes, in the window: between what is printed, on the heading's line
 * (the box as high as its letters, its letters on the line). */
static void head_place(struct pad *p, int i, GdkRectangle *r)
{
	struct desk_nb_heading hd;
	desk_nb_heading(gtk_widget_get_allocated_width(p->overlay), &hd);
	double x0 = i == 0 ? hd.title_x0 : hd.part_x0[i - 1], x1 = i == 0 ? hd.title_x1 : hd.part_x1[i - 1];
	PangoLayout *pl = gtk_widget_create_pango_layout(p->head[i], "Ag");
	PangoRectangle lr;
	pango_layout_get_pixel_extents(pl, NULL, &lr);
	int base = pango_layout_get_baseline(pl) / PANGO_SCALE;
	g_object_unref(pl);
	r->x = (int)x0;
	r->width = (int)(x1 - x0);
	r->y = (int)hd.base - base;
	r->height = lr.height;
}

/* Where what is over the page goes: the sheet over all of the window, the heading on its line (and
 * the rest, as GTK would have it, over the page). */
static gboolean on_over_place(GtkOverlay *o, GtkWidget *w, GdkRectangle *r, gpointer data)
{
	struct pad *p = data;
	if (w == p->sheet) {
		GtkRequisition least;
		gtk_widget_get_preferred_size(w, &least, NULL);
		*r = (GdkRectangle){ 0, 0, gtk_widget_get_allocated_width(GTK_WIDGET(o)),
			gtk_widget_get_allocated_height(GTK_WIDGET(o)) };
		return TRUE;
	}
	for (int i = 0; i < 4; i++) {
		if (w == p->head[i]) {
			/* (what it asks for, asked: it gets what the line has room for) */
			GtkRequisition least;
			gtk_widget_get_preferred_size(w, &least, NULL);
			head_place(p, i, r);
			return TRUE;
		}
	}
	return FALSE;
}

/* Written there, as a part of the heading shows it: for a picture of a page not open. */
static void head_write(cairo_t *cr, struct pad *p, int i, const char *text)
{
	if (!text || !*text) {
		return;
	}
	GdkRectangle r;
	head_place(p, i, &r);
	PangoLayout *pl = gtk_widget_create_pango_layout(p->head[i], text);
	PangoRectangle lr;
	pango_layout_get_pixel_extents(pl, NULL, &lr);
	float along = gtk_entry_get_alignment(GTK_ENTRY(p->head[i]));
	double x = r.x + (lr.width < r.width ? (r.width - lr.width) * along : 0.0);
	cairo_save(cr);
	cairo_rectangle(cr, r.x, r.y - 4, r.width, r.height + 8);
	cairo_clip(cr);
	cairo_set_source_rgba(cr, HEAD_INK);
	cairo_move_to(cr, x, r.y + (r.height - lr.height) / 2);
	pango_cairo_show_layout(cr, pl);
	cairo_restore(cr);
	g_object_unref(pl);
}

/* The page's number, after "No.": the notebook's, and only its own pages have one. */
static void head_number(cairo_t *cr, struct pad *p, struct doc *d)
{
	if (!head_ours(d)) {
		return;
	}
	struct desk_nb_heading hd;
	desk_nb_heading(gtk_widget_get_allocated_width(p->overlay), &hd);
	char n[16];
	snprintf(n, sizeof n, "%d", d->number);
	PangoLayout *pl = gtk_widget_create_pango_layout(p->head[1], n);
	PangoRectangle lr;
	pango_layout_get_pixel_extents(pl, NULL, &lr);
	double room = hd.no_x1 - hd.no_x0, k = lr.width > room ? room / lr.width : 1.0;
	double base = pango_layout_get_baseline(pl) / (double)PANGO_SCALE;
	cairo_save(cr);
	cairo_translate(cr, hd.no_x0 + (room - lr.width * k) * 0.5, hd.base);
	/* (a long number written smaller, to go in) */
	cairo_scale(cr, k, k);
	cairo_set_source_rgba(cr, HEAD_INK);
	cairo_move_to(cr, 0, -base);
	pango_cairo_show_layout(cr, pl);
	cairo_restore(cr);
	g_object_unref(pl);
}

/* All that is written in the heading of page d, which is not the one open. */
static void head_draw(cairo_t *cr, struct pad *p, struct doc *d)
{
	head_number(cr, p, d);
	if (head_ours(d)) {
		head_write(cr, p, 0, d->title);
		for (int i = 0; p->head_wide && i < 3; i++) {
			head_write(cr, p, i + 1, d->date[i]);
		}
	} else if (d) {
		char name[300];
		head_write(cr, p, 0, doc_name(d, name, sizeof name));
	}
}

/* What the heading is printed with, on a page of its own: the desk's has it already. */
static void head_print(cairo_t *cr, struct pad *p, int w)
{
	if (!PAD_VIEW(p->view)->own_paper) {
		return;
	}
	paper_ready();
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, NB_HEAD_H);
	cairo_clip(cr);
	desk_nb_lay_print(cr, &g_paper.art, 0, 0, w, NB_HEAD_H + 8);
	cairo_restore(cr);
}

/* What is written by hand in the heading of page d, on a sheet w wide: as much of it as is there to
 * be seen (no date on a sheet too narrow for one). */
static void head_ink(cairo_t *cr, struct pad *p, struct doc *d, int w)
{
	if (!head_ours(d)) {
		return;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (guint k = 0; k < d->strokes->len; k++) {
			const struct stroke *s = g_ptr_array_index(d->strokes, k);
			if (s->head && (s->head == 1 || p->head_wide) && (s->ink == INK_MARKER) == (pass == 0)) {
				stroke_paint(cr, s, HEAD_PITCH, head_from(s, w), 0.0);
			}
		}
	}
	if (p->head_wet && d == pad_doc(p)) {
		stroke_paint(cr, p->head_wet, HEAD_PITCH, head_from(p->head_wet, w), 0.0);
	}
}

/* Which part of the heading a hand here writes in: 1 the title, 2 the date, or 0, neither (only the
 * notebook's own pages have a heading to write in). */
static int head_part_at(struct pad *p, double x, double y)
{
	if (!head_ours(pad_doc(p)) || y >= NB_HEAD_H || x < NB_INSET_L) {
		return 0;
	}
	struct desk_nb_heading hd;
	desk_nb_heading(gtk_widget_get_allocated_width(p->overlay), &hd);
	return p->head_wide && hd.has_date && x >= hd.date_x ? 2 : 1;
}

static bool head_written(struct doc *d, int part)
{
	for (guint k = 0; k < d->strokes->len; k++) {
		if (((const struct stroke *)g_ptr_array_index(d->strokes, k))->head == part) {
			return true;
		}
	}
	return false;
}

static void head_dirty(struct pad *p)
{
	gtk_widget_queue_draw_area(p->sheet, 0, 0, gtk_widget_get_allocated_width(p->sheet), NB_HEAD_H + 40);
}

static void head_ink_on(struct pad *p, double x, double y, double pressure)
{
	struct stroke *s = p->head_wet;
	if (!s) {
		return;
	}
	double sx = (x - head_from(s, gtk_widget_get_allocated_width(p->overlay))) / HEAD_PITCH;
	double sy = y / HEAD_PITCH;
	const struct pt *last = &s->pts[s->n - 1];
	if (hypot(sx - last->x, sy - last->y) * HEAD_PITCH < 0.8) {
		return;
	}
	stroke_add(s, sx, sy, last->p + (pressure - last->p) * 0.45);
	head_dirty(p);
}

static void head_ink_forget(struct pad *p)
{
	if (p->head_wet) {
		stroke_free(p->head_wet);
		p->head_wet = NULL;
		head_dirty(p);
	}
}

static void head_ink_end(struct pad *p)
{
	struct stroke *s = p->head_wet;
	if (!s) {
		return;
	}
	p->head_wet = NULL;
	struct doc *d = pad_doc(p);
	g_ptr_array_add(d->strokes, s);
	struct action *a = action_new();
	g_ptr_array_add(a->came, s);
	doc_action_done(d, a);
	pad_changed(p, d);
	head_dirty(p);
}

/* What is typed there, come to: the title's, or the part of the date nearest. */
static void head_to_type(struct pad *p, int part, double x)
{
	int i = 0;
	if (part == 2) {
		struct desk_nb_heading hd;
		desk_nb_heading(gtk_widget_get_allocated_width(p->overlay), &hd);
		i = 1;
		for (int k = 1; k < 3; k++) {
			if (x >= (hd.part_x1[k - 1] + hd.part_x0[k]) * 0.5) {
				i = k + 1;
			}
		}
	}
	if (gtk_widget_get_visible(p->head[i])) {
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(p->head[i]));
	}
}

/* A hand in the heading, with something out of the case in it or a pen: it writes, or with a rubber
 * rubs out, in the part it is in. Tapped where nothing is written yet, it is a hand come to type
 * in it. */
static void head_hand_begin(struct pad *p, int part, double x, double y, double pressure, bool rub)
{
	head_ink_forget(p);
	p->head_x0 = x;
	p->head_y0 = y;
	p->head_to_type = !rub && !head_written(pad_doc(p), part);
	if (rub) {
		rub_begin(p, x, y, true);
		return;
	}
	struct stroke *s = stroke_new(pad_ink(p));
	s->head = part;
	stroke_add(s, (x - head_from(s, gtk_widget_get_allocated_width(p->overlay))) / HEAD_PITCH,
		y / HEAD_PITCH, pressure);
	p->head_wet = s;
	head_dirty(p);
}

static void head_hand_moves(struct pad *p, double x, double y, double pressure)
{
	if (hypot(x - p->head_x0, y - p->head_y0) > 3.0) {
		p->head_to_type = false;
	}
	if (p->rubbing && p->rubbing_head) {
		rub_at(p, x, y, true);
	} else {
		head_ink_on(p, x, y, pressure);
	}
}

static void head_hand_done(struct pad *p, double x, double y, double pressure, bool taken_away)
{
	if (p->rubbing && p->rubbing_head) {
		rub_end(p);
		return;
	}
	if (!p->head_wet) {
		return;
	}
	if (taken_away) {
		head_ink_forget(p);
		return;
	}
	head_hand_moves(p, x, y, pressure);
	if (p->head_to_type) {
		int part = p->head_wet->head;
		head_ink_forget(p);
		head_to_type(p, part, x);
		return;
	}
	head_ink_end(p);
}

/* The heading of the page that is open, from the page. */
static void head_fill(struct pad *p)
{
	struct doc *d = pad_doc(p);
	if (!d) {
		return;
	}
	bool ours = head_ours(d);
	char name[300];
	p->head_filling = true;
	GtkEntry *title = GTK_ENTRY(p->head[0]);
	const char *t = ours ? (d->title ? d->title : "") : doc_name(d, name, sizeof name);
	if (strcmp(gtk_entry_get_text(title), t)) {
		gtk_entry_set_text(title, t);
	}
	gtk_editable_set_editable(GTK_EDITABLE(title), ours);
	gtk_widget_set_can_focus(p->head[0], ours);
	for (int i = 0; i < 3; i++) {
		GtkWidget *e = p->head[i + 1];
		const char *v = ours && d->date[i] ? d->date[i] : "";
		if (strcmp(gtk_entry_get_text(GTK_ENTRY(e)), v)) {
			gtk_entry_set_text(GTK_ENTRY(e), v);
		}
		gtk_widget_set_visible(e, ours && p->head_wide);
	}
	p->head_filling = false;
	gtk_widget_queue_draw(p->sheet);
}

/* Today's date, in the three parts of it, as one stamps a page. */
static void head_today(struct pad *p)
{
	GDateTime *now = g_date_time_new_now_local();
	char part[3][8];
	snprintf(part[0], sizeof part[0], "%d", g_date_time_get_year(now));
	snprintf(part[1], sizeof part[1], "%d", g_date_time_get_month(now));
	snprintf(part[2], sizeof part[2], "%d", g_date_time_get_day_of_month(now));
	g_date_time_unref(now);
	p->head_stamping = true;
	for (int i = 0; i < 3; i++) {
		gtk_entry_set_text(GTK_ENTRY(p->head[i + 1]), part[i]);
	}
	p->head_stamping = false;
}

static void head_next(struct pad *p, int i)
{
	if (i + 1 < 4 && gtk_widget_get_visible(p->head[i + 1])) {
		gtk_widget_grab_focus(p->head[i + 1]);
	} else {
		gtk_widget_grab_focus(p->view);
	}
}

static void on_head_changed(GtkEditable *e, gpointer data)
{
	struct pad *p = data;
	struct doc *d = pad_doc(p);
	if (p->head_filling || !head_ours(d)) {
		return;
	}
	int i = 0;
	while (i < 4 && GTK_WIDGET(e) != p->head[i]) {
		i++;
	}
	if (i == 4) {
		return;
	}
	const char *t = gtk_entry_get_text(GTK_ENTRY(e));
	char **field = head_field(d, i);
	g_free(*field);
	*field = *t ? g_strdup(t) : NULL;
	pad_changed(p, d);
	/* a part of the date written out: on to the next */
	glong n = g_utf8_strlen(t, -1);
	if (i > 0 && !p->head_stamping && gtk_widget_has_focus(GTK_WIDGET(e)) && n >= (i == 1 ? 4 : 2)) {
		head_next(p, i);
	}
}

static void on_head_enter(GtkEntry *e, gpointer data)
{
	struct pad *p = data;
	for (int i = 0; i < 4; i++) {
		if (GTK_WIDGET(e) == p->head[i]) {
			head_next(p, i == 0 ? 3 : i);
		}
	}
}

static gboolean head_select_all(gpointer data)
{
	GtkWidget *e = data;
	if (gtk_widget_has_focus(e)) {
		gtk_editable_select_region(GTK_EDITABLE(e), 0, -1);
	}
	g_object_unref(e);
	return G_SOURCE_REMOVE;
}

/* Come to a date not written yet: today's, there to be written over. */
static gboolean on_head_date_in(GtkWidget *w, GdkEvent *ev, gpointer data)
{
	(void)ev;
	struct pad *p = data;
	struct doc *d = pad_doc(p);
	if (head_ours(d) && !d->date[0] && !d->date[1] && !d->date[2]) {
		head_today(p);
		/* (after the click that came to it has put the cursor down) */
		g_idle_add(head_select_all, g_object_ref(w));
	}
	return FALSE;
}

/* Gone from: nothing of it left chosen. */
static gboolean on_head_out(GtkWidget *w, GdkEvent *ev, gpointer data)
{
	(void)ev;
	(void)data;
	int at = gtk_editable_get_position(GTK_EDITABLE(w));
	gtk_editable_select_region(GTK_EDITABLE(w), at, at);
	return FALSE;
}

static void head_build(struct pad *p)
{
	for (int i = 0; i < 4; i++) {
		GtkWidget *e = gtk_entry_new();
		p->head[i] = e;
		GtkStyleContext *sc = gtk_widget_get_style_context(e);
		gtk_style_context_add_class(sc, "head");
		if (i > 0) {
			gtk_style_context_add_class(sc, "date");
			gtk_entry_set_alignment(GTK_ENTRY(e), 0.5f);
			g_signal_connect(e, "focus-in-event", G_CALLBACK(on_head_date_in), p);
		}
		gtk_entry_set_has_frame(GTK_ENTRY(e), FALSE);
		gtk_entry_set_width_chars(GTK_ENTRY(e), 1);
		g_signal_connect(e, "changed", G_CALLBACK(on_head_changed), p);
		g_signal_connect(e, "focus-out-event", G_CALLBACK(on_head_out), p);
		g_signal_connect(e, "activate", G_CALLBACK(on_head_enter), p);
		gtk_overlay_add_overlay(GTK_OVERLAY(p->overlay), e);
	}
	g_signal_connect(p->overlay, "get-child-position", G_CALLBACK(on_over_place), p);
}

/* What is written on the page, the heading's and the rest, shown or not: not while the sheet it is
 * on is in the air, which shows a picture of it. */
static void pad_writing_shows(struct pad *p, bool shows)
{
	gtk_widget_set_opacity(p->scroll, shows ? 1.0 : 0.0);
	for (int i = 0; i < 4; i++) {
		gtk_widget_set_opacity(p->head[i], shows ? 1.0 : 0.0);
	}
}

/* ---- a page drawn here ---------------------------------------------------------------------------
 *
 * For a picture of a page that is not the one on the sheet -- the one under it as it is turned, a
 * PDF, a picture to paste -- it is laid out as the sheet lays it: the same letters, as wide, lines
 * as far apart. */

struct laid {
	PangoLayout **lines;      /* a layout to each line of the buffer */
	double *top;              /* where each starts */
	int n;
	double end;               /* where the last ends */
};

static void laid_free(struct laid *l)
{
	for (int i = 0; i < l->n; i++) {
		g_object_unref(l->lines[i]);
	}
	g_free(l->lines);
	g_free(l->top);
}

static struct laid page_lay(struct pad *p, struct doc *d, int w)
{
	GtkTextView *tv = GTK_TEXT_VIEW(p->view);
	struct laid l = { 0 };
	l.n = gtk_text_buffer_get_line_count(d->buf);
	l.lines = g_new0(PangoLayout *, l.n);
	l.top = g_new0(double, l.n);
	int above = gtk_text_view_get_pixels_above_lines(tv);
	int inside = gtk_text_view_get_pixels_inside_wrap(tv);
	double y = gtk_text_view_get_top_margin(tv);
	for (int i = 0; i < l.n; i++) {
		GtkTextIter a, b;
		gtk_text_buffer_get_iter_at_line(d->buf, &a, i);
		b = a;
		if (!gtk_text_iter_ends_line(&b)) {
			gtk_text_iter_forward_to_line_end(&b);
		}
		char *text = gtk_text_buffer_get_slice(d->buf, &a, &b, TRUE);
		PangoLayout *pl = gtk_widget_create_pango_layout(p->view, text);
		g_free(text);
		if (g_set.wrap) {
			pango_layout_set_width(pl, (w - LEFT_MARGIN - RIGHT_MARGIN) * PANGO_SCALE);
			pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
		}
		pango_layout_set_spacing(pl, inside * PANGO_SCALE);
		y += above;
		l.top[i] = y;
		l.lines[i] = pl;
		int lw, lh;
		pango_layout_get_pixel_size(pl, &lw, &lh);
		y += lh;
	}
	l.end = y;
	return l;
}

/* Where, laid out so, the line that has the character at offset begins. */
static double laid_line_top(const struct laid *l, struct doc *d, int offset)
{
	GtkTextIter it;
	gtk_text_buffer_get_iter_at_offset(d->buf, &it, offset);
	int line = gtk_text_iter_get_line(&it);
	if (line < 0 || line >= l->n) {
		return 0.0;
	}
	GtkTextIter start = it;
	gtk_text_iter_set_line_offset(&start, 0);
	char *before = gtk_text_buffer_get_slice(d->buf, &start, &it, TRUE);
	int index = (int)strlen(before);
	g_free(before);
	PangoRectangle r;
	pango_layout_index_to_pos(l->lines[line], index, &r);
	return l->top[line] + r.y / (double)PANGO_SCALE;
}

/* The page, w by h, from y0 down the page: its ruling, its words and what is written on it. */
static void page_draw(cairo_t *cr, struct pad *p, struct doc *d, const struct laid *l, int w, int h,
	double y0, bool own_paper)
{
	PadView *pv = PAD_VIEW(p->view);
	int pitch = (int)pad_pitch(p);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cairo_translate(cr, 0, -y0);
	cairo_pattern_t *blue = desk_nb_ink(NB_BLUE, NB_BLUE_A);
	if (own_paper) {
		cairo_pattern_t *red = desk_nb_ink(NB_RED, NB_RED_A);
		desk_nb_line_down(cr, red, MARGIN_AT, (int)y0, (int)(y0 + h));
		cairo_pattern_destroy(red);
	}
	/* the rules, at each line's foot and on down the page */
	if (d->paper != PAPER_PLAIN) {
		double last = 0.0;
		cairo_set_source_rgba(cr, NB_BLUE, 0.72);
		for (int i = 0; i < l->n; i++) {
			PangoLayoutIter *it = pango_layout_get_iter(l->lines[i]);
			do {
				PangoRectangle lr;
				pango_layout_iter_get_line_extents(it, NULL, &lr);
				double foot = l->top[i] + (lr.y + lr.height) / (double)PANGO_SCALE - pv->sink;
				last = foot;
				if (foot < y0 - 2 || foot > y0 + h - 3) {
					continue;
				}
				if (d->paper == PAPER_DOTTED) {
					for (int x = MARGIN_AT + pitch; x < w - 2; x += pitch) {
						cairo_new_sub_path(cr);
						cairo_arc(cr, x, (int)foot, 0.95, 0.0, 2.0 * G_PI);
					}
					cairo_fill(cr);
				} else {
					desk_nb_line_across(cr, blue, 0, w, (int)foot);
				}
			} while (pango_layout_iter_next_line(it));
			pango_layout_iter_free(it);
		}
		for (double y = last + pitch; y <= y0 + h - 3; y += pitch) {
			if (y < y0) {
				continue;
			}
			if (d->paper == PAPER_DOTTED) {
				cairo_set_source_rgba(cr, NB_BLUE, 0.72);
				for (int x = MARGIN_AT + pitch; x < w - 2; x += pitch) {
					cairo_new_sub_path(cr);
					cairo_arc(cr, x, (int)y, 0.95, 0.0, 2.0 * G_PI);
				}
				cairo_fill(cr);
			} else {
				desk_nb_line_across(cr, blue, 0, w, (int)y);
			}
		}
		if (d->paper == PAPER_SQUARED) {
			for (int x = MARGIN_AT + pitch; x < w - 2; x += pitch) {
				desk_nb_line_down(cr, blue, x, (int)y0, (int)(y0 + h - 3));
			}
			for (int x = MARGIN_AT - pitch; x > 2; x -= pitch) {
				desk_nb_line_down(cr, blue, x, (int)y0, (int)(y0 + h - 3));
			}
		}
	}
	cairo_pattern_destroy(blue);
	for (int pass = 0; pass < 3; pass++) {
		if (pass == 1) {
			/* the words, over the highlighter and under the ink */
			cairo_set_source_rgba(cr, 27 / 255.0, 42 / 255.0, 87 / 255.0, 0.94);
			for (int i = 0; i < l->n; i++) {
				if (l->top[i] > y0 + h || (i + 1 < l->n && l->top[i + 1] < y0)) {
					continue;
				}
				cairo_move_to(cr, LEFT_MARGIN, l->top[i]);
				pango_cairo_show_layout(cr, l->lines[i]);
			}
			continue;
		}
		for (guint k = 0; k < d->strokes->len; k++) {
			const struct stroke *s = g_ptr_array_index(d->strokes, k);
			if ((s->ink == INK_MARKER) != (pass == 0) || !s->at) {
				continue;
			}
			GtkTextIter it;
			gtk_text_buffer_get_iter_at_mark(d->buf, &it, s->at);
			double hy = laid_line_top(l, d, gtk_text_iter_get_offset(&it));
			stroke_paint(cr, s, pitch, LEFT_MARGIN, hy);
		}
	}
	cairo_restore(cr);
}

static cairo_surface_t *surface_like(struct pad *p, int w, int h)
{
	GdkWindow *win = gtk_widget_get_window(p->sheet);
	if (win) {
		int scale = gtk_widget_get_scale_factor(p->sheet);
		return gdk_window_create_similar_image_surface(win, CAIRO_FORMAT_ARGB32, w * scale, h * scale, scale);
	}
	return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
}

/* Where the page's words begin, in the window. */
static void view_at(struct pad *p, int *x, int *y)
{
	if (!gtk_widget_translate_coordinates(p->view, p->sheet, 0, 0, x, y)) {
		*x = NB_INSET_L;
		*y = NB_HEAD_H;
	}
}

/* A picture of the page at i, as its sheet would show it from its top, its heading and all; i -1,
 * a clean one. With print, what a page of its own is printed with as well. */
static cairo_surface_t *page_picture(struct pad *p, int i, int w, int h, bool print)
{
	cairo_surface_t *s = surface_like(p, w, h);
	cairo_t *cr = cairo_create(s);
	struct doc *d = i >= 0 ? pad_doc_at(p, i) : NULL;
	struct doc *clean = NULL;
	if (!d) {
		clean = doc_new(NULL, p->notebook ? (int)p->docs->len + 1 : 0);
		clean->paper = pad_doc(p) ? pad_doc(p)->paper : PAPER_LINED;
		d = clean;
	}
	if (print) {
		head_print(cr, p, w);
	}
	head_draw(cr, p, d);
	head_ink(cr, p, d, w);
	int vx, vy;
	view_at(p, &vx, &vy);
	int vw = gtk_widget_get_allocated_width(p->view), vh = gtk_widget_get_allocated_height(p->view);
	cairo_translate(cr, vx, vy);
	struct laid l = page_lay(p, d, vw);
	page_draw(cr, p, d, &l, vw, vh, 0.0, PAD_VIEW(p->view)->own_paper);
	laid_free(&l);
	cairo_destroy(cr);
	if (clean) {
		doc_free(clean);
	}
	return s;
}

/* What the sheet shows now, as it is: its heading, as written in, and its page. */
static cairo_surface_t *sheet_picture(struct pad *p, int w, int h, bool print)
{
	cairo_surface_t *s = surface_like(p, w, h);
	cairo_t *cr = cairo_create(s);
	if (print) {
		head_print(cr, p, w);
	}
	head_number(cr, p, pad_doc(p));
	for (int i = 0; i < 4; i++) {
		int x, y;
		if (gtk_widget_is_drawable(p->head[i])
			&& gtk_widget_translate_coordinates(p->head[i], p->sheet, 0, 0, &x, &y)) {
			cairo_save(cr);
			cairo_translate(cr, x, y);
			gtk_widget_draw(p->head[i], cr);
			cairo_restore(cr);
		}
	}
	head_ink(cr, p, pad_doc(p), w);
	int vx, vy;
	view_at(p, &vx, &vy);
	cairo_translate(cr, vx, vy);
	gtk_widget_draw(p->view, cr);
	cairo_destroy(cr);
	return s;
}

/* The page as a PDF: as many sheets as its words go down. */
static void pdf_save_to(struct pad *p, const char *path)
{
	struct doc *d = pad_doc(p);
	int w = gtk_widget_get_allocated_width(p->view), h = NB_WIN_H - NB_HEAD_H;
	w = w < 200 ? NB_WIN_W - NB_INSET_L : w;
	struct laid l = page_lay(p, d, w);
	double most = l.end;
	for (guint k = 0; k < d->strokes->len; k++) {
		const struct stroke *s = g_ptr_array_index(d->strokes, k);
		if (!s->at) {
			continue;
		}
		GtkTextIter it;
		gtk_text_buffer_get_iter_at_mark(d->buf, &it, s->at);
		double hy = laid_line_top(&l, d, gtk_text_iter_get_offset(&it));
		most = fmax(most, hy + s->y1 * pad_pitch(p) + 8.0);
	}
	cairo_surface_t *pdf = cairo_pdf_surface_create(path, w, h);
	cairo_t *cr = cairo_create(pdf);
	for (double y0 = 0.0; y0 < most || y0 == 0.0; y0 += h - 40) {
		cairo_save(cr);
		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		cairo_paint(cr);
		page_draw(cr, p, d, &l, w, h, y0, true);
		cairo_restore(cr);
		cairo_show_page(cr);
	}
	laid_free(&l);
	cairo_destroy(cr);
	cairo_surface_destroy(pdf);
}

static void pdf_ask(struct pad *p)
{
	GtkFileChooserNative *c = gtk_file_chooser_native_new(TR("Save as PDF"), GTK_WINDOW(p->win),
		GTK_FILE_CHOOSER_ACTION_SAVE, TR("Save"), TR("Cancel"));
	GtkFileChooser *fc = GTK_FILE_CHOOSER(c);
	gtk_file_chooser_set_do_overwrite_confirmation(fc, TRUE);
	char name[300], with[320];
	doc_name(pad_doc(p), name, sizeof name);
	char *dot = strrchr(name, '.');
	if (dot && dot != name) {
		*dot = '\0';
	}
	snprintf(with, sizeof with, "%s.pdf", name);
	gtk_file_chooser_set_current_name(fc, with);
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		char *path = gtk_file_chooser_get_filename(fc);
		if (path) {
			pdf_save_to(p, path);
			g_free(path);
		}
	}
	g_object_unref(c);
}

/* The page as it shows, for pasting as a picture. */
static void page_copy(struct pad *p)
{
	int w = gtk_widget_get_allocated_width(p->view), h = gtk_widget_get_allocated_height(p->view);
	struct doc *d = pad_doc(p);
	GdkRectangle vis;
	gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(p->view), &vis);
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * 2, h * 2);
	cairo_t *cr = cairo_create(img);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);
	cairo_scale(cr, 2.0, 2.0);
	struct laid l = page_lay(p, d, w);
	page_draw(cr, p, d, &l, w, h, vis.y, true);
	laid_free(&l);
	cairo_destroy(cr);
	GdkPixbuf *pix = gdk_pixbuf_get_from_surface(img, 0, 0, w * 2, h * 2);
	cairo_surface_destroy(img);
	if (pix) {
		gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pix);
		g_object_unref(pix);
	}
}

/* ---- the page that is open --------------------------------------------------------------------- */

static void sheet_shape(struct pad *p);

static void pad_show(struct pad *p, int i)
{
	struct doc *d = pad_doc_at(p, i);
	if (!d) {
		return;
	}
	if (gtk_widget_get_visible(p->note)) {
		note_hide(p);
	}
	write_forget(p);
	head_ink_forget(p);
	p->at = i;
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(p->view), d->buf);
	GtkTextIter start;
	gtk_text_buffer_get_start_iter(d->buf, &start);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(p->view), &start, 0.0, FALSE, 0.0, 0.0);
	if (p->notebook) {
		g_set.page = i;
		conf_save();
	}
	pad_title(p);
	head_fill(p);
	sheet_shape(p);
	gtk_widget_queue_draw(p->view);
	gtk_widget_queue_draw(p->sheet);
}

/* A clean page after the last, open: of the notebook's, or one to save as a file. */
static void pad_new_page(struct pad *p)
{
	int n = (int)p->docs->len;
	struct doc *last = pad_doc_at(p, n - 1);
	if (last && doc_blank(last) && !last->path) {
		pad_show(p, n - 1);
		return;
	}
	g_ptr_array_add(p->docs, doc_new(p, p->notebook ? n + 1 : 0));
	pad_show(p, n);
}

/* ---- turning a sheet over, and what is under it ------------------------------------------------- */

/* (the window is all of the sheet, from where the wire holds it) */
static double hinge_x(void)
{
	return 0.0;
}

static double sheet_w(struct pad *p)
{
	return gtk_widget_get_allocated_width(p->sheet);
}

/* On the desk, where the window has the hand: the page, and what is written in its heading; or all
 * of it while a sheet is up. The holes and the rest of the heading are the desk's, to take hold
 * of the notebook by. */
static void pad_input_shape(struct pad *p)
{
	GdkWindow *top = gtk_widget_get_window(p->win);
	if (!top || !g_on_desk) {
		return;
	}
	if (p->whole || p->turn.on) {
		gdk_window_input_shape_combine_region(top, NULL, 0, 0);
		return;
	}
	int w = gtk_widget_get_allocated_width(p->win), h = gtk_widget_get_allocated_height(p->win);
	cairo_rectangle_int_t page = { NB_INSET_L, NB_HEAD_H, w - NB_INSET_L, h - NB_HEAD_H };
	cairo_region_t *r = cairo_region_create_rectangle(&page);
	/* the heading's title and date, the height of the heading, to be written in by hand: a pen's
	 * letters go higher than typed ones, and across the date's dots */
	if (head_ours(pad_doc(p))) {
		struct desk_nb_heading hd;
		desk_nb_heading(w, &hd);
		cairo_rectangle_int_t title = { (int)hd.title_x0 - 6, 3, (int)(hd.title_x1 - hd.title_x0) + 12,
			NB_HEAD_H - 3 };
		cairo_region_union_rectangle(r, &title);
		if (p->head_wide && hd.has_date) {
			cairo_rectangle_int_t date = { (int)hd.date_x, 3, (int)(hd.part_x1[2] - hd.date_x) + 8,
				NB_HEAD_H - 3 };
			cairo_region_union_rectangle(r, &date);
		}
	}
	gdk_window_input_shape_combine_region(top, r, 0, 0);
	cairo_region_destroy(r);
}

/* Where a hand finds what is over the window: its corners, or all of it while a sheet is up. */
static void sheet_shape(struct pad *p)
{
	GdkWindow *win = gtk_widget_get_window(p->sheet);
	if (!win) {
		return;
	}
	int w = gtk_widget_get_allocated_width(p->sheet), h = gtk_widget_get_allocated_height(p->sheet);
	cairo_rectangle_int_t all = { 0, 0, w, h };
	cairo_rectangle_int_t foot = { w - (int)CORNER, h - (int)CORNER, (int)CORNER, (int)CORNER };
	cairo_rectangle_int_t back = { NB_INSET_L, h - (int)CORNER, (int)CORNER, (int)CORNER };
	cairo_region_t *r = cairo_region_create_rectangle(p->turn.on ? &all : &foot);
	if (!p->turn.on && p->at > 0) {
		cairo_region_union_rectangle(r, &back);
	}
	gdk_window_input_shape_combine_region(win, r, 0, 0);
	cairo_region_destroy(r);
	pad_input_shape(p);
}

static void turn_end(struct pad *p)
{
	if (p->turn.tick) {
		gtk_widget_remove_tick_callback(p->sheet, p->turn.tick);
		p->turn.tick = 0;
	}
	if (p->turn.pic) {
		cairo_surface_destroy(p->turn.pic);
		p->turn.pic = NULL;
	}
	if (p->turn.under_pic) {
		cairo_surface_destroy(p->turn.under_pic);
		p->turn.under_pic = NULL;
	}
	p->turn.on = false;
	p->turn.held = false;
	p->turn.propped = false;
	p->turn.hot = -1;
	pad_writing_shows(p, true);
	sheet_shape(p);
	gtk_widget_queue_draw(p->sheet);
}

/* Done: where it has come to is the page that is open -- on to the next, back to the last, or as
 * it was; after the last, a clean one. */
static void turn_settled(struct pad *p, int page)
{
	turn_end(p);
	pads_keep_all(p);
	if (page < 0) {
		pad_new_page(p);
	} else if (page != p->at) {
		pad_show(p, page);
	}
	gtk_widget_grab_focus(p->view);
}

/* How far up it is propped for what is under it to show: the card on it, all of it, in the light. */
static double prop_angle(struct pad *p)
{
	int w = gtk_widget_get_allocated_width(p->sheet);
	double left = p->n_items > 0 ? p->items[0].r.x - 6 : w - 196;
	double c = (left - 12.0 - hinge_x()) / sheet_w(p);
	double most = cos(PEEK + 0.12), least = cos(BALANCE - 0.025);
	c = c < least ? least : c > most ? most : c;
	return acos(c);
}

static void menu_lay(struct pad *p);
static void menu_up(struct pad *p);

/* It falls the way it leans: flat on this side under its own weight, past standing on over to
 * the other; slowed by the air it pushes. Propped, it is held up off the page under it. */
static gboolean turn_tick(GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
	(void)w;
	struct pad *p = data;
	gint64 now = gdk_frame_clock_get_frame_time(clock);
	double dt = (now - p->turn.last) / 1e6;
	p->turn.last = now;
	dt = dt < 0.0 ? 0.0 : dt > 0.05 ? 0.05 : dt;
	if (p->turn.held) {
		return G_SOURCE_CONTINUE;
	}
	int n = (int)ceil(dt * 400.0);
	double h = n > 0 ? dt / n : 0.0;
	for (int i = 0; i < n; i++) {
		if (p->turn.propped) {
			/* a sheet with the curve of a page held up in it: it springs to where it is held */
			p->turn.w += (-70.0 * (p->turn.a - p->turn.prop) - 11.0 * p->turn.w) * h;
			p->turn.a += p->turn.w * h;
			continue;
		}
		double lean = p->turn.a * (G_PI * 0.5 / BALANCE);
		p->turn.w += (-GRAVITY * cos(lean > G_PI ? G_PI : lean) - AIR * p->turn.w) * h;
		p->turn.a += p->turn.w * h;
		if (!p->turn.can_go && p->turn.a > BALANCE - 0.04) {
			/* nothing to go over to: it comes back off the wire */
			p->turn.a = BALANCE - 0.04;
			p->turn.w = -fabs(p->turn.w) * 0.3 - 0.4;
		}
		if (p->turn.a <= 0.0) {
			if (p->turn.w < -1.2) {
				/* it lands, and lifts a little at the edge */
				p->turn.a = 0.0;
				p->turn.w = -p->turn.w * 0.22;
			} else {
				turn_settled(p, p->turn.sheet);
				return G_SOURCE_REMOVE;
			}
		} else if (p->turn.a >= G_PI * 0.5 + 0.02) {
			/* gone over the wire */
			turn_settled(p, p->turn.under);
			return G_SOURCE_REMOVE;
		}
	}
	if (p->turn.propped && fabs(p->turn.a - p->turn.prop) < 0.002 && fabs(p->turn.w) < 0.02) {
		p->turn.a = p->turn.prop;
		p->turn.w = 0.0;
		p->turn.tick = 0;
		gtk_widget_queue_draw(p->sheet);
		return G_SOURCE_REMOVE;
	}
	gtk_widget_queue_draw(p->sheet);
	return G_SOURCE_CONTINUE;
}

static void turn_go(struct pad *p)
{
	if (!p->turn.tick) {
		p->turn.last = g_get_monotonic_time();
		p->turn.tick = gtk_widget_add_tick_callback(p->sheet, turn_tick, p, NULL);
	}
	gtk_widget_queue_draw(p->sheet);
}

/* (the window's own paper, PAPER_CSS: lying flat, the sheet is the page it lies on) */
static void paper_fill(cairo_t *cr, int w, int h)
{
	cairo_set_source_rgb(cr, 0xf4 / 255.0, 0xef / 255.0, 0xe2 / 255.0);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);
}

/* The sheet in the air, before what is on it: on DEsk the desk's own paper, holes, print and all,
 * as it lies; or a page of its own. */
static void sheet_paper(struct pad *p, cairo_t *cr, int w, int h)
{
	if (PAD_VIEW(p->view)->own_paper || !paper_ready()) {
		paper_fill(cr, w, h);
		head_print(cr, p, w);
		return;
	}
	const double R = NB_SHEET_R;
	cairo_save(cr);
	cairo_new_sub_path(cr);
	cairo_arc(cr, w - R, R, R, -G_PI / 2.0, 0.0);
	cairo_arc(cr, w - R, h - R, R, 0.0, G_PI / 2.0);
	cairo_arc(cr, R, h - R, R, G_PI / 2.0, G_PI);
	cairo_arc(cr, R, R, R, G_PI, G_PI * 1.5);
	cairo_close_path(cr);
	cairo_clip(cr);
	desk_nb_lay_page(cr, &g_paper.art, 0, 0, w, h);
	desk_nb_lay_print(cr, &g_paper.art, 0, 0, w, h);
	desk_nb_lay_tooth(cr, &g_paper.art, 0, 0, w, h);
	cairo_restore(cr);
}

/* Taken hold of, or set going, towards the next page (dir 1) or back to the last (-1). */
static void turn_begin(struct pad *p, int dir, bool held, double x)
{
	int to = p->at + dir;
	int n = (int)p->docs->len;
	if (to < 0 || (dir < 0 && to >= n)) {
		return;
	}
	struct doc *cur = pad_doc(p);
	bool past = to >= n;
	/* a clean page with none after it has nothing to turn over to, but what is under it */
	bool can_go = !(past && cur && doc_blank(cur) && !cur->path);
	if (!held && !can_go) {
		return;
	}
	if (p->turn.on) {
		turn_end(p);
	}
	if (gtk_widget_get_visible(p->note)) {
		note_hide(p);
	}
	write_forget(p);
	int w = gtk_widget_get_allocated_width(p->sheet), h = gtk_widget_get_allocated_height(p->sheet);
	p->turn.on = true;
	p->turn.held = held;
	p->turn.propped = false;
	p->turn.by_corner = false;
	p->turn.can_go = can_go;
	p->turn.dir = dir;
	p->turn.w = 0.0;
	p->turn.hot = -1;
	p->turn.sheet = dir > 0 ? p->at : to;
	p->turn.under = dir > 0 ? (past ? -1 : to) : p->at;
	/* the page coming back comes up from over the wire, standing; the page going starts flat */
	p->turn.a = dir > 0 ? 0.0 : G_PI * 0.5;
	p->turn.x0 = x;
	p->turn.x = x;
	p->turn.tx = 0.0;
	p->turn.vx = 0.0;
	p->turn.edge0 = hinge_x() + sheet_w(p) * cos(p->turn.a);
	/* the sheet in the air, on paper of its own; and the page under it, on the desk's */
	p->turn.pic = surface_like(p, w, h);
	cairo_t *cr = cairo_create(p->turn.pic);
	sheet_paper(p, cr, w, h);
	cairo_surface_t *face = dir > 0 ? sheet_picture(p, w, h, false) : page_picture(p, to, w, h, false);
	cairo_set_source_surface(cr, face, 0, 0);
	cairo_paint(cr);
	cairo_surface_destroy(face);
	cairo_destroy(cr);
	p->turn.under_pic = dir > 0 ? page_picture(p, past ? -1 : to, w, h, true) : sheet_picture(p, w, h, true);
	pad_writing_shows(p, false);
	menu_lay(p);
	sheet_shape(p);
	turn_go(p);
	if (!held) {
		/* on its way by itself, as a key asks */
		p->turn.w = dir > 0 ? 7.5 : -6.0;
	}
}

static void turn_hand_moves(struct pad *p, double x, gint64 t_us)
{
	if (!p->turn.on || !p->turn.held) {
		return;
	}
	/* the free edge goes where the hand goes */
	double t = t_us / 1e6;
	double dt = t - p->turn.tx;
	if (dt > 0.0005) {
		double a = dt > 0.08 ? 1.0 : 0.4;
		p->turn.vx = (1.0 - a) * p->turn.vx + a * (x - p->turn.x) / dt;
		p->turn.tx = t;
		p->turn.x = x;
	}
	double edge = p->turn.edge0 + (x - p->turn.x0);
	double c = (edge - hinge_x()) / sheet_w(p);
	c = c < 0.0 ? 0.0 : c > 1.0 ? 1.0 : c;
	p->turn.a = acos(c);
	if (!p->turn.can_go && p->turn.a > BALANCE - 0.04) {
		p->turn.a = BALANCE - 0.04;
	}
	gtk_widget_queue_draw(p->sheet);
}

/* Let go: on with how fast the hand was going. Short of going over and not thrown, a sheet lifted
 * off the page under it stays up, and what can be done is there. */
static void turn_hand_lets_go(struct pad *p, gint64 t_us)
{
	if (!p->turn.on || !p->turn.held) {
		return;
	}
	p->turn.held = false;
	if (p->turn.retaken && fabs(p->turn.x - p->turn.x0) < TAKES_HOLD) {
		/* a tap on it, up: down it goes */
		p->turn.retaken = false;
		turn_drop(p);
		return;
	}
	if (p->turn.by_corner && fabs(p->turn.x - p->turn.x0) < TAKES_HOLD) {
		p->turn.by_corner = false;
		if (p->turn.dir > 0) {
			/* the foot's corner tapped: up only as far as what can be done shows; over to the
			 * next page (or a new one) only taken across by hand, or by a key (2026-11-11, the
			 * owner: a click shows the items, no more) */
			p->turn.propped = true;
			p->turn.prop = prop_angle(p);
			p->turn.w = 4.0;
			turn_go(p);
			return;
		}
		/* the other corner tapped: back over it goes, as a key would send it */
		if (!p->turn.can_go) {
			turn_drop(p);
			return;
		}
		p->turn.w = p->turn.dir > 0 ? 7.5 : -6.0;
		turn_go(p);
		return;
	}
	p->turn.by_corner = false;
	p->turn.retaken = false;
	double still = t_us / 1e6 - p->turn.tx > 0.08 ? 0.0 : 1.0;
	double s = sin(p->turn.a);
	s = s < 0.15 ? 0.15 : s;
	p->turn.w = -p->turn.vx * still / (sheet_w(p) * s);
	p->turn.w = p->turn.w < -24.0 ? -24.0 : p->turn.w > 24.0 ? 24.0 : p->turn.w;
	if (p->turn.dir > 0 && p->turn.a >= PEEK && fabs(p->turn.w) < THROWN
		&& (p->turn.a < BALANCE || !p->turn.can_go)) {
		p->turn.propped = true;
		p->turn.prop = prop_angle(p);
	}
	/* (let go at standing, it goes the way it was going, or on over) */
	if (p->turn.a >= G_PI * 0.5 - 1e-3 && p->turn.w < 0.0) {
		p->turn.a = G_PI * 0.5 - 2e-3;
	}
	turn_go(p);
}

/* Propped up, let fall back: done with what is under it. */
static void turn_drop(struct pad *p)
{
	if (!p->turn.on || p->turn.held) {
		return;
	}
	p->turn.propped = false;
	p->turn.hot = -1;
	if (p->turn.dir > 0 && p->turn.w > -0.5) {
		p->turn.w = -0.5;
	}
	turn_go(p);
}

/* ---- what can be done, on the page under a sheet turned up -------------------------------------- */

static void items_add(struct pad *p, int what, const char *label, const char *keys, bool ticked, int row)
{
	if (p->n_items >= ITEMS_MOST) {
		return;
	}
	p->items[p->n_items++] = (struct item){ .what = what, .label = label, .keys = keys, .ticked = ticked,
		.row = row, .r = { -1, -1, 0, 0 } };
}

static PangoLayout *menu_words(struct pad *p, const char *text, double size)
{
	PangoLayout *pl = gtk_widget_create_pango_layout(p->sheet, text);
	PangoFontDescription *fd = pango_font_description_from_string(desk_font_family(DESK_FONT_HAND, text));
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	return pl;
}

#define MENU_ROW 27.0
#define MENU_PIC 18.0

/* What there is, for this page, and where each goes on its card: at the foot of the page under,
 * against its right edge, where the hand is. */
static void menu_lay(struct pad *p)
{
	struct doc *d = pad_doc(p);
	p->n_items = 0;
	if (!d) {
		return;
	}
	bool file = !d->number;
	if (file) {
		items_add(p, DO_SAVE, TR("Save"), "Ctrl+S", false, 0);
		items_add(p, DO_SAVE_AS, TR("Save As"), "Ctrl+Shift+S", false, 0);
		items_add(p, DO_OPEN, TR("Open"), "Ctrl+O", false, 0);
	} else {
		items_add(p, DO_OPEN, TR("Open"), "Ctrl+O", false, 0);
		items_add(p, DO_SAVE_AS, TR("Save As"), "Ctrl+Shift+S", false, 0);
	}
	items_add(p, DO_PDF, TR("Save as PDF"), "Ctrl+E", false, 0);
	items_add(p, DO_PICTURE, TR("Copy this page"), NULL, false, 0);
	items_add(p, DO_FIND, TR("Find"), "Ctrl+F", false, 0);
	if (file) {
		items_add(p, DO_REPLACE, TR("Replace"), "Ctrl+H", false, 0);
		items_add(p, DO_WRAP, TR("Wrap long lines"), "Alt+Z", g_set.wrap, 0);
	}
	items_add(p, DO_TYPED, TR("Typewriter letters"), "Ctrl+T", g_set.typed, 0);
	items_add(p, DO_RUB_ALL, TR("Rub out what is written"), NULL, false, 0);
	items_add(p, DO_OPEN_OUT, p->opened_out ? TR("Refold") : TR("Unfold"), NULL, false, 0);
	items_add(p, DO_SHUT, TR("Shut"), NULL, false, 0);
	items_add(p, DO_PUT_AWAY, TR("Into the drawer"), "Ctrl+W", false, 0);
	/* how it is ruled, in pictures */
	items_add(p, DO_LINED, TR("Lined"), NULL, d->paper == PAPER_LINED, 1);
	items_add(p, DO_SQUARED, TR("Squared"), NULL, d->paper == PAPER_SQUARED, 1);
	items_add(p, DO_DOTTED, TR("Dotted"), NULL, d->paper == PAPER_DOTTED, 1);
	items_add(p, DO_PLAIN, TR("Plain"), NULL, d->paper == PAPER_PLAIN, 1);
	if (!pad_has_case(p)) {
		/* with no case to take them out of, the pens are here */
		for (int k = 0; k < NB_PENS; k++) {
			items_add(p, DO_PEN + k, NULL, NULL, g_case.own_hand == NB_HAND_PEN && g_case.own_pen == k, 2);
		}
		items_add(p, DO_MARKER, NULL, NULL, g_case.own_hand == NB_HAND_MARKER, 2);
		items_add(p, DO_RUBBER, NULL, NULL, g_case.own_hand == NB_HAND_RUBBER, 2);
		items_add(p, DO_NO_PEN, NULL, NULL, g_case.own_hand == NB_HAND_NONE, 2);
	}
	/* how wide the card is: its widest words */
	double size = fmax(11.0, pad_pitch(p) * 0.48);
	int widest = 0;
	for (int i = 0; i < p->n_items; i++) {
		if (p->items[i].row || !p->items[i].label) {
			continue;
		}
		PangoLayout *pl = menu_words(p, p->items[i].label, size);
		int lw, lh;
		pango_layout_get_pixel_size(pl, &lw, &lh);
		g_object_unref(pl);
		widest = lw > widest ? lw : widest;
	}
	int W = gtk_widget_get_allocated_width(p->sheet), H = gtk_widget_get_allocated_height(p->sheet);
	int card_w = widest + 34;
	card_w = card_w < 160 ? 160 : card_w > W / 2 ? W / 2 : card_w;
	int rows = 0;
	bool row_pics[3] = { false, false, false };
	for (int i = 0; i < p->n_items; i++) {
		if (p->items[i].row == 0) {
			rows++;
		} else if (!row_pics[p->items[i].row]) {
			row_pics[p->items[i].row] = true;
			rows++;
		}
	}
	int card_h = (int)(rows * MENU_ROW) + 22;
	int x0 = W - 14 - card_w, y = H - 30 - card_h + 12;
	if (y < NB_HEAD_H + 4) {
		y = NB_HEAD_H + 4;
	}
	int row_x[3] = { 0, x0 + 16, x0 + 16 };
	int row_y[3] = { 0, -1, -1 };
	for (int i = 0; i < p->n_items; i++) {
		struct item *it = &p->items[i];
		if (it->row == 0) {
			it->r = (GdkRectangle){ x0 + 6, y, card_w - 12, (int)MENU_ROW };
			y += (int)MENU_ROW;
			continue;
		}
		if (row_y[it->row] < 0) {
			row_y[it->row] = y;
			y += (int)MENU_ROW;
		}
		it->r = (GdkRectangle){ row_x[it->row], row_y[it->row] + 3, (int)MENU_PIC + 6, (int)MENU_ROW - 6 };
		row_x[it->row] += (int)MENU_PIC + 10;
	}
}

static int menu_item_at(struct pad *p, double x, double y)
{
	for (int i = 0; i < p->n_items; i++) {
		const GdkRectangle *r = &p->items[i].r;
		if (x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height) {
			return i;
		}
	}
	return -1;
}

/* A little picture of a page so ruled, or of a pen. */
static void menu_pic(struct pad *p, cairo_t *cr, const struct item *it, double x, double y)
{
	double s = MENU_PIC;
	if (it->what >= DO_LINED && it->what <= DO_PLAIN) {
		cairo_set_source_rgb(cr, 0.985, 0.975, 0.945);
		cairo_rectangle(cr, x, y, s, s);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.35, 0.30, 0.22, 0.45);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, NB_BLUE, 0.85);
		cairo_set_line_width(cr, 0.8);
		for (double k = 4.5; k < s - 1; k += 4.5) {
			if (it->what == DO_LINED || it->what == DO_SQUARED) {
				cairo_move_to(cr, x + 1, y + k);
				cairo_line_to(cr, x + s - 1, y + k);
			}
			if (it->what == DO_SQUARED) {
				cairo_move_to(cr, x + k, y + 1);
				cairo_line_to(cr, x + k, y + s - 1);
			}
		}
		cairo_stroke(cr);
		if (it->what == DO_DOTTED) {
			for (double a = 4.5; a < s - 1; a += 4.5) {
				for (double b = 4.5; b < s - 1; b += 4.5) {
					cairo_new_sub_path(cr);
					cairo_arc(cr, x + b, y + a, 0.75, 0.0, 2.0 * G_PI);
				}
			}
			cairo_fill(cr);
		}
	} else if (it->what >= DO_PEN && it->what <= DO_PEN_LAST) {
		int ink = PEN_INK[it->what - DO_PEN];
		cairo_set_source_rgba(cr, INK[ink].r, INK[ink].g, INK[ink].b, 0.92);
		cairo_arc(cr, x + s * 0.5, y + s * 0.5, s * 0.36, 0.0, 2.0 * G_PI);
		cairo_fill(cr);
	} else if (it->what == DO_MARKER) {
		cairo_set_source_rgba(cr, INK[INK_MARKER].r, INK[INK_MARKER].g, INK[INK_MARKER].b, 0.75);
		cairo_rectangle(cr, x + 2, y + s * 0.3, s - 4, s * 0.4);
		cairo_fill(cr);
	} else if (it->what == DO_RUBBER) {
		cairo_set_source_rgb(cr, 0.93, 0.62, 0.62);
		cairo_rectangle(cr, x + 3, y + s * 0.28, s - 6, s * 0.44);
		cairo_fill(cr);
		cairo_set_source_rgb(cr, 0.30, 0.42, 0.62);
		cairo_rectangle(cr, x + 3, y + s * 0.28, (s - 6) * 0.45, s * 0.44);
		cairo_fill(cr);
	} else if (it->what == DO_NO_PEN) {
		/* the hand empty: the words' pointer */
		cairo_set_source_rgba(cr, 0.20, 0.20, 0.22, 0.8);
		cairo_set_line_width(cr, 1.3);
		cairo_move_to(cr, x + s * 0.5, y + 2);
		cairo_line_to(cr, x + s * 0.5, y + s - 2);
		cairo_move_to(cr, x + s * 0.32, y + 2);
		cairo_line_to(cr, x + s * 0.68, y + 2);
		cairo_move_to(cr, x + s * 0.32, y + s - 2);
		cairo_line_to(cr, x + s * 0.68, y + s - 2);
		cairo_stroke(cr);
	}
	(void)p;
}

/* The card on the page under, with what can be done written on it: in the light as much as the
 * sheet over it is lifted. */
static void menu_paint(struct pad *p, cairo_t *cr, double show)
{
	if (p->n_items == 0 || show <= 0.0) {
		return;
	}
	int x0 = p->items[0].r.x - 6, y0 = p->items[0].r.y - 10;
	int x1 = 0, y1 = 0;
	for (int i = 0; i < p->n_items; i++) {
		const GdkRectangle *r = &p->items[i].r;
		x1 = r->x + r->width + 6 > x1 ? r->x + r->width + 6 : x1;
		y1 = r->y + r->height > y1 ? r->y + r->height : y1;
	}
	x1 = x1 < x0 + 168 ? x0 + 168 : x1;
	y1 += 12;
	cairo_save(cr);
	cairo_push_group(cr);
	/* a card left between the pages: a soft shadow under it, a little warm */
	for (int k = 6; k >= 1; k--) {
		cairo_set_source_rgba(cr, 0.16, 0.11, 0.05, 0.035);
		cairo_rectangle(cr, x0 - k * 0.6 + 1.5, y0 + k * 0.4 + 2.0, x1 - x0 + k * 1.2, y1 - y0 + k * 0.8);
		cairo_fill(cr);
	}
	cairo_pattern_t *face = cairo_pattern_create_linear(x0, y0, x1, y1);
	cairo_pattern_add_color_stop_rgb(face, 0.0, 0.992, 0.984, 0.958);
	cairo_pattern_add_color_stop_rgb(face, 1.0, 0.958, 0.944, 0.905);
	cairo_set_source(cr, face);
	cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
	cairo_fill(cr);
	cairo_pattern_destroy(face);
	/* ruled as an index card is: red at the top, blue under each */
	cairo_set_source_rgba(cr, NB_RED, 0.55);
	cairo_rectangle(cr, x0 + 4, y0 + 6, x1 - x0 - 8, 1.1);
	cairo_fill(cr);
	double size = fmax(11.0, pad_pitch(p) * 0.48);
	bool pics_done[3] = { false, false, false };
	for (int i = 0; i < p->n_items; i++) {
		const struct item *it = &p->items[i];
		if (it->row == 0 || !pics_done[it->row]) {
			if (it->row) {
				pics_done[it->row] = true;
			}
			cairo_set_source_rgba(cr, NB_BLUE, 0.40);
			cairo_rectangle(cr, x0 + 4, it->r.y + it->r.height - 2, x1 - x0 - 8, 0.9);
			cairo_fill(cr);
		}
		bool hot = p->turn.hot == i;
		if (it->row) {
			double px = it->r.x + 3, py = it->r.y + (it->r.height - MENU_PIC) * 0.5;
			menu_pic(p, cr, it, px, py);
			if (it->ticked || hot) {
				/* ringed, in pencil */
				cairo_set_source_rgba(cr, 0.20, 0.20, 0.22, hot && !it->ticked ? 0.45 : 0.85);
				cairo_set_line_width(cr, 1.1);
				cairo_new_path(cr);
				cairo_arc(cr, px + MENU_PIC * 0.5, py + MENU_PIC * 0.5, MENU_PIC * 0.5 + 3.0, 0.0, 2.0 * G_PI);
				cairo_stroke(cr);
			}
			continue;
		}
		if (hot) {
			/* gone over with the highlighter */
			cairo_set_source_rgba(cr, 1.0, 0.86, 0.14, 0.42);
			cairo_rectangle(cr, it->r.x + 2, it->r.y + 4, it->r.width - 4, it->r.height - 7);
			cairo_fill(cr);
		}
		PangoLayout *pl = menu_words(p, it->label, size);
		int lw, lh;
		pango_layout_get_pixel_size(pl, &lw, &lh);
		cairo_set_source_rgba(cr, 27 / 255.0, 42 / 255.0, 87 / 255.0, 0.92);
		cairo_move_to(cr, it->r.x + 10, it->r.y + (it->r.height - lh) * 0.5);
		pango_cairo_show_layout(cr, pl);
		g_object_unref(pl);
		if (it->ticked) {
			/* ticked in pencil */
			double tx = it->r.x + it->r.width - 16, ty = it->r.y + it->r.height * 0.5;
			cairo_set_source_rgba(cr, 0.20, 0.20, 0.22, 0.85);
			cairo_set_line_width(cr, 1.6);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_move_to(cr, tx - 5, ty);
			cairo_line_to(cr, tx - 1.5, ty + 4);
			cairo_line_to(cr, tx + 6, ty - 6);
			cairo_stroke(cr);
		}
	}
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, show);
	cairo_restore(cr);
}

static void do_paper(struct pad *p, int kind)
{
	struct doc *d = pad_doc(p);
	d->paper = kind;
	if (!d->number) {
		g_set.paper = kind;
		conf_save();
		/* (a file's ruling is kept with what is written over it, when it is saved) */
		d->saved_serial = (unsigned)-1;
		gtk_text_buffer_set_modified(d->buf, TRUE);
	}
	pad_changed(p, d);
	gtk_widget_queue_draw(p->view);
}

static void pad_open_ask(struct pad *p);
static bool sheet_up_hand(struct pad *p, double x, double y);

/* What was chosen on the card. */
static void do_this(struct pad *p, int what)
{
	struct doc *d = pad_doc(p);
	switch (what) {
	case DO_OPEN: pad_open_ask(p); break;
	case DO_SAVE: doc_save(d); break;
	case DO_SAVE_AS: doc_save_as(d); break;
	case DO_PDF: pdf_ask(p); break;
	case DO_PICTURE: page_copy(p); break;
	case DO_FIND: note_show(p, false); break;
	case DO_REPLACE: note_show(p, true); break;
	case DO_WRAP: g_set.wrap = !g_set.wrap; pads_restyle(); break;
	case DO_TYPED: g_set.typed = !g_set.typed; pads_restyle(); break;
	case DO_LINED: do_paper(p, PAPER_LINED); break;
	case DO_SQUARED: do_paper(p, PAPER_SQUARED); break;
	case DO_DOTTED: do_paper(p, PAPER_DOTTED); break;
	case DO_PLAIN: do_paper(p, PAPER_PLAIN); break;
	case DO_RUB_ALL: ink_rub_all(p); break;
	case DO_SHUT: gtk_window_iconify(GTK_WINDOW(p->win)); break;
	case DO_PUT_AWAY: pad_close_ask(p); break;
	case DO_OPEN_OUT:
		if (p->opened_out) {
			gtk_window_unmaximize(GTK_WINDOW(p->win));
		} else {
			gtk_window_maximize(GTK_WINDOW(p->win));
		}
		break;
	case DO_MARKER: g_case.own_hand = NB_HAND_MARKER; conf_save(); case_say(); break;
	case DO_RUBBER: g_case.own_hand = NB_HAND_RUBBER; conf_save(); case_say(); break;
	case DO_NO_PEN: g_case.own_hand = NB_HAND_NONE; conf_save(); case_say(); break;
	default:
		if (what >= DO_PEN && what <= DO_PEN_LAST) {
			g_case.own_pen = what - DO_PEN;
			g_case.own_hand = NB_HAND_PEN;
			conf_save();
			case_say();
		}
		break;
	}
}

/* ---- what is over the window: the corners, and a sheet in the air ---------------------------------- */

/* A corner to take hold of: the sheet's foot on the right turned up a little; and on the left a
 * smaller one, if there is a page to go back to. */
static void corner_paint(cairo_t *cr, int w, int h, bool back)
{
	double s = 15.0;
	cairo_pattern_t *under = cairo_pattern_create_linear(w - s, h, w, h - s);
	cairo_pattern_add_color_stop_rgba(under, 0.0, 0.25, 0.20, 0.12, 0.30);
	cairo_pattern_add_color_stop_rgba(under, 1.0, 0.25, 0.20, 0.12, 0.0);
	cairo_set_source(cr, under);
	cairo_move_to(cr, w - s - 3.0, h);
	cairo_line_to(cr, w, h - s - 3.0);
	cairo_line_to(cr, w, h);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_pattern_destroy(under);
	if (back) {
		/* (by the holes) */
		cairo_translate(cr, NB_INSET_L, 0);
		double b = 9.0;
		cairo_pattern_t *l = cairo_pattern_create_linear(b, h, 0, h - b);
		cairo_pattern_add_color_stop_rgba(l, 0.0, 0.25, 0.20, 0.12, 0.22);
		cairo_pattern_add_color_stop_rgba(l, 1.0, 0.25, 0.20, 0.12, 0.0);
		cairo_set_source(cr, l);
		cairo_move_to(cr, b + 2.0, h);
		cairo_line_to(cr, 0, h - b - 2.0);
		cairo_line_to(cr, 0, h);
		cairo_close_path(cr);
		cairo_fill(cr);
		cairo_pattern_destroy(l);
	}
}

static void turn_paint(struct pad *p, cairo_t *cr, int w, int h)
{
	(void)w;
	/* the page under it */
	cairo_set_source_surface(cr, p->turn.under_pic, 0, 0);
	cairo_paint(cr);
	double a = p->turn.a;
	if (p->turn.dir > 0) {
		/* and what can be done, on it, coming into the light as the sheet goes up */
		menu_paint(p, cr, fmin(1.0, fmax(0.0, (a - PEEK * 0.4) / (PEEK * 0.9))));
	}
	if (a >= G_PI * 0.5) {
		return;
	}
	double hx = hinge_x(), W = sheet_w(p);
	double c = cos(a), s = sin(a);
	double edge = hx + W * c;
	/* its shadow on what is under, from the edge on out: the higher it is, the softer and further */
	double reach = 10.0 + 70.0 * s;
	cairo_pattern_t *sh = cairo_pattern_create_linear(edge, 0, edge + reach, 0);
	double dark = 0.34 * pow(s, 0.6);
	cairo_pattern_add_color_stop_rgba(sh, 0.0, 0.10, 0.08, 0.05, dark);
	cairo_pattern_add_color_stop_rgba(sh, 1.0, 0.10, 0.08, 0.05, 0.0);
	cairo_set_source(cr, sh);
	cairo_rectangle(cr, edge, 0, reach, h);
	cairo_fill(cr);
	cairo_pattern_destroy(sh);
	/* the sheet, strip by strip, each nearer the eye the further from the wire: the paper it lay
	 * as, the desk's (holes and all) or the window's own */
	bool desks = !PAD_VIEW(p->view)->own_paper && paper_ready();
	for (int i = 0; i < STRIPS; i++) {
		double s0 = (double)i / STRIPS, s1 = (double)(i + 1) / STRIPS;
		double x0 = hx + W * s0 * c, x1 = hx + W * s1 * c;
		if (x1 <= 0.0) {
			continue;
		}
		double scale = 1.0 + 0.09 * s0 * s;
		double src0 = W * s0, src1 = W * s1;
		cairo_save(cr);
		cairo_rectangle(cr, x0, 0, x1 - x0 + 0.6, h);
		cairo_clip(cr);
		cairo_translate(cr, x0, h * 0.5);
		cairo_scale(cr, (x1 - x0) / (src1 - src0), scale);
		cairo_translate(cr, -src0, -h * 0.5);
		cairo_set_source_surface(cr, p->turn.pic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
		cairo_paint(cr);
		cairo_restore(cr);
		/* lifted away from the lamp it goes darker, more towards the wire */
		double fade = 0.26 * s * (1.0 - 0.5 * s0);
		cairo_set_source_rgba(cr, 0.10, 0.08, 0.05, fade);
		cairo_rectangle(cr, x0, 0, x1 - x0 + 0.6, h);
		cairo_fill(cr);
	}
	/* and the wire through its holes, over it, as the desk has it over the sheet under */
	if (desks && edge > 0.0) {
		cairo_save(cr);
		cairo_rectangle(cr, 0, 0, edge + 1.0, h);
		cairo_clip(cr);
		desk_nb_lay_wire(cr, &g_paper.art, 0, 0, h);
		cairo_restore(cr);
	}
}

static gboolean on_sheet_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
	struct pad *p = data;
	int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
	if (p->turn.on && p->turn.under_pic) {
		turn_paint(p, cr, W, H);
	} else {
		head_print(cr, p, W);
		head_number(cr, p, pad_doc(p));
		head_ink(cr, p, pad_doc(p), W);
		corner_paint(cr, W, H, p->at > 0);
	}
	return FALSE;
}

static bool on_corner(double x, double y, int w, int h)
{
	return x > w - CORNER && y > h - CORNER;
}

static bool on_back_corner(double x, double y, int h)
{
	return x >= NB_INSET_L && x < NB_INSET_L + CORNER && y > h - CORNER;
}

static gboolean on_sheet_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	struct pad *p = data;
	if (ev->type != GDK_BUTTON_PRESS) {
		return TRUE;
	}
	GdkDevice *dev = gdk_event_get_source_device((GdkEvent *)ev);
	if (dev && gdk_device_get_source(dev) == GDK_SOURCE_TOUCHSCREEN) {
		return FALSE;
	}
	int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
	if (!p->turn.on) {
		if (ev->button != GDK_BUTTON_PRIMARY) {
			return TRUE;
		}
		if (on_corner(ev->x, ev->y, W, H)) {
			turn_begin(p, 1, true, ev->x);
		} else if (on_back_corner(ev->x, ev->y, H) && p->at > 0) {
			turn_begin(p, -1, true, ev->x);
		}
		p->turn.by_corner = p->turn.on;
		return TRUE;
	}
	if (!p->turn.held && ev->button == GDK_BUTTON_PRIMARY) {
		sheet_up_hand(p, ev->x, ev->y);
	} else if (!p->turn.held) {
		turn_drop(p);
	}
	return TRUE;
}

static gboolean on_sheet_motion(GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
	(void)w;
	struct pad *p = data;
	if (p->turn.on && p->turn.held) {
		turn_hand_moves(p, ev->x, g_get_monotonic_time());
		return TRUE;
	}
	if (p->turn.on && p->turn.propped) {
		int i = menu_item_at(p, ev->x, ev->y);
		if (i != p->turn.hot) {
			p->turn.hot = i;
			gtk_widget_queue_draw(p->sheet);
		}
	}
	return TRUE;
}

static gboolean on_sheet_release(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	(void)w;
	struct pad *p = data;
	if (ev->button == GDK_BUTTON_PRIMARY && p->turn.on && p->turn.held) {
		turn_hand_lets_go(p, g_get_monotonic_time());
	}
	return TRUE;
}

static gboolean on_sheet_leave(GtkWidget *w, GdkEventCrossing *ev, gpointer data)
{
	(void)w;
	(void)ev;
	struct pad *p = data;
	if (p->turn.hot >= 0) {
		p->turn.hot = -1;
		gtk_widget_queue_draw(p->sheet);
	}
	return FALSE;
}

static void on_sheet_size(GtkWidget *w, GdkRectangle *r, gpointer data)
{
	(void)w;
	struct pad *p = data;
	struct desk_nb_heading hd;
	desk_nb_heading(r->width, &hd);
	if (hd.has_date != p->head_wide) {
		p->head_wide = hd.has_date;
		head_fill(p);
	}
	sheet_shape(p);
}

static void on_sheet_realize(GtkWidget *w, gpointer data)
{
	(void)w;
	sheet_shape(data);
}

/* ---- a pen, a mouse, on the sheet --------------------------------------------------------------------
 *
 * Taken before the words have them (the capture phase): a pen's point writes, its other end rubs out,
 * and so does anything else with a pen out of the case in the hand; the rest is the words'. */

static double ev_pressure(const GdkEvent *ev)
{
	GdkDevice *dev = ev ? gdk_event_get_source_device(ev) : NULL;
	double p = 0.0;
	if (dev && gdk_device_get_source(dev) != GDK_SOURCE_MOUSE && gdk_event_get_axis(ev, GDK_AXIS_PRESSURE, &p)) {
		return p < 0.02 ? 0.02 : p;
	}
	return 0.5;
}

static const GdkEvent *gesture_event(GtkGesture *g)
{
	return gtk_gesture_get_last_event(g, gtk_gesture_get_last_updated_sequence(g));
}

/* From the view's own coordinates to its buffer's. */
static void view_to_buffer(struct pad *p, double x, double y, double *bx, double *by)
{
	int ix = (int)floor(x), iy = (int)floor(y), ox, oy;
	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(p->view), GTK_TEXT_WINDOW_WIDGET, ix, iy, &ox, &oy);
	*bx = ox + (x - ix);
	*by = oy + (y - iy);
}

static void on_pen_begin(GtkGestureDrag *g, double x, double y, gpointer data)
{
	struct pad *p = data;
	GtkGesture *ge = GTK_GESTURE(g);
	const GdkEvent *ev = gesture_event(ge);
	GdkDevice *dev = ev ? gdk_event_get_source_device(ev) : NULL;
	GdkInputSource src = dev ? gdk_device_get_source(dev) : GDK_SOURCE_MOUSE;
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
	bool pen = src == GDK_SOURCE_PEN || src == GDK_SOURCE_ERASER;
	if (src == GDK_SOURCE_TOUCHSCREEN || p->turn.on) {
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_DENIED);
		return;
	}
	if (pen && button != GDK_BUTTON_PRIMARY) {
		/* a pen's side buttons rub out as it writes, and paste nothing */
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
		return;
	}
	if (button != GDK_BUTTON_PRIMARY || (!pen && pad_hand(p) == NB_HAND_NONE)) {
		/* a mouse with nothing in the hand: the words' */
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_DENIED);
		return;
	}
	gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
	if (gtk_widget_get_visible(p->note)) {
		note_hide(p);
	}
	gtk_widget_grab_focus(p->view);
	GdkModifierType state = 0;
	if (ev) {
		gdk_event_get_state(ev, &state);
	}
	double bx, by;
	view_to_buffer(p, x, y, &bx, &by);
	bool rub = src == GDK_SOURCE_ERASER || (pen && (state & (GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)))
		|| pad_ink(p) < 0;
	p->writing_with = dev;
	if (rub) {
		rub_begin(p, bx, by, false);
	} else {
		write_begin(p, bx, by, ev_pressure(ev), pad_ink(p));
	}
}

static void on_pen_update(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	struct pad *p = data;
	double x0, y0, bx, by;
	gtk_gesture_drag_get_start_point(g, &x0, &y0);
	view_to_buffer(p, x0 + dx, y0 + dy, &bx, &by);
	if (p->rubbing) {
		rub_at(p, bx, by, false);
	} else if (PAD_VIEW(p->view)->wet) {
		write_on(p, bx, by, ev_pressure(gesture_event(GTK_GESTURE(g))));
	}
}

static void on_pen_end(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	struct pad *p = data;
	if (p->rubbing) {
		rub_end(p);
	} else if (PAD_VIEW(p->view)->wet) {
		double x0, y0, bx, by;
		gtk_gesture_drag_get_start_point(g, &x0, &y0);
		view_to_buffer(p, x0 + dx, y0 + dy, &bx, &by);
		write_on(p, bx, by, ev_pressure(gesture_event(GTK_GESTURE(g))));
		write_end(p);
	}
	p->writing_with = NULL;
}

static void on_pen_cancel(GtkGesture *g, GdkEventSequence *seq, gpointer data)
{
	(void)g;
	(void)seq;
	struct pad *p = data;
	write_forget(p);
	if (p->rubbing) {
		rub_end(p);
	}
	p->writing_with = NULL;
}

/* From the window's own coordinates to the sheet's. (A hand where the window has nothing of its own
 * under it is the window's, not the sheet's: there is nothing for it to go through.) */
static void win_to_sheet(struct pad *p, double *x, double *y)
{
	int ox = 0, oy = 0;
	gtk_widget_translate_coordinates(p->overlay, p->win, 0, 0, &ox, &oy);
	*x -= ox;
	*y -= oy;
}

/* And in the heading, over the window: a pen, or a mouse with something out of the case, writes in it;
 * anything else is what is typed there's. */
static void on_head_pen_begin(GtkGestureDrag *g, double x, double y, gpointer data)
{
	struct pad *p = data;
	win_to_sheet(p, &x, &y);
	GtkGesture *ge = GTK_GESTURE(g);
	const GdkEvent *ev = gesture_event(ge);
	GdkDevice *dev = ev ? gdk_event_get_source_device(ev) : NULL;
	GdkInputSource src = dev ? gdk_device_get_source(dev) : GDK_SOURCE_MOUSE;
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
	bool pen = src == GDK_SOURCE_PEN || src == GDK_SOURCE_ERASER;
	int part = head_part_at(p, x, y);
	if (!part || src == GDK_SOURCE_TOUCHSCREEN || p->turn.on
		|| (!pen && (button != GDK_BUTTON_PRIMARY || pad_hand(p) == NB_HAND_NONE))) {
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_DENIED);
		return;
	}
	gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
	if (button != GDK_BUTTON_PRIMARY) {
		return;
	}
	GdkModifierType state = 0;
	if (ev) {
		gdk_event_get_state(ev, &state);
	}
	bool rub = src == GDK_SOURCE_ERASER || (pen && (state & (GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)))
		|| pad_ink(p) < 0;
	p->head_pen = true;
	head_hand_begin(p, part, x, y, ev_pressure(ev), rub);
}

static void on_head_pen_update(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	struct pad *p = data;
	if (!p->head_pen) {
		return;
	}
	double x0, y0;
	gtk_gesture_drag_get_start_point(g, &x0, &y0);
	win_to_sheet(p, &x0, &y0);
	head_hand_moves(p, x0 + dx, y0 + dy, ev_pressure(gesture_event(GTK_GESTURE(g))));
}

static void on_head_pen_end(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	struct pad *p = data;
	if (!p->head_pen) {
		return;
	}
	p->head_pen = false;
	double x0, y0;
	gtk_gesture_drag_get_start_point(g, &x0, &y0);
	win_to_sheet(p, &x0, &y0);
	head_hand_done(p, x0 + dx, y0 + dy, ev_pressure(gesture_event(GTK_GESTURE(g))), false);
}

static void on_head_pen_cancel(GtkGesture *g, GdkEventSequence *seq, gpointer data)
{
	(void)g;
	(void)seq;
	struct pad *p = data;
	if (p->head_pen) {
		p->head_pen = false;
		head_hand_done(p, p->head_x0, p->head_y0, 0.5, true);
	}
}

/* ---- fingers -------------------------------------------------------------------------------------
 *
 * Over the whole window, before the words have them: one going across turns the sheet, on the
 * finger; up or down it is the words' (scrolling, the cursor put down). With something out of the
 * case in the hand, one writes. Two tapped take back what was done last. */

static void finger_to_buffer(struct pad *p, double ox, double oy, double *bx, double *by)
{
	int vx, vy;
	gtk_widget_translate_coordinates(p->overlay, p->view, (int)floor(ox), (int)floor(oy), &vx, &vy);
	view_to_buffer(p, vx + (ox - floor(ox)), vy + (oy - floor(oy)), bx, by);
}

/* A hand on a sheet that is up: what is on the card under it, or the sheet itself, held again; or
 * elsewhere, and it falls back. True if it is held. */
static bool sheet_up_hand(struct pad *p, double x, double y)
{
	double edge = hinge_x() + sheet_w(p) * cos(p->turn.a);
	int i = p->turn.propped ? menu_item_at(p, x, y) : -1;
	if (i >= 0) {
		int what = p->items[i].what;
		turn_drop(p);
		do_this(p, what);
		return false;
	}
	if (x < edge) {
		p->turn.held = true;
		p->turn.retaken = p->turn.propped;
		p->turn.propped = false;
		p->turn.x0 = x;
		p->turn.x = x;
		p->turn.vx = 0.0;
		p->turn.tx = g_get_monotonic_time() / 1e6;
		p->turn.edge0 = edge;
		p->turn.hot = -1;
		turn_go(p);
		return true;
	}
	turn_drop(p);
	return false;
}

static void on_finger_begin(GtkGestureDrag *g, double x, double y, gpointer data)
{
	struct pad *p = data;
	GtkGesture *ge = GTK_GESTURE(g);
	win_to_sheet(p, &x, &y);
	p->finger_x0 = x;
	p->finger_y0 = y;
	p->finger_moved = false;
	p->finger_turns = false;
	p->finger_writes = false;
	p->finger_head = false;
	p->finger_corner = 0;
	if (p->turn.on) {
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
		if (!p->turn.held) {
			p->finger_turns = sheet_up_hand(p, x, y);
		}
		return;
	}
	int W = gtk_widget_get_allocated_width(p->sheet), H = gtk_widget_get_allocated_height(p->sheet);
	if (on_corner(x, y, W, H) || (on_back_corner(x, y, H) && p->at > 0)) {
		/* a corner: tapped, the foot's shows what can be done and the other turns back; taken
		 * across, it turns on the finger */
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
		p->finger_corner = on_corner(x, y, W, H) ? 1 : -1;
		return;
	}
	int part = pad_hand(p) != NB_HAND_NONE ? head_part_at(p, x, y) : 0;
	if (part) {
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
		p->finger_writes = true;
		p->finger_head = true;
		head_hand_begin(p, part, x, y, 0.5, pad_ink(p) < 0);
		return;
	}
	if (pad_hand(p) != NB_HAND_NONE && x >= NB_INSET_L && y >= NB_HEAD_H) {
		gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
		p->finger_writes = true;
		double bx, by;
		finger_to_buffer(p, x, y, &bx, &by);
		if (pad_ink(p) < 0) {
			rub_begin(p, bx, by, false);
		} else {
			write_begin(p, bx, by, 0.5, pad_ink(p));
		}
	}
}

static void on_finger_update(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	struct pad *p = data;
	GtkGesture *ge = GTK_GESTURE(g);
	double x = p->finger_x0 + dx, y = p->finger_y0 + dy;
	gint64 now = g_get_monotonic_time();
	if (p->finger_writes && p->finger_head) {
		head_hand_moves(p, x, y, 0.5);
		return;
	}
	if (p->finger_writes) {
		double bx, by;
		finger_to_buffer(p, x, y, &bx, &by);
		if (p->rubbing) {
			rub_at(p, bx, by, false);
		} else {
			write_on(p, bx, by, 0.5);
		}
		return;
	}
	if (p->finger_turns) {
		turn_hand_moves(p, x, now);
		return;
	}
	if (p->finger_moved || hypot(dx, dy) <= TAKES_HOLD) {
		return;
	}
	p->finger_moved = true;
	if (fabs(dx) > fabs(dy) * 1.2 && !p->turn.on) {
		turn_begin(p, dx < 0 ? 1 : -1, true, p->finger_x0);
		if (p->turn.on) {
			gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_CLAIMED);
			p->finger_turns = true;
			turn_hand_moves(p, x, now);
			return;
		}
	}
	/* up or down: the words', to be scrolled */
	gtk_gesture_set_state(ge, GTK_EVENT_SEQUENCE_DENIED);
}

static void finger_done(struct pad *p, bool taken_away, double x, double y)
{
	if (p->finger_writes && p->finger_head) {
		head_hand_done(p, x, y, 0.5, taken_away);
	} else if (p->finger_writes) {
		if (p->rubbing) {
			rub_end(p);
		} else if (taken_away) {
			write_forget(p);
		} else if (PAD_VIEW(p->view)->wet) {
			double bx, by;
			finger_to_buffer(p, x, y, &bx, &by);
			write_on(p, bx, by, 0.5);
			write_end(p);
		}
	}
	if (p->finger_turns) {
		turn_hand_lets_go(p, g_get_monotonic_time());
	} else if (p->finger_corner > 0 && !p->finger_moved && !taken_away && !p->turn.on) {
		menu_up(p);
	} else if (p->finger_corner < 0 && !p->finger_moved && !taken_away && !p->turn.on) {
		turn_begin(p, -1, false, x);
	}
	p->finger_writes = false;
	p->finger_head = false;
	p->finger_turns = false;
	p->finger_corner = 0;
}

static void on_finger_end(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
	(void)g;
	struct pad *p = data;
	finger_done(p, false, p->finger_x0 + dx, p->finger_y0 + dy);
}

static void on_finger_cancel(GtkGesture *g, GdkEventSequence *seq, gpointer data)
{
	(void)g;
	(void)seq;
	struct pad *p = data;
	finger_done(p, true, p->finger_x0, p->finger_y0);
}

/* A second finger: the first was not writing, it was a hand. */
static void on_two_begin(GtkGesture *g, GdkEventSequence *seq, gpointer data)
{
	(void)g;
	(void)seq;
	struct pad *p = data;
	p->two_at = g_get_monotonic_time();
	if (p->finger_writes) {
		write_forget(p);
		head_ink_forget(p);
		if (p->rubbing) {
			rub_end(p);
		}
		p->finger_writes = false;
		p->finger_head = false;
	}
}

static void on_two_end(GtkGesture *g, GdkEventSequence *seq, gpointer data)
{
	(void)seq;
	struct pad *p = data;
	double scale = gtk_gesture_zoom_get_scale_delta(GTK_GESTURE_ZOOM(g));
	/* tapped, not pinched */
	if (g_get_monotonic_time() - p->two_at < 350000 && fabs(scale - 1.0) < 0.08 && !p->finger_turns) {
		doc_undo(pad_doc(p), true);
	}
}

/* ---- going away ----------------------------------------------------------------------------------- */

static void pad_destroyed(GtkWidget *w, gpointer data)
{
	struct pad *p = data;
	(void)w;
	g_pads = g_list_remove(g_pads, p);
	if (g_book == p) {
		g_book = NULL;
	}
	if (p->keep_timer) {
		g_source_remove(p->keep_timer);
	}
	if (p->save_timer) {
		g_source_remove(p->save_timer);
	}
	if (p->turn.tick) {
		p->turn.tick = 0;
	}
	if (p->turn.pic) {
		cairo_surface_destroy(p->turn.pic);
	}
	if (p->turn.under_pic) {
		cairo_surface_destroy(p->turn.under_pic);
	}
	if (p->rub) {
		action_free(p->rub);
	}
	if (p->head_wet) {
		stroke_free(p->head_wet);
	}
	for (guint i = 0; i < p->docs->len; i++) {
		doc_free(g_ptr_array_index(p->docs, i));
	}
	g_ptr_array_free(p->docs, TRUE);
	if (p->dot) {
		g_object_unref(p->dot);
	}
	g_clear_object(&p->pen_g);
	g_clear_object(&p->head_g);
	g_clear_object(&p->swipe);
	g_clear_object(&p->pinch);
	g_free(p);
	if (!g_pads) {
		gtk_main_quit();
	}
}

/* Asked to go: the notebook's pages are kept, and for each of the rest not saved, whether to save it. */
static void pad_close_ask(struct pad *p)
{
	if (p->turn.on) {
		turn_end(p);
	}
	write_forget(p);
	pads_keep_all(p);
	for (guint i = 0; i < p->docs->len; i++) {
		struct doc *d = g_ptr_array_index(p->docs, i);
		if (!d || d->number || !gtk_text_buffer_get_modified(d->buf)) {
			continue;
		}
		if (!d->path && doc_blank(d)) {
			continue;
		}
		pad_show(p, (int)i);
		char name[300], msg[500];
		doc_name(d, name, sizeof name);
		snprintf(msg, sizeof msg, TR("Save the changes to %s?"), name);
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(p->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
			GTK_BUTTONS_NONE, "%s", msg);
		gtk_dialog_add_buttons(GTK_DIALOG(dlg), TR("Don't Save"), GTK_RESPONSE_REJECT, TR("Cancel"),
			GTK_RESPONSE_CANCEL, TR("Save"), GTK_RESPONSE_ACCEPT, NULL);
		gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
		int r = gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		if (r == GTK_RESPONSE_ACCEPT) {
			if (!doc_save(d)) {
				return;
			}
		} else if (r != GTK_RESPONSE_REJECT) {
			return;
		}
	}
	for (guint i = 0; i < p->docs->len; i++) {
		struct doc *d = g_ptr_array_index(p->docs, i);
		if (d) {
			doc_keep_drop(d);
		}
	}
	gtk_widget_destroy(p->win);
}

static gboolean on_delete_event(GtkWidget *w, GdkEvent *ev, gpointer data)
{
	(void)w;
	(void)ev;
	pad_close_ask(data);
	return TRUE;
}

/* ---- the keys and the right click ------------------------------------------------------------------ */

static void pad_size_by(int by)
{
	int to = by == 0 ? 13 : g_set.size + by;
	if (to >= SIZE_LEAST && to <= SIZE_MOST && to != g_set.size) {
		g_set.size = to;
		pads_restyle();
	}
}

/* The sheet's foot taken up as far as what can be done shows, as if by a hand. */
static void menu_up(struct pad *p)
{
	if (p->turn.on) {
		return;
	}
	turn_begin(p, 1, true, gtk_widget_get_allocated_width(p->sheet));
	if (!p->turn.on) {
		return;
	}
	p->turn.held = false;
	p->turn.propped = true;
	p->turn.prop = prop_angle(p);
	p->turn.w = 4.0;
	turn_go(p);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	struct pad *p = data;
	(void)w;
	guint mods = ev->state & gtk_accelerator_get_default_mod_mask();
	guint key = gdk_keyval_to_lower(ev->keyval);
	bool ctrl = mods == GDK_CONTROL_MASK, ctrl_shift = mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	struct doc *d = pad_doc(p);
	if (p->turn.on) {
		/* a sheet up: it falls back, and the key is for the page */
		if (!p->turn.held) {
			turn_drop(p);
		}
		if (ev->keyval == GDK_KEY_Escape || ev->keyval == GDK_KEY_Menu || ev->keyval == GDK_KEY_F10) {
			return TRUE;
		}
	}
	if (ev->keyval == GDK_KEY_Escape && gtk_widget_get_visible(p->note)) {
		note_hide(p);
		return TRUE;
	}
	if ((ev->keyval == GDK_KEY_Menu && mods == 0) || (ev->keyval == GDK_KEY_F10 && mods == 0)) {
		menu_up(p);
		return TRUE;
	}
	if (ev->keyval == GDK_KEY_F3 && (mods == 0 || mods == GDK_SHIFT_MASK)) {
		find_go(p, mods != 0);
		return TRUE;
	}
	if (mods == GDK_MOD1_MASK && key == GDK_KEY_z) {
		g_set.wrap = !g_set.wrap;
		pads_restyle();
		return TRUE;
	}
	if (ctrl_shift) {
		switch (key) {
		case GDK_KEY_s: doc_save_as(d); return TRUE;
		case GDK_KEY_z: doc_undo(d, false); return TRUE;
		case GDK_KEY_g: find_go(p, true); return TRUE;
		case GDK_KEY_plus: pad_size_by(1); return TRUE;
		case GDK_KEY_semicolon: pad_size_by(1); return TRUE;
		case GDK_KEY_equal: pad_size_by(1); return TRUE;
		default: break;
		}
	}
	if (!ctrl) {
		return FALSE;
	}
	switch (key) {
	case GDK_KEY_Page_Down: turn_begin(p, 1, false, 0.0); return TRUE;
	case GDK_KEY_Page_Up: turn_begin(p, -1, false, 0.0); return TRUE;
	case GDK_KEY_n:
		/* a clean page: turned over to, from the last; or gone to */
		pads_keep_all(p);
		if (p->at == (int)p->docs->len - 1 && !doc_blank(d)) {
			turn_begin(p, 1, false, 0.0);
		} else {
			pad_new_page(p);
		}
		return TRUE;
	case GDK_KEY_o: pad_open_ask(p); return TRUE;
	case GDK_KEY_s:
		if (d->number) {
			pads_keep_all(p);
		} else {
			doc_save(d);
		}
		return TRUE;
	case GDK_KEY_e: pdf_ask(p); return TRUE;
	case GDK_KEY_w: pad_close_ask(p); return TRUE;
	case GDK_KEY_q: {
		GList *all = g_list_copy(g_pads);
		for (GList *l = all; l; l = l->next) {
			if (g_list_find(g_pads, l->data)) {
				pad_close_ask(l->data);
			}
		}
		g_list_free(all);
		return TRUE;
	}
	case GDK_KEY_f: note_show(p, false); return TRUE;
	case GDK_KEY_h: note_show(p, true); return TRUE;
	case GDK_KEY_g: find_go(p, false); return TRUE;
	case GDK_KEY_z: doc_undo(d, true); return TRUE;
	case GDK_KEY_y: doc_undo(d, false); return TRUE;
	case GDK_KEY_t:
		g_set.typed = !g_set.typed;
		pads_restyle();
		return TRUE;
	case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add: pad_size_by(1); return TRUE;
	case GDK_KEY_minus: case GDK_KEY_KP_Subtract: pad_size_by(-1); return TRUE;
	case GDK_KEY_0: case GDK_KEY_KP_0: pad_size_by(0); return TRUE;
	default:
		return FALSE;
	}
}

struct popup_act {
	struct pad *p;
	int what;
};

static void popup_run(GtkMenuItem *item, gpointer data)
{
	(void)item;
	struct popup_act *a = data;
	do_this(a->p, a->what);
}

static void popup_act_free(gpointer data, GClosure *closure)
{
	(void)closure;
	g_free(data);
}

static void popup_add(GtkMenuShell *menu, struct pad *p, const char *label, const char *keys, int what, int at)
{
	GtkWidget *item = gtk_menu_item_new();
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
	GtkWidget *l = gtk_label_new(label);
	GtkWidget *k = gtk_label_new(keys);
	gtk_widget_set_opacity(k, 0.6);
	gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(row), k, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(item), row);
	struct popup_act *a = g_new(struct popup_act, 1);
	a->p = p;
	a->what = what;
	g_signal_connect_data(item, "activate", G_CALLBACK(popup_run), a, popup_act_free, 0);
	gtk_widget_show_all(item);
	gtk_menu_shell_insert(menu, item, at);
}

/* The right click: GTK's own (cut, copy, paste), and under those what is on the card under the
 * sheet, for a hand that does not turn it up. */
static void on_populate(GtkTextView *tv, GtkWidget *popup, gpointer data)
{
	struct pad *p = data;
	(void)tv;
	if (!GTK_IS_MENU(popup)) {
		return;
	}
	struct doc *d = pad_doc(p);
	GtkMenuShell *m = GTK_MENU_SHELL(popup);
	int at = 0;
	if (!d->number) {
		popup_add(m, p, TR("Save"), "Ctrl+S", DO_SAVE, at++);
	}
	popup_add(m, p, TR("Save As"), "Ctrl+Shift+S", DO_SAVE_AS, at++);
	popup_add(m, p, TR("Open"), "Ctrl+O", DO_OPEN, at++);
	GtkWidget *sep = gtk_separator_menu_item_new();
	gtk_widget_show(sep);
	gtk_menu_shell_insert(m, sep, at++);
	popup_add(m, p, TR("Find"), "Ctrl+F", DO_FIND, at++);
	popup_add(m, p, TR("Replace"), "Ctrl+H", DO_REPLACE, at++);
	popup_add(m, p, TR("Wrap long lines"), "Alt+Z", DO_WRAP, at++);
	popup_add(m, p, TR("Typewriter letters"), "Ctrl+T", DO_TYPED, at++);
	sep = gtk_separator_menu_item_new();
	gtk_widget_show(sep);
	gtk_menu_shell_insert(m, sep, at++);
}

/* Files let go on the sheet: read, as Open would. */
static void on_drag_received(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, GtkSelectionData *sel, guint info,
	guint time_, gpointer data)
{
	struct pad *p = data;
	(void)w;
	(void)x;
	(void)y;
	(void)info;
	char **uris = gtk_selection_data_get_uris(sel);
	if (!uris) {
		return;
	}
	for (int i = 0; uris[i]; i++) {
		char *path = g_filename_from_uri(uris[i], NULL, NULL);
		if (path) {
			struct doc *d = pad_doc(p);
			if (i == 0 && !p->notebook && !d->path && doc_blank(d)) {
				doc_read(p, d, path);
				pad_show(p, p->at);
			} else {
				pad_new(false, path);
			}
			g_free(path);
		}
	}
	g_strfreev(uris);
	gtk_drag_finish(ctx, TRUE, FALSE, time_);
	g_signal_stop_emission_by_name(w, "drag-data-received");
}

static void pad_open_ask(struct pad *p)
{
	GtkFileChooserNative *c = gtk_file_chooser_native_new(TR("Open"), GTK_WINDOW(p->win),
		GTK_FILE_CHOOSER_ACTION_OPEN, TR("Open"), TR("Cancel"));
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c));
		if (path) {
			/* a blank sheet of its own is used; otherwise another notebook */
			struct doc *d = pad_doc(p);
			if (!p->notebook && !d->path && doc_blank(d)) {
				doc_read(p, d, path);
				pad_show(p, p->at);
			} else {
				pad_new(false, path);
			}
			g_free(path);
		}
	}
	g_object_unref(c);
}

/* Opened at the size the notebook is, if the screen has room for it. */
static void pad_opening_size(struct pad *p)
{
	int w = NB_WIN_W, h = NB_WIN_H;
	GdkDisplay *d = gdk_display_get_default();
	GdkMonitor *m = gdk_display_get_primary_monitor(d);
	if (!m && gdk_display_get_n_monitors(d) > 0) {
		m = gdk_display_get_monitor(d, 0);
	}
	if (m) {
		GdkRectangle wa;
		gdk_monitor_get_workarea(m, &wa);
		/* (the pencil case below it, and something of the desk: on the desk the window is the
		 * sheet, heading and all) */
		int most = wa.height - 200 + (g_on_desk ? NB_HEAD_H : 0);
		if (most > 240 && h > most) {
			h = most;
		}
	}
	gtk_window_set_default_size(GTK_WINDOW(p->win), w, h);
}

/* With the whole screen to itself there is no notebook under it, nor a pencil case: a page of its
 * own, until it goes back. */
static gboolean on_window_state(GtkWidget *w, GdkEventWindowState *ev, gpointer data)
{
	struct pad *p = data;
	p->opened_out = (ev->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
	if (g_on_desk && (ev->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)) {
		bool whole = (ev->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
		GtkStyleContext *sc = gtk_widget_get_style_context(w);
		if (whole) {
			gtk_style_context_add_class(sc, "whole");
		} else {
			gtk_style_context_remove_class(sc, "whole");
		}
		p->whole = whole;
		PAD_VIEW(p->view)->own_paper = whole;
		pad_pointer(p);
		sheet_shape(p);
		gtk_widget_queue_draw(p->view);
		gtk_widget_queue_draw(p->sheet);
	}
	return FALSE;
}

static void on_win_size(GtkWidget *w, GdkRectangle *r, gpointer data)
{
	(void)w;
	(void)r;
	pad_input_shape(data);
}

static void on_view_realize(GtkWidget *w, gpointer data)
{
	struct pad *p = data;
	/* every point of a line the pen went along, not one to a frame */
	GdkWindow *text = gtk_text_view_get_window(GTK_TEXT_VIEW(w), GTK_TEXT_WINDOW_TEXT);
	if (text) {
		gdk_window_set_event_compression(text, FALSE);
	}
	GdkWindow *top = gtk_widget_get_window(p->win);
	if (top) {
		gdk_window_set_event_compression(top, FALSE);
	}
	pad_pointer(p);
}

static struct pad *pad_new(bool notebook, const char *path)
{
	struct pad *p = g_new0(struct pad, 1);
	p->notebook = notebook;
	p->docs = g_ptr_array_new();
	p->turn.hot = -1;
	g_pads = g_list_prepend(g_pads, p);

	p->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_style_context_add_class(gtk_widget_get_style_context(p->win), "pad-win");
	pad_opening_size(p);
	gtk_window_set_icon_name(GTK_WINDOW(p->win), "desk-notepad");
	g_signal_connect(p->win, "window-state-event", G_CALLBACK(on_window_state), p);
	g_signal_connect(p->win, "delete-event", G_CALLBACK(on_delete_event), p);
	g_signal_connect(p->win, "destroy", G_CALLBACK(pad_destroyed), p);
	g_signal_connect(p->win, "key-press-event", G_CALLBACK(on_key), p);

	p->overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(p->win), p->overlay);
	p->scroll = gtk_scrolled_window_new(NULL, NULL);
	/* (the page, clear of the holes and below the heading) */
	gtk_widget_set_margin_start(p->scroll, NB_INSET_L);
	gtk_widget_set_margin_top(p->scroll, NB_HEAD_H);
	gtk_container_add(GTK_CONTAINER(p->overlay), p->scroll);

	p->view = g_object_new(PAD_TYPE_VIEW, NULL);
	PAD_VIEW(p->view)->own_paper = !g_on_desk;
	gtk_style_context_add_class(gtk_widget_get_style_context(p->view), "pad");
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(p->view), LEFT_MARGIN);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(p->view), RIGHT_MARGIN);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(p->view), 40);
	gtk_container_add(GTK_CONTAINER(p->scroll), p->view);
	gtk_widget_add_events(p->view, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK
		| GDK_TOUCH_MASK | GDK_PROXIMITY_IN_MASK | GDK_PROXIMITY_OUT_MASK);
	g_signal_connect(p->view, "populate-popup", G_CALLBACK(on_populate), p);
	g_signal_connect(p->view, "realize", G_CALLBACK(on_view_realize), p);
	/* a pen and a mouse, before the words have them */
	p->pen_g = gtk_gesture_drag_new(p->view);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(p->pen_g), 0);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(p->pen_g), GTK_PHASE_CAPTURE);
	g_signal_connect(p->pen_g, "drag-begin", G_CALLBACK(on_pen_begin), p);
	g_signal_connect(p->pen_g, "drag-update", G_CALLBACK(on_pen_update), p);
	g_signal_connect(p->pen_g, "drag-end", G_CALLBACK(on_pen_end), p);
	g_signal_connect(p->pen_g, "cancel", G_CALLBACK(on_pen_cancel), p);
	/* and in the heading, before what is typed there has them (on the window: there is nothing of
	 * the sheet's own between what is typed there) */
	p->head_g = gtk_gesture_drag_new(p->win);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(p->head_g), 0);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(p->head_g), GTK_PHASE_CAPTURE);
	g_signal_connect(p->head_g, "drag-begin", G_CALLBACK(on_head_pen_begin), p);
	g_signal_connect(p->head_g, "drag-update", G_CALLBACK(on_head_pen_update), p);
	g_signal_connect(p->head_g, "drag-end", G_CALLBACK(on_head_pen_end), p);
	g_signal_connect(p->head_g, "cancel", G_CALLBACK(on_head_pen_cancel), p);
	/* and fingers, anywhere on it, before the scrolling has them */
	p->swipe = gtk_gesture_drag_new(p->win);
	gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(p->swipe), TRUE);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(p->swipe), GTK_PHASE_CAPTURE);
	g_signal_connect(p->swipe, "drag-begin", G_CALLBACK(on_finger_begin), p);
	g_signal_connect(p->swipe, "drag-update", G_CALLBACK(on_finger_update), p);
	g_signal_connect(p->swipe, "drag-end", G_CALLBACK(on_finger_end), p);
	g_signal_connect(p->swipe, "cancel", G_CALLBACK(on_finger_cancel), p);
	p->pinch = gtk_gesture_zoom_new(p->win);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(p->pinch), GTK_PHASE_CAPTURE);
	gtk_gesture_group(p->pinch, p->swipe);
	g_signal_connect(p->pinch, "begin", G_CALLBACK(on_two_begin), p);
	g_signal_connect(p->pinch, "end", G_CALLBACK(on_two_end), p);
	gtk_drag_dest_add_uri_targets(p->view);
	g_signal_connect(p->view, "drag-data-received", G_CALLBACK(on_drag_received), p);

	/* the pages: the notebook's, from where it was open, or a file's (or a blank one) */
	if (notebook) {
		int n = notes_count();
		for (int i = 0; i < n; i++) {
			g_ptr_array_add(p->docs, NULL);
		}
		p->at = g_set.page >= 0 && g_set.page < n ? g_set.page : n - 1;
	} else {
		g_ptr_array_add(p->docs, doc_new(p, 0));
		p->at = 0;
	}
	struct doc *d = pad_doc(p);
	if (!d) {
		/* (a notebook with no pages at all: one, clean) */
		d = doc_new(p, notebook ? 1 : 0);
		g_ptr_array_set_size(p->docs, 0);
		g_ptr_array_add(p->docs, d);
		p->at = 0;
	}
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(p->view), d->buf);

	/* what is written in the heading, and over it all the corners to turn it by, and the sheet in
	 * the air */
	p->head_wide = true;
	head_build(p);
	p->sheet = gtk_drawing_area_new();
	gtk_widget_add_events(p->sheet, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK
		| GDK_LEAVE_NOTIFY_MASK);
	gtk_overlay_add_overlay(GTK_OVERLAY(p->overlay), p->sheet);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(p->overlay), p->sheet, TRUE);
	g_signal_connect(p->sheet, "draw", G_CALLBACK(on_sheet_draw), p);
	g_signal_connect(p->sheet, "button-press-event", G_CALLBACK(on_sheet_press), p);
	g_signal_connect(p->sheet, "motion-notify-event", G_CALLBACK(on_sheet_motion), p);
	g_signal_connect(p->sheet, "button-release-event", G_CALLBACK(on_sheet_release), p);
	g_signal_connect(p->sheet, "leave-notify-event", G_CALLBACK(on_sheet_leave), p);
	g_signal_connect(p->sheet, "size-allocate", G_CALLBACK(on_sheet_size), p);
	g_signal_connect_after(p->sheet, "realize", G_CALLBACK(on_sheet_realize), p);

	p->note = note_build(p);
	gtk_overlay_add_overlay(GTK_OVERLAY(p->overlay), p->note);

	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(p->view), g_set.wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
	/* (not NEVER, with which the sheet could get no narrower than its widest line
	 * is just now, however that line would wrap: the width depended on the words) */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->scroll),
		g_set.wrap ? GTK_POLICY_EXTERNAL : GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_widget_show_all(p->overlay);
	/* The note lies over the letters and asks for no room of its own: never a
	 * sheet too small for it, with the most it has to say on it. */
	int note_w, note_h;
	gtk_label_set_text(GTK_LABEL(p->count), TR("not found"));
	gtk_widget_get_preferred_width(p->note, &note_w, NULL);
	gtk_widget_get_preferred_height(p->note, &note_h, NULL);
	gtk_label_set_text(GTK_LABEL(p->count), "");
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(p->scroll), note_w);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(p->scroll), note_h);
	gtk_widget_hide(p->note);
	gtk_widget_show(p->win);
	g_signal_connect_after(p->win, "size-allocate", G_CALLBACK(on_win_size), p);
	pad_measure(p);
	pad_title(p);
	head_fill(p);
	gtk_widget_grab_focus(p->view);
	p->keep_timer = g_timeout_add_seconds(KEEP_EVERY_S, pad_keep_tick, p);

	if (path) {
		doc_read(p, d, path);
		pad_show(p, 0);
	}
	return p;
}

/* What went down with nothing to save it to: on a notebook of its own again, unless
 * the program that has it is still running. */
static void pads_bring_back_untitled(void)
{
	char *dir = keep_dir();
	GDir *d = g_dir_open(dir, 0, NULL);
	if (d) {
		const char *name;
		while ((name = g_dir_read_name(d))) {
			int pid = 0;
			if (!g_str_has_suffix(name, ".keep") || sscanf(name, "untitled-%d-", &pid) != 1
				|| pid == (int)getpid() || (pid > 0 && kill(pid, 0) == 0)) {
				continue;
			}
			char *path = g_build_filename(dir, name, NULL);
			char *all = NULL;
			if (g_file_get_contents(path, &all, NULL, NULL)) {
				const char *nl = strchr(all, '\n');
				struct doc *doc = nl && nl[1] ? pad_doc(pad_new(false, NULL)) : NULL;
				if (doc) {
					gtk_text_buffer_set_text(doc->buf, nl + 1, -1);
					edits_clear(&doc->undo);
					edits_clear(&doc->redo);
					doc->saved_serial = (unsigned)-1;
					gtk_text_buffer_set_modified(doc->buf, TRUE);
					/* it is this program's copy now */
					doc->keep_dirty = true;
					doc_keep_now(doc);
				}
				g_unlink(path);
				g_free(all);
			}
			g_free(path);
		}
		g_dir_close(d);
	}
	g_free(dir);
}

/* ---- one notebook ---------------------------------------------------------------------------------- */

/* A second program started for the notebook shows the first, if it can, and goes. The first may have
 * put its notebook away and be open on files still: it is taken out again. */
static gboolean book_present(gpointer data)
{
	(void)data;
	if (!g_book) {
		g_book = pad_new(true, NULL);
		pad_show(g_book, g_book->at);
	}
	gtk_window_present(GTK_WINDOW(g_book->win));
	return G_SOURCE_REMOVE;
}

static void show_first(int sig)
{
	(void)sig;
	/* (gtk is not for a signal handler) */
	g_idle_add(book_present, NULL);
}

/* Is the notebook ours to open? If another has it, that one is asked to show it. */
static bool book_ours(void)
{
	char *dir = notebook_dir();
	g_mkdir_with_parents(dir, 0700);
	char *path = g_build_filename(dir, ".lock", NULL);
	g_free(dir);
	int fd = g_open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	g_free(path);
	if (fd < 0) {
		return true;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		char num[32] = "";
		ssize_t n = read(fd, num, sizeof num - 1);
		int pid = n > 0 ? atoi(num) : 0;
		if (pid > 1) {
			kill(pid, SIGUSR1);
		}
		close(fd);
		return false;
	}
	char num[32];
	int len = snprintf(num, sizeof num, "%d\n", (int)getpid());
	if (ftruncate(fd, 0) != 0 || write(fd, num, (size_t)len) != len) {
		/* (it only says whom to ask) */
	}
	g_book_lock = fd;
	return true;
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	g_set_prgname("desk-notepad");
	if (argc > 1 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v"))) {
		puts(DESK_RELEASE);
		return 0;
	}
	gtk_init(&argc, &argv);
	g_on_desk = on_the_desk();
	language_load();
	conf_load();
	style_apply();
	case_watch();
	bool any = false;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1]) {
			continue;
		}
		/* (file:// as some programs hand them over) */
		char *path = g_str_has_prefix(argv[i], "file://") ? g_filename_from_uri(argv[i], NULL, NULL)
			: g_strdup(argv[i]);
		if (path) {
			/* a name not there yet is a new file of that name */
			if (g_file_test(path, G_FILE_TEST_EXISTS)) {
				pad_new(false, path);
			} else {
				struct pad *p = pad_new(false, NULL);
				struct doc *d = pad_doc(p);
				d->path = g_canonicalize_filename(path, NULL);
				pad_title(p);
			}
			any = true;
			g_free(path);
		}
	}
	if (!any) {
		pads_bring_back_untitled();
		if (book_ours()) {
			signal(SIGUSR1, show_first);
			g_book = pad_new(true, NULL);
			pad_show(g_book, g_book->at);
		} else if (!g_pads) {
			return 0;
		}
	}
	gtk_main();
	return 0;
}
