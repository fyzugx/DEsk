/* ================================================================
 * What a program kept in the drawer can look like (looks.h).
 *
 * The desk's own: its card; its icon, lying there as a thing; the five things
 * the drawer has always had (draw.c's tools); the calculator, the typewriter,
 * the notebook and Print's camera as they lie there. Then the looks: folders,
 * each a picture of a thing seen from straight above in the drawer's light
 * with nothing round it -- the drawer gives it its shadows -- and a few words
 * on what it is called and how big it is. The desk's own looks first (in
 * share/desk/looks, rendered by art/looks/looks.py), then any put beside
 * them for everyone ($XDG_DATA_DIRS/desk/looks), then the person's own
 * ($XDG_DATA_HOME/desk/looks): folders, or only PNGs, as big as a card.
 * ================================================================ */

#define _DEFAULT_SOURCE
#include "looks.h"
#include "desk.h"
#include "i18n.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* a look read no bigger than this, however big it is */
#define LOOK_MOST_PX 1024
#define LOOKS_MOST 64

static const struct {
	const char *id, *name;
	enum desk_look_kind kind;
	int tool;
	const char *thing;
} DESKS[] = {
	{ "card", "drawer|Card", DESK_LOOKS_CARD, 0, NULL },
	{ "icon", "drawer|Icon", DESK_LOOKS_ICON, 0, NULL },
	{ "terminal", "Pocket computer", DESK_LOOKS_TOOL, DESK_ICON_TERM, NULL },
	{ "compass", "Compass", DESK_LOOKS_TOOL, DESK_ICON_GLOBE, NULL },
	{ "folder", "Folder", DESK_LOOKS_TOOL, DESK_ICON_FOLDER, NULL },
	{ "notepad", "Notepad", DESK_LOOKS_TOOL, DESK_ICON_NOTES, NULL },
	{ "screwdriver", "Screwdriver", DESK_LOOKS_TOOL, DESK_ICON_GEAR, NULL },
	{ "calculator", "Calculator", DESK_LOOKS_THING, 0, "calculator" },
	{ "typewriter", "Typewriter", DESK_LOOKS_THING, 0, "keyboard" },
	{ "notebook", "Notebook", DESK_LOOKS_NOTEBOOK, 0, NULL },
	{ "camera", "Camera", DESK_LOOKS_CAMERA, 0, NULL },
};
#define DESKS_N (sizeof DESKS / sizeof DESKS[0])

static struct desk_look g_looks[LOOKS_MOST];
static int g_orders[LOOKS_MOST];
static int g_n;
/* one looked up by its id, that is not in the list as it was last made */
static struct desk_look g_one;

/* Pictures read, kept by where they were read from and when it was last changed. */
static struct {
	char path[PATH_MAX];
	long long mtime;
	cairo_surface_t *pic;
} g_pics[LOOKS_MOST];

static bool is_png(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && name[0] != '.' && !strcasecmp(name + n - 4, ".png");
}

/* One of the desk's own, i in DESKS. */
static void desks_look(size_t i, struct desk_look *l)
{
	memset(l, 0, sizeof *l);
	snprintf(l->id, sizeof l->id, "%s", DESKS[i].id);
	snprintf(l->name, sizeof l->name, "%s", TR(DESKS[i].name));
	l->kind = DESKS[i].kind;
	l->tool = DESKS[i].tool;
	l->thing = DESKS[i].thing;
}

/* A look in dir, a folder or a PNG of the person's own (prefix "own/"), called id after it. */
static bool look_at(const char *dir, const char *entry, const char *prefix, struct desk_look *l, int *order)
{
	memset(l, 0, sizeof *l);
	char where[PATH_MAX];
	if (!entry[0] || entry[0] == '.' || strchr(entry, '/')
		|| (size_t)snprintf(where, sizeof where, "%s/%s", dir, entry) >= sizeof where
		|| (size_t)snprintf(l->id, sizeof l->id, "%s%s", prefix, entry) >= sizeof l->id) {
		return false;
	}
	l->kind = DESK_LOOKS_PICTURE;
	l->own = prefix[0] != '\0';
	*order = 1000;
	struct stat st;
	if (stat(where, &st) != 0) {
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
		return desk_look_folder(where, desk_lang_code(desk_lang_get()), l->path, sizeof l->path, l->name,
			sizeof l->name, &l->along, &l->thick, order);
	}
	if (!l->own || !S_ISREG(st.st_mode) || !is_png(entry)) {
		return false;
	}
	/* only a picture: called what the file is called, without its .png, as big as a card */
	snprintf(l->path, sizeof l->path, "%s", where);
	snprintf(l->name, sizeof l->name, "%.*s", (int)(strlen(entry) - 4), entry);
	l->along = DESK_LOOK_CM / DESK_LOOK_CM_PER_UNIT;
	l->thick = DESK_LOOK_THICK_CM / DESK_LOOK_CM_PER_UNIT;
	return true;
}

static int by_name(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Every look in dir, by name; prefix for the person's own. Not those there already by id. */
static void looks_in(const char *dir, const char *prefix)
{
	DIR *d = dir[0] ? opendir(dir) : NULL;
	if (!d) {
		return;
	}
	char *names[LOOKS_MOST * 2];
	int n = 0;
	struct dirent *de;
	while ((de = readdir(d)) && n < (int)(sizeof names / sizeof names[0])) {
		if (de->d_name[0] != '.' && strlen(de->d_name) < 80) {
			names[n++] = strdup(de->d_name);
		}
	}
	closedir(d);
	qsort(names, (size_t)n, sizeof names[0], by_name);
	int first = g_n;
	for (int i = 0; i < n; i++) {
		struct desk_look l;
		int order;
		bool there = false;
		if (names[i] && g_n < LOOKS_MOST && look_at(dir, names[i], prefix, &l, &order)) {
			for (int k = 0; k < g_n && !there; k++) {
				there = !strcmp(g_looks[k].id, l.id);
			}
			if (!there) {
				/* (in its place by its order, among those from here) */
				int at = g_n;
				while (at > first && g_orders[at - 1] > order) {
					g_looks[at] = g_looks[at - 1];
					g_orders[at] = g_orders[at - 1];
					at--;
				}
				g_looks[at] = l;
				g_orders[at] = order;
				g_n++;
			}
		}
		free(names[i]);
	}
}

/* The desk's own looks' folder, and the others everyone has: each once, however it is reached. */
static int shared_dirs(char out[][PATH_MAX], int most)
{
	int n = 0;
	char path[PATH_MAX];
	/* (the desk's: where its pictures are, the first of those places there is) */
	if (desk_art_find("looks", ".", path, sizeof path)) {
		path[strlen(path) - 2] = '\0';
		snprintf(out[n++], PATH_MAX, "%s", path);
	}
	const char *dirs = getenv("XDG_DATA_DIRS");
	char list[4096];
	snprintf(list, sizeof list, "%s", dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
	for (char *save = NULL, *t = strtok_r(list, ":", &save); t && n < most; t = strtok_r(NULL, ":", &save)) {
		snprintf(path, sizeof path, "%s/desk/looks", t);
		char real[PATH_MAX], other[PATH_MAX];
		if (!realpath(path, real)) {
			continue;
		}
		bool seen = false;
		for (int i = 0; i < n && !seen; i++) {
			seen = realpath(out[i], other) && !strcmp(real, other);
		}
		if (!seen) {
			snprintf(out[n++], PATH_MAX, "%s", path);
		}
	}
	return n;
}

int desk_looks_all(const struct desk_look **out)
{
	g_n = 0;
	for (size_t i = 0; i < DESKS_N; i++) {
		desks_look(i, &g_looks[g_n]);
		g_orders[g_n++] = 0;
	}
	char dirs[8][PATH_MAX];
	int nd = shared_dirs(dirs, 8);
	for (int i = 0; i < nd; i++) {
		looks_in(dirs[i], "");
	}
	char own[PATH_MAX];
	desk_looks_own_dir(own, sizeof own);
	looks_in(own, "own/");
	*out = g_looks;
	return g_n;
}

const struct desk_look *desk_look_find(const char *id)
{
	if (!id || !*id) {
		return NULL;
	}
	for (int i = 0; i < g_n; i++) {
		if (!strcmp(g_looks[i].id, id)) {
			return &g_looks[i];
		}
	}
	for (size_t i = 0; i < DESKS_N; i++) {
		if (!strcmp(DESKS[i].id, id)) {
			desks_look(i, &g_one);
			return &g_one;
		}
	}
	int order;
	if (!strncmp(id, "own/", 4)) {
		char own[PATH_MAX];
		desk_looks_own_dir(own, sizeof own);
		return look_at(own, id + 4, "own/", &g_one, &order) ? &g_one : NULL;
	}
	char dirs[8][PATH_MAX];
	int nd = shared_dirs(dirs, 8);
	for (int i = 0; i < nd; i++) {
		if (look_at(dirs[i], id, "", &g_one, &order)) {
			return &g_one;
		}
	}
	return NULL;
}

/* A picture as read, made no bigger than LOOK_MOST_PX across. */
static cairo_surface_t *read_png(const char *path)
{
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		fprintf(stderr, "desk: a look for the drawer would not read (%s)\n", path);
		return NULL;
	}
	int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img);
	int most = w > h ? w : h;
	if (most <= LOOK_MOST_PX) {
		return img;
	}
	double k = (double)LOOK_MOST_PX / most;
	int sw = (int)(w * k + 0.5), sh = (int)(h * k + 0.5);
	cairo_surface_t *small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw > 0 ? sw : 1, sh > 0 ? sh : 1);
	cairo_t *cr = cairo_create(small);
	cairo_scale(cr, k, k);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(img);
	return small;
}

cairo_surface_t *desk_look_picture(const struct desk_look *look)
{
	if (!look || look->kind != DESK_LOOKS_PICTURE || !look->path[0]) {
		return NULL;
	}
	struct stat st;
	if (stat(look->path, &st) != 0) {
		return NULL;
	}
	long long mtime = (long long)st.st_mtime;
	int free_at = -1;
	for (int i = 0; i < LOOKS_MOST; i++) {
		if (!g_pics[i].path[0]) {
			free_at = free_at < 0 ? i : free_at;
			continue;
		}
		if (strcmp(g_pics[i].path, look->path)) {
			continue;
		}
		if (g_pics[i].mtime == mtime) {
			return g_pics[i].pic;
		}
		/* changed since: read again */
		if (g_pics[i].pic) {
			cairo_surface_destroy(g_pics[i].pic);
		}
		g_pics[i].pic = read_png(look->path);
		g_pics[i].mtime = mtime;
		return g_pics[i].pic;
	}
	if (free_at < 0) {
		/* (more read than there can be: the first goes) */
		free_at = 0;
		if (g_pics[0].pic) {
			cairo_surface_destroy(g_pics[0].pic);
		}
	}
	snprintf(g_pics[free_at].path, sizeof g_pics[free_at].path, "%s", look->path);
	g_pics[free_at].mtime = mtime;
	g_pics[free_at].pic = read_png(look->path);
	return g_pics[free_at].pic;
}
