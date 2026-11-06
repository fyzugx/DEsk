/* ================================================================
 * org.freedesktop.ScreenSaver: programs asking for the screen to stay lit.
 *
 * Only the asking is answered -- Inhibit, UnInhibit, SimulateUserActivity
 * -- which is all a player wants of it; the screen going dark is wm.c's.
 * Each hold keeps an sd_bus_track on the program that asked, so one that
 * crashes in the middle of a film, or forgets, lets go as it leaves the
 * bus. Polled on the compositor's event loop, as the post box is.
 * ================================================================ */

#include "saver.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

enum { SAVER_MAX = 64 };

struct hold {
	uint32_t cookie;
	sd_bus_track *track;   /* the program that asked, while it is on the bus */
	char app[64];          /* the name it asked under */
};

static sd_bus *g_bus;
static sd_bus_slot *g_slot[2];
static struct wl_event_source *g_src;
static struct hold g_hold[SAVER_MAX];  /* oldest first */
static int g_n;
static uint32_t g_next = 1;
static void (*g_changed)(void *);
static void (*g_awake)(void *);
static void *g_user;

static void changed(void)
{
	if (g_changed) {
		g_changed(g_user);
	}
}

/* cut between characters, never inside one */
static void copy_name(char *dst, size_t n, const char *src)
{
	size_t l = strlen(src);
	if (l >= n) {
		l = n - 1;
		while (l > 0 && ((unsigned char)src[l] & 0xC0) == 0x80) {
			l--;
		}
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static void drop(int i)
{
	sd_bus_track_unref(g_hold[i].track);
	memmove(&g_hold[i], &g_hold[i + 1], (size_t)(g_n - i - 1) * sizeof g_hold[0]);
	g_n--;
	changed();
}

/* The program that asked has left the bus without letting go. Its track
 * is let go of here, which is how sd-bus is told not to call again. */
static int on_gone(sd_bus_track *track, void *user)
{
	(void)user;
	for (int i = 0; i < g_n; i++) {
		if (g_hold[i].track == track) {
			drop(i);
			break;
		}
	}
	return 0;
}

static int on_inhibit(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	const char *app = NULL, *why = NULL;
	int r = sd_bus_message_read(m, "ss", &app, &why);
	if (r < 0) {
		return r;
	}
	if (g_n >= SAVER_MAX) {
		return sd_bus_error_set(err, SD_BUS_ERROR_LIMITS_EXCEEDED,
			"Too many holds on the screen already");
	}
	struct hold *h = &g_hold[g_n];
	memset(h, 0, sizeof *h);
	r = sd_bus_track_new(g_bus, &h->track, on_gone, NULL);
	if (r >= 0) {
		r = sd_bus_track_add_sender(h->track, m);
	}
	if (r < 0) {
		h->track = sd_bus_track_unref(h->track);
		return r;
	}
	h->cookie = g_next++;
	if (g_next == 0) {
		g_next = 1;   /* 0 is what some programs take for "none" */
	}
	copy_name(h->app, sizeof h->app, app && *app ? app : "a program");
	g_n++;
	changed();
	return sd_bus_reply_method_return(m, "u", h->cookie);
}

static int on_uninhibit(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	uint32_t cookie = 0;
	int r = sd_bus_message_read(m, "u", &cookie);
	if (r < 0) {
		return r;
	}
	/* only the program that asked can let go of its hold */
	const char *sender = sd_bus_message_get_sender(m);
	for (int i = 0; i < g_n; i++) {
		if (g_hold[i].cookie == cookie
			&& (!sender || sd_bus_track_contains(g_hold[i].track, sender))) {
			drop(i);
			break;
		}
	}
	return sd_bus_reply_method_return(m, "");
}

static int on_simulate(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	if (g_awake) {
		g_awake(g_user);
	}
	return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable SAVER_VTABLE[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Inhibit", "ss", "u", on_inhibit, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("UnInhibit", "u", "", on_uninhibit, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SimulateUserActivity", "", "", on_simulate, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

static int bus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	if (!g_bus) {
		return 0;
	}
	for (;;) {
		int r = sd_bus_process(g_bus, NULL);
		if (r <= 0) {
			break;
		}
	}
	sd_bus_flush(g_bus);
	return 0;
}

bool desk_saver_init(struct wl_event_loop *loop, void (*changed_cb)(void *user),
	void (*awake)(void *user), void *user)
{
	if (g_bus) {
		return true;
	}
	g_changed = changed_cb;
	g_awake = awake;
	g_user = user;
	if (sd_bus_open_user(&g_bus) < 0) {
		g_bus = NULL;
		return false;
	}
	/* programs ask at either path */
	static const char *const PATHS[2] = { "/org/freedesktop/ScreenSaver", "/ScreenSaver" };
	for (int i = 0; i < 2; i++) {
		if (sd_bus_add_object_vtable(g_bus, &g_slot[i], PATHS[i],
				"org.freedesktop.ScreenSaver", SAVER_VTABLE, NULL) < 0) {
			desk_saver_finish();
			return false;
		}
	}
	/* Not taken from a screen saver that has it already -- that one
	 * answers, then -- and one started later may have it. */
	if (sd_bus_request_name(g_bus, "org.freedesktop.ScreenSaver",
			SD_BUS_NAME_ALLOW_REPLACEMENT) < 0) {
		desk_saver_finish();
		return false;
	}
	int fd = sd_bus_get_fd(g_bus);
	if (fd < 0) {
		desk_saver_finish();
		return false;
	}
	g_src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, bus_ready, NULL);
	if (!g_src) {
		desk_saver_finish();
		return false;
	}
	return true;
}

void desk_saver_finish(void)
{
	if (g_src) {
		wl_event_source_remove(g_src);
		g_src = NULL;
	}
	for (int i = 0; i < g_n; i++) {
		sd_bus_track_unref(g_hold[i].track);
	}
	g_n = 0;
	for (int i = 0; i < 2; i++) {
		g_slot[i] = sd_bus_slot_unref(g_slot[i]);
	}
	if (g_bus) {
		sd_bus_flush_close_unref(g_bus);
		g_bus = NULL;
	}
}

int desk_saver_holds(char *who, size_t n)
{
	if (who && n) {
		copy_name(who, n, g_n ? g_hold[g_n - 1].app : "");
	}
	return g_n;
}
