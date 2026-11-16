/* ================================================================
 * desk-mat: pictures for the clear mat on DEsk's desk (shell.conf desk_mat =
 * clear, docs/CONFIGURATION.md).
 *
 *   desk-mat pick [--whole] [--lang CODE]
 *       pictures chosen from anywhere, made prints of and laid under the mat
 *       (made a clear one, its pictures laid where you like, if it is not);
 *       --whole, one picture to lie all over it
 *   desk-mat add PICTURE... [--width POINTS]
 *       the same, from pictures named
 *   desk-mat whole PICTURE
 *       that picture all over the mat
 *   desk-mat list
 *   desk-mat where
 *
 * A print is the picture made a PNG no larger than the desk reads it (2048
 * across), turned as its camera was held, in $XDG_DATA_HOME/desk/mat: so the
 * picture can go from where it was and the print stays, and the desk reads
 * only its own small PNGs. Where the prints lie is the desk's file,
 * $XDG_CONFIG_HOME/desk/desk-mat; a print made goes on top, near the middle
 * of the mat, and the desk, which reads that file again when it changes,
 * lays it there. To move one, lift the mat by a corner.
 * ================================================================ */

#define _DEFAULT_SOURCE
#include "i18n.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* a print is no larger than this across, which is all the desk reads of one */
#define PRINT_PX 2048
/* and lies this long on its longer side, in points, when it is first laid */
#define PRINT_LONG_PT 280.0
/* no more than the desk keeps under it */
#define PAPERS_MOST 40

static const char *opt(int argc, char **argv, const char *name)
{
	for (int i = 2; i + 1 < argc; i++) {
		if (!strcmp(argv[i], name)) {
			return argv[i + 1];
		}
	}
	return NULL;
}

static bool flag(int argc, char **argv, const char *name)
{
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], name)) {
			return true;
		}
	}
	return false;
}

/* ---- where things are --------------------------------------------------- */

static void prints_dir(char *out, size_t n)
{
	const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (data && *data) {
		snprintf(out, n, "%s/desk/mat", data);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/share/desk/mat", home);
	} else {
		out[0] = '\0';
	}
}

/* $XDG_CONFIG_HOME/desk/<file> */
static void config_file(const char *file, char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/%s", cfg, file);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/%s", home, file);
	} else {
		out[0] = '\0';
	}
}

/* Written whole beside it and then over it, so the desk reading it never
 * sees half of it. */
static bool write_whole(const char *path, const char *text)
{
	char *dir = g_path_get_dirname(path);
	bool ok = g_mkdir_with_parents(dir, 0700) == 0;
	g_free(dir);
	GError *err = NULL;
	ok = ok && g_file_set_contents(path, text, -1, &err);
	if (!ok) {
		fprintf(stderr, "desk-mat: could not write %s: %s\n", path, err ? err->message : g_strerror(errno));
		g_clear_error(&err);
	}
	return ok;
}

/* ---- a print ------------------------------------------------------------- */

/* A print of picture among the prints, its name in name: false, and why
 * said, if it could not be made. */
static bool print_of(const char *picture, char *name, size_t n, int *pw, int *ph)
{
	int w = 0, h = 0;
	GdkPixbufFormat *fmt = gdk_pixbuf_get_file_info(picture, &w, &h);
	if (!fmt) {
		fprintf(stderr, "desk-mat: %s is not a picture this machine can read\n", picture);
		return false;
	}
	GError *err = NULL;
	GdkPixbuf *pb;
	if (gdk_pixbuf_format_is_scalable(fmt) || w <= 0 || h <= 0 || w > PRINT_PX || h > PRINT_PX) {
		pb = gdk_pixbuf_new_from_file_at_scale(picture, PRINT_PX, PRINT_PX, TRUE, &err);
	} else {
		pb = gdk_pixbuf_new_from_file(picture, &err);
	}
	if (!pb) {
		fprintf(stderr, "desk-mat: %s would not read: %s\n", picture, err ? err->message : "");
		g_clear_error(&err);
		return false;
	}
	/* (as the camera was held) */
	GdkPixbuf *up = gdk_pixbuf_apply_embedded_orientation(pb);
	g_object_unref(pb);
	char dir[PATH_MAX - 128];
	prints_dir(dir, sizeof dir);
	if (!dir[0] || g_mkdir_with_parents(dir, 0700) != 0) {
		fprintf(stderr, "desk-mat: there is nowhere to keep prints\n");
		g_object_unref(up);
		return false;
	}
	/* its name: the picture's, in plain letters, and a mark of its own */
	char *base = g_path_get_basename(picture);
	char *dot = strrchr(base, '.');
	if (dot && dot != base) {
		*dot = '\0';
	}
	char clean[49];
	size_t k = 0;
	for (const char *s = base; *s && k + 1 < sizeof clean; s++) {
		unsigned char c = (unsigned char)*s;
		clean[k++] = g_ascii_isalnum(c) || c == '-' || c == '_' ? (char)c : '-';
	}
	clean[k] = '\0';
	g_free(base);
	if (!clean[0]) {
		snprintf(clean, sizeof clean, "picture");
	}
	if (clean[0] == '-') {
		clean[0] = 'p';   /* (not a word a command would take for an option) */
	}
	char path[PATH_MAX];
	bool ok = false;
	for (int i = 0; i < 100 && !ok; i++) {
		unsigned mark = g_str_hash(picture) ^ (unsigned)g_get_real_time() ^ (unsigned)(i * 2654435761u);
		snprintf(name, n, "%s-%06x.png", clean, mark & 0xffffff);
		snprintf(path, sizeof path, "%s/%s", dir, name);
		ok = access(path, F_OK) != 0;
	}
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.part", path);
	ok = ok && gdk_pixbuf_save(up, tmp, "png", &err, NULL) && rename(tmp, path) == 0;
	if (!ok) {
		fprintf(stderr, "desk-mat: could not make a print of %s: %s\n", picture,
			err ? err->message : g_strerror(errno));
		g_clear_error(&err);
		unlink(tmp);
	}
	*pw = gdk_pixbuf_get_width(up);
	*ph = gdk_pixbuf_get_height(up);
	g_object_unref(up);
	return ok;
}

/* ---- the desk's file of where they lie ----------------------------------- */

/* The prints made, laid on top of what is there, near the middle and a
 * little fanned, the longer side long_pt (or width, across). */
static bool lay(char names[][96], const int *pw, const int *ph, int count, double width)
{
	char path[PATH_MAX];
	config_file("desk-mat", path, sizeof path);
	if (!path[0]) {
		return false;
	}
	char *had = NULL;
	g_file_get_contents(path, &had, NULL, NULL);
	GString *s = g_string_new(NULL);
	int there = 0;
	if (had && *had) {
		g_string_append(s, had);
		if (s->str[s->len - 1] != '\n') {
			g_string_append_c(s, '\n');
		}
		for (char *l = had; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
			while (*l == ' ' || *l == '\t') {
				l++;
			}
			there += !strncmp(l, "paper", 5);
		}
	} else {
		g_string_append(s, "# The pictures under the clear mat, the bottom one first (desk-mat, "
			"docs/CONFIGURATION.md):\n"
			"# paper = PRINT X Y WIDTH TURN -- its middle X, Y points from the mat's, WIDTH points\n"
			"# across, turned TURN degrees clockwise. The prints are in $XDG_DATA_HOME/desk/mat.\n");
	}
	g_free(had);
	int laid = 0;
	for (int i = 0; i < count && there + laid < PAPERS_MOST; i++) {
		double aspect = pw[i] > 0 && ph[i] > 0 ? (double)pw[i] / ph[i] : 1.5;
		double w = width > 0.0 ? width : aspect >= 1.0 ? PRINT_LONG_PT : PRINT_LONG_PT * aspect;
		/* fanned out from the middle, each a little turned, as a handful dropped */
		double at = i - (count - 1) * 0.5;
		unsigned hsh = g_str_hash(names[i]);
		double turn = ((int)(hsh % 1400) - 700) / 100.0;
		double x = at * 46.0 + ((int)(hsh >> 12 & 63) - 32), y = at * 20.0 - 30.0 + ((int)(hsh >> 18 & 31) - 16);
		g_string_append_printf(s, "paper = %s %.1f %.1f %.1f %.1f\n", names[i], x, y, w, turn);
		printf("laid %s\n", names[i]);
		laid++;
	}
	if (there + laid >= PAPERS_MOST && laid < count) {
		fprintf(stderr, "desk-mat: there are %d under the mat already, all it holds\n", PAPERS_MOST);
	}
	fflush(stdout);
	bool ok = write_whole(path, s->str);
	g_string_free(s, TRUE);
	return ok && laid > 0;
}

/* What shell.conf says key is, into out ("" if nothing). */
static void conf_get(const char *key, char *out, size_t n)
{
	char path[PATH_MAX], *had = NULL;
	out[0] = '\0';
	config_file("shell.conf", path, sizeof path);
	if (!path[0] || !g_file_get_contents(path, &had, NULL, NULL)) {
		return;
	}
	size_t kl = strlen(key);
	for (char *l = had; l && *l;) {
		char *end = strchr(l, '\n');
		if (end) {
			*end = '\0';
		}
		char *p = l;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		char *q = p + kl;
		while (*q == ' ' || *q == '\t') {
			q++;
		}
		if (!strncmp(p, key, kl) && *q == '=') {
			char *v = g_strstrip(g_strdup(q + 1));
			snprintf(out, n, "%s", v);
			g_free(v);
		}
		l = end ? end + 1 : NULL;
	}
	g_free(had);
}

/* One key of shell.conf set to value, the rest of it as it was. */
static bool conf_set(const char *key, const char *value)
{
	char path[PATH_MAX];
	config_file("shell.conf", path, sizeof path);
	if (!path[0]) {
		return false;
	}
	char *had = NULL;
	g_file_get_contents(path, &had, NULL, NULL);
	GString *s = g_string_new(NULL);
	bool set = false;
	size_t kl = strlen(key);
	for (char *l = had; l && *l;) {
		char *end = strchr(l, '\n');
		size_t len = end ? (size_t)(end - l) : strlen(l);
		const char *p = l;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		const char *q = p + kl;
		while (*q == ' ' || *q == '\t') {
			q++;
		}
		if (!strncmp(p, key, kl) && *q == '=' && !set) {
			g_string_append_printf(s, "%s = %s\n", key, value);
			set = true;
		} else if (strncmp(p, key, kl) || *q != '=') {
			g_string_append_len(s, l, (gssize)len);
			g_string_append_c(s, '\n');
		}
		l = end ? end + 1 : NULL;
	}
	if (!set) {
		g_string_append_printf(s, "%s = %s\n", key, value);
	}
	g_free(had);
	bool ok = write_whole(path, s->str);
	g_string_free(s, TRUE);
	return ok;
}

static int add(char **pictures, int count, double width)
{
	char (*names)[96] = g_malloc0_n(count > 0 ? (gsize)count : 1, sizeof *names);
	int *pw = g_new0(int, count > 0 ? count : 1), *ph = g_new0(int, count > 0 ? count : 1);
	int made = 0;
	for (int i = 0; i < count; i++) {
		if (print_of(pictures[i], names[made], sizeof names[made], &pw[made], &ph[made])) {
			made++;
		}
	}
	/* under a clear mat, laid where you like: made so, if the mat is not
	 * (before they are laid, so the desk has the sheet when they come) */
	char mat[32], under[32];
	conf_get("desk_mat", mat, sizeof mat);
	conf_get("desk_mat_under", under, sizeof under);
	bool ok = made > 0;
	if (ok && g_ascii_strcasecmp(mat, "clear")) {
		ok = conf_set("desk_mat", "clear");
		printf("the mat made clear (it was %s)\n", mat[0] ? mat : "none");
	}
	if (ok && !g_ascii_strcasecmp(under, "whole")) {
		ok = conf_set("desk_mat_under", "free");
		printf("the pictures under it laid where you like (it had one all over it)\n");
	}
	ok = ok && lay(names, pw, ph, made, width);
	g_free(names);
	g_free(pw);
	g_free(ph);
	return ok && made == count ? 0 : 1;
}

/* That picture all over a clear mat, or a photo mat's if that is the mat.
 * Not a print: the desk reads it where it is (desk_mat_photo). */
static int whole(const char *picture)
{
	char *abs = g_canonicalize_filename(picture, NULL);
	int w = 0, h = 0;
	bool ok = gdk_pixbuf_get_file_info(abs, &w, &h) != NULL;
	if (!ok) {
		fprintf(stderr, "desk-mat: %s is not a picture this machine can read\n", picture);
	}
	char mat[32];
	conf_get("desk_mat", mat, sizeof mat);
	bool photo_mat = !g_ascii_strcasecmp(mat, "photo");
	ok = ok && conf_set("desk_mat_photo", abs)
		&& (photo_mat || (conf_set("desk_mat_under", "whole") && conf_set("desk_mat", "clear")));
	if (ok) {
		printf("whole %s\n", abs);
		fflush(stdout);
	}
	g_free(abs);
	return ok ? 0 : 1;
}

/* ---- chosen from anywhere ------------------------------------------------ */

static void on_preview(GtkFileChooser *chooser, gpointer data)
{
	GtkWidget *img = data;
	char *path = gtk_file_chooser_get_preview_filename(chooser);
	GdkPixbuf *pb = path ? gdk_pixbuf_new_from_file_at_scale(path, 220, 220, TRUE, NULL) : NULL;
	gtk_image_set_from_pixbuf(GTK_IMAGE(img), pb);
	gtk_file_chooser_set_preview_widget_active(chooser, pb != NULL);
	if (pb) {
		g_object_unref(pb);
	}
	g_free(path);
}

static int pick(bool all_over, const char *lang)
{
	desk_lang_set(lang ? desk_lang_parse(lang) : desk_lang_from_locale());
	GtkWidget *dlg = gtk_file_chooser_dialog_new(all_over ? TR("A picture to lie under all of the mat")
		: TR("Pictures to lay under the mat"), NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
		TR("Cancel"), GTK_RESPONSE_CANCEL, TR("Choose"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_icon_name(GTK_WINDOW(dlg), "desk");
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(dlg);
	gtk_file_chooser_set_select_multiple(chooser, !all_over);
	GtkFileFilter *pictures = gtk_file_filter_new();
	gtk_file_filter_set_name(pictures, TR("Pictures"));
	gtk_file_filter_add_pixbuf_formats(pictures);
	gtk_file_chooser_add_filter(chooser, pictures);
	const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	gtk_file_chooser_set_current_folder(chooser, pics && g_file_test(pics, G_FILE_TEST_IS_DIR) ? pics
		: g_get_home_dir());
	if (pics && g_file_test(pics, G_FILE_TEST_IS_DIR)) {
		gtk_file_chooser_add_shortcut_folder(chooser, pics, NULL);
	}
	GtkWidget *img = gtk_image_new();
	gtk_file_chooser_set_preview_widget(chooser, img);
	g_signal_connect(dlg, "update-preview", G_CALLBACK(on_preview), img);
	int rc = 1;
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		GSList *files = gtk_file_chooser_get_filenames(chooser);
		gtk_widget_hide(dlg);
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
		int count = (int)g_slist_length(files);
		char **paths = g_new0(char *, count + 1);
		int i = 0;
		for (GSList *l = files; l; l = l->next) {
			paths[i++] = l->data;
		}
		rc = count == 0 ? 1 : all_over ? whole(paths[0]) : add(paths, count, 0.0);
		g_free(paths);
		g_slist_free_full(files, g_free);
	}
	gtk_widget_destroy(dlg);
	while (gtk_events_pending()) {
		gtk_main_iteration();
	}
	return rc;
}

/* ---- what is there --------------------------------------------------------- */

static int list(void)
{
	char path[PATH_MAX];
	config_file("desk-mat", path, sizeof path);
	char *had = NULL;
	if (!path[0] || !g_file_get_contents(path, &had, NULL, NULL)) {
		return 0;
	}
	for (char *l = had; l && *l;) {
		char *end = strchr(l, '\n');
		if (end) {
			*end = '\0';
		}
		char name[96];
		double x, y, w, t;
		if (sscanf(l, " paper = %95s %lf %lf %lf %lf", name, &x, &y, &w, &t) == 5) {
			printf("%s  at %.0f, %.0f  %.0f pt across  turned %.1f\n", name, x, y, w, t);
		}
		l = end ? end + 1 : NULL;
	}
	g_free(had);
	return 0;
}

static int usage(void)
{
	fprintf(stderr,
		"desk-mat: pictures for the clear mat on DEsk's desk\n"
		"  desk-mat pick [--whole] [--lang CODE]       pictures chosen, laid under it (--whole: one, all over it)\n"
		"  desk-mat add PICTURE... [--width POINTS]    pictures named, laid under it\n"
		"  desk-mat whole PICTURE                      that picture all over it\n"
		"  desk-mat list                               what lies under it, and where\n"
		"  desk-mat where                              where the prints and the file of where they lie are\n");
	return 2;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return usage();
	}
	const char *cmd = argv[1];
	if (!strcmp(cmd, "pick")) {
		/* (asked before GTK has the words) */
		bool all_over = flag(argc, argv, "--whole");
		const char *lang = opt(argc, argv, "--lang");
		if (!gtk_init_check(&argc, &argv)) {
			fprintf(stderr, "desk-mat pick: no screen to choose on\n");
			return 1;
		}
		return pick(all_over, lang);
	}
	if (!strcmp(cmd, "add")) {
		const char *width = opt(argc, argv, "--width");
		double w = width ? strtod(width, NULL) : 0.0;
		if (width && !(isfinite(w) && w >= 60.0 && w <= 900.0)) {
			fprintf(stderr, "desk-mat: --width is from 60 to 900 points\n");
			return 2;
		}
		char **pictures = g_new0(char *, argc);
		int count = 0;
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--width")) {
				i++;
			} else {
				pictures[count++] = argv[i];
			}
		}
		int rc = count ? add(pictures, count, w) : usage();
		g_free(pictures);
		return rc;
	}
	if (!strcmp(cmd, "whole")) {
		return argc >= 3 ? whole(argv[2]) : usage();
	}
	if (!strcmp(cmd, "list")) {
		return list();
	}
	if (!strcmp(cmd, "where")) {
		char dir[PATH_MAX], file[PATH_MAX];
		prints_dir(dir, sizeof dir);
		config_file("desk-mat", file, sizeof file);
		printf("%s\n%s\n", dir, file);
		return 0;
	}
	return usage();
}
