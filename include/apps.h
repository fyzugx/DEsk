#ifndef DESK_APPS_H
#define DESK_APPS_H

/* What is in the bookcase.
 *
 * The eight binders are filled from the installed .desktop files, so the
 * shelf is the machine's actual software rather than a list somebody typed
 * in. The scan runs on a worker thread and publishes an immutable snapshot;
 * nothing here blocks a frame. */

#include <cairo.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

enum desk_shelf {
	DESK_SHELF_DEVELOPMENT = 0,
	DESK_SHELF_INTERNET,
	DESK_SHELF_OFFICE,
	DESK_SHELF_GRAPHICS,
	DESK_SHELF_MEDIA,
	DESK_SHELF_GAMES,
	DESK_SHELF_UTILITIES,
	DESK_SHELF_SYSTEM,
	DESK_SHELF_COUNT,
};

/* Past the last binder on the shelf stands one more thing to take down: the
 * book of every program, to look one up in. On the shelf it is the thing
 * after the binders. */
#define DESK_SHELF_BOOK DESK_SHELF_COUNT
#define DESK_SHELF_THINGS (DESK_SHELF_COUNT + 1)

enum {
	DESK_APP_ICON_PX = 128,
	DESK_APP_ARGV_MAX = 64,
};

struct desk_app {
	char id[96];          /* the desktop file id, e.g. org.kde.kate.desktop */
	char name[128];       /* Name, localised */
	char generic[128];    /* GenericName, localised; may be empty */
	char name_c[128];     /* Name as written, before any translation */
	char keywords[256];   /* Keywords, localised: what else it may be looked up by */
	char exec[512];       /* Exec, verbatim, field codes and all */
	char workdir[256];    /* Path=, may be empty */
	char icon_name[128];  /* Icon=, verbatim */
	char icon_file[512];  /* a PNG on disk, or empty if none was found */
	char initial[8];      /* the letter to print when there is no icon */
	char wmclass[96];     /* StartupWMClass, which a window's class matches */
	enum desk_shelf shelf;
	unsigned seed;        /* stable per app: which way its printing goes wrong */
	bool terminal;        /* Terminal=true: needs a terminal wrapped round it */
	bool user_filed;      /* the shelf came from shelves.conf, not the rules */
	/* Which of the drawer's five things it is, by what it does: a terminal
	 * is the little terminal, a browser the globe, a file manager the
	 * folder, a text editor the notepad, settings the gear (see desk.h's
	 * desk_icon). -1: none, it goes in the drawer as a card. */
	int model;
};

struct desk_shelf_info {
	const char *key;        /* Development, Internet, ... */
	const char *label_en;
	const char *label_ja;
	double r, g, b;         /* the dye on the binder's boards */
	int first, count;       /* a run in the apps array */
};

/* One scan's worth. Immutable once published. */
struct desk_app_list {
	const struct desk_app *apps;
	int n;
	struct desk_shelf_info shelf[DESK_SHELF_COUNT];
	unsigned generation;
	time_t scanned_at;
	int n_files;    /* .desktop files looked at */
	/* not on a shelf: NoDisplay, another desktop's, Hidden, reserved,
	 * unrunnable */
	int n_hidden;
};

/* Start the first scan. async false blocks until it is done. */
void desk_apps_init(bool async);
/* Block until whatever scan is running has published. */
void desk_apps_finish(void);
/* The language names are read in: "en" (as written), "ja", or "" (the
 * locale's). Set before the first scan; after it, rescan. */
void desk_apps_set_language(const char *code);
void desk_apps_rescan(void);
/* Called now and then (it keeps its own time, a look every few seconds):
 * if a folder of desktop files has changed since the last scan -- a program
 * installed or taken away -- read them again on a thread of their own and
 * say in why which folder; true then. The new list lands as a new
 * generation. */
bool desk_apps_look(char *why, size_t n);
void desk_apps_shutdown(void);

/* Borrow the current snapshot. Never NULL once init has been called; before
 * the first scan lands it is an empty list. Release when done. */
const struct desk_app_list *desk_apps_acquire(void);
void desk_apps_release(const struct desk_app_list *list);

/* The program whose desktop file id is id, or NULL. It also finds, by its
 * id alone, what the scan keeps off the shelves: NoDisplay entries, and
 * those only another desktop shows (OnlyShowIn), such as Caja's entries,
 * xfce4-mime-settings and mate-volume-control. These are never on a shelf,
 * never in the book, and never matched by desk_apps_match's name and
 * program passes. */
const struct desk_app *desk_apps_find(const struct desk_app_list *list, const char *id);

/* DEsk's standard programs, one for each of the drawer's things (and
 * the terminal for Super+Enter): the same numbers as a program's model.
 * For each, what shell.conf sets (app.<role> = <desktop id>), else the
 * default program for its kind of file, as GIO and xdg-open find it by the
 * XDG MIME applications spec -- the user's mimeapps.list before the
 * desk-mimeapps.list DEsk installs; the terminal has no kind of file --
 * else the first installed of DEsk's standard set, else any program of
 * the kind. NULL if there is none at all. */
enum desk_std {
	DESK_STD_TERMINAL, DESK_STD_BROWSER, DESK_STD_FILES, DESK_STD_EDITOR,
	DESK_STD_COUNT
};
const char *desk_std_name(int role);   /* "terminal", "browser", "files", "editor" */
int desk_std_parse(const char *s);     /* or -1 */
void desk_apps_set_std(int role, const char *id);
const struct desk_app *desk_apps_for_std(const struct desk_app_list *list, int role);
/* The application a window belongs to, from what the window says of itself:
 * its Wayland app_id, or its X class and instance -- any may be NULL. By
 * desktop file name, then StartupWMClass, the program it runs, and last its
 * name. NULL if nothing fits. */
const struct desk_app *desk_apps_match(const struct desk_app_list *list,
	const char *app_id, const char *wm_class, const char *wm_instance);
/* A script's name without its file's ending (mintinstall.py is
 * mintinstall) into out; false if name has no script's ending. */
bool desk_app_script_stem(const char *name, char *out, size_t n);
/* Is this the name of a program that runs scripts (python3, perl, node)? */
bool desk_app_is_interpreter(const char *name);
/* An SVG drawn into a PNG px square, with whatever is installed for it
 * (rsvg-convert, else ImageMagick); false if nothing could. */
bool desk_svg_raster(const char *svg, const char *png, int px);
/* The first character of a name, whole -- a byte of a UTF-8 character is
 * not a character -- upper case if it has one. */
void desk_initial_of(const char *name, char *out, size_t n);

/* Turn Exec into an argv. Returns the count, or -1. The strings point into
 * pool, which must be at least as long as app->exec. */
int desk_app_build_argv(const struct desk_app *app, char **argv, int argv_max,
	char *pool, size_t pool_size);
/* The same for any Exec line (an autostart entry's); and starting an argv
 * the way an app is started. */
int desk_exec_argv(const char *exec, char **argv, int argv_max, char *pool, size_t pool_size);
pid_t desk_exec_launch(char **argv, const char *workdir);
/* Start it, detached: fork twice, setsid, exec. Returns the session it was
 * started in (its processes' getsid()) once it is on its way, or -1. */
pid_t desk_app_launch(const struct desk_app *app);
/* In a child about to exec: every file descriptor from lowest up closed,
 * so that nothing of the parent's goes with the program it becomes. */
void desk_close_from(int lowest);

/* The icon, decoded on demand and kept. NULL when the app has no icon file,
 * which is the normal case for about half of them. */
cairo_surface_t *desk_app_icon(const struct desk_app *app);
/* A PNG for an icon named the way a tray item names one -- any category,
 * the item's own directory first -- or false. May start a process to turn
 * an SVG into a PNG, the first time: not for the compositor's thread. */
bool desk_icon_path(const char *name, const char *extra_dir, char *out, size_t n);

#endif
