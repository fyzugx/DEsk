/* Playing the desk's sounds.
 *
 * The sounds are made once (sfx.c), a few of each, and mixed here on a
 * thread of their own: each time one is played it is one of its variants,
 * pitched a shade up or down and a shade louder or softer, and placed left
 * or right where the thing that made it is on the screen.
 *
 * The mix goes, in order of preference, to
 *   - the sound server, through PulseAudio's client library -- which
 *     PipeWire answers as well (built in when libpulse is there). A few
 *     streams ("lanes"), a new sound going into one holding nothing but
 *     silence, just ahead of where the server is reading, rather than
 *     behind what it holds (see the lanes, below). Kept running while
 *     somebody is at the desk; corked once nobody has been for a few
 *     seconds and there is nothing to play, so an idle desk costs the
 *     server nothing and lets the card sleep.
 *   - ALSA's default device, the library loaded only if it is there (no
 *     need of its headers to build, or of it at all to run), opened when
 *     there is something to play and closed again after a pause.
 *   - nowhere.
 * DESK_SOUND=off|pulse|alsa|file:PATH chooses; file: writes what would
 * have been heard, in real time, to a WAV -- for tests, without a sound
 * on anybody's speakers.
 *
 * A sound can be asked for ahead of time, to be heard at a moment (the
 * drawer reaching its stop): it is put out early by as long as the way to
 * the speakers takes -- the server says how long its buffers and the
 * card's are -- and a sound for a motion is cut to end as the motion is
 * seen to. DESK_SOUND_DEBUG=1 logs, for every sound, how long after it
 * was played it is heard and against when it was wanted, by the server's
 * own clock. */

#define _GNU_SOURCE
#include "sound.h"

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef DESK_HAVE_PULSE
#include <pulse/pulseaudio.h>
#endif

#define RATE DESK_SFX_RATE
#define MAX_VOICES 32
#define MAX_VARIANTS 6
#define BLOCK 480            /* 10 ms */

struct bank {
	float *x[MAX_VARIANTS];
	int n[MAX_VARIANTS];
	int count;
	int last;
};

struct voice {
	int handle;              /* 0: free */
	int sound;
	double asked;            /* when it was played (debug) */
	const float *x;
	int n;
	double pos, rate;
	float gl, gr;
	/* going round until stopped (desk_sound_loop), and the gains it is
	 * gliding to (desk_sound_set); it stops by itself once it has not
	 * been set for a second (keep, in frames played), as a thing let go
	 * of does */
	bool loop;
	float gl_to, gr_to;
	long keep;
	long age;                /* frames played */
	long stop_at, fade;      /* fading out from stop_at, over fade frames */
	int lane;                /* the stream it goes to (-1: none yet) */
	/* When it is to be heard (0: as soon as it can be) and, once it has
	 * been placed in what goes out, when it will be; the frames of silence
	 * before it starts; when it is to stop, as heard (0: when it ends). */
	double due, heard, stop_due, stop_fade;
	bool placed;
	long wait;
};

static struct bank g_bank[DESK_SND_COUNT];
static struct voice g_voices[MAX_VOICES];
static pthread_mutex_t g_mix = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static int g_next = 1;
static double g_level = 1.0;
/* how long after it is put in what goes out a sound is heard: the
 * server's and the card's buffers, as the server tells it (or ALSA's) */
static _Atomic double g_lat;
static uint64_t g_dice = 0x2545F4914F6CDD1Dull;
static atomic_bool g_ready;
static atomic_bool g_running;
static pthread_t g_thread;
static bool g_thread_on;
static char g_app[64] = "DEsk";

enum backend { B_NONE, B_PULSE, B_ALSA, B_FILE };
static enum backend g_backend;
static char g_file[512];

/* DESK_SOUND_DEBUG: for each sound, how long after it was played it is
 * heard -- by the server's own clock of what has been played out */
static bool g_debug;
#ifdef DESK_HAVE_PULSE
struct dbg_wait {
	int sound;
	double asked, queued;
	uint64_t at_us;          /* where in the stream it starts */
	bool live, ahead;        /* ahead: written over the silence */
	int lane;
	double due;
};
static struct dbg_wait g_dbg[64];
#endif

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double dice(void)
{
	g_dice ^= g_dice >> 12;
	g_dice ^= g_dice << 25;
	g_dice ^= g_dice >> 27;
	return (double)((g_dice * 2685821657736338717ull) >> 11) * (1.0 / 9007199254740992.0);
}

/* ---- the mix: called with g_mix held ---- */

static bool voices_busy(void)
{
	for (int i = 0; i < MAX_VOICES; i++) {
		if (g_voices[i].handle) {
			return true;
		}
	}
	return false;
}

/* frames of stereo float into out; true if anything was playing */
static int g_started_n;
static struct {
	int sound;
	double asked, due;
	int off;                 /* frames into the block */
} g_started[16];

/* A voice put into what goes out now, where what is put in now is heard
 * lat seconds later: how long it waits first, to be heard when it is due,
 * and when it stops, as heard. Never shorter than a breath: a sound for a
 * motion asked for late still sounds. */
static void place(struct voice *v, double now, double lat)
{
	double at = now + lat;
	v->wait = v->due > at ? (long)((v->due - at) * RATE) : 0;
	v->heard = at + (double)v->wait / RATE;
	v->placed = true;
	if (v->stop_due > 0.0) {
		double len = fmax(v->stop_due - v->heard, 0.06);
		long when = (long)(len * RATE);
		if (when < v->stop_at) {
			v->stop_at = when;
			v->fade = (long)(v->stop_fade * RATE) + 1;
		}
	}
}

/* lane: only the voices of that lane, those not given one yet going to
 * lane 0; -1, all of them */
static bool mix(float *out, int frames, int lane)
{
	memset(out, 0, sizeof(float) * 2 * (size_t)frames);
	bool any = false;
	g_started_n = 0;
	double now = -1.0;
	for (int k = 0; k < MAX_VOICES; k++) {
		struct voice *v = &g_voices[k];
		if (!v->handle) {
			continue;
		}
		if (!v->placed) {
			now = now < 0.0 ? now_sec() : now;
			if (lane < 0) {
				place(v, now, atomic_load(&g_lat));
			} else if (lane == 0 && v->due <= now) {
				/* left without a lane (the server was away, say): lane 0
				 * has it, behind what it holds */
				v->lane = 0;
				place(v, now, atomic_load(&g_lat));
			} else {
				continue;
			}
		}
		if (lane >= 0 && v->lane != lane) {
			continue;
		}
		any = true;
		int i = 0;
		if (v->wait > 0) {
			long w = v->wait < frames ? v->wait : frames;
			v->wait -= w;
			i = (int)w;
		}
		if (v->age == 0 && i < frames && g_started_n < 16) {
			g_started[g_started_n].sound = v->sound;
			g_started[g_started_n].asked = v->asked;
			g_started[g_started_n].due = v->due;
			g_started[g_started_n].off = i;
			g_started_n++;
		}
		if (v->loop && v->age > v->keep && v->age < v->stop_at) {
			v->stop_at = v->age;
			v->fade = RATE / 10;
		}
		for (; i < frames; i++) {
			int i0 = (int)v->pos;
			if (i0 + 1 >= v->n) {
				if (!v->loop) {
					v->handle = 0;
					break;
				}
				v->pos -= (double)(v->n - 1);
				i0 = (int)v->pos;
			}
			/* (10 ms to a new gain: no step to be heard as a click) */
			v->gl += (v->gl_to - v->gl) * 0.002f;
			v->gr += (v->gr_to - v->gr) * 0.002f;
			double g = 1.0;
			if (v->age >= v->stop_at) {
				g = 1.0 - (double)(v->age - v->stop_at) / (double)(v->fade > 0 ? v->fade : 1);
				if (g <= 0.0) {
					v->handle = 0;
					break;
				}
			}
			double fr = v->pos - i0;
			double s = (v->x[i0] + (v->x[i0 + 1] - v->x[i0]) * fr) * g;
			out[2 * i] += (float)(s * v->gl);
			out[2 * i + 1] += (float)(s * v->gr);
			v->pos += v->rate;
			v->age++;
		}
	}
	/* how loud, and nothing past full scale: a gentle knee rather than a
	 * hard edge, should several land at once */
	for (int i = 0; i < frames * 2; i++) {
		double y = out[i] * g_level;
		double a = fabs(y);
		if (a > 0.85) {
			y = (y < 0.0 ? -1.0 : 1.0) * (0.85 + 0.15 * tanh((a - 0.85) / 0.15));
		}
		out[i] = (float)y;
	}
	return any;
}

/* ---- the sound server: PulseAudio's API (PulseAudio, or PipeWire) ---- */

#ifdef DESK_HAVE_PULSE
/* A few streams ("lanes"), not one. A sound written behind what a stream
 * already holds is heard only after all of it -- some 50 ms of it, here
 * -- but a stream holding nothing but silence can take a new sound just
 * ahead of where the server is reading, over the silence. So a sound goes
 * into a lane that is quiet, and two or three at once each find one; the
 * server mixes the lanes. */
#define LANES 4
struct lane {
	pa_stream *st;
	int state;               /* 0 connecting, 1 ready, -1 gone */
	bool corked;
	long quiet;              /* frames of silence written since its last sound */
	long tlen;               /* what the server keeps of it, in frames */
	/* debug: the server's clock of what is heard, at the end of what has
	 * been written (the library loses it after a seek); when uncorked */
	uint64_t w_us, t0;
	bool w_known;
	double uncorked;
	double started;          /* when it was last uncorked */
};
static pa_threaded_mainloop *g_ml;
static pa_context *g_ctx;
static double g_quantum;  /* what the server takes of a lane at a time, s */
static bool g_lat_told;   /* g_lat is what the server said, not the guess */
static struct lane g_lane[LANES];
static _Atomic double g_awake_until;  /* the lanes kept running until then */
static double g_awake_s;                 /* DESK_SOUND_AWAKE_S, or 0 */
static int g_pa_state;   /* 0 connecting, 1 ready, -1 gone */
/* held while the connection is being made or taken down (by the sound
 * thread); the compositor only ever tries it, and does not wait */
static pthread_mutex_t g_pa_life = PTHREAD_MUTEX_INITIALIZER;
/* How far ahead of where the server is reading a sound is written, over
 * the silence. The server places it as it takes it in, so nothing more is
 * needed than a hair for its own bookkeeping. */
#define AHEAD_MS 4
/* A sound wanted later is given a lane this long before it has to go
 * out; until then it holds none. */
#define PLACE_EARLY 0.03
/* How long the lanes are kept running after the last key or motion:
 * by default not at all, a lane being corked half a second after its last
 * sound. Running, a lane keeps the card running, and some cards cannot be
 * kept running: a virtual machine's (VMware's ES1371 under PipeWire)
 * stops and starts again five or more times a second, which is heard as
 * a hiss and crackle for as long as anybody moves the pointer. Where the
 * card is sound, DESK_SOUND_AWAKE_S=8 spares the first sound after a
 * pause the few milliseconds of starting a lane again. */

/* (debug) a block of `frames` just written to lane l -- `ahead` seconds
 * ahead of where the server reads, or (ahead < 0) after what it held --
 * with `started` sounds starting in it: where on the server's clock of
 * what is heard each begins */
static void dbg_note(int l, int started, int frames, double ahead)
{
	struct lane *ln = &g_lane[l];
	uint64_t blk = (uint64_t)frames * 1000000ull / RATE;
	pa_usec_t t = 0, lat = 0;
	int neg = 0;
	bool has_t = pa_stream_get_time(ln->st, &t) >= 0;
	if (ahead >= 0.0) {
		const pa_timing_info *ti = pa_stream_get_timing_info(ln->st);
		ln->w_known = has_t;
		ln->w_us = t + (ti ? ti->sink_usec : 0) + (uint64_t)(ahead * 1e6) + blk;
	} else if (has_t && pa_stream_get_latency(ln->st, &lat, &neg) >= 0) {
		ln->w_known = true;
		ln->w_us = t + (neg ? 0 : lat);
	} else {
		ln->w_us += blk;
	}
	if (!started) {
		return;
	}
	if (!ln->w_known) {
		fprintf(stderr, "desk: sound: (no timing yet for %d sound%s)\n", started,
			started == 1 ? "" : "s");
		return;
	}
	uint64_t at = ln->w_us > blk ? ln->w_us - blk : 0;
	for (int k = 0; k < started; k++) {
		for (int j = 0; j < 64; j++) {
			if (!g_dbg[j].live) {
				g_dbg[j] = (struct dbg_wait){ g_started[k].sound, g_started[k].asked,
					now_sec(), at + (uint64_t)g_started[k].off * 1000000ull / RATE,
					true, ahead >= 0.0, l, g_started[k].due };
				break;
			}
		}
	}
}

static void lane_cork(struct lane *ln, bool cork)
{
	pa_operation *o = pa_stream_cork(ln->st, cork, NULL, NULL);
	if (o) {
		pa_operation_unref(o);
	}
	ln->corked = cork;
	if (!cork) {
		ln->started = now_sec();
	}
	if (!cork && g_debug) {
		o = pa_stream_update_timing_info(ln->st, NULL, NULL);
		if (o) {
			pa_operation_unref(o);
		}
		pa_usec_t t0 = 0;
		pa_stream_get_time(ln->st, &t0);
		ln->t0 = t0;
		ln->uncorked = now_sec();
	}
}

static void st_write(pa_stream *s, size_t nbytes, void *u)
{
	int l = (int)(intptr_t)u;
	struct lane *ln = &g_lane[l];
	/* What it asks for at a time is what the server takes at a time: a
	 * sound written over the silence waits, on average, half of that for
	 * it to come round. */
	if (!ln->corked && nbytes >= 8 && (long)(nbytes / 8) < ln->tlen * 9 / 10) {
		/* (not counting a lane filling up again after it was started) */
		double q = (double)(nbytes / 8) / RATE;
		g_quantum = g_quantum <= 0.0 ? q : g_quantum + (q - g_quantum) * 0.1;
	}
	while (nbytes >= 8) {
		void *data = NULL;
		size_t n = nbytes;
		if (pa_stream_begin_write(s, &data, &n) < 0 || !data || n < 8) {
			return;
		}
		int frames = (int)(n / 8);
		pthread_mutex_lock(&g_mix);
		bool any = mix(data, frames, l);
		int started = g_started_n;
		pthread_mutex_unlock(&g_mix);
		pa_stream_write(s, data, (size_t)frames * 8, NULL, 0, PA_SEEK_RELATIVE);
		if (g_debug) {
			dbg_note(l, started, frames, -1.0);
		}
		nbytes -= (size_t)frames * 8;
		ln->quiet = any ? 0 : ln->quiet + frames;
	}
	/* Nothing to play for half a second: corked, so the server and the
	 * card can go back to sleep (unless asked to keep them running while
	 * somebody is at the desk: DESK_SOUND_AWAKE_S). */
	if (ln->quiet > RATE / 2 && !ln->corked && now_sec() > atomic_load(&g_awake_until)) {
		lane_cork(ln, true);
	}
}

/* How long until what is written over the silence is heard, as the server
 * tells it whenever the timing is brought up to date: the card's and the
 * server's buffers, half of what the server takes at a time, and the hair
 * ahead. Only from a lane that has been running a little while -- for a
 * corked one, or one just started, with the card asleep or waking, it
 * tells 0 -- and kept through a pause, for the first sound after it.
 * Steadied: a card that stops and starts again tells it differently every
 * time. */
static void st_latency(pa_stream *s, void *u)
{
	struct lane *ln = &g_lane[(int)(intptr_t)u];
	const pa_timing_info *ti = pa_stream_get_timing_info(s);
	if (!ti || ln->corked || now_sec() - ln->started < 0.25) {
		return;
	}
	double lat = AHEAD_MS / 1000.0 + (g_quantum > 0.0 ? g_quantum : 0.02) * 0.5
		+ (double)(ti->sink_usec + ti->transport_usec) / 1e6;
	double was = atomic_load(&g_lat);
	atomic_store(&g_lat, g_lat_told ? was + (lat - was) * 0.1 : lat);
	g_lat_told = true;
}

static void st_underflow(pa_stream *s, void *u)
{
	(void)s;
	fprintf(stderr, "desk: sound: lane %d ran dry (underflow)\n", (int)(intptr_t)u);
}

/* (debug) the sounds waiting to be heard: heard when the server's clock of
 * what has been played reaches where they start */
static void dbg_poll(void)
{
	if (!g_debug || !g_ml) {
		return;
	}
	pa_usec_t t[LANES] = { 0 };
	bool ok[LANES] = { false };
	pa_threaded_mainloop_lock(g_ml);
	for (int l = 0; l < LANES; l++) {
		ok[l] = g_pa_state == 1 && g_lane[l].st && pa_stream_get_time(g_lane[l].st, &t[l]) >= 0;
		struct lane *ln = &g_lane[l];
		if (ok[l] && ln->uncorked > 0.0 && t[l] > ln->t0) {
			fprintf(stderr, "desk: sound: lane %d playing out %.0f ms after it was uncorked\n",
				l, (now_sec() - ln->uncorked) * 1000.0);
			ln->uncorked = 0.0;
		}
	}
	for (int j = 0; j < 64; j++) {
		struct dbg_wait *w = &g_dbg[j];
		if (w->live && ok[w->lane] && t[w->lane] >= w->at_us) {
			double now = now_sec();
			char due[64] = "";
			if (w->due > 0.0) {
				snprintf(due, sizeof due, ", %+.0f ms against when it was wanted, %.3f",
					(now - w->due) * 1000.0, w->due);
			}
			fprintf(stderr, "desk: sound: %-16s heard %5.0f ms after it was played "
				"(lane %d, %s%s)\n", desk_sound_name(w->sound),
				(now - w->asked) * 1000.0, w->lane,
				w->ahead ? "over the silence" : "behind what it held", due);
			w->live = false;
		}
	}
	pa_threaded_mainloop_unlock(g_ml);
}

static void st_state(pa_stream *s, void *u)
{
	struct lane *ln = &g_lane[(int)(intptr_t)u];
	switch (pa_stream_get_state(s)) {
	case PA_STREAM_READY: {
		const pa_buffer_attr *a = pa_stream_get_buffer_attr(s);
		ln->tlen = a ? (long)(a->tlength / 8) : RATE / 20;
		ln->state = 1;
		if (atomic_load(&g_lat) <= 0.0) {
			atomic_store(&g_lat, 0.06);   /* until the server says */
		}
		if (g_debug && a && u == 0) {
			fprintf(stderr, "desk: sound: %d lanes, each holding %.1f ms (asks for %.1f ms "
				"at a time, prebuf %.1f ms)\n", LANES, a->tlength / 8 * 1000.0 / RATE,
				a->minreq / 8 * 1000.0 / RATE, a->prebuf / 8 * 1000.0 / RATE);
		}
		bool all = true;
		for (int l = 0; l < LANES; l++) {
			all = all && g_lane[l].state == 1;
		}
		if (all) {
			g_pa_state = 1;
		}
		pa_threaded_mainloop_signal(g_ml, 0);
		break;
	}
	case PA_STREAM_FAILED:
	case PA_STREAM_TERMINATED:
		ln->state = -1;
		g_pa_state = -1;
		pa_threaded_mainloop_signal(g_ml, 0);
		break;
	default:
		break;
	}
}

static void ctx_state(pa_context *c, void *u)
{
	(void)u;
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY: {
		pa_sample_spec ss = { PA_SAMPLE_FLOAT32LE, RATE, 2 };
		/* Enough that the server never runs dry -- PipeWire grants about
		 * half of what is asked, and a VM's card pulls in big periods --
		 * and no waiting to fill up again after a dry spell, which would
		 * hold up whatever came next. A new sound is not kept behind
		 * this: see pulse_wake. */
		const char *e;
		int tl = (e = getenv("DESK_SOUND_TLEN_MS")) ? atoi(e) : 80;
		int mr = (e = getenv("DESK_SOUND_MINREQ_MS")) ? atoi(e) : 10;
		int pb = (e = getenv("DESK_SOUND_PREBUF_MS")) ? atoi(e) : 0;
		pa_buffer_attr ba;
		ba.maxlength = (uint32_t)-1;
		ba.tlength = (uint32_t)pa_usec_to_bytes((pa_usec_t)tl * 1000, &ss);
		ba.prebuf = pb < 0 ? (uint32_t)-1 : (uint32_t)pa_usec_to_bytes((pa_usec_t)pb * 1000, &ss);
		ba.minreq = (uint32_t)pa_usec_to_bytes((pa_usec_t)mr * 1000, &ss);
		ba.fragsize = (uint32_t)-1;
		for (int l = 0; l < LANES; l++) {
			struct lane *ln = &g_lane[l];
			memset(ln, 0, sizeof *ln);
			pa_proplist *pl = pa_proplist_new();
			pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "event");
			pa_proplist_sets(pl, PA_PROP_MEDIA_NAME, "Desk sounds");
			ln->st = pa_stream_new_with_proplist(c, "Desk sounds", &ss, NULL, pl);
			pa_proplist_free(pl);
			if (!ln->st) {
				g_pa_state = -1;
				pa_threaded_mainloop_signal(g_ml, 0);
				break;
			}
			ln->corked = true;
			ln->quiet = RATE;
			void *ud = (void *)(intptr_t)l;
			pa_stream_set_state_callback(ln->st, st_state, ud);
			pa_stream_set_write_callback(ln->st, st_write, ud);
			pa_stream_set_latency_update_callback(ln->st, st_latency, ud);
			if (g_debug) {
				pa_stream_set_underflow_callback(ln->st, st_underflow, ud);
			}
			if (pa_stream_connect_playback(ln->st, NULL, &ba,
					PA_STREAM_ADJUST_LATENCY | PA_STREAM_START_CORKED
					| PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING,
					NULL, NULL) < 0) {
				g_pa_state = -1;
				pa_threaded_mainloop_signal(g_ml, 0);
				break;
			}
		}
		break;
	}
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		g_pa_state = -1;
		pa_threaded_mainloop_signal(g_ml, 0);
		break;
	default:
		break;
	}
}

static void pulse_stop(void)
{
	if (!g_ml) {
		return;
	}
	pa_threaded_mainloop_lock(g_ml);
	for (int l = 0; l < LANES; l++) {
		if (g_lane[l].st) {
			pa_stream_disconnect(g_lane[l].st);
			pa_stream_unref(g_lane[l].st);
			g_lane[l].st = NULL;
		}
	}
	if (g_ctx) {
		pa_context_disconnect(g_ctx);
		pa_context_unref(g_ctx);
		g_ctx = NULL;
	}
	pa_threaded_mainloop_unlock(g_ml);
	pa_threaded_mainloop_stop(g_ml);
	pa_threaded_mainloop_free(g_ml);
	g_ml = NULL;
	/* voices given to a lane that is gone are placed again, in whichever
	 * has them next */
	pthread_mutex_lock(&g_mix);
	for (int i = 0; i < MAX_VOICES; i++) {
		g_voices[i].lane = -1;
		g_voices[i].placed = false;
		g_voices[i].wait = 0;
	}
	pthread_mutex_unlock(&g_mix);
}

static bool pulse_start(void)
{
	g_pa_state = 0;
	g_ml = pa_threaded_mainloop_new();
	if (!g_ml) {
		return false;
	}
	pa_proplist *pl = pa_proplist_new();
	pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, g_app);
	pa_proplist_sets(pl, PA_PROP_APPLICATION_ID, "desk");
	pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "event");
	g_ctx = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(g_ml), g_app, pl);
	pa_proplist_free(pl);
	if (!g_ctx) {
		pulse_stop();
		return false;
	}
	pa_context_set_state_callback(g_ctx, ctx_state, NULL);
	if (pa_context_connect(g_ctx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0
		|| pa_threaded_mainloop_start(g_ml) < 0) {
		pulse_stop();
		return false;
	}
	/* two seconds for the server to answer and the streams to be made */
	for (int i = 0; i < 100; i++) {
		pa_threaded_mainloop_lock(g_ml);
		int st = g_pa_state;
		pa_threaded_mainloop_unlock(g_ml);
		if (st != 0) {
			break;
		}
		usleep(20000);
	}
	pa_threaded_mainloop_lock(g_ml);
	int st = g_pa_state;
	pa_threaded_mainloop_unlock(g_ml);
	if (st != 1) {
		pulse_stop();
		return false;
	}
	return true;
}

/* A sound has been played (sound), or somebody is at the desk: the new
 * sounds each into a lane with nothing but silence in it, just ahead of
 * where the server is reading; the lanes running. */
static void pulse_wake(bool sound)
{
	if (pthread_mutex_trylock(&g_pa_life) != 0) {
		return;   /* being put back together: lane 0 takes it when it can */
	}
	if (!g_ml) {
		pthread_mutex_unlock(&g_pa_life);
		pthread_mutex_lock(&g_mix);
		pthread_cond_signal(&g_wake);
		pthread_mutex_unlock(&g_mix);
		return;
	}
	pa_threaded_mainloop_lock(g_ml);
	if (g_pa_state == 1) {
		bool fresh[LANES] = { false };
		if (sound) {
			pthread_mutex_lock(&g_mix);
			double now = now_sec(), lat = atomic_load(&g_lat);
			for (int i = 0; i < MAX_VOICES; i++) {
				struct voice *v = &g_voices[i];
				if (!v->handle || v->lane >= 0 || v->due > now + lat + PLACE_EARLY) {
					continue;
				}
				/* a quiet lane -- one already running before one that has
				 * to be started -- or, if none is, behind what lane 0
				 * holds */
				int best = -1;
				for (int pass = 0; pass < 2 && best < 0; pass++) {
					for (int l = 0; l < LANES && best < 0; l++) {
						struct lane *ln = &g_lane[l];
						if (ln->quiet >= ln->tlen && !fresh[l] && ln->corked == (pass == 1)) {
							best = l;
						}
					}
				}
				/* a lane just given a sound takes the others played with it */
				for (int l = 0; l < LANES && best < 0; l++) {
					best = fresh[l] ? l : -1;
				}
				v->lane = best >= 0 ? best : 0;
				if (best >= 0) {
					fresh[best] = true;
					place(v, now, lat);
				} else {
					place(v, now, lat + (double)g_lane[0].tlen / RATE);
				}
			}
			pthread_mutex_unlock(&g_mix);
		}
		for (int l = 0; l < LANES; l++) {
			struct lane *ln = &g_lane[l];
			if (ln->corked && (fresh[l] || !sound)) {
				lane_cork(ln, false);
			}
			if (!fresh[l]) {
				continue;
			}
			/* over the silence it holds, from just ahead of where the
			 * server is reading */
			void *data = NULL;
			size_t n = (size_t)ln->tlen * 8;
			if (pa_stream_begin_write(ln->st, &data, &n) < 0 || !data || n < 8) {
				continue;
			}
			int frames = (int)(n / 8);
			pthread_mutex_lock(&g_mix);
			bool any = mix(data, frames, l);
			int started = g_started_n;
			pthread_mutex_unlock(&g_mix);
			int64_t ahead = (int64_t)(RATE * AHEAD_MS / 1000) * 8;
			pa_stream_write(ln->st, data, (size_t)frames * 8, NULL, ahead,
				PA_SEEK_RELATIVE_ON_READ);
			ln->quiet = any ? 0 : ln->quiet + frames;
			if (g_debug) {
				dbg_note(l, started, frames, AHEAD_MS / 1000.0);
			}
		}
	}
	bool gone = g_pa_state < 0;
	pa_threaded_mainloop_unlock(g_ml);
	pthread_mutex_unlock(&g_pa_life);
	if (gone) {
		/* the server went away (restarted, say): the thread tries again */
		pthread_mutex_lock(&g_mix);
		pthread_cond_signal(&g_wake);
		pthread_mutex_unlock(&g_mix);
	}
}
#endif

/* ---- ALSA, if the library is there ---- */

typedef struct snd_pcm snd_pcm_t;
static void *g_alsa;
static int (*a_open)(snd_pcm_t **, const char *, int, int);
static int (*a_set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
static long (*a_writei)(snd_pcm_t *, const void *, unsigned long);
static int (*a_recover)(snd_pcm_t *, int, int);
static int (*a_close)(snd_pcm_t *);

/* the stable numbers of ALSA's ABI: playback, S16_LE, interleaved */
#define A_PLAYBACK 0
#define A_S16_LE 2
#define A_RW_INTERLEAVED 3

static bool alsa_load(void)
{
	g_alsa = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
	if (!g_alsa) {
		return false;
	}
	*(void **)&a_open = dlsym(g_alsa, "snd_pcm_open");
	*(void **)&a_set_params = dlsym(g_alsa, "snd_pcm_set_params");
	*(void **)&a_writei = dlsym(g_alsa, "snd_pcm_writei");
	*(void **)&a_recover = dlsym(g_alsa, "snd_pcm_recover");
	*(void **)&a_close = dlsym(g_alsa, "snd_pcm_close");
	if (!a_open || !a_set_params || !a_writei || !a_recover || !a_close) {
		dlclose(g_alsa);
		g_alsa = NULL;
		return false;
	}
	return true;
}

static void to_s16(const float *in, int16_t *out, int n)
{
	for (int i = 0; i < n; i++) {
		double v = in[i] * 32767.0;
		out[i] = (int16_t)(v > 32767.0 ? 32767 : (v < -32768.0 ? -32768 : lrint(v)));
	}
}

/* Wait for something to play (true), or for the end (false). */
static bool wait_for_work(void)
{
	pthread_mutex_lock(&g_mix);
	while (atomic_load(&g_running) && !voices_busy()) {
		pthread_cond_wait(&g_wake, &g_mix);
	}
	pthread_mutex_unlock(&g_mix);
	return atomic_load(&g_running);
}

static void alsa_run(void)
{
	float f[BLOCK * 2];
	int16_t s[BLOCK * 2];
	while (wait_for_work()) {
		snd_pcm_t *pcm = NULL;
		if (a_open(&pcm, "default", A_PLAYBACK, 0) < 0 || !pcm
			|| a_set_params(pcm, A_S16_LE, A_RW_INTERLEAVED, 2, RATE, 1, 40000) < 0) {
			if (pcm) {
				a_close(pcm);
			}
			/* busy, or nothing there: these sounds are lost, and the
			 * next ones try again in a while */
			pthread_mutex_lock(&g_mix);
			for (int i = 0; i < MAX_VOICES; i++) {
				g_voices[i].handle = 0;
			}
			pthread_mutex_unlock(&g_mix);
			sleep(2);
			continue;
		}
		long quiet = 0;
		while (atomic_load(&g_running) && quiet < RATE * 3 / 2) {
			pthread_mutex_lock(&g_mix);
			bool any = mix(f, BLOCK, -1);
			pthread_mutex_unlock(&g_mix);
			to_s16(f, s, BLOCK * 2);
			long w = a_writei(pcm, s, BLOCK);
			if (w < 0) {
				a_recover(pcm, (int)w, 1);
			}
			quiet = any ? 0 : quiet + BLOCK;
		}
		a_close(pcm);
	}
}

/* ---- to a file, in real time (tests) ---- */

static void wav_header(FILE *f, int channels, long frames)
{
	uint32_t data = (uint32_t)(frames * channels * 2);
	uint32_t riff = 36 + data;
	uint16_t fmt = 1, ch = (uint16_t)channels, bits = 16, align = (uint16_t)(channels * 2);
	uint32_t rate = RATE, bps = (uint32_t)(RATE * channels * 2), sixteen = 16;
	fseek(f, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, f);
	fwrite(&riff, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&sixteen, 4, 1, f);
	fwrite(&fmt, 2, 1, f);
	fwrite(&ch, 2, 1, f);
	fwrite(&rate, 4, 1, f);
	fwrite(&bps, 4, 1, f);
	fwrite(&align, 2, 1, f);
	fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&data, 4, 1, f);
}

static void file_run(void)
{
	FILE *f = fopen(g_file, "wbe");
	if (!f) {
		fprintf(stderr, "desk: sound: cannot write %s: %s\n", g_file, strerror(errno));
		return;
	}
	wav_header(f, 2, 0);
	float b[BLOCK * 2];
	int16_t s[BLOCK * 2];
	long frames = 0;
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (atomic_load(&g_running)) {
		pthread_mutex_lock(&g_mix);
		mix(b, BLOCK, -1);
		pthread_mutex_unlock(&g_mix);
		to_s16(b, s, BLOCK * 2);
		fwrite(s, sizeof s, 1, f);
		frames += BLOCK;
		if (frames % (RATE / 2) == 0) {
			long end = ftell(f);
			wav_header(f, 2, frames);
			fseek(f, end, SEEK_SET);
			fflush(f);
		}
		next.tv_nsec += 10000000;
		if (next.tv_nsec >= 1000000000) {
			next.tv_nsec -= 1000000000;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}
	wav_header(f, 2, frames);
	fclose(f);
}

/* ---- the baked ones ----
 *
 * Some of the sounds are not made here but in Faust, by ear, and baked
 * (art/sound/bake.py): <name>-<n>.wav, n from 1, 16-bit mono at RATE and
 * half as loud as they are played. Where there is one, it is played
 * instead of what sfx.c makes. They are looked for where the pictures are
 * (draw.c desk_art_dirs): beside the prefix the binary is in
 * (bin/../share/desk/sound), the source's art/sound when run from where it
 * was built, then the prefix it was built for; DESK_ART_DIR=<a source's
 * art>, there only. */

#define BAKED_DIR_LEN 1100
static int baked_dirs(char dirs[3][BAKED_DIR_LEN])
{
	const char *env = getenv("DESK_ART_DIR");
	if (env && *env) {
		snprintf(dirs[0], BAKED_DIR_LEN, "%.1000s/sound", env);
		return 1;
	}
	char exe[1024] = "";
	ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[k > 0 ? k : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash) {
		*slash = '\0';
	}
	int nd = 0;
	if (exe[0]) {
		snprintf(dirs[nd++], BAKED_DIR_LEN, "%s/../share/desk/sound", exe);
		snprintf(dirs[nd++], BAKED_DIR_LEN, "%s/../art/sound", exe);
	}
#ifdef DESK_DATADIR
	snprintf(dirs[nd++], BAKED_DIR_LEN, "%s/desk/sound", DESK_DATADIR);
#endif
	return nd;
}

/* A baked take, as floats at the loudness it is played (the caller frees
 * it); NULL if it is not there, or not 16-bit mono at RATE. */
static float *baked_read(const char *path, int *frames)
{
	*frames = 0;
	FILE *f = fopen(path, "rbe");
	if (!f) {
		return NULL;
	}
	unsigned char h[12];
	float *x = NULL;
	if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
		fclose(f);
		return NULL;
	}
	bool fmt_ok = false;
	unsigned char c[8];
	while (fread(c, 1, 8, f) == 8) {
		uint32_t len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
		if (!memcmp(c, "fmt ", 4) && len >= 16) {
			unsigned char fm[16];
			if (fread(fm, 1, 16, f) != 16) {
				break;
			}
			fmt_ok = (fm[0] | fm[1] << 8) == 1 && (fm[2] | fm[3] << 8) == 1
				&& (uint32_t)(fm[4] | fm[5] << 8 | fm[6] << 16 | (uint32_t)fm[7] << 24) == RATE
				&& (fm[14] | fm[15] << 8) == 16;
			fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
		} else if (!memcmp(c, "data", 4) && fmt_ok && len >= 4 && len < 64u << 20) {
			int n = (int)(len / 2);
			int16_t *q = malloc((size_t)n * sizeof *q);
			x = q ? malloc((size_t)n * sizeof *x) : NULL;
			if (x && fread(q, 2, (size_t)n, f) == (size_t)n) {
				for (int i = 0; i < n; i++) {
					x[i] = (float)q[i] * (2.0f / 32767.0f);
				}
				*frames = n;
			} else {
				free(x);
				x = NULL;
			}
			free(q);
			break;
		} else {
			fseek(f, (long)(len + (len & 1)), SEEK_CUR);
		}
	}
	fclose(f);
	return x;
}

/* Sound s's baked take v (from 0), or NULL. */
static float *baked_take(enum desk_sound s, int v, int *frames)
{
	char dirs[3][BAKED_DIR_LEN], path[BAKED_DIR_LEN + 64];
	int nd = baked_dirs(dirs);
	for (int i = 0; i < nd; i++) {
		snprintf(path, sizeof path, "%.1000s/%.40s-%d.wav", dirs[i], desk_sound_name(s), v + 1);
		float *x = baked_read(path, frames);
		if (x) {
			return x;
		}
	}
	*frames = 0;
	return NULL;
}

/* ---- the thread: make the sounds, find somewhere to play them ---- */

static void bank_make(void)
{
	for (int s = 0; s < DESK_SND_COUNT; s++) {
		/* baked, as many takes as there are; else made */
		for (int v = 0; v < MAX_VARIANTS; v++) {
			int n = 0;
			float *x = baked_take(s, v, &n);
			if (!x) {
				break;
			}
			g_bank[s].x[g_bank[s].count] = x;
			g_bank[s].n[g_bank[s].count] = n;
			g_bank[s].count++;
		}
		g_bank[s].last = -1;
		if (g_bank[s].count) {
			continue;
		}
		int nv = desk_sound_variants(s);
		nv = nv > MAX_VARIANTS ? MAX_VARIANTS : nv;
		for (int v = 0; v < nv; v++) {
			int n = 0;
			float *x = desk_sfx_make(s, v, &n);
			if (x && n > 1) {
				g_bank[s].x[g_bank[s].count] = x;
				g_bank[s].n[g_bank[s].count] = n;
				g_bank[s].count++;
			} else {
				free(x);
			}
		}
		g_bank[s].last = -1;
	}
}

static void *sound_main(void *arg)
{
	(void)arg;
	bank_make();
	g_debug = getenv("DESK_SOUND_DEBUG") != NULL;
#ifdef DESK_HAVE_PULSE
	const char *aw = getenv("DESK_SOUND_AWAKE_S");
	g_awake_s = aw ? fmax(0.0, fmin(atof(aw), 600.0)) : 0.0;
#endif
	const char *want = getenv("DESK_SOUND");
	want = want && *want ? want : "auto";
	g_backend = B_NONE;
	if (!strncmp(want, "file:", 5)) {
		/* %p is this process, so that the programs DEsk starts -- who
		 * inherit the setting -- each write a file of their own */
		const char *src = want + 5;
		size_t o = 0;
		for (const char *q = src; *q && o + 16 < sizeof g_file; q++) {
			if (q[0] == '%' && q[1] == 'p') {
				o += (size_t)snprintf(g_file + o, sizeof g_file - o, "%ld", (long)getpid());
				q++;
			} else {
				g_file[o++] = *q;
			}
		}
		g_file[o] = '\0';
		g_backend = B_FILE;
		atomic_store(&g_lat, 0.0);   /* written as it would be heard */
	} else if (strcmp(want, "off") != 0) {
#ifdef DESK_HAVE_PULSE
		pthread_mutex_lock(&g_pa_life);
		if ((!strcmp(want, "auto") || !strcmp(want, "pulse")) && pulse_start()) {
			g_backend = B_PULSE;
		}
		pthread_mutex_unlock(&g_pa_life);
#endif
		if (g_backend == B_NONE && (!strcmp(want, "auto") || !strcmp(want, "alsa"))
			&& alsa_load()) {
			g_backend = B_ALSA;
			atomic_store(&g_lat, 0.05);  /* the 40 ms asked of it, and a block */
		}
	}
	static const char *names[] = { "nowhere", "the sound server", "ALSA", "a file" };
	fprintf(stderr, "desk: sound: %d sounds made; playing to %s\n", DESK_SND_COUNT,
		names[g_backend]);
	atomic_store(&g_ready, true);
	switch (g_backend) {
	case B_ALSA:
		alsa_run();
		break;
	case B_FILE:
		file_run();
		break;
#ifdef DESK_HAVE_PULSE
	case B_PULSE:
		/* Sounds wanted later are given a lane when their time comes;
		 * otherwise nothing to do but put it back together if the server
		 * goes. */
		while (atomic_load(&g_running)) {
			pthread_mutex_lock(&g_mix);
			double now = now_sec(), lat = atomic_load(&g_lat), next = now + 5.0;
			for (int i = 0; i < MAX_VOICES; i++) {
				struct voice *v = &g_voices[i];
				if (v->handle && !v->placed && v->due > 0.0) {
					next = fmin(next, v->due - lat - PLACE_EARLY);
				}
			}
			if (next > now) {
				if (g_debug) {
					/* watching for sounds being heard, every 2 ms */
					next = fmin(next, now + 0.002);
				}
				struct timespec t;
				clock_gettime(CLOCK_REALTIME, &t);
				double at = (double)t.tv_sec + t.tv_nsec / 1e9 + (next - now);
				t.tv_sec = (time_t)at;
				t.tv_nsec = (long)((at - (double)t.tv_sec) * 1e9);
				pthread_cond_timedwait(&g_wake, &g_mix, &t);
			}
			now = now_sec();
			bool due = false;
			for (int i = 0; i < MAX_VOICES; i++) {
				struct voice *v = &g_voices[i];
				due = due || (v->handle && !v->placed && v->due > 0.0
					&& v->due - lat - PLACE_EARLY <= now);
			}
			pthread_mutex_unlock(&g_mix);
			if (due) {
				pulse_wake(true);
			}
			if (g_debug) {
				dbg_poll();
			}
			static double checked;
			if (now - checked < 2.0) {
				continue;
			}
			checked = now;
			if (!atomic_load(&g_running)) {
				break;
			}
			pthread_mutex_lock(&g_pa_life);
			bool gone = true;
			if (g_ml) {
				pa_threaded_mainloop_lock(g_ml);
				gone = g_pa_state < 0;
				pa_threaded_mainloop_unlock(g_ml);
			}
			if (gone) {
				/* the server went away (restarted, say): again, now and
				 * then, until it is back */
				pulse_stop();
				if (pulse_start()) {
					fprintf(stderr, "desk: sound: the sound server is back\n");
				}
			}
			pthread_mutex_unlock(&g_pa_life);
		}
		break;
#endif
	default:
		break;
	}
	return NULL;
}

void desk_sound_init(const char *app)
{
	if (g_thread_on) {
		return;
	}
	snprintf(g_app, sizeof g_app, "%s", app && *app ? app : "DEsk");
	g_dice ^= (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull;
	atomic_store(&g_running, true);
	if (pthread_create(&g_thread, NULL, sound_main, NULL) == 0) {
		g_thread_on = true;
	}
}

void desk_sound_shutdown(void)
{
	if (!g_thread_on) {
		return;
	}
	atomic_store(&g_running, false);
	pthread_mutex_lock(&g_mix);
	pthread_cond_broadcast(&g_wake);
	pthread_mutex_unlock(&g_mix);
	pthread_join(g_thread, NULL);
	g_thread_on = false;
#ifdef DESK_HAVE_PULSE
	pthread_mutex_lock(&g_pa_life);
	pulse_stop();
	pthread_mutex_unlock(&g_pa_life);
#endif
	if (g_alsa) {
		dlclose(g_alsa);
		g_alsa = NULL;
	}
	atomic_store(&g_ready, false);
	for (int s = 0; s < DESK_SND_COUNT; s++) {
		for (int v = 0; v < g_bank[s].count; v++) {
			free(g_bank[s].x[v]);
		}
		memset(&g_bank[s], 0, sizeof g_bank[s]);
	}
}

void desk_sound_set_level(double level)
{
	pthread_mutex_lock(&g_mix);
	g_level = level < 0.0 ? 0.0 : (level > 2.0 ? 2.0 : level);
	pthread_mutex_unlock(&g_mix);
}

int desk_sound_play(enum desk_sound s, double pan, double gain, double rate)
{
	return desk_sound_play_in(s, pan, gain, rate, 0.0);
}

static int play(enum desk_sound s, double pan, double gain, double rate, double in, bool exact,
	bool loop);

int desk_sound_play_in(enum desk_sound s, double pan, double gain, double rate, double in)
{
	return play(s, pan, gain, rate, in, false, false);
}

int desk_sound_play_exact(enum desk_sound s, double pan, double gain, double rate, double in)
{
	return play(s, pan, gain, rate, in, true, false);
}

int desk_sound_loop(enum desk_sound s, double pan, double gain)
{
	return play(s, pan, gain, 0.0, 0.0, true, true);
}

static void pan_gains(double pan, double g, float *gl, float *gr)
{
	pan = pan < -1.0 ? -1.0 : (pan > 1.0 ? 1.0 : pan);
	double a = (pan + 1.0) * 0.25 * 3.14159265358979;
	*gl = (float)(g * cos(a));
	*gr = (float)(g * sin(a));
}

void desk_sound_set(int handle, double pan, double gain)
{
	if (handle <= 0) {
		return;
	}
	pthread_mutex_lock(&g_mix);
	for (int i = 0; i < MAX_VOICES; i++) {
		struct voice *v = &g_voices[i];
		if (v->handle == handle) {
			pan_gains(pan, gain, &v->gl_to, &v->gr_to);
			v->keep = v->age + RATE;
			break;
		}
	}
	pthread_mutex_unlock(&g_mix);
}

static int play(enum desk_sound s, double pan, double gain, double rate, double in, bool exact,
	bool loop)
{
	if (!atomic_load(&g_ready) || g_backend == B_NONE || s < 0 || s >= DESK_SND_COUNT) {
		return 0;
	}
	pthread_mutex_lock(&g_mix);
	struct bank *b = &g_bank[s];
	if (g_level <= 0.0 || b->count == 0) {
		pthread_mutex_unlock(&g_mix);
		return 0;
	}
	/* one of its variants -- not the one it was last time */
	int var = (int)(dice() * b->count);
	if (b->count > 1 && var == b->last) {
		var = (var + 1 + (int)(dice() * (b->count - 1))) % b->count;
	}
	b->last = var;
	/* a free voice, or the one furthest through */
	struct voice *v = NULL;
	for (int i = 0; i < MAX_VOICES && !v; i++) {
		if (!g_voices[i].handle) {
			v = &g_voices[i];
		}
	}
	if (!v) {
		v = &g_voices[0];
		for (int i = 1; i < MAX_VOICES; i++) {
			if (g_voices[i].age > v->age) {
				v = &g_voices[i];
			}
		}
	}
	v->x = b->x[var];
	v->n = b->n[var];
	v->sound = s;
	v->asked = now_sec();
	v->due = in > 0.0 ? v->asked + in : 0.0;
	v->heard = 0.0;
	v->stop_due = 0.0;
	v->placed = false;
	v->wait = 0;
	v->pos = 0.0;
	/* a shade up or down, a shade louder or softer: nothing twice -- but
	 * a note of a tune */
	v->rate = (rate > 0.0 ? rate : 1.0) * (exact ? 1.0 : 1.0 + 0.035 * (dice() * 2.0 - 1.0));
	double g = exact ? gain : gain * pow(10.0, (dice() * 2.0 - 1.0) * 1.2 / 20.0);
	pan_gains(pan, g, &v->gl, &v->gr);
	v->gl_to = v->gl;
	v->gr_to = v->gr;
	v->loop = loop;
	v->keep = RATE;
	v->age = 0;
	v->stop_at = (long)1 << 50;
	v->fade = 1;
	v->lane = -1;
	v->handle = g_next++;
	if (g_next > 1 << 30) {
		g_next = 1;
	}
	int h = v->handle;
	pthread_cond_signal(&g_wake);
	pthread_mutex_unlock(&g_mix);
#ifdef DESK_HAVE_PULSE
	/* (one wanted later is given its lane by the thread, in time) */
	if (g_backend == B_PULSE && in <= atomic_load(&g_lat) + PLACE_EARLY) {
		pulse_wake(true);
	}
#endif
	return h;
}

void desk_sound_awake(void)
{
#ifdef DESK_HAVE_PULSE
	static double told;
	/* (the level is only set from the caller's thread) */
	if (g_backend != B_PULSE || !atomic_load(&g_ready) || g_level <= 0.0 || g_awake_s <= 0.0) {
		return;
	}
	double now = now_sec();
	atomic_store(&g_awake_until, now + g_awake_s);
	/* the lanes started, if they have stopped -- looked at once a second,
	 * not on every motion of the pointer */
	if (now - told > 1.0) {
		told = now;
		pulse_wake(false);
	}
#endif
}

void desk_sound_stop(int handle, double fade)
{
	if (handle <= 0) {
		return;
	}
	pthread_mutex_lock(&g_mix);
	for (int i = 0; i < MAX_VOICES; i++) {
		struct voice *v = &g_voices[i];
		if (v->handle == handle) {
			if (!v->placed || v->age == 0) {
				v->handle = 0;   /* not begun: never heard */
			} else if (v->age + 1 < v->stop_at) {
				v->stop_at = v->age + 1;
				v->fade = (long)(fade * RATE) + 1;
			}
			break;
		}
	}
	pthread_mutex_unlock(&g_mix);
}

void desk_sound_stop_after(int handle, double after, double fade)
{
	if (handle <= 0) {
		return;
	}
	pthread_mutex_lock(&g_mix);
	for (int i = 0; i < MAX_VOICES; i++) {
		struct voice *v = &g_voices[i];
		if (v->handle == handle) {
			v->stop_due = now_sec() + after;
			v->stop_fade = fade;
			if (v->placed) {
				/* placed already: from when it is heard to start */
				double len = fmax(v->stop_due - v->heard, 0.06);
				long when = (long)(len * RATE);
				if (when < v->stop_at) {
					v->stop_at = when;
					v->fade = (long)(fade * RATE) + 1;
				}
			}
			break;
		}
	}
	pthread_mutex_unlock(&g_mix);
}

void desk_sound_cancel(int handle)
{
	if (handle <= 0) {
		return;
	}
	pthread_mutex_lock(&g_mix);
	for (int i = 0; i < MAX_VOICES; i++) {
		struct voice *v = &g_voices[i];
		if (v->handle == handle) {
			if (!v->placed || v->age == 0) {
				v->handle = 0;
			}
			break;
		}
	}
	pthread_mutex_unlock(&g_mix);
}

double desk_sound_latency(void)
{
	if (!atomic_load(&g_ready) || g_backend == B_NONE) {
		return 0.0;
	}
	return atomic_load(&g_lat);
}

bool desk_sound_write_wav(enum desk_sound s, int variant, const char *path)
{
	int n = 0;
	float *x = baked_take(s, variant, &n);
	x = x ? x : desk_sfx_make(s, variant, &n);
	if (!x) {
		return false;
	}
	FILE *f = fopen(path, "wbe");
	if (!f) {
		free(x);
		return false;
	}
	wav_header(f, 1, n);
	for (int i = 0; i < n; i++) {
		double v = x[i] * 32767.0;
		int16_t q = (int16_t)(v > 32767.0 ? 32767 : (v < -32768.0 ? -32768 : lrint(v)));
		fwrite(&q, 2, 1, f);
	}
	bool ok = fclose(f) == 0;
	free(x);
	return ok;
}
