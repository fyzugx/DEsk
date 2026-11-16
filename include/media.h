#ifndef DESK_MEDIA_H
#define DESK_MEDIA_H

/* What is playing: the players on the session bus that say so through
 * MPRIS (org.mpris.MediaPlayer2.*) -- the browsers, and nearly every
 * music and video player there is. Of several, the one playing; else the
 * one that most lately was. */

#include <stdbool.h>

struct wl_event_loop;

struct desk_media {
	bool have;               /* there is a player at all */
	bool playing;            /* and it is playing; else paused or stopped */
	bool stopped;            /* stopped outright, not paused */
	char title[128];         /* the track's title, else its file's name */
	char artist[128];        /* its artists, a comma between */
	char player[64];         /* what the player calls itself */
	double length;           /* seconds; < 0 unknown */
	double position;         /* seconds, as of the time asked for; < 0 unknown */
	double since;            /* when this track came up, on the monotonic clock */
	bool can_prev, can_next, can_play, can_pause, can_raise, can_seek;
};

/* changed is called on the compositor's thread when anything shown of it
 * changes. False when there is no session bus. */
bool desk_media_init(struct wl_event_loop *loop, void (*changed)(void *user), void *user);
void desk_media_finish(void);

/* The player to show, and where it is at now: now is CLOCK_MONOTONIC in
 * seconds. */
void desk_media_read(struct desk_media *out, double now);

enum {
	DESK_MEDIA_PREV = 0,
	DESK_MEDIA_PLAY_PAUSE,
	DESK_MEDIA_NEXT,
	DESK_MEDIA_RAISE,      /* bring the player's window up */
};
void desk_media_command(int what);

/* The same for one program's player, for a window that is showing what it plays:
 * app is the program's id or X class. If no player goes by that name, the one above. */
void desk_media_read_for(const char *app, struct desk_media *out, double now);
void desk_media_command_for(const char *app, int what);
/* Sent on or back by so many seconds, or to a place in the track. */
void desk_media_seek_for(const char *app, double by);
void desk_media_goto_for(const char *app, double at);

/* Once a second or so. */
void desk_media_tick(void);

#endif
