/* ================================================================
 * Filling the binders.
 *
 * The eight shelves hold whatever is installed. This walks the XDG
 * application directories, applies the precedence and visibility rules the
 * menu spec asks for, files each entry on one of the eight shelves, and
 * finds a picture for it. It runs on a worker thread and publishes an
 * immutable snapshot, because a frame must never wait for a disk walk.
 * ================================================================ */

/* syscall(), for close_range, which glibc has had a name for only lately */
#define _DEFAULT_SOURCE
#include "apps.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---- small string work ----------------------------------------- */

static void str_copy(char *dst, size_t n, const char *src)
{
	if (!n) {
		return;
	}
	if (!src) {
		dst[0] = '\0';
		return;
	}
	size_t i = 0;
	while (src[i] && i + 1 < n) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static char *str_trim(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
		s[--n] = '\0';
	}
	return s;
}

/* .desktop values escape \s \n \t \r \\ */
static void unescape(char *s)
{
	char *o = s;
	for (const char *p = s; *p; p++) {
		if (*p != '\\' || !p[1]) {
			*o++ = *p;
			continue;
		}
		p++;
		switch (*p) {
		case 's': *o++ = ' '; break;
		case 'n': *o++ = '\n'; break;
		case 't': *o++ = '\t'; break;
		case 'r': *o++ = '\r'; break;
		case '\\': *o++ = '\\'; break;
		default: *o++ = '\\'; *o++ = *p; break;
		}
	}
	*o = '\0';
}

static bool truthy(const char *v)
{
	return v && (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
}

/* Does a ;-separated list contain this token? */
static bool list_has(const char *list, const char *tok)
{
	if (!list || !*list) {
		return false;
	}
	size_t tl = strlen(tok);
	for (const char *p = list; *p; ) {
		const char *e = strchr(p, ';');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (len == tl && strncmp(p, tok, tl) == 0) {
			return true;
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return false;
}

static unsigned hash_str(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) {
		h = (h ^ (unsigned char)*s) * 16777619u;
	}
	return h;
}

/* ---- the locale chain ------------------------------------------ */

/* LANG=ja_JP.UTF-8 wants Name[ja_JP] then Name[ja]. On this machine every
 * translated entry uses the short form, but the spec order is the long one
 * first and it costs one strcmp. */
static char g_loc[4][96];
static int g_nloc;
/* The desk's language, if it is not the locale's (Shell Settings): "en"
 * reads every name as it is written, untranslated; "ja", "zh", "ko", "es",
 * "fr", "de" the names in that language. */
static char g_lang[16];

void desk_apps_set_language(const char *code)
{
	desk_apps_finish();   /* not while a scan reads it */
	str_copy(g_lang, sizeof g_lang, code ? code : "");
}

static void locale_build(void)
{
	if (!strcmp(g_lang, "en")) {
		g_nloc = 0;
		return;
	}
	/* LC_ALL over LC_MESSAGES, as POSIX has it and the desk's words do */
	const char *l = getenv("LC_ALL");
	if (!l || !*l) {
		l = getenv("LC_MESSAGES");
	}
	if (!l || !*l) {
		l = getenv("LANG");
	}
	g_nloc = 0;
	/* a language chosen in Shell Settings: the locale's own way of it if
	 * the locale is in it too (es_MX for Spanish in Mexico), else as the
	 * country most of its speakers are in has it, and then plainly */
	static const char *const REGION[][2] = { { "ja", "ja_JP" }, { "zh", "zh_CN" },
		{ "ko", "ko_KR" }, { "es", "es_ES" }, { "fr", "fr_FR" }, { "de", "de_DE" } };
	for (size_t i = 0; *g_lang && i < sizeof REGION / sizeof REGION[0]; i++) {
		size_t k = strlen(REGION[i][0]);
		if (strcmp(g_lang, REGION[i][0]) == 0
			&& !(l && !strncmp(l, g_lang, k) && (l[k] == '_' || l[k] == '.' || !l[k]))) {
			str_copy(g_loc[g_nloc++], 96, REGION[i][1]);
			str_copy(g_loc[g_nloc++], 96, REGION[i][0]);
			return;
		}
	}
	if (!l || !*l || strcmp(l, "C") == 0 || strcmp(l, "POSIX") == 0) {
		return;
	}
	char buf[48];
	str_copy(buf, sizeof buf, l);
	char *dot = strchr(buf, '.');
	if (dot) {
		*dot = '\0';
	}
	char *at = strchr(buf, '@');
	char mod[32] = { 0 };
	if (at) {
		str_copy(mod, sizeof mod, at + 1);
		*at = '\0';
	}
	char *us = strchr(buf, '_');
	char lang[32];
	str_copy(lang, sizeof lang, buf);
	if (us) {
		lang[us - buf] = '\0';
	}
	if (*mod && us) {
		snprintf(g_loc[g_nloc++], 96, "%s@%s", buf, mod);
	}
	if (us) {
		str_copy(g_loc[g_nloc++], 96, buf);
	}
	if (*mod) {
		snprintf(g_loc[g_nloc++], 96, "%s@%s", lang, mod);
	}
	str_copy(g_loc[g_nloc++], 96, lang);
}

/* ---- filing ---------------------------------------------------- */

static const struct {
	const char *cat;
	enum desk_shelf shelf;
} ADDITIONAL[] = {
	/* Development */
	{ "Building", DESK_SHELF_DEVELOPMENT }, { "Debugger", DESK_SHELF_DEVELOPMENT },
	{ "IDE", DESK_SHELF_DEVELOPMENT }, { "GUIDesigner", DESK_SHELF_DEVELOPMENT },
	{ "Profiling", DESK_SHELF_DEVELOPMENT }, { "RevisionControl", DESK_SHELF_DEVELOPMENT },
	{ "Translation", DESK_SHELF_DEVELOPMENT }, { "WebDevelopment", DESK_SHELF_DEVELOPMENT },
	{ "ComputerScience", DESK_SHELF_DEVELOPMENT }, { "ParallelComputing", DESK_SHELF_DEVELOPMENT },
	/* Internet */
	{ "WebBrowser", DESK_SHELF_INTERNET }, { "Email", DESK_SHELF_INTERNET },
	{ "InstantMessaging", DESK_SHELF_INTERNET }, { "Chat", DESK_SHELF_INTERNET },
	{ "IRCClient", DESK_SHELF_INTERNET }, { "Feed", DESK_SHELF_INTERNET },
	{ "News", DESK_SHELF_INTERNET }, { "FileTransfer", DESK_SHELF_INTERNET },
	{ "P2P", DESK_SHELF_INTERNET }, { "RemoteAccess", DESK_SHELF_INTERNET },
	{ "Telephony", DESK_SHELF_INTERNET }, { "TelephonyTools", DESK_SHELF_INTERNET },
	{ "VideoConference", DESK_SHELF_INTERNET }, { "Dialup", DESK_SHELF_INTERNET },
	{ "HamRadio", DESK_SHELF_INTERNET }, { "Maps", DESK_SHELF_INTERNET },
	/* Office */
	{ "Calendar", DESK_SHELF_OFFICE }, { "ContactManagement", DESK_SHELF_OFFICE },
	{ "Database", DESK_SHELF_OFFICE }, { "Dictionary", DESK_SHELF_OFFICE },
	{ "Chart", DESK_SHELF_OFFICE }, { "Finance", DESK_SHELF_OFFICE },
	{ "FlowChart", DESK_SHELF_OFFICE }, { "PDA", DESK_SHELF_OFFICE },
	{ "ProjectManagement", DESK_SHELF_OFFICE }, { "Presentation", DESK_SHELF_OFFICE },
	{ "Spreadsheet", DESK_SHELF_OFFICE }, { "WordProcessor", DESK_SHELF_OFFICE },
	{ "Publishing", DESK_SHELF_OFFICE }, { "Viewer", DESK_SHELF_OFFICE },
	{ "Documentation", DESK_SHELF_OFFICE }, { "Literature", DESK_SHELF_OFFICE },
	{ "Languages", DESK_SHELF_OFFICE }, { "Math", DESK_SHELF_OFFICE },
	{ "NumericalAnalysis", DESK_SHELF_OFFICE }, { "Astronomy", DESK_SHELF_OFFICE },
	{ "Biology", DESK_SHELF_OFFICE }, { "Chemistry", DESK_SHELF_OFFICE },
	{ "Geography", DESK_SHELF_OFFICE }, { "Geology", DESK_SHELF_OFFICE },
	{ "Geoscience", DESK_SHELF_OFFICE }, { "History", DESK_SHELF_OFFICE },
	{ "Humanities", DESK_SHELF_OFFICE }, { "MedicalSoftware", DESK_SHELF_OFFICE },
	{ "Physics", DESK_SHELF_OFFICE }, { "Economy", DESK_SHELF_OFFICE },
	{ "Electricity", DESK_SHELF_OFFICE }, { "Electronics", DESK_SHELF_OFFICE },
	{ "Engineering", DESK_SHELF_OFFICE }, { "Construction", DESK_SHELF_OFFICE },
	{ "Robotics", DESK_SHELF_OFFICE }, { "ArtificialIntelligence", DESK_SHELF_OFFICE },
	{ "DataVisualization", DESK_SHELF_OFFICE }, { "Sports", DESK_SHELF_OFFICE },
	{ "Spirituality", DESK_SHELF_OFFICE }, { "Art", DESK_SHELF_OFFICE },
	/* Graphics */
	{ "2DGraphics", DESK_SHELF_GRAPHICS }, { "3DGraphics", DESK_SHELF_GRAPHICS },
	{ "RasterGraphics", DESK_SHELF_GRAPHICS }, { "VectorGraphics", DESK_SHELF_GRAPHICS },
	{ "Photography", DESK_SHELF_GRAPHICS }, { "Scanning", DESK_SHELF_GRAPHICS },
	{ "OCR", DESK_SHELF_GRAPHICS }, { "ImageProcessing", DESK_SHELF_GRAPHICS },
	/* Media */
	{ "Player", DESK_SHELF_MEDIA }, { "Recorder", DESK_SHELF_MEDIA },
	{ "Mixer", DESK_SHELF_MEDIA }, { "Sequencer", DESK_SHELF_MEDIA },
	{ "Tuner", DESK_SHELF_MEDIA }, { "TV", DESK_SHELF_MEDIA },
	{ "AudioVideoEditing", DESK_SHELF_MEDIA }, { "DiscBurning", DESK_SHELF_MEDIA },
	{ "Midi", DESK_SHELF_MEDIA }, { "Karaoke", DESK_SHELF_MEDIA },
	{ "Music", DESK_SHELF_MEDIA }, { "Podcast", DESK_SHELF_MEDIA },
	/* Games */
	{ "ActionGame", DESK_SHELF_GAMES }, { "AdventureGame", DESK_SHELF_GAMES },
	{ "ArcadeGame", DESK_SHELF_GAMES }, { "BoardGame", DESK_SHELF_GAMES },
	{ "BlocksGame", DESK_SHELF_GAMES }, { "CardGame", DESK_SHELF_GAMES },
	{ "KidsGame", DESK_SHELF_GAMES }, { "LogicGame", DESK_SHELF_GAMES },
	{ "RolePlaying", DESK_SHELF_GAMES }, { "Shooter", DESK_SHELF_GAMES },
	{ "Simulation", DESK_SHELF_GAMES }, { "SportsGame", DESK_SHELF_GAMES },
	{ "StrategyGame", DESK_SHELF_GAMES }, { "Amusement", DESK_SHELF_GAMES },
	{ "Emulator", DESK_SHELF_GAMES }, { "Adult", DESK_SHELF_GAMES },
	/* Utilities */
	{ "TextEditor", DESK_SHELF_UTILITIES }, { "TextTools", DESK_SHELF_UTILITIES },
	{ "Calculator", DESK_SHELF_UTILITIES }, { "Clock", DESK_SHELF_UTILITIES },
	{ "Archiving", DESK_SHELF_UTILITIES }, { "Compression", DESK_SHELF_UTILITIES },
	{ "FileTools", DESK_SHELF_UTILITIES }, { "FileManager", DESK_SHELF_UTILITIES },
	{ "TerminalEmulator", DESK_SHELF_UTILITIES }, { "Accessibility", DESK_SHELF_UTILITIES },
	/* System */
	{ "Monitor", DESK_SHELF_SYSTEM }, { "Security", DESK_SHELF_SYSTEM },
	{ "PackageManager", DESK_SHELF_SYSTEM }, { "Filesystem", DESK_SHELF_SYSTEM },
	{ "HardwareSettings", DESK_SHELF_SYSTEM }, { "DesktopSettings", DESK_SHELF_SYSTEM },
	{ "Printing", DESK_SHELF_SYSTEM },
};

/* A few whose own Categories file them where nobody would look. */
static const struct {
	const char *id;
	enum desk_shelf shelf;
} SHELF_FIX[] = {
	{ "libreoffice-draw.desktop", DESK_SHELF_GRAPHICS },
	{ "org.gnome.Evolution.desktop", DESK_SHELF_INTERNET },
	{ "thunderbird.desktop", DESK_SHELF_INTERNET },
	{ "firefox-esr.desktop", DESK_SHELF_INTERNET },
	{ "org.kde.kmail2.desktop", DESK_SHELF_INTERNET },
	{ "org.kde.akregator.desktop", DESK_SHELF_INTERNET },
	{ "org.kde.sieveeditor.desktop", DESK_SHELF_INTERNET },
	{ "org.gnome.Maps.desktop", DESK_SHELF_INTERNET },
	{ "org.kde.okular.desktop", DESK_SHELF_OFFICE },
	{ "org.gnome.Evince.desktop", DESK_SHELF_OFFICE },
	{ "atril.desktop", DESK_SHELF_OFFICE },
};

/* shelves.conf: "org.kde.kate.desktop = Development", one per line. */
struct user_file {
	char id[96];
	enum desk_shelf shelf;
};
static struct user_file *g_userfile;
static int g_nuserfile;

static const char *SHELF_KEY[DESK_SHELF_COUNT] = {
	"Development", "Internet", "Office", "Graphics",
	"Media", "Games", "Utilities", "System",
};

static void user_file_load(void)
{
	free(g_userfile);
	g_userfile = NULL;
	g_nuserfile = 0;
	char path[512];
	const char *cfg = getenv("XDG_CONFIG_HOME");
	if (cfg && *cfg) {
		snprintf(path, sizeof path, "%s/desk/shelves.conf", cfg);
	} else {
		const char *home = getenv("HOME");
		if (!home) {
			return;
		}
		snprintf(path, sizeof path, "%s/.config/desk/shelves.conf", home);
	}
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	int cap = 32;
	g_userfile = calloc(cap, sizeof *g_userfile);
	char line[256];
	while (g_userfile && fgets(line, sizeof line, f)) {
		char *s = str_trim(line);
		if (!*s || *s == '#') {
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *id = str_trim(s);
		char *val = str_trim(eq + 1);
		for (int i = 0; i < DESK_SHELF_COUNT; i++) {
			if (strcasecmp(val, SHELF_KEY[i]) != 0) {
				continue;
			}
			if (g_nuserfile == cap) {
				cap *= 2;
				struct user_file *n = realloc(g_userfile, (size_t)cap * sizeof *n);
				if (!n) {
					break;
				}
				g_userfile = n;
			}
			str_copy(g_userfile[g_nuserfile].id, 96, id);
			g_userfile[g_nuserfile].shelf = (enum desk_shelf)i;
			g_nuserfile++;
			break;
		}
	}
	fclose(f);
}

/* Which of the drawer's five things a program is, by its own Categories
 * (the order of desk.h's desk_icon), or -1. */
static int model_for(const char *cats)
{
	if (list_has(cats, "TerminalEmulator")) {
		return 0;
	}
	if (list_has(cats, "WebBrowser")) {
		return 1;
	}
	if (list_has(cats, "FileManager")) {
		return 2;
	}
	if (list_has(cats, "TextEditor")) {
		return 3;
	}
	if (list_has(cats, "Settings") && !list_has(cats, "HardwareSettings")) {
		return 4;
	}
	return -1;
}

static enum desk_shelf shelf_for(const char *cats, const char *id, bool *user_filed)
{
	*user_filed = false;
	for (int i = 0; i < g_nuserfile; i++) {
		if (strcmp(g_userfile[i].id, id) == 0) {
			*user_filed = true;
			return g_userfile[i].shelf;
		}
	}
	for (size_t i = 0; i < sizeof SHELF_FIX / sizeof *SHELF_FIX; i++) {
		if (strcmp(SHELF_FIX[i].id, id) == 0) {
			return SHELF_FIX[i].shelf;
		}
	}

	/* strong main categories, in a fixed decision order so the answer does
	 * not move when a package is upgraded */
	if (list_has(cats, "Development")) {
		return DESK_SHELF_DEVELOPMENT;
	}
	if (list_has(cats, "Game")) {
		return DESK_SHELF_GAMES;
	}
	if (list_has(cats, "Office")) {
		return DESK_SHELF_OFFICE;
	}
	if (list_has(cats, "Graphics")) {
		return DESK_SHELF_GRAPHICS;
	}
	if (list_has(cats, "AudioVideo") || list_has(cats, "Audio") || list_has(cats, "Video")) {
		return DESK_SHELF_MEDIA;
	}
	if (list_has(cats, "Network")) {
		return DESK_SHELF_INTERNET;
	}
	if (list_has(cats, "Education") || list_has(cats, "Science")) {
		return DESK_SHELF_OFFICE;
	}

	/* the entry's own additional categories, left to right: authors put the
	 * most specific first and honouring that beats scoring */
	for (const char *p = cats; p && *p; ) {
		const char *e = strchr(p, ';');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		for (size_t i = 0; i < sizeof ADDITIONAL / sizeof *ADDITIONAL; i++) {
			if (strlen(ADDITIONAL[i].cat) == len && strncmp(p, ADDITIONAL[i].cat, len) == 0) {
				return ADDITIONAL[i].shelf;
			}
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}

	if (list_has(cats, "Settings") || list_has(cats, "System")) {
		return DESK_SHELF_SYSTEM;
	}
	if (list_has(cats, "Utility")) {
		return DESK_SHELF_UTILITIES;
	}
	return DESK_SHELF_UTILITIES;
}

/* ---- finding a picture ----------------------------------------- */

static bool file_ok(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int cache_dir(char *buf, size_t n)
{
	const char *base = getenv("XDG_CACHE_HOME");
	char tmp[400];
	if (base && *base) {
		snprintf(tmp, sizeof tmp, "%s/desk", base);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home) {
			return 0;
		}
		snprintf(tmp, sizeof tmp, "%s/.cache/desk", home);
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return 0;
	}
	if ((size_t)snprintf(buf, n, "%s/icons", tmp) >= n) {
		return 0;
	}
	if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
		return 0;
	}
	return 1;
}

/* Half the icon themes on a modern machine are SVG only and cairo cannot
 * read SVG. Rather than take a new library dependency for a picture printed
 * at 40 pixels, rasterise once with whatever is installed -- rsvg-convert,
 * small and made for it (the installers bring it), else ImageMagick -- and
 * keep the PNG. If neither is there the sheet gets its printed initial. */
bool desk_svg_raster(const char *svg, const char *png, int px)
{
	static const char *const TOOLS[] = { "/usr/bin/rsvg-convert", "/usr/bin/magick",
		"/usr/bin/convert" };
	const char *tool = NULL;
	for (size_t i = 0; i < sizeof TOOLS / sizeof TOOLS[0] && !tool; i++) {
		if (access(TOOLS[i], X_OK) == 0) {
			tool = TOOLS[i];
		}
	}
	if (!tool) {
		return false;
	}
	char side[16], geom[32];
	snprintf(side, sizeof side, "%d", px);
	snprintf(geom, sizeof geom, "%dx%d", px, px);
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		int null = open("/dev/null", O_WRONLY);
		if (null >= 0) {
			dup2(null, 1);
			dup2(null, 2);
			close(null);
		}
		if (tool == TOOLS[0]) {
			/* drawn at the size it is wanted, square, its own shape kept */
			execl(tool, "rsvg-convert", "-w", side, "-h", side, "-a", "-f", "png", "-o", png,
				svg, (char *)NULL);
		} else {
			/* (read at twice the size, so a small picture is drawn from
			 * enough of it) */
			execl(tool, tool, "-background", "none", "-density", "192", svg, "-resize", geom,
				png, (char *)NULL);
		}
		_exit(127);
	}
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
		;
	}
	return WIFEXITED(st) && WEXITSTATUS(st) == 0 && file_ok(png);
}

static bool raster_svg(const char *svg, const char *png)
{
	return desk_svg_raster(svg, png, DESK_APP_ICON_PX);
}

/* Where icon themes are, by the icon theme spec, nearest first:
 * $XDG_DATA_HOME/icons, ~/.icons, each of $XDG_DATA_DIRS/icons -- and the
 * icons Flatpak exports, the user's and the system's, for a session that
 * has not put them on $XDG_DATA_DIRS. Only those that are there, each
 * once, and gathered for each lookup: the scan is on a thread of its own,
 * and a list kept between calls pointed into the stack of the last one.
 * (The Flatpak ones after dentarthurdent's flatpak-icons, AiO v0.7.) */
#define ICON_ROOTS_MAX 16
struct icon_roots {
	char dir[ICON_ROOTS_MAX][512];
	size_t n;
};

static void icon_root_add(struct icon_roots *r, const char *dir)
{
	struct stat st;
	if (r->n >= ICON_ROOTS_MAX || dir[0] != '/' || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		return;
	}
	for (size_t i = 0; i < r->n; i++) {
		if (strcmp(r->dir[i], dir) == 0) {
			return;
		}
	}
	str_copy(r->dir[r->n++], sizeof r->dir[0], dir);
}

static void icon_roots_get(struct icon_roots *r)
{
	r->n = 0;
	char p[512];
	const char *home = getenv("HOME");
	const char *dh = getenv("XDG_DATA_HOME");
	if (dh && *dh == '/') {
		snprintf(p, sizeof p, "%s/icons", dh);
		icon_root_add(r, p);
	} else if (home && *home) {
		snprintf(p, sizeof p, "%s/.local/share/icons", home);
		icon_root_add(r, p);
	}
	if (home && *home) {
		snprintf(p, sizeof p, "%s/.icons", home);
		icon_root_add(r, p);
	}
	const char *dirs = getenv("XDG_DATA_DIRS");
	char buf[2048];
	str_copy(buf, sizeof buf, dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
	char *save = NULL;
	for (char *d = strtok_r(buf, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
		if ((size_t)snprintf(p, sizeof p, "%s/icons", d) < sizeof p) {
			icon_root_add(r, p);
		}
	}
	if (home && *home) {
		snprintf(p, sizeof p, "%s/.local/share/flatpak/exports/share/icons", home);
		icon_root_add(r, p);
	}
	icon_root_add(r, "/var/lib/flatpak/exports/share/icons");
}

static const char *ICON_THEMES[] = {
	"hicolor", "Adwaita", "breeze", "Tango", "gnome", "oxygen", "mate", "Humanity",
};
/* Big enough to print at, best first; and the small ones, used only when
 * there is nothing else -- not even an SVG to draw one from. */
static const int ICON_SIZES[] = { 128, 96, 64, 256, 512, 72, 48 };
static const int ICON_SMALL[] = { 32, 24, 22, 16 };

static bool icon_png_at(const struct icon_roots *roots, const char *name, int size,
	char *out, size_t n)
{
	char path[768];
	for (size_t r = 0; r < roots->n; r++) {
		for (size_t t = 0; t < sizeof ICON_THEMES / sizeof *ICON_THEMES; t++) {
			snprintf(path, sizeof path, "%s/%s/%dx%d/apps/%s.png",
				roots->dir[r], ICON_THEMES[t], size, size, name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
			snprintf(path, sizeof path, "%s/%s/apps/%d/%s.png",
				roots->dir[r], ICON_THEMES[t], size, name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
		}
	}
	return false;
}

static void icon_resolve(const char *name, char *out, size_t n)
{
	out[0] = '\0';
	if (!name || !*name) {
		return;
	}
	if (name[0] == '/') {
		if (file_ok(name)) {
			str_copy(out, n, name);
		}
		return;
	}
	struct icon_roots roots;
	icon_roots_get(&roots);

	char path[768];
	/* a real PNG at a usable size, closest to what a sheet prints at, in
	 * whichever theme has one -- not the first theme's 32 pixel one blown
	 * up to four times its size when another has it at 128 */
	for (size_t s = 0; s < sizeof ICON_SIZES / sizeof *ICON_SIZES; s++) {
		if (icon_png_at(&roots, name, ICON_SIZES[s], out, n)) {
			return;
		}
	}
	/* the old flat pile */
	static const char *PIXMAPS[] = { "/usr/share/pixmaps", "/usr/local/share/pixmaps" };
	for (size_t i = 0; i < sizeof PIXMAPS / sizeof *PIXMAPS; i++) {
		snprintf(path, sizeof path, "%s/%s.png", PIXMAPS[i], name);
		if (file_ok(path)) {
			str_copy(out, n, path);
			return;
		}
	}

	/* nothing but SVG: bake it once into our own cache */
	char cdir[400];
	if (!cache_dir(cdir, sizeof cdir)) {
		return;
	}
	char cached[512];
	snprintf(cached, sizeof cached, "%s/%08x-%d.png", cdir, hash_str(name), DESK_APP_ICON_PX);
	if (file_ok(cached)) {
		str_copy(out, n, cached);
		return;
	}
	for (size_t r = 0; r < roots.n; r++) {
		for (size_t t = 0; t < sizeof ICON_THEMES / sizeof *ICON_THEMES; t++) {
			snprintf(path, sizeof path, "%s/%s/scalable/apps/%s.svg",
				roots.dir[r], ICON_THEMES[t], name);
			if (!file_ok(path)) {
				snprintf(path, sizeof path, "%s/%s/apps/scalable/%s.svg",
					roots.dir[r], ICON_THEMES[t], name);
			}
			if (!file_ok(path)) {
				continue;
			}
			if (raster_svg(path, cached)) {
				str_copy(out, n, cached);
				return;
			}
			/* nothing to draw it with: the small PNG, below, if any */
			goto small;
		}
	}
	for (size_t i = 0; i < sizeof PIXMAPS / sizeof *PIXMAPS; i++) {
		snprintf(path, sizeof path, "%s/%s.svg", PIXMAPS[i], name);
		if (file_ok(path) && raster_svg(path, cached)) {
			str_copy(out, n, cached);
			return;
		}
	}
small:
	/* and only now the small ones */
	for (size_t s = 0; s < sizeof ICON_SMALL / sizeof *ICON_SMALL; s++) {
		if (icon_png_at(&roots, name, ICON_SMALL[s], out, n)) {
			return;
		}
	}
}

/* ---- a tray icon, by name ---------------------------------------
 *
 * A tray item names its icon the way an application does, but the icon is
 * as likely to be a status or a device one as an application's, it wants to
 * be small, and the item may carry a directory of its own to look in first.
 * Called off the compositor's thread: it can end in a process that turns
 * an SVG into a PNG. */

static const char *TRAY_THEMES[] = {
	"hicolor", "Adwaita", "AdwaitaLegacy", "breeze", "breeze-dark", "mate", "menta",
	"Yaru", "Papirus", "Papirus-Dark", "Humanity", "gnome", "Tango", "oxygen",
	"elementary", "ubuntu-mono-dark", "ubuntu-mono-light",
};
static const char *TRAY_CATS[] = {
	"status", "apps", "devices", "panel", "actions", "categories", "places",
	"legacy", "notifications", "emblems", "stock",
};
static const int TRAY_SIZES[] = { 48, 32, 24, 22, 64, 16, 128 };

static bool tray_png_under(const char *root, const char *name, char *out, size_t n)
{
	char path[768];
	for (size_t z = 0; z < sizeof TRAY_SIZES / sizeof *TRAY_SIZES; z++) {
		int sz = TRAY_SIZES[z];
		for (size_t c = 0; c < sizeof TRAY_CATS / sizeof *TRAY_CATS; c++) {
			snprintf(path, sizeof path, "%s/%dx%d/%s/%s.png", root, sz, sz, TRAY_CATS[c], name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
			snprintf(path, sizeof path, "%s/%s/%d/%s.png", root, TRAY_CATS[c], sz, name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
		}
	}
	return false;
}

static bool tray_svg_under(const char *root, const char *name, char *out, size_t n)
{
	static const char *DIRS[] = { "scalable", "symbolic" };
	char path[768];
	for (size_t k = 0; k < sizeof DIRS / sizeof *DIRS; k++) {
		for (size_t c = 0; c < sizeof TRAY_CATS / sizeof *TRAY_CATS; c++) {
			snprintf(path, sizeof path, "%s/%s/%s/%s.svg", root, DIRS[k], TRAY_CATS[c], name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
			snprintf(path, sizeof path, "%s/%s/%s/%s.svg", root, TRAY_CATS[c], DIRS[k], name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
		}
	}
	return false;
}

bool desk_icon_path(const char *name, const char *extra_dir, char *out, size_t n)
{
	out[0] = '\0';
	if (!name || !*name) {
		return false;
	}
	if (name[0] == '/') {
		if (file_ok(name)) {
			str_copy(out, n, name);
			return true;
		}
		return false;
	}
	char path[768];
	char svg[768] = { 0 };
	/* the item's own directory: flat, or laid out like a theme */
	if (extra_dir && *extra_dir) {
		snprintf(path, sizeof path, "%s/%s.png", extra_dir, name);
		if (file_ok(path)) {
			str_copy(out, n, path);
			return true;
		}
		char root[600];
		snprintf(root, sizeof root, "%s/hicolor", extra_dir);
		if (tray_png_under(root, name, out, n) || tray_png_under(extra_dir, name, out, n)) {
			return true;
		}
		snprintf(path, sizeof path, "%s/%s.svg", extra_dir, name);
		if (file_ok(path)) {
			str_copy(svg, sizeof svg, path);
		} else if (!tray_svg_under(root, name, svg, sizeof svg)) {
			tray_svg_under(extra_dir, name, svg, sizeof svg);
		}
	}
	struct icon_roots roots;
	icon_roots_get(&roots);
	if (!svg[0]) {
		for (size_t r = 0; r < roots.n; r++) {
			for (size_t t = 0; t < sizeof TRAY_THEMES / sizeof *TRAY_THEMES; t++) {
				char root[600];
				snprintf(root, sizeof root, "%s/%s", roots.dir[r], TRAY_THEMES[t]);
				if (tray_png_under(root, name, out, n)) {
					return true;
				}
			}
		}
		static const char *PIXMAPS[] = { "/usr/share/pixmaps", "/usr/local/share/pixmaps" };
		for (size_t i = 0; i < sizeof PIXMAPS / sizeof *PIXMAPS; i++) {
			snprintf(path, sizeof path, "%s/%s.png", PIXMAPS[i], name);
			if (file_ok(path)) {
				str_copy(out, n, path);
				return true;
			}
		}
		for (size_t r = 0; r < roots.n && !svg[0]; r++) {
			for (size_t t = 0; t < sizeof TRAY_THEMES / sizeof *TRAY_THEMES && !svg[0]; t++) {
				char root[600];
				snprintf(root, sizeof root, "%s/%s", roots.dir[r], TRAY_THEMES[t]);
				tray_svg_under(root, name, svg, sizeof svg);
			}
		}
	}
	if (!svg[0]) {
		return false;
	}
	/* nothing but SVG: turned into a PNG once, and kept */
	char cdir[400];
	if (!cache_dir(cdir, sizeof cdir)) {
		return false;
	}
	char cached[512];
	snprintf(cached, sizeof cached, "%s/tray-%08x-%d.png", cdir, hash_str(svg), DESK_APP_ICON_PX);
	if (file_ok(cached) || raster_svg(svg, cached)) {
		str_copy(out, n, cached);
		return true;
	}
	return false;
}

/* The letter a sheet gets printed with when there is no picture. Takes the
 * first character of the name, whole if it is multibyte -- and "?" for a
 * name that does not start with a whole UTF-8 letter (an X title in
 * Latin-1, a name cut off inside its first letter): read on past it, that
 * went beyond the end of the name, and cairo, given it, stops drawing. */
void desk_initial_of(const char *name, char *out, size_t n)
{
	if (!n) {
		return;
	}
	out[0] = '\0';
	if (!name || !*name) {
		str_copy(out, n, "?");
		return;
	}
	unsigned char c = (unsigned char)name[0];
	size_t len = 1;
	if (c >= 0xf8 || (c >= 0x80 && c < 0xc0)) {
		len = 0;
	} else if (c >= 0xf0) {
		len = 4;
	} else if (c >= 0xe0) {
		len = 3;
	} else if (c >= 0xc0) {
		len = 2;
	}
	for (size_t i = 1; i < len; i++) {
		if (((unsigned char)name[i] & 0xc0) != 0x80) {
			len = 0;   /* stops at the end of the name too */
		}
	}
	if (len == 0 || len >= n) {
		str_copy(out, n, "?");
		return;
	}
	memcpy(out, name, len);
	out[len] = '\0';
	if (len == 1 && islower((unsigned char)out[0])) {
		out[0] = (char)toupper((unsigned char)out[0]);
	}
}

/* ---- parsing one file ------------------------------------------ */

struct entry {
	char name[128], generic[128], exec[512], tryexec[256], path[256];
	char name_c[128], keywords[256];
	char icon[128], cats[512], onlyshow[256], notshow[256], type[32];
	char wmclass[96];
	bool nodisplay, hidden, terminal;
	int name_score, generic_score, kw_score;   /* how good a locale match we have */
};

static void take_localised(const char *key, const char *base, const char *loc,
	const char *val, char *dst, size_t dn, int *score)
{
	if (strcmp(key, base) != 0) {
		return;
	}
	int s = 0;
	if (loc) {
		for (int i = 0; i < g_nloc; i++) {
			if (strcmp(loc, g_loc[i]) == 0) {
				s = g_nloc - i;
				break;
			}
		}
		if (!s) {
			return;    /* a language we do not read */
		}
	}
	if (s >= *score) {
		*score = s;
		str_copy(dst, dn, val);
	}
}

/* Whatever in s is not UTF-8 taken out, in place: a desktop file in some
 * other encoding, or a name cut off inside a letter where it was copied
 * into its field. What is drawn of it goes to cairo, which, given broken
 * UTF-8, draws nothing more on that cr. */
static void utf8_scrub(char *s)
{
	unsigned char *p = (unsigned char *)s, *o = p;
	while (*p) {
		int len = *p < 0x80 ? 1 : (*p >= 0xc2 && *p <= 0xdf) ? 2 : (*p >= 0xe0 && *p <= 0xef) ? 3
			: (*p >= 0xf0 && *p <= 0xf4) ? 4 : 0;
		bool ok = len > 0;
		for (int k = 1; ok && k < len; k++) {
			ok = (p[k] & 0xc0) == 0x80;
		}
		if (ok && len == 3) {
			ok = !(p[0] == 0xe0 && p[1] < 0xa0) && !(p[0] == 0xed && p[1] > 0x9f);
		} else if (ok && len == 4) {
			ok = !(p[0] == 0xf0 && p[1] < 0x90) && !(p[0] == 0xf4 && p[1] > 0x8f);
		}
		if (!ok) {
			p++;
			continue;
		}
		memmove(o, p, (size_t)len);
		o += len;
		p += len;
	}
	*o = '\0';
}

static bool parse_entry(const char *file, struct entry *e)
{
	FILE *f = fopen(file, "re");
	if (!f) {
		return false;
	}
	memset(e, 0, sizeof *e);
	str_copy(e->type, sizeof e->type, "Application");
	bool in_entry = false;
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		char *s = str_trim(line);
		if (*s == '#' || !*s) {
			continue;
		}
		if (*s == '[') {
			in_entry = strcmp(s, "[Desktop Entry]") == 0;
			continue;
		}
		if (!in_entry) {
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *key = str_trim(s);
		char *val = str_trim(eq + 1);
		unescape(val);
		char *loc = NULL;
		char *br = strchr(key, '[');
		if (br) {
			*br = '\0';
			loc = br + 1;
			char *cb = strchr(loc, ']');
			if (cb) {
				*cb = '\0';
			}
			key = str_trim(key);
		}
		take_localised(key, "Name", loc, val, e->name, sizeof e->name, &e->name_score);
		take_localised(key, "GenericName", loc, val, e->generic, sizeof e->generic,
			&e->generic_score);
		take_localised(key, "Keywords", loc, val, e->keywords, sizeof e->keywords,
			&e->kw_score);
		if (!loc && strcmp(key, "Name") == 0) {
			str_copy(e->name_c, sizeof e->name_c, val);
		}
		if (loc) {
			continue;
		}
		if (strcmp(key, "Exec") == 0) {
			str_copy(e->exec, sizeof e->exec, val);
		} else if (strcmp(key, "TryExec") == 0) {
			str_copy(e->tryexec, sizeof e->tryexec, val);
		} else if (strcmp(key, "Path") == 0) {
			str_copy(e->path, sizeof e->path, val);
		} else if (strcmp(key, "Icon") == 0) {
			str_copy(e->icon, sizeof e->icon, val);
		} else if (strcmp(key, "Categories") == 0) {
			str_copy(e->cats, sizeof e->cats, val);
		} else if (strcmp(key, "OnlyShowIn") == 0) {
			str_copy(e->onlyshow, sizeof e->onlyshow, val);
		} else if (strcmp(key, "NotShowIn") == 0) {
			str_copy(e->notshow, sizeof e->notshow, val);
		} else if (strcmp(key, "Type") == 0) {
			str_copy(e->type, sizeof e->type, val);
		} else if (strcmp(key, "StartupWMClass") == 0) {
			str_copy(e->wmclass, sizeof e->wmclass, val);
		} else if (strcmp(key, "NoDisplay") == 0) {
			e->nodisplay = truthy(val);
		} else if (strcmp(key, "Hidden") == 0) {
			e->hidden = truthy(val);
		} else if (strcmp(key, "Terminal") == 0) {
			e->terminal = truthy(val);
		}
	}
	fclose(f);
	/* the words that are shown and searched */
	utf8_scrub(e->name);
	utf8_scrub(e->name_c);
	utf8_scrub(e->generic);
	utf8_scrub(e->keywords);
	return true;
}

static bool on_path(const char *prog)
{
	if (strchr(prog, '/')) {
		return access(prog, X_OK) == 0;
	}
	const char *p = getenv("PATH");
	if (!p) {
		p = "/usr/local/bin:/usr/bin:/bin";
	}
	char buf[512];
	while (*p) {
		const char *e = strchr(p, ':');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (len && len + strlen(prog) + 2 < sizeof buf) {
			memcpy(buf, p, len);
			buf[len] = '/';
			strcpy(buf + len + 1, prog);
			if (access(buf, X_OK) == 0) {
				return true;
			}
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return false;
}

/* ---- the scan -------------------------------------------------- */

/* A folder the scan read, or looked for and did not find, and when it was
 * last changed: what desk_apps_look stats again. */
#define WATCH_MAX 64
struct watch {
	char dir[512];
	bool there;
	struct timespec changed;
};

struct scan {
	struct desk_app *apps;
	int n, cap;
	int n_files, n_hidden;
	struct watch watch[WATCH_MAX];
	int n_watch;
};

static void watch_stat(struct watch *w)
{
	struct stat st;
	w->there = stat(w->dir, &st) == 0 && S_ISDIR(st.st_mode);
	w->changed = w->there ? st.st_mtim : (struct timespec){ 0, 0 };
}

/* every folder read, and the top ones not there (made later, they count) */
static void watch_add(struct scan *sc, const char *dir)
{
	if (sc->n_watch >= WATCH_MAX) {
		return;
	}
	for (int i = 0; i < sc->n_watch; i++) {
		if (strcmp(sc->watch[i].dir, dir) == 0) {
			return;
		}
	}
	struct watch *w = &sc->watch[sc->n_watch++];
	str_copy(w->dir, sizeof w->dir, dir);
	watch_stat(w);
}

static bool seen_id(const struct scan *s, const char *id)
{
	for (int i = 0; i < s->n; i++) {
		if (strcmp(s->apps[i].id, id) == 0) {
			return true;
		}
	}
	return false;
}

static void scan_dir(struct scan *sc, const char *root, const char *sub, int depth)
{
	char dir[512];
	if (sub && *sub) {
		snprintf(dir, sizeof dir, "%s/%s", root, sub);
	} else {
		str_copy(dir, sizeof dir, root);
	}
	/* its time taken before it is read: a file put in while it is being
	 * read changes it again, and it is read again */
	watch_add(sc, dir);
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.') {
			continue;
		}
		char full[1024];
		snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
		struct stat st;
		if (stat(full, &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			/* a link can lead back up the tree (x -> .): no further down
			 * than a real applications folder goes, and never on with a
			 * path that did not fit, which could be the same one again */
			char nsub[256];
			int w = sub && *sub ? snprintf(nsub, sizeof nsub, "%s/%s", sub, de->d_name)
				: snprintf(nsub, sizeof nsub, "%s", de->d_name);
			if (depth < 8 && w > 0 && (size_t)w < sizeof nsub) {
				scan_dir(sc, root, nsub, depth + 1);
			}
			continue;
		}
		size_t nl = strlen(de->d_name);
		if (nl < 9 || strcmp(de->d_name + nl - 8, ".desktop") != 0) {
			continue;
		}
		sc->n_files++;

		/* the desktop file id: path below the root, slashes to dashes */
		char id[96];
		if (sub && *sub) {
			/* an id that does not fit could only be named wrongly */
			int w = snprintf(id, sizeof id, "%s-%s", sub, de->d_name);
			if (w < 0 || (size_t)w >= sizeof id) {
				continue;
			}
			for (char *p = id; *p; p++) {
				if (*p == '/') {
					*p = '-';
				}
			}
		} else {
			str_copy(id, sizeof id, de->d_name);
		}
		if (seen_id(sc, id)) {
			continue;   /* an earlier directory already won this id */
		}

		struct entry e;
		if (!parse_entry(full, &e)) {
			continue;
		}
		/* Record the id as taken even when we drop it: a later directory
		 * must not resurrect an entry a nearer one deliberately hid.
		 * Dropped: not a program, deleted (Hidden), kept out of this
		 * desktop (NotShowIn), not installed (TryExec), or part of some
		 * other desktop's furniture. Kept, but off the shelves: one not
		 * for a menu (NoDisplay), or for another desktop's menus alone
		 * (OnlyShowIn) -- still opened by its id, as a standard program
		 * or the default for a kind of file. Every one of Caja's is
		 * OnlyShowIn=MATE. */
		bool drop = false, off = false;
		if (strcmp(e.type, "Application") != 0 || !e.exec[0] || e.hidden) {
			drop = true;
		} else if (e.notshow[0] && (list_has(e.notshow, "DEsk") || list_has(e.notshow, "DeskWM"))) {
			drop = true;
		} else if (e.tryexec[0] && !on_path(e.tryexec)) {
			drop = true;
		} else if (list_has(e.cats, "Screensaver") || list_has(e.cats, "TrayIcon")
			|| list_has(e.cats, "Applet") || list_has(e.cats, "Shell")) {
			drop = true;
		} else if (e.nodisplay || (e.onlyshow[0] && !list_has(e.onlyshow, "DEsk")
			&& !list_has(e.onlyshow, "DeskWM"))) {
			off = true;
		}

		if (sc->n == sc->cap) {
			int cap = sc->cap ? sc->cap * 2 : 128;
			struct desk_app *n = realloc(sc->apps, (size_t)cap * sizeof *n);
			if (!n) {
				break;
			}
			sc->apps = n;
			sc->cap = cap;
		}
		struct desk_app *a = &sc->apps[sc->n];
		memset(a, 0, sizeof *a);
		a->model = -1;
		str_copy(a->id, sizeof a->id, id);
		if (drop) {
			/* a tombstone: holds the id, never shown */
			a->shelf = DESK_SHELF_COUNT;
			sc->n++;
			sc->n_hidden++;
			continue;
		}
		str_copy(a->name, sizeof a->name, e.name[0] ? e.name : id);
		str_copy(a->generic, sizeof a->generic, e.generic);
		str_copy(a->name_c, sizeof a->name_c, e.name_c[0] ? e.name_c : a->name);
		str_copy(a->keywords, sizeof a->keywords, e.keywords);
		str_copy(a->exec, sizeof a->exec, e.exec);
		str_copy(a->workdir, sizeof a->workdir, e.path);
		str_copy(a->icon_name, sizeof a->icon_name, e.icon);
		str_copy(a->wmclass, sizeof a->wmclass, e.wmclass);
		a->terminal = e.terminal;
		a->seed = hash_str(id);
		bool uf = false;
		a->shelf = shelf_for(e.cats, id, &uf);
		a->user_filed = uf;
		a->model = model_for(e.cats);
		desk_initial_of(a->name, a->initial, sizeof a->initial);
		/* What shelves.conf files somewhere is on that shelf, whatever
		 * the file says. The rest of what is kept off is opened, never
		 * shown, so its icon is not looked for: there are hundreds of
		 * them, and the scan would take three times as long. */
		if (off && !uf) {
			a->shelf = DESK_SHELF_COUNT;
			sc->n_hidden++;
		} else {
			icon_resolve(a->icon_name, a->icon_file, sizeof a->icon_file);
		}
		sc->n++;
	}
	closedir(d);
}

static int cmp_app(const void *pa, const void *pb)
{
	const struct desk_app *a = pa, *b = pb;
	if (a->shelf != b->shelf) {
		return (int)a->shelf - (int)b->shelf;
	}
	int c = strcoll(a->name, b->name);
	return c ? c : strcmp(a->id, b->id);
}

/* ---- the published snapshot ------------------------------------ */

struct snapshot {
	struct desk_app_list pub;
	/* what is kept off the shelves, which only desk_apps_find sees */
	struct desk_app *off;
	int n_off;
	int refs;
};

static struct snapshot *g_snap;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_worker;
static bool g_worker_live;
static atomic_bool g_worker_done;   /* the worker's scan has published */
static unsigned g_generation;
/* the folders the last scan read (desk_apps_look): written by the scan,
 * read only while no scan is running */
static struct watch g_watch[WATCH_MAX];
static int g_nwatch;

static void snapshot_free(struct snapshot *s)
{
	free((void *)s->pub.apps);
	free(s->off);
	free(s);
}

static void snapshot_unref(struct snapshot *s)
{
	if (!s || --s->refs > 0) {
		return;
	}
	snapshot_free(s);
}

static const struct desk_shelf_info SHELF_DEFAULT[DESK_SHELF_COUNT] = {
	{ "Development", "Development", "開発",           0.212, 0.278, 0.200, 0, 0 },
	{ "Internet",    "Internet",    "インターネット", 0.180, 0.250, 0.352, 0, 0 },
	{ "Office",      "Office",      "文書",           0.404, 0.286, 0.152, 0, 0 },
	{ "Graphics",    "Graphics",    "画像",           0.368, 0.196, 0.244, 0, 0 },
	{ "Media",       "Media",       "音と映像",       0.298, 0.196, 0.330, 0, 0 },
	{ "Games",       "Games",       "遊び",           0.462, 0.330, 0.132, 0, 0 },
	{ "Utilities",   "Utilities",   "道具",           0.296, 0.296, 0.290, 0, 0 },
	{ "System",      "System",      "システム",       0.176, 0.176, 0.186, 0, 0 },
};

static void publish(struct scan *sc)
{
	/* the tombstones go, and what is kept off the shelves (an Exec, but
	 * no shelf) goes to a list of its own */
	int keep = 0, noff = 0;
	for (int i = 0; i < sc->n; i++) {
		noff += sc->apps[i].shelf >= DESK_SHELF_COUNT && sc->apps[i].exec[0];
	}
	struct desk_app *off = noff ? malloc((size_t)noff * sizeof *off) : NULL;
	noff = 0;
	for (int i = 0; i < sc->n; i++) {
		if (sc->apps[i].shelf < DESK_SHELF_COUNT) {
			sc->apps[keep++] = sc->apps[i];
		} else if (sc->apps[i].exec[0] && off) {
			off[noff++] = sc->apps[i];
		}
	}
	sc->n = keep;
	qsort(sc->apps, (size_t)sc->n, sizeof *sc->apps, cmp_app);

	struct snapshot *s = calloc(1, sizeof *s);
	if (!s) {
		free(sc->apps);
		free(off);
		return;
	}
	s->refs = 1;
	s->off = off;
	s->n_off = noff;
	s->pub.apps = sc->apps;
	s->pub.n = sc->n;
	s->pub.n_files = sc->n_files;
	s->pub.n_hidden = sc->n_hidden;
	s->pub.scanned_at = time(NULL);
	for (int i = 0; i < DESK_SHELF_COUNT; i++) {
		s->pub.shelf[i] = SHELF_DEFAULT[i];
		s->pub.shelf[i].first = sc->n;
		s->pub.shelf[i].count = 0;
	}
	for (int i = 0; i < sc->n; i++) {
		struct desk_shelf_info *si = &s->pub.shelf[sc->apps[i].shelf];
		if (!si->count) {
			si->first = i;
		}
		si->count++;
	}

	pthread_mutex_lock(&g_lock);
	s->pub.generation = ++g_generation;
	struct snapshot *old = g_snap;
	g_snap = s;
	pthread_mutex_unlock(&g_lock);
	snapshot_unref(old);
}

static void do_scan(void)
{
	locale_build();
	user_file_load();

	struct scan sc;
	memset(&sc, 0, sizeof sc);

	/* $XDG_DATA_HOME first, then $XDG_DATA_DIRS in order: nearest wins */
	char home_apps[512] = { 0 };
	const char *dh = getenv("XDG_DATA_HOME");
	if (dh && *dh) {
		snprintf(home_apps, sizeof home_apps, "%s/applications", dh);
	} else {
		const char *home = getenv("HOME");
		if (home) {
			snprintf(home_apps, sizeof home_apps, "%s/.local/share/applications", home);
		}
	}
	if (home_apps[0]) {
		scan_dir(&sc, home_apps, NULL, 0);
	}
	const char *dirs = getenv("XDG_DATA_DIRS");
	if (!dirs || !*dirs) {
		dirs = "/usr/local/share:/usr/share";
	}
	char buf[1024];
	str_copy(buf, sizeof buf, dirs);
	for (char *p = buf; p && *p; ) {
		char *e = strchr(p, ':');
		if (e) {
			*e = '\0';
		}
		if (*p) {
			char root[1088];
			snprintf(root, sizeof root, "%s/applications", p);
			scan_dir(&sc, root, NULL, 0);
		}
		p = e ? e + 1 : NULL;
	}
	/* and last, applications/ beside DEsk's own binary: what it brings
	 * with it (Shell Settings), when it is run from where it was built
	 * rather than installed */
	char exe[1024];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n > 0) {
		exe[n] = '\0';
		char *slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			char root[1088];
			snprintf(root, sizeof root, "%s/applications", exe);
			scan_dir(&sc, root, NULL, 0);
		}
	}
	memcpy(g_watch, sc.watch, (size_t)sc.n_watch * sizeof *g_watch);
	g_nwatch = sc.n_watch;
	publish(&sc);
}

static void *worker(void *arg)
{
	(void)arg;
	do_scan();
	atomic_store(&g_worker_done, true);
	return NULL;
}

static void worker_start(void)
{
	atomic_store(&g_worker_done, false);
	if (pthread_create(&g_worker, NULL, worker, NULL) == 0) {
		g_worker_live = true;
	} else {
		do_scan();
	}
}

void desk_apps_init(bool async)
{
	pthread_mutex_lock(&g_lock);
	bool have = g_snap != NULL || g_worker_live;
	pthread_mutex_unlock(&g_lock);
	if (have) {
		return;
	}
	if (!async) {
		do_scan();
		return;
	}
	worker_start();
}

/* Read them all again, now, on this thread: for a change of language,
 * which changes every name. The generation moves on. */
void desk_apps_rescan(void)
{
	desk_apps_finish();
	do_scan();
}

void desk_apps_finish(void)
{
	if (g_worker_live) {
		pthread_join(g_worker, NULL);
		g_worker_live = false;
	}
}

/* ---- looking again ---------------------------------------------- */

/* A program installed or taken away while the desk is up -- from a
 * terminal, Flatpak, a software centre -- puts its desktop file in, or
 * takes it out of, one of the folders the scan read, and that folder's
 * time of change moves (a package manager's rename into place does too).
 * So every few seconds those folders are looked at again, a stat each, and
 * when one has changed -- or one that was not there is now -- they are all
 * read again on a thread of their own, as at the start, and the books
 * follow the new list. Nothing else is read until then. (GitHub #34: a
 * Flatpak installed from a terminal was in the book only after logging in
 * again.) */
#define LOOK_EVERY_S 3

bool desk_apps_look(char *why, size_t n)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	static time_t last;
	if (last && now.tv_sec - last < LOOK_EVERY_S) {
		return false;
	}
	last = now.tv_sec;
	if (g_worker_live) {
		if (!atomic_load(&g_worker_done)) {
			return false;   /* still reading; looked at once it has */
		}
		pthread_join(g_worker, NULL);
		g_worker_live = false;
	}
	for (int i = 0; i < g_nwatch; i++) {
		struct watch w = g_watch[i];
		watch_stat(&w);
		if (w.there != g_watch[i].there || w.changed.tv_sec != g_watch[i].changed.tv_sec
			|| w.changed.tv_nsec != g_watch[i].changed.tv_nsec) {
			snprintf(why, n, "%s %s", w.dir,
				!w.there ? "has gone" : !g_watch[i].there ? "is there now" : "has changed");
			worker_start();
			return true;
		}
	}
	return false;
}

void desk_apps_shutdown(void)
{
	desk_apps_finish();
	pthread_mutex_lock(&g_lock);
	struct snapshot *s = g_snap;
	g_snap = NULL;
	pthread_mutex_unlock(&g_lock);
	snapshot_unref(s);
	free(g_userfile);
	g_userfile = NULL;
	g_nuserfile = 0;
}

static struct desk_app_list g_empty;

const struct desk_app_list *desk_apps_acquire(void)
{
	pthread_mutex_lock(&g_lock);
	struct snapshot *s = g_snap;
	if (s) {
		s->refs++;
	}
	pthread_mutex_unlock(&g_lock);
	if (!s) {
		for (int i = 0; i < DESK_SHELF_COUNT; i++) {
			g_empty.shelf[i] = SHELF_DEFAULT[i];
		}
		return &g_empty;
	}
	return &s->pub;
}

void desk_apps_release(const struct desk_app_list *list)
{
	if (!list || list == &g_empty) {
		return;
	}
	pthread_mutex_lock(&g_lock);
	struct snapshot *s = (struct snapshot *)list;
	int refs = --s->refs;
	pthread_mutex_unlock(&g_lock);
	if (refs <= 0) {
		snapshot_free(s);
	}
}

/* ---- the standard programs ------------------------------------------
 *
 * What the drawer's four things are and what Super+Enter opens: DEsk's
 * terminal, web browser, file manager and text editor. For each, the first
 * of: what shell.conf sets for it (app.terminal = <desktop id>); the
 * default program for its kind of file, found where GIO and xdg-open find
 * it (mime_default) -- the user's choice in mimeapps.list before the
 * desk-mimeapps.list DEsk installs -- for all but the terminal, which no
 * kind of file stands for; then the first installed of DEsk's standard
 * set; then any program of its kind by its Categories, one not filed under
 * Development (a programmer's editor) before one that is.
 *
 * The standard set is the classic GTK programs of Xfce and MATE, and
 * LXDE's: a menu bar, a row of plain tools under it and the document in
 * the rest -- the kind of window the desk's paper is cut for, not a strip
 * of flat buttons across the top. They are small and quick to start, which
 * a slow machine feels, run on Wayland as they are, and take input
 * methods. KDE's programs come after them: they leave their frame to the
 * shell, but they are drawn for Plasma, which is a newer room than this
 * one. GNOME's come last, whose header bars are a frame of their own. */

static const char *const ROLE_NAME[DESK_STD_COUNT] = {
	"terminal", "browser", "files", "editor",
};

static const char *const ROLE_MIME[DESK_STD_COUNT] = {
	NULL, "x-scheme-handler/https", "inode/directory", "text/plain",
};

static const char *const STD_TERMINAL[] = {
	"xfce4-terminal.desktop", "mate-terminal.desktop", "lxterminal.desktop",
	"terminator.desktop", "org.kde.konsole.desktop", "foot.desktop", "kitty.desktop",
	"Alacritty.desktop", "org.wezfurlong.wezterm.desktop", "org.gnome.Terminal.desktop",
	"org.gnome.Console.desktop", "debian-xterm.desktop", "xterm.desktop", NULL,
};
/* firefox_firefox and chromium_chromium: Ubuntu's, which come as snaps */
static const char *const STD_BROWSER[] = {
	"firefox-esr.desktop", "firefox.desktop", "org.mozilla.firefox.desktop",
	"firefox_firefox.desktop", "chromium.desktop", "org.chromium.Chromium.desktop",
	"chromium_chromium.desktop", "google-chrome.desktop", "org.kde.falkon.desktop",
	"org.gnome.Epiphany.desktop", NULL,
};
/* Caja as a window and nothing more (--no-desktop --browser): the plain
 * caja.desktop also makes it MATE's desktop, icons over the background */
static const char *const STD_FILES[] = {
	"thunar.desktop", "caja-browser.desktop", "pcmanfm.desktop", "nemo.desktop",
	"pcmanfm-qt.desktop", "org.kde.dolphin.desktop", "org.gnome.Nautilus.desktop", NULL,
};
static const char *const STD_EDITOR[] = {
	"org.xfce.mousepad.desktop", "mousepad.desktop", "pluma.desktop", "featherpad.desktop",
	"org.kde.kwrite.desktop", "org.kde.kate.desktop", "org.gnome.gedit.desktop",
	"gedit.desktop", "org.gnome.TextEditor.desktop", NULL,
};
static const char *const *const STD_SET[DESK_STD_COUNT] = {
	STD_TERMINAL, STD_BROWSER, STD_FILES, STD_EDITOR,
};

static char g_role_set[DESK_STD_COUNT][96];

const char *desk_std_name(int role)
{
	return role >= 0 && role < DESK_STD_COUNT ? ROLE_NAME[role] : "";
}

int desk_std_parse(const char *s)
{
	for (int i = 0; s && i < DESK_STD_COUNT; i++) {
		if (!strcmp(s, ROLE_NAME[i])) {
			return i;
		}
	}
	return -1;
}

void desk_apps_set_std(int role, const char *id)
{
	if (role < 0 || role >= DESK_STD_COUNT) {
		return;
	}
	str_copy(g_role_set[role], sizeof g_role_set[role], id ? id : "");
}

/* id as a desktop file id, with ".desktop" if it was given without */
static const struct desk_app *find_loose(const struct desk_app_list *l, const char *id)
{
	const struct desk_app *a = desk_apps_find(l, id);
	if (!a && id && *id && !strstr(id, ".desktop")) {
		char full[128];
		snprintf(full, sizeof full, "%s.desktop", id);
		a = desk_apps_find(l, full);
	}
	return a;
}

/* Installed, as GIO and xdg-open take it: its entry is there and so is the
 * program its Exec line runs. An entry left behind by a program that has
 * gone is passed over for the next in the list. */
static bool mime_installed(const struct desk_app *a)
{
	char pool[sizeof a->exec + 2];
	char *argv[DESK_APP_ARGV_MAX];
	return desk_app_build_argv(a, argv, DESK_APP_ARGV_MAX, pool, sizeof pool) > 0
		&& on_path(argv[0]);
}

/* The first installed program the [Default Applications] group of file
 * names for mime, or NULL. Read as GLib reads a key file, which is how GIO
 * reads it: space around the = does not count, space in the list of ids
 * does (" pluma.desktop" is no program's id), and of two lines for the
 * same kind of file, the later one counts. */
static const struct desk_app *mime_default_in(const struct desk_app_list *l, const char *file,
	const char *mime)
{
	FILE *f = fopen(file, "re");
	if (!f) {
		return NULL;
	}
	char line[4096], ids[4096] = "";
	bool in = false, whole = true;
	while (fgets(line, sizeof line, f)) {
		/* the rest of a line longer than line[] is not a line of its own */
		bool head = whole;
		whole = strchr(line, '\n') != NULL;
		if (!head) {
			continue;
		}
		line[strcspn(line, "\r\n")] = '\0';
		char *s = line + strspn(line, " \t");
		if (*s == '[') {
			in = strcmp(str_trim(s), "[Default Applications]") == 0;
			continue;
		}
		char *eq = strchr(s, '=');
		if (!in || !eq) {
			continue;
		}
		*eq = '\0';
		if (strcmp(str_trim(s), mime) == 0) {
			str_copy(ids, sizeof ids, eq + 1 + strspn(eq + 1, " \t"));
		}
	}
	fclose(f);
	const struct desk_app *found = NULL;
	char *save = NULL;
	for (char *id = strtok_r(ids, ";", &save); id && !found; id = strtok_r(NULL, ";", &save)) {
		found = desk_apps_find(l, id);
		if (found && !mime_installed(found)) {
			found = NULL;
		}
	}
	return found;
}

/* In one directory: desk-mimeapps.list, then mimeapps.list. */
static const struct desk_app *mime_default_dir(const struct desk_app_list *l, const char *dir,
	const char *mime)
{
	static const char *const NAME[] = { "desk-mimeapps.list", "mimeapps.list" };
	const struct desk_app *a = NULL;
	for (int i = 0; !a && i < 2; i++) {
		char file[1100];
		if ((size_t)snprintf(file, sizeof file, "%s/%s", dir, NAME[i]) < sizeof file) {
			a = mime_default_in(l, file, mime);
		}
	}
	return a;
}

/* In each directory of a list like $XDG_DATA_DIRS, in order, with sub
 * after each ("/applications", or ""). A directory that is not a full
 * path is no directory, as the base directory spec has it. */
static const struct desk_app *mime_default_dirs(const struct desk_app_list *l, const char *dirs,
	const char *sub, const char *mime)
{
	char buf[2048];
	str_copy(buf, sizeof buf, dirs);
	const struct desk_app *a = NULL;
	char *save = NULL;
	for (char *d = strtok_r(buf, ":", &save); d && !a; d = strtok_r(NULL, ":", &save)) {
		char dir[1024];
		if (*d == '/' && (size_t)snprintf(dir, sizeof dir, "%s%s", d, sub) < sizeof dir) {
			a = mime_default_dir(l, dir, mime);
		}
	}
	return a;
}

/* The default program for mime, by the XDG MIME applications spec for the
 * desktop "desk" (DEsk sets XDG_CURRENT_DESKTOP=DEsk): the first
 * installed program a [Default Applications] line names, the files looked
 * at in the spec's order -- $XDG_CONFIG_HOME, each of $XDG_CONFIG_DIRS,
 * $XDG_DATA_HOME/applications, each of $XDG_DATA_DIRS/applications, and in
 * each desk-mimeapps.list before mimeapps.list. That is how GIO and
 * xdg-open find it in the session, so the drawer opens what a double click
 * on a file opens: the user's own choice in mimeapps.list (which
 * xfce4-mime-settings, Default programs in Shell Settings, writes) before
 * the desk-mimeapps.list DEsk installs. Like them, it takes a default as
 * it is written, and does not also ask, as the spec does, that the program
 * be associated with the kind of file (its MimeType=, [Added
 * Associations]) and not taken back by a [Removed Associations] line. NULL
 * if no list names an installed program. */
static const struct desk_app *mime_default(const struct desk_app_list *l, const char *mime)
{
	const char *home = getenv("HOME");
	if (!home) {
		home = "";
	}
	char dir[1024];
	const char *v = getenv("XDG_CONFIG_HOME");
	if (v && *v == '/') {
		str_copy(dir, sizeof dir, v);
	} else {
		snprintf(dir, sizeof dir, "%s/.config", home);
	}
	const struct desk_app *a = mime_default_dir(l, dir, mime);
	if (!a) {
		v = getenv("XDG_CONFIG_DIRS");
		a = mime_default_dirs(l, v && *v ? v : "/etc/xdg", "", mime);
	}
	if (!a) {
		v = getenv("XDG_DATA_HOME");
		if (v && *v == '/') {
			snprintf(dir, sizeof dir, "%s/applications", v);
		} else {
			snprintf(dir, sizeof dir, "%s/.local/share/applications", home);
		}
		a = mime_default_dir(l, dir, mime);
	}
	if (!a) {
		v = getenv("XDG_DATA_DIRS");
		a = mime_default_dirs(l, v && *v ? v : "/usr/local/share:/usr/share", "/applications",
			mime);
	}
	return a;
}

const struct desk_app *desk_apps_for_std(const struct desk_app_list *l, int role)
{
	if (!l || role < 0 || role >= DESK_STD_COUNT) {
		return NULL;
	}
	const struct desk_app *a = NULL;
	if (g_role_set[role][0]) {
		a = find_loose(l, g_role_set[role]);
	}
	if (!a && ROLE_MIME[role]) {
		a = mime_default(l, ROLE_MIME[role]);
	}
	for (int i = 0; !a && STD_SET[role][i]; i++) {
		a = desk_apps_find(l, STD_SET[role][i]);
	}
	/* anything of the kind: a programmer's editor is the last resort */
	for (int pass = 0; !a && pass < 2; pass++) {
		for (int i = 0; i < l->n && !a; i++) {
			const struct desk_app *c = &l->apps[i];
			if (c->model == role && c->shelf < DESK_SHELF_COUNT && !c->terminal
				&& (pass || c->shelf != DESK_SHELF_DEVELOPMENT)) {
				a = c;
			}
		}
	}
	return a;
}

const struct desk_app *desk_apps_find(const struct desk_app_list *list, const char *id)
{
	if (!list || !id) {
		return NULL;
	}
	for (int i = 0; i < list->n; i++) {
		if (strcmp(list->apps[i].id, id) == 0) {
			return &list->apps[i];
		}
	}
	/* and what is kept off the shelves, which a snapshot has and the
	 * empty list before the first scan does not */
	if (list != &g_empty) {
		const struct snapshot *s = (const struct snapshot *)list;
		for (int i = 0; i < s->n_off; i++) {
			if (strcmp(s->off[i].id, id) == 0) {
				return &s->off[i];
			}
		}
	}
	return NULL;
}

/* The last part of a reverse-DNS id -- org.kde.konsole is konsole -- and
 * without ".desktop". */
static void id_stem(const char *id, char *out, size_t n)
{
	str_copy(out, n, id);
	size_t len = strlen(out);
	if (len > 8 && strcmp(out + len - 8, ".desktop") == 0) {
		out[len - 8] = '\0';
	}
}

static bool same_word(const char *a, const char *b)
{
	return a && b && *a && *b && strcasecmp(a, b) == 0;
}

/* The endings of scripts' files. GTK names a window after the program it
 * is, and for a script that is the script's file: Mint's Software Manager
 * is mintinstall.py. */
static const char *const SCRIPT_ENDS[] = { "py", "pyw", "pl", "rb", "sh", "js", "lua", "tcl",
	"jar" };

bool desk_app_script_stem(const char *name, char *out, size_t n)
{
	const char *dot = name ? strrchr(name, '.') : NULL;
	if (!dot || dot == name || n == 0) {
		return false;
	}
	for (size_t i = 0; i < sizeof SCRIPT_ENDS / sizeof SCRIPT_ENDS[0]; i++) {
		if (strcasecmp(dot + 1, SCRIPT_ENDS[i]) == 0) {
			size_t len = (size_t)(dot - name);
			if (len >= n) {
				len = n - 1;
			}
			memcpy(out, name, len);
			out[len] = '\0';
			return true;
		}
	}
	return false;
}

/* The programs that run scripts: a window that calls itself python3 says
 * nothing of which program it is. */
static const char *const INTERPRETERS[] = { "python", "perl", "ruby", "node", "nodejs", "sh",
	"bash", "lua", "wish", "tclsh", "java", "gjs" };

bool desk_app_is_interpreter(const char *name)
{
	if (!name || !*name) {
		return false;
	}
	for (size_t i = 0; i < sizeof INTERPRETERS / sizeof INTERPRETERS[0]; i++) {
		size_t l = strlen(INTERPRETERS[i]);
		if (strncasecmp(name, INTERPRETERS[i], l) != 0) {
			continue;
		}
		/* python3, python3.13, lua5.4 */
		const char *p = name + l;
		while (*p && (isdigit((unsigned char)*p) || *p == '.')) {
			p++;
		}
		if (!*p) {
			return true;
		}
	}
	return false;
}

/* The program an Exec line runs, without its path or arguments. */
static void exec_prog(const char *exec, char *out, size_t n)
{
	const char *p = exec;
	while (*p == ' ') {
		p++;
	}
	/* env VAR=x prog ... runs prog */
	if (strncmp(p, "env ", 4) == 0) {
		p += 4;
		while (*p) {
			while (*p == ' ') {
				p++;
			}
			const char *w = p;
			while (*p && *p != ' ') {
				p++;
			}
			if (!memchr(w, '=', (size_t)(p - w))) {
				p = w;
				break;
			}
		}
	}
	/* python3 -O /usr/lib/thing/thing.py runs thing: past the
	 * interpreter and its options, the script */
	for (bool past = false;; past = true) {
		while (*p == ' ') {
			p++;
		}
		const char *end = p;
		while (*end && *end != ' ') {
			end++;
		}
		if (past && *p == '-' && *end) {
			p = end;
			continue;
		}
		const char *base = p;
		for (const char *q = p; q < end; q++) {
			if (*q == '/') {
				base = q + 1;
			}
		}
		size_t len = (size_t)(end - base);
		if (len >= n) {
			len = n - 1;
		}
		memcpy(out, base, len);
		out[len] = '\0';
		if (past || !desk_app_is_interpreter(out) || !*end) {
			break;
		}
		p = end;
	}
	char stem[128];
	if (desk_app_script_stem(out, stem, sizeof stem)) {
		str_copy(out, n, stem);
	}
}

const struct desk_app *desk_apps_match(const struct desk_app_list *list,
	const char *app_id, const char *wm_class, const char *wm_instance)
{
	if (!list) {
		return NULL;
	}
	/* what the window calls itself, and the same without a script's ending
	 * (mintinstall.py is mintinstall); an interpreter's name (python3) is
	 * no clue at all to which program it is */
	const char *given[3] = { app_id, wm_class, wm_instance };
	const char *keys[6] = { NULL };
	char stems[3][128];
	for (int k = 0; k < 3; k++) {
		if (desk_app_is_interpreter(given[k])) {
			continue;
		}
		keys[k] = given[k];
		if (desk_app_script_stem(given[k], stems[k], sizeof stems[k])) {
			keys[3 + k] = stems[k];
		}
	}
	char buf[160];
	/* the desktop file it names, exactly */
	for (int k = 0; k < 6; k += 3) {
		if (keys[k] && *keys[k]) {
			snprintf(buf, sizeof buf, "%s.desktop", keys[k]);
			const struct desk_app *a = desk_apps_find(list, buf);
			if (a) {
				return a;
			}
		}
	}
	for (int pass = 0; pass < 4; pass++) {
		for (int i = 0; i < list->n; i++) {
			const struct desk_app *a = &list->apps[i];
			char stem[128], last[128], prog[128];
			id_stem(a->id, stem, sizeof stem);
			const char *dot = strrchr(stem, '.');
			str_copy(last, sizeof last, dot ? dot + 1 : stem);
			exec_prog(a->exec, prog, sizeof prog);
			for (int k = 0; k < 6; k++) {
				const char *key = keys[k];
				if (!key || !*key) {
					continue;
				}
				bool hit = false;
				switch (pass) {
				case 0: /* the file name, whatever its case */
					hit = same_word(key, stem) || same_word(key, last);
					break;
				case 1: /* what the file says its windows are called */
					hit = same_word(key, a->wmclass);
					break;
				case 2: /* the program */
					hit = same_word(key, prog);
					break;
				default: /* and last, the name printed on it */
					hit = same_word(key, a->name);
					break;
				}
				if (hit) {
					return a;
				}
			}
		}
	}
	return NULL;
}

/* ---- running one ----------------------------------------------- */

int desk_app_build_argv(const struct desk_app *app, char **argv, int argv_max,
	char *pool, size_t pool_size)
{
	return app ? desk_exec_argv(app->exec, argv, argv_max, pool, pool_size) : -1;
}

int desk_exec_argv(const char *exec, char **argv, int argv_max, char *pool, size_t pool_size)
{
	if (!exec || argv_max < 2 || pool_size < strlen(exec) + 2) {
		return -1;
	}
	const char *p = exec;
	char *out = pool;
	int n = 0;
	while (*p) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (!*p) {
			break;
		}
		char *tok = out;
		bool any = false;
		while (*p && *p != ' ' && *p != '\t') {
			if (*p == '"') {
				p++;
				while (*p && *p != '"') {
					if (*p == '\\' && p[1]) {
						p++;
					}
					*out++ = *p++;
					any = true;
				}
				if (*p == '"') {
					p++;
				}
				any = true;
				continue;
			}
			if (*p == '%') {
				p++;
				if (*p == '%') {
					*out++ = '%';
					p++;
					any = true;
				} else if (*p) {
					/* a field code. We open applications with no
					 * arguments, so every one of them drops out. */
					p++;
				}
				continue;
			}
			*out++ = *p++;
			any = true;
		}
		*out++ = '\0';
		if (any && *tok && n < argv_max - 1) {
			argv[n++] = tok;
		} else {
			out = tok;
		}
	}
	argv[n] = NULL;
	return n ? n : -1;
}

static pid_t launch_argv(char **argv, const char *workdir);

/* How a terminal is told to run a program in itself: what goes between
 * its own command and the program's. Most take -e and the rest of the
 * line, LXTerminal and Konsole among them. Xfce's, MATE's and Terminator
 * take -x for that (their -e is one word, the whole command line in it);
 * a few take the program as it is, or after "--". */
static int term_exec_words(const char *id, const char *words[2])
{
	static const struct {
		const char *id;
		const char *w0, *w1;
	} TERMS[] = {
		{ "xfce4-terminal.desktop", "-x", NULL },
		{ "mate-terminal.desktop", "-x", NULL },
		{ "terminator.desktop", "-x", NULL },
		{ "foot.desktop", NULL, NULL },
		{ "kitty.desktop", NULL, NULL },
		{ "org.gnome.Console.desktop", "--", NULL },
		{ "org.gnome.Terminal.desktop", "--", NULL },
		{ "org.wezfurlong.wezterm.desktop", "start", "--" },
	};
	for (size_t i = 0; i < sizeof TERMS / sizeof TERMS[0]; i++) {
		if (!strcmp(id, TERMS[i].id)) {
			words[0] = TERMS[i].w0;
			words[1] = TERMS[i].w1;
			return TERMS[i].w1 ? 2 : TERMS[i].w0 ? 1 : 0;
		}
	}
	words[0] = "-e";
	words[1] = NULL;
	return 1;
}

pid_t desk_app_launch(const struct desk_app *app)
{
	if (!app) {
		return -1;
	}
	char pool[1024];
	char *argv[DESK_APP_ARGV_MAX];
	if (desk_app_build_argv(app, argv, DESK_APP_ARGV_MAX, pool, sizeof pool) < 0) {
		return -1;
	}
	/* Terminal=true: it has no window of its own, so it is run in the
	 * standard terminal's -- or it would start and never be seen */
	char tpool[1024];
	char *targv[DESK_APP_ARGV_MAX * 2 + 4];
	if (app->terminal) {
		const struct desk_app_list *l = desk_apps_acquire();
		const struct desk_app *term = desk_apps_for_std(l, DESK_STD_TERMINAL);
		int nt = term && !term->terminal ? desk_app_build_argv(term, targv,
			DESK_APP_ARGV_MAX, tpool, sizeof tpool) : -1;
		if (nt > 0) {
			const char *words[2];
			int nw = term_exec_words(term->id, words);
			for (int i = 0; i < nw; i++) {
				targv[nt++] = (char *)words[i];
			}
			for (int i = 0; argv[i] && nt < (int)(sizeof targv / sizeof *targv) - 1; i++) {
				targv[nt++] = argv[i];
			}
			targv[nt] = NULL;
		}
		desk_apps_release(l);
		if (nt > 0) {
			return launch_argv(targv, app->workdir);
		}
	}
	return launch_argv(argv, app->workdir);
}

pid_t desk_exec_launch(char **argv, const char *workdir)
{
	return launch_argv(argv, workdir);
}

void desk_close_from(int lowest)
{
#ifdef SYS_close_range
	if (syscall(SYS_close_range, (unsigned)lowest, ~0u, 0u) == 0) {
		return;
	}
#endif
	for (int fd = lowest; fd < 1024; fd++) {
		close(fd);
	}
}

static pid_t launch_argv(char **argv, const char *workdir)
{
	/* Forked twice: the program is not our child, so nothing has to wait
	 * for it and it leaves no zombie behind when it quits. The first child
	 * makes a session of its own and the program is started in it, so the
	 * session's number -- that child's pid -- is the program's, and stays
	 * on whatever it starts: what desk_app_launch returns. */
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid > 0) {
		int st = 0;
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
			;
		}
		return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? pid : -1;
	}
	setsid();
	sigset_t set;
	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	pid_t run = fork();
	if (run != 0) {
		_exit(run < 0 ? 1 : 0);
	}
	if (workdir && workdir[0]) {
		if (chdir(workdir) != 0) {
			/* the app said where it wants to run; if we cannot, run
			 * anyway rather than not at all */
		}
	}
	/* nothing of the desk's goes with it but the three it is given */
	desk_close_from(3);
	execvp(argv[0], argv);
	_exit(127);
}

/* ---- the picture, decoded once --------------------------------- */

struct icon_cache_entry {
	char file[512];
	cairo_surface_t *surf;
};
static struct icon_cache_entry *g_icons;
static int g_nicons, g_icons_cap;
static pthread_mutex_t g_icon_lock = PTHREAD_MUTEX_INITIALIZER;

cairo_surface_t *desk_app_icon(const struct desk_app *app)
{
	if (!app || !app->icon_file[0]) {
		return NULL;
	}
	pthread_mutex_lock(&g_icon_lock);
	for (int i = 0; i < g_nicons; i++) {
		if (strcmp(g_icons[i].file, app->icon_file) == 0) {
			cairo_surface_t *s = g_icons[i].surf;
			pthread_mutex_unlock(&g_icon_lock);
			return s;
		}
	}
	if (g_nicons == g_icons_cap) {
		int cap = g_icons_cap ? g_icons_cap * 2 : 64;
		struct icon_cache_entry *n = realloc(g_icons, (size_t)cap * sizeof *n);
		if (!n) {
			pthread_mutex_unlock(&g_icon_lock);
			return NULL;
		}
		g_icons = n;
		g_icons_cap = cap;
	}
	cairo_surface_t *s = cairo_image_surface_create_from_png(app->icon_file);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		s = NULL;
	}
	str_copy(g_icons[g_nicons].file, 512, app->icon_file);
	g_icons[g_nicons].surf = s;
	g_nicons++;
	pthread_mutex_unlock(&g_icon_lock);
	return s;
}
