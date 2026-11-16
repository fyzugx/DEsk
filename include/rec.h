#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The camera recording (rec.c), for Print with a tape in it (wm.c).
 *
 * The desk reads each frame of the screen it is recording as it goes out,
 * the part being recorded, into one of two pictures here -- w by h, four
 * bytes a pixel, blue first, the fourth byte nothing -- and a thread of its
 * own hands the newest down a socket to desk-record, which has ffmpeg
 * make a film of them and of the sound the desk is making, each stamped
 * with when it arrived. A frame the desk reads while both pictures are
 * still on their way is let go of -- the film runs at whatever pace the
 * screen and the machine allow, and keeps time all the same -- and the
 * last is sent again every half second while nothing moves, so that the
 * film has pictures for all of its sound. Nothing here waits on ffmpeg on
 * the desk's own thread: stopped, the thread sends the last frame once
 * more (the film's end) and lets the socket go -- and whichever lets go
 * of it last, the desk or the thread, frees it. */

struct desk_rec;

/* desk-record started, with the time the film begins at (wall clock,
 * seconds; what the frames and the sound are both measured from), the
 * screen's turn (a wl_output_transform: how what is read is to be turned
 * back), the desk's language (for its letter: "ja") and, mute, no sound.
 * NULL if it could not be (no socket, no fork). That desk-record or
 * ffmpeg is not there is only known once a frame cannot be sent:
 * desk_rec_running. */
struct desk_rec *desk_rec_start(int w, int h, double t0, int transform, const char *lang,
	bool mute);
/* A picture to read the next frame into, its rows w * 4 bytes apart; NULL
 * if there is none to be had (it is let go of). */
uint8_t *desk_rec_frame(struct desk_rec *r);
/* That picture read (ok) or not: to be sent, or given back. */
void desk_rec_frame_done(struct desk_rec *r, uint8_t *frame, bool ok);
/* Whether desk-record is still taking the frames. */
bool desk_rec_running(struct desk_rec *r);
/* Stopped: r is not to be used again. */
void desk_rec_stop(struct desk_rec *r);
