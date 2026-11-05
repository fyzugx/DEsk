#ifndef DESK_SAVER_H
#define DESK_SAVER_H

/* Programs asking, on the session bus, for the screen to stay lit.
 *
 * A Wayland program asks with idle-inhibit, which wm.c answers. An X one
 * -- Spotify, VLC, a browser through Xwayland -- and anything in a Flatpak,
 * through the portal, asks org.freedesktop.ScreenSaver instead: Inhibit for
 * a cookie while a film plays, UnInhibit with it when it is over. DEsk
 * takes that name unless something on the session already has it, and a
 * hold lasts until it is let go of or the program that asked leaves the
 * bus. */

#include <stdbool.h>
#include <stddef.h>

struct wl_event_loop;

/* changed(user) when a hold comes or goes; awake(user) when a program says
 * somebody is there (SimulateUserActivity). Both on the compositor's
 * thread. False with no session bus, or the name somebody else's. */
bool desk_saver_init(struct wl_event_loop *loop, void (*changed)(void *user),
	void (*awake)(void *user), void *user);
void desk_saver_finish(void);

/* How many holds there are, and in who (n bytes, may be NULL) the name the
 * latest one was asked under. */
int desk_saver_holds(char *who, size_t n);

#endif
