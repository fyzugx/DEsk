#ifndef DESK_ORIENT_H
#define DESK_ORIENT_H

#include <stdbool.h>

struct wl_event_loop;

/* The way the machine is held, from its accelerometer (iio-sensor-proxy, on the system bus), as
 * the wl_output transform that keeps the picture upright: WL_OUTPUT_TRANSFORM_NORMAL, _90, _180
 * or _270. said() is called with it at once, and again whenever it changes. False (and nothing
 * called) with no accelerometer, no bus, or no iio-sensor-proxy. */
bool desk_orient_watch(struct wl_event_loop *loop, void (*said)(void *data, int transform),
	void *data);
/* Let go of the accelerometer (the machine can sleep it) or take it again. */
void desk_orient_hold(bool hold);

#endif
