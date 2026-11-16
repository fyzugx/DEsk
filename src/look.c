/* ================================================================
 * desk-look: looks for DEsk's drawer (looks.h, docs/LOOKS.md), made, and
 * what the desk will make of one said.
 *
 *   desk-look pick [--for NAME] [--lang CODE]
 *       a picture chosen from anywhere, made one of your own looks, for the program NAME
 *   desk-look add PICTURE [--name NAME] [--size CM] [--thick CM] [-o FOLDER]
 *       the same, from a picture you name: any GTK reads (PNG, JPEG, SVG ...)
 *   desk-look bake MODEL [-o FOLDER] [--name NAME] [--name-ja NAME ...] [--size CM]
 *                  [--thick CM] [--turn DEGREES] [--units m|cm|mm] [--samples N]
 *                  [--quick] [--author WHO] [--license WHICH]
 *       a model of a thing rendered as the desk's own looks are, in Blender
 *   desk-look check FOLDER
 *   desk-look where
 *
 * Made among your own ($XDG_DATA_HOME/desk/looks), a look is said on the
 * output as the drawer knows it, "own/NAME"; the desk, which runs pick for a
 * program in its drawer, gives it to that program. Made anywhere else (-o),
 * it is a folder to give to anyone: put in their own looks, or for everyone
 * in $XDG_DATA_DIRS/desk/looks, it is there.
 * ================================================================ */

#define _DEFAULT_SOURCE
#include "looks.h"
#include "i18n.h"

#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* read no bigger than this, and a picture with no size of its own (SVG) this big */
#define MOST_PX 1024
#define VECTOR_PX 512

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

/* The first word after the command that is not an option's. */
static const char *operand(int argc, char **argv)
{
	for (int i = 2; i < argc; i++) {
		if (argv[i][0] != '-') {
			return argv[i];
		}
		if (strcmp(argv[i], "--quick") && strcmp(argv[i], "--verbose")) {
			i++;
		}
	}
	return NULL;
}

/* A folder among your own for a look called name: what it is called made a folder's name, and
 * another if that is taken. */
static bool own_folder(const char *name, char *out, size_t n, const char **base)
{
	char dir[PATH_MAX - 128];
	desk_looks_own_dir(dir, sizeof dir);
	if (!dir[0] || g_mkdir_with_parents(dir, 0755) != 0) {
		return false;
	}
	char clean[96];
	const char *s = name && *name ? name : "look";
	size_t k = 0;
	while (*s == ' ' || *s == '.') {
		s++;
	}
	for (; *s && k + 1 < sizeof clean; s++) {
		clean[k++] = *s == '/' ? '-' : *s;
	}
	clean[k] = '\0';
	/* (not cut in the middle of a letter) */
	if (!g_utf8_validate(clean, -1, NULL)) {
		const char *end;
		g_utf8_validate(clean, -1, &end);
		clean[end - clean] = '\0';
	}
	if (!clean[0]) {
		snprintf(clean, sizeof clean, "look");
	}
	for (int i = 1; i < 1000; i++) {
		if (i == 1) {
			snprintf(out, n, "%s/%s", dir, clean);
		} else {
			snprintf(out, n, "%s/%s %d", dir, clean, i);
		}
		if (access(out, F_OK) != 0) {
			*base = strrchr(out, '/') + 1;
			return true;
		}
	}
	return false;
}

/* What it is called, from its file's name. */
static char *name_of_file(const char *path)
{
	char *base = g_path_get_basename(path);
	char *dot = strrchr(base, '.');
	if (dot && dot != base) {
		*dot = '\0';
	}
	return base;
}

/* Where it was made: as the drawer knows it if among your own, else the folder. */
static void say_made(const char *folder)
{
	char own[PATH_MAX];
	desk_looks_own_dir(own, sizeof own);
	size_t n = strlen(own);
	if (n && !strncmp(folder, own, n) && folder[n] == '/' && !strchr(folder + n + 1, '/')) {
		printf("own/%s\n", folder + n + 1);
	} else {
		printf("%s\n", folder);
	}
	fflush(stdout);
}

/* ---- from a picture ------------------------------------------------- */

static int add(const char *picture, const char *name, const char *size, const char *thick, const char *out)
{
	int w = 0, h = 0;
	GdkPixbufFormat *fmt = gdk_pixbuf_get_file_info(picture, &w, &h);
	if (!fmt) {
		fprintf(stderr, "desk-look: %s is not a picture this machine can read\n", picture);
		return 1;
	}
	GError *err = NULL;
	GdkPixbuf *pb;
	if (gdk_pixbuf_format_is_scalable(fmt) || w <= 0 || h <= 0) {
		pb = gdk_pixbuf_new_from_file_at_scale(picture, VECTOR_PX, VECTOR_PX, TRUE, &err);
	} else if (w > MOST_PX || h > MOST_PX) {
		pb = gdk_pixbuf_new_from_file_at_scale(picture, MOST_PX, MOST_PX, TRUE, &err);
	} else {
		pb = gdk_pixbuf_new_from_file(picture, &err);
	}
	if (!pb) {
		fprintf(stderr, "desk-look: %s would not read: %s\n", picture, err ? err->message : "");
		g_clear_error(&err);
		return 1;
	}
	/* (as a camera held it, and with somewhere for nothing to be) */
	GdkPixbuf *up = gdk_pixbuf_apply_embedded_orientation(pb);
	g_object_unref(pb);
	GdkPixbuf *clear = gdk_pixbuf_add_alpha(up, FALSE, 0, 0, 0);
	g_object_unref(up);
	char *called = name && *name ? g_strdup(name) : name_of_file(picture);
	char folder[PATH_MAX];
	const char *base = NULL;
	if (out) {
		snprintf(folder, sizeof folder, "%s", out);
	} else if (!own_folder(called, folder, sizeof folder, &base)) {
		fprintf(stderr, "desk-look: there is nowhere to keep your own looks\n");
		g_free(called);
		g_object_unref(clear);
		return 1;
	}
	char png[PATH_MAX + 16], ini[PATH_MAX + 16];
	snprintf(png, sizeof png, "%s/look.png", folder);
	snprintf(ini, sizeof ini, "%s/look.ini", folder);
	bool ok = g_mkdir_with_parents(folder, 0755) == 0 && gdk_pixbuf_save(clear, png, "png", &err, NULL);
	g_object_unref(clear);
	if (ok) {
		char *from = g_path_get_basename(picture);
		GString *s = g_string_new("[Look]\n");
		g_string_append_printf(s, "Name=%s\n", called);
		if (size) {
			g_string_append_printf(s, "Size=%s\n", size);
		}
		if (thick) {
			g_string_append_printf(s, "Thick=%s\n", thick);
		}
		g_string_append_printf(s, "Comment=Made by desk-look from %s\n", from);
		ok = g_file_set_contents(ini, s->str, (gssize)s->len, &err);
		g_string_free(s, TRUE);
		g_free(from);
	}
	g_free(called);
	if (!ok) {
		fprintf(stderr, "desk-look: could not make %s: %s\n", folder, err ? err->message : g_strerror(errno));
		g_clear_error(&err);
		return 1;
	}
	say_made(folder);
	return 0;
}

/* ---- chosen from anywhere -------------------------------------------- */

static void on_preview(GtkFileChooser *chooser, gpointer data)
{
	GtkWidget *img = data;
	char *path = gtk_file_chooser_get_preview_filename(chooser);
	GdkPixbuf *pb = path ? gdk_pixbuf_new_from_file_at_scale(path, 180, 180, TRUE, NULL) : NULL;
	gtk_image_set_from_pixbuf(GTK_IMAGE(img), pb);
	gtk_file_chooser_set_preview_widget_active(chooser, pb != NULL);
	if (pb) {
		g_object_unref(pb);
	}
	g_free(path);
}

static void shortcut(GtkFileChooser *chooser, const char *dir)
{
	if (dir && g_file_test(dir, G_FILE_TEST_IS_DIR)) {
		gtk_file_chooser_add_shortcut_folder(chooser, dir, NULL);
	}
}

static int pick(const char *name, const char *lang)
{
	desk_lang_set(lang ? desk_lang_parse(lang) : desk_lang_from_locale());
	char title[256];
	snprintf(title, sizeof title, TR("A picture for %s, to lie in the drawer"), name && *name ? name : "DEsk");
	GtkWidget *dlg = gtk_file_chooser_dialog_new(title, NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
		TR("Cancel"), GTK_RESPONSE_CANCEL, TR("Choose"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_icon_name(GTK_WINDOW(dlg), "desk");
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(dlg);
	GtkFileFilter *pictures = gtk_file_filter_new();
	gtk_file_filter_set_name(pictures, TR("Pictures"));
	gtk_file_filter_add_pixbuf_formats(pictures);
	gtk_file_chooser_add_filter(chooser, pictures);
	/* where pictures are, your own looks, and every icon there is */
	const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	gtk_file_chooser_set_current_folder(chooser, pics && g_file_test(pics, G_FILE_TEST_IS_DIR) ? pics
		: g_get_home_dir());
	char own[PATH_MAX];
	desk_looks_own_dir(own, sizeof own);
	shortcut(chooser, pics);
	shortcut(chooser, own);
	shortcut(chooser, "/usr/share/icons");
	shortcut(chooser, "/usr/share/pixmaps");
	GtkWidget *img = gtk_image_new();
	gtk_file_chooser_set_preview_widget(chooser, img);
	g_signal_connect(dlg, "update-preview", G_CALLBACK(on_preview), img);
	int rc = 1;
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *path = gtk_file_chooser_get_filename(chooser);
		if (path) {
			gtk_widget_hide(dlg);
			rc = add(path, NULL, NULL, NULL, NULL);
			g_free(path);
		}
	}
	gtk_widget_destroy(dlg);
	while (gtk_events_pending()) {
		gtk_main_iteration();
	}
	return rc;
}

/* ---- from a model ------------------------------------------------- */

/* The first of these that is there. */
static bool first_there(char *out, size_t n, const char *const *places, int count)
{
	for (int i = 0; i < count; i++) {
		if (places[i] && access(places[i], R_OK) == 0) {
			snprintf(out, n, "%s", places[i]);
			return true;
		}
	}
	return false;
}

/* What renders it: art/looks/bake.py, as built beside this or installed with it. */
static bool find_bake(char *out, size_t n)
{
	char exe[PATH_MAX] = "";
	ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[k > 0 ? k : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash) {
		*slash = '\0';
	}
	const char *art = getenv("DESK_ART_DIR");
	char a[PATH_MAX + 32], b[PATH_MAX + 64], c[PATH_MAX + 64], d[PATH_MAX + 64];
	snprintf(a, sizeof a, "%s/looks/bake.py", art && *art ? art : "/nonexistent");
	snprintf(b, sizeof b, "%s/../art/looks/bake.py", exe);
	snprintf(c, sizeof c, "%s/../share/desk/looks/bake.py", exe);
#ifdef DESK_DATADIR
	snprintf(d, sizeof d, "%s/desk/looks/bake.py", DESK_DATADIR);
#else
	d[0] = '\0';
#endif
	const char *const places[] = { a, b, c, d[0] ? d : NULL };
	return first_there(out, n, places, 4);
}

static int bake(int argc, char **argv)
{
	const char *model = operand(argc, argv);
	if (!model || access(model, R_OK) != 0) {
		fprintf(stderr, "desk-look bake: which model? (%s)\n", model ? g_strerror(errno) : "none given");
		return 2;
	}
	const char *blender = getenv("DESK_BLENDER");
	char *found = blender && *blender ? g_strdup(blender) : g_find_program_in_path("blender");
	if (!found) {
		fprintf(stderr, "desk-look bake: Blender renders it, and it is not here: put it on the PATH, or say "
			"where it is with DESK_BLENDER (https://www.blender.org)\n");
		return 1;
	}
	char script[PATH_MAX];
	if (!find_bake(script, sizeof script)) {
		fprintf(stderr, "desk-look bake: bake.py is not where DEsk was installed\n");
		g_free(found);
		return 1;
	}
	char *model_abs = realpath(model, NULL);
	char *called = opt(argc, argv, "--name") ? g_strdup(opt(argc, argv, "--name")) : name_of_file(model);
	char folder[PATH_MAX];
	const char *base = NULL;
	if (opt(argc, argv, "-o")) {
		snprintf(folder, sizeof folder, "%s", opt(argc, argv, "-o"));
	} else if (!own_folder(called, folder, sizeof folder, &base)) {
		fprintf(stderr, "desk-look: there is nowhere to keep your own looks\n");
		return 1;
	}
	GPtrArray *av = g_ptr_array_new();
	g_ptr_array_add(av, found);
	g_ptr_array_add(av, (gpointer)"-b");
	g_ptr_array_add(av, (gpointer)"--factory-startup");
	g_ptr_array_add(av, (gpointer)"-P");
	g_ptr_array_add(av, script);
	g_ptr_array_add(av, (gpointer)"--");
	g_ptr_array_add(av, folder);
	g_ptr_array_add(av, model_abs ? model_abs : (char *)model);
	g_ptr_array_add(av, (gpointer)"--name");
	g_ptr_array_add(av, called);
	static const char *const PASS[] = { "--size", "--thick", "--turn", "--units", "--samples", "--author",
		"--license", "--order", "--name-ja", "--name-zh", "--name-ko", "--name-es", "--name-fr", "--name-de" };
	for (size_t i = 0; i < G_N_ELEMENTS(PASS); i++) {
		const char *v = opt(argc, argv, PASS[i]);
		if (v) {
			g_ptr_array_add(av, (gpointer)PASS[i]);
			g_ptr_array_add(av, (gpointer)v);
		}
	}
	if (flag(argc, argv, "--quick")) {
		g_ptr_array_add(av, (gpointer)"--quick");
	}
	g_ptr_array_add(av, NULL);
	fprintf(stderr, "desk-look: rendering %s in Blender (a minute or so)\n", model);
	int rc = 1;
	pid_t pid = fork();
	if (pid == 0) {
		/* (what Blender says is its own: the folder made is what is said here) */
		int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
		if (null >= 0 && !flag(argc, argv, "--verbose")) {
			dup2(null, STDOUT_FILENO);
		} else {
			dup2(STDERR_FILENO, STDOUT_FILENO);
		}
		execv((char *)av->pdata[0], (char **)av->pdata);
		_exit(127);
	}
	int status = 0;
	while (pid > 0 && waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		;
	}
	char png[PATH_MAX + 16];
	snprintf(png, sizeof png, "%s/look.png", folder);
	if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && access(png, R_OK) == 0) {
		say_made(folder);
		rc = 0;
	} else {
		fprintf(stderr, "desk-look bake: Blender did not make it (run again with --verbose to see why)\n");
	}
	g_ptr_array_free(av, TRUE);
	g_free(called);
	free(model_abs);
	return rc;
}

/* ---- what the desk will make of it ------------------------------------- */

static int check(const char *folder)
{
	char path[PATH_MAX], name[96];
	double along, thick;
	int order;
	if (!folder || !desk_look_folder(folder, desk_lang_code(desk_lang_from_locale()), path, sizeof path, name,
			sizeof name, &along, &thick, &order)) {
		fprintf(stderr, "desk-look check: %s has no look.png in it\n", folder ? folder : "(which folder?)");
		return 1;
	}
	int w = 0, h = 0;
	desk_png_size(path, &w, &h);
	printf("%s\n  called      %s\n  picture     %d by %d pixels\n  lies        %.1f cm along its longest side"
		" (%.2f of the room's units)\n  stands      %.1f cm, for its shadow\n  among them  %d\n", folder, name, w,
		h, along * DESK_LOOK_CM_PER_UNIT, along, thick * DESK_LOOK_CM_PER_UNIT, order);
	GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, NULL);
	int warn = 0;
	if (!pb) {
		printf("  ! look.png would not read\n");
		return 1;
	}
	if (!gdk_pixbuf_get_has_alpha(pb)) {
		printf("  ! it has no clear part: it lies as a rectangle\n");
		warn++;
	} else {
		/* its corners clear, or it is a rectangle lying there */
		const guchar *px = gdk_pixbuf_get_pixels(pb);
		int stride = gdk_pixbuf_get_rowstride(pb), pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb);
		int corners[4][2] = { { 0, 0 }, { pw - 1, 0 }, { 0, ph - 1 }, { pw - 1, ph - 1 } };
		int solid = 0;
		for (int i = 0; i < 4; i++) {
			solid += px[corners[i][1] * stride + corners[i][0] * 4 + 3] > 40;
		}
		if (solid) {
			printf("  ! %d of its corners are not clear: what is round the thing should be\n", solid);
			warn++;
		}
	}
	if (w > MOST_PX || h > MOST_PX) {
		printf("  ! it is read at %d pixels at most: no more is ever seen\n", MOST_PX);
		warn++;
	}
	g_object_unref(pb);
	printf("  %s\n", warn ? "it will lie in the drawer, as said" : "it will lie in the drawer as it is");
	return 0;
}

static int usage(void)
{
	fprintf(stderr,
		"desk-look: looks for DEsk's drawer (docs/LOOKS.md)\n"
		"  desk-look pick [--for NAME] [--lang CODE]      a picture chosen, made one of your own\n"
		"  desk-look add PICTURE [--name NAME] [--size CM] [--thick CM] [-o FOLDER]\n"
		"  desk-look bake MODEL [-o FOLDER] [--name NAME] [--name-ja NAME ...] [--size CM] [--thick CM]\n"
		"                 [--turn DEGREES] [--units m|cm|mm] [--samples N] [--quick] [--author WHO]\n"
		"                 [--license WHICH] [--verbose]\n"
		"  desk-look check FOLDER                         what the desk will make of it\n"
		"  desk-look where                                where your own looks are\n");
	return 2;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return usage();
	}
	const char *cmd = argv[1];
	if (!strcmp(cmd, "pick")) {
		/* (asked before GTK has the words: it takes some, --name among them) */
		const char *name = opt(argc, argv, "--for"), *lang = opt(argc, argv, "--lang");
		if (!gtk_init_check(&argc, &argv)) {
			fprintf(stderr, "desk-look pick: no screen to choose on\n");
			return 1;
		}
		return pick(name, lang);
	}
	if (!strcmp(cmd, "add")) {
		const char *picture = operand(argc, argv);
		return picture ? add(picture, opt(argc, argv, "--name"), opt(argc, argv, "--size"),
			opt(argc, argv, "--thick"), opt(argc, argv, "-o")) : usage();
	}
	if (!strcmp(cmd, "bake")) {
		return bake(argc, argv);
	}
	if (!strcmp(cmd, "check")) {
		return check(operand(argc, argv));
	}
	if (!strcmp(cmd, "where")) {
		char own[PATH_MAX];
		desk_looks_own_dir(own, sizeof own);
		printf("%s\n", own);
		return 0;
	}
	return usage();
}
