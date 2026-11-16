/* The way the machine is held (2026-11-12, the owner, after trying a Surface Pro: 後は自動回転
 * ぐらいかな): iio-sensor-proxy reads the accelerometer and says which edge of the screen is up --
 * normal, bottom-up, left-up, right-up -- as net.hadess.SensorProxy's AccelerometerOrientation,
 * on the system bus, to whoever has claimed it. Said here as the output transform that turns the
 * picture back upright, the one GNOME uses for each (left-up: a quarter turn anticlockwise). */

#include "orient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/util/log.h>

#ifdef DESK_HAVE_POST
#include <systemd/sd-bus.h>

#define SP_NAME "net.hadess.SensorProxy"
#define SP_PATH "/net/hadess/SensorProxy"

static sd_bus *g_bus;
static void (*g_said)(void *data, int transform);
static void *g_data;
static int g_was = -1;
static bool g_claimed;

static int transform_of(const char *o)
{
	if (!strcmp(o, "left-up")) {
		return WL_OUTPUT_TRANSFORM_90;
	}
	if (!strcmp(o, "bottom-up")) {
		return WL_OUTPUT_TRANSFORM_180;
	}
	if (!strcmp(o, "right-up")) {
		return WL_OUTPUT_TRANSFORM_270;
	}
	if (!strcmp(o, "normal")) {
		return WL_OUTPUT_TRANSFORM_NORMAL;
	}
	return -1;   /* "undefined": flat on a table, say -- left as it was */
}

static void orientation_now(void)
{
	char *o = NULL;
	if (sd_bus_get_property_string(g_bus, SP_NAME, SP_PATH, SP_NAME,
			"AccelerometerOrientation", NULL, &o) < 0 || !o) {
		return;
	}
	int t = transform_of(o);
	if (t >= 0 && t != g_was) {
		wlr_log(WLR_INFO, "orientation: %s", o);
		g_was = t;
		g_said(g_data, t);
	}
	free(o);
}

static int on_changed(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	(void)m;
	(void)userdata;
	(void)err;
	orientation_now();
	return 0;
}

static int bus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	while (sd_bus_process(g_bus, NULL) > 0) {
		;
	}
	return 0;
}

void desk_orient_hold(bool hold)
{
	if (!g_bus || hold == g_claimed) {
		return;
	}
	sd_bus_call_method(g_bus, SP_NAME, SP_PATH, SP_NAME,
		hold ? "ClaimAccelerometer" : "ReleaseAccelerometer", NULL, NULL, "");
	g_claimed = hold;
	if (hold) {
		orientation_now();
	}
}

bool desk_orient_watch(struct wl_event_loop *loop, void (*said)(void *data, int transform),
	void *data)
{
	if (sd_bus_open_system(&g_bus) < 0) {
		g_bus = NULL;
		return false;
	}
	int has = 0;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	if (sd_bus_get_property(g_bus, SP_NAME, SP_PATH, SP_NAME, "HasAccelerometer", &err, &reply,
			"b") < 0 || sd_bus_message_read(reply, "b", &has) < 0 || !has) {
		wlr_log(WLR_INFO, "orientation: no accelerometer (%s)",
			err.message ? err.message : "iio-sensor-proxy says none");
		sd_bus_error_free(&err);
		sd_bus_message_unref(reply);
		sd_bus_unref(g_bus);
		g_bus = NULL;
		return false;
	}
	sd_bus_message_unref(reply);
	g_said = said;
	g_data = data;
	sd_bus_match_signal(g_bus, NULL, SP_NAME, SP_PATH, "org.freedesktop.DBus.Properties",
		"PropertiesChanged", on_changed, NULL);
	wl_event_loop_add_fd(loop, sd_bus_get_fd(g_bus), WL_EVENT_READABLE, bus_ready, NULL);
	desk_orient_hold(true);
	bus_ready(0, 0, NULL);
	wlr_log(WLR_INFO, "orientation: watching the accelerometer");
	return true;
}
#else
bool desk_orient_watch(struct wl_event_loop *loop, void (*said)(void *data, int transform),
	void *data)
{
	(void)loop;
	(void)said;
	(void)data;
	return false;
}

void desk_orient_hold(bool hold)
{
	(void)hold;
}
#endif
