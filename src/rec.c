/* The camera recording: the frames, handed to desk-record (see rec.h).
 *
 * Two pictures, each free, being read into on the desk's thread, ready to
 * go, or going; the thread here sends whichever is ready -- the newer, if
 * both are -- and, nothing new for REC_AGAIN, the last one again. A frame
 * goes down the socket whole, with its own little waits rather than the
 * socket's (send never blocks): what cannot be sent in REC_GIVE_UP means
 * desk-record has stuck, and the recording is over as if it had gone.
 * Whichever lets go of it last, the desk (desk_rec_stop) or the thread,
 * frees it. */

#define _GNU_SOURCE
#include "rec.h"

#include "apps.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define REC_AGAIN 0.5     /* nothing new this long: the last frame again */
#define REC_GIVE_UP 5.0   /* a frame not taken in this long: desk-record has stuck */

enum { PIC_FREE, PIC_FILLING, PIC_READY, PIC_SENDING };

struct desk_rec {
	int fd;
	size_t size;
	uint8_t *pic[2];
	int state[2];
	unsigned seq[2];       /* the order they were read in */
	unsigned seqs;
	int last;              /* the one last sent, for sending again (-1: none, or taken since) */
	bool stopping;         /* the desk has let go of it */
	bool done;             /* and the thread has: whichever is the later frees it */
	atomic_bool dead;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

static double mono_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* A picture down the socket, all of it; false if desk-record has gone or
 * stuck. */
static bool rec_send(struct desk_rec *r, const uint8_t *p)
{
	size_t done = 0;
	double began = mono_sec();
	while (done < r->size) {
		ssize_t n = send(r->fd, p + done, r->size - done, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n > 0) {
			done += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)
			&& mono_sec() - began < REC_GIVE_UP) {
			struct pollfd pf = { .fd = r->fd, .events = POLLOUT };
			poll(&pf, 1, 200);
			continue;
		}
		return false;
	}
	return true;
}

/* The one ready to go -- the newer, the older let go of -- or -1. */
static int rec_ready(struct desk_rec *r)
{
	int pick = -1;
	for (int i = 0; i < 2; i++) {
		if (r->state[i] != PIC_READY) {
			continue;
		}
		if (pick < 0 || r->seq[i] > r->seq[pick]) {
			if (pick >= 0) {
				r->state[pick] = PIC_FREE;
			}
			pick = i;
		} else {
			r->state[i] = PIC_FREE;
		}
	}
	return pick;
}

static void rec_free(struct desk_rec *r)
{
	if (r->fd >= 0) {
		shutdown(r->fd, SHUT_WR);
		close(r->fd);
	}
	free(r->pic[0]);
	free(r->pic[1]);
	pthread_cond_destroy(&r->cond);
	pthread_mutex_destroy(&r->lock);
	free(r);
}

static void *rec_run(void *data)
{
	struct desk_rec *r = data;
	pthread_mutex_lock(&r->lock);
	double sent_at = mono_sec();
	for (;;) {
		int i = rec_ready(r);
		if (i < 0 && !r->stopping) {
			double until = sent_at + REC_AGAIN;
			struct timespec ts = { (time_t)until, (long)((until - (time_t)until) * 1e9) };
			int e = pthread_cond_timedwait(&r->cond, &r->lock, &ts);
			if (e != ETIMEDOUT || r->last < 0 || r->state[r->last] != PIC_FREE) {
				if (e == ETIMEDOUT) {
					sent_at = mono_sec();
				}
				continue;
			}
			i = r->last;   /* nothing new: the last again */
		}
		if (i < 0) {
			break;   /* stopped, and all sent */
		}
		r->state[i] = PIC_SENDING;
		pthread_mutex_unlock(&r->lock);
		bool ok = rec_send(r, r->pic[i]);
		pthread_mutex_lock(&r->lock);
		r->state[i] = PIC_FREE;
		r->last = i;
		sent_at = mono_sec();
		if (!ok) {
			atomic_store(&r->dead, true);
			break;
		}
	}
	/* the end: the last frame once more, for the film to run until now,
	 * and the socket let go of -- desk-record finishes the film */
	int last = r->last;
	bool dead = atomic_load(&r->dead);
	pthread_mutex_unlock(&r->lock);
	if (!dead && last >= 0) {
		rec_send(r, r->pic[last]);
	}
	shutdown(r->fd, SHUT_WR);
	close(r->fd);
	r->fd = -1;
	pthread_mutex_lock(&r->lock);
	r->done = true;
	bool free_now = r->stopping;
	pthread_mutex_unlock(&r->lock);
	if (free_now) {
		rec_free(r);
	}
	return NULL;
}

struct desk_rec *desk_rec_start(int w, int h, double t0, int transform, const char *lang,
	bool mute)
{
	if (w < 2 || h < 2) {
		return NULL;
	}
	struct desk_rec *r = calloc(1, sizeof *r);
	if (!r) {
		return NULL;
	}
	r->fd = -1;
	r->last = -1;
	r->size = (size_t)w * (size_t)h * 4;
	r->pic[0] = malloc(r->size);
	r->pic[1] = malloc(r->size);
	pthread_condattr_t ca;
	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_init(&r->cond, &ca);
	pthread_condattr_destroy(&ca);
	pthread_mutex_init(&r->lock, NULL);
	int sv[2];
	if (!r->pic[0] || !r->pic[1] || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
		rec_free(r);
		return NULL;
	}
	char size_arg[32], start_arg[40], turn_arg[16];
	snprintf(size_arg, sizeof size_arg, "%dx%d", w, h);
	snprintf(start_arg, sizeof start_arg, "%.6f", t0);
	snprintf(turn_arg, sizeof turn_arg, "%d", transform);
	char *argv[] = { (char *)"desk-record", (char *)"--size", size_arg, (char *)"--start",
		start_arg, (char *)"--transform", turn_arg, (char *)"--lang",
		(char *)(lang && *lang ? lang : "en"), mute ? (char *)"--mute" : NULL, NULL };
	/* forked twice, as spawn_argv's are: nobody's child, its frames
	 * coming in on its stdin */
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		if (fork() == 0) {
			dup2(sv[1], 0);
			desk_close_from(3);
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	close(sv[1]);
	if (pid < 0) {
		close(sv[0]);
		rec_free(r);
		return NULL;
	}
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
		;
	}
	r->fd = sv[0];
	pthread_attr_t at;
	pthread_t th;
	bool started = false;
	if (pthread_attr_init(&at) == 0) {
		pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
		started = pthread_create(&th, &at, rec_run, r) == 0;
		pthread_attr_destroy(&at);
	}
	if (!started) {
		rec_free(r);
		return NULL;
	}
	return r;
}

uint8_t *desk_rec_frame(struct desk_rec *r)
{
	if (!r || atomic_load(&r->dead)) {
		return NULL;
	}
	pthread_mutex_lock(&r->lock);
	/* a free one -- not the one last sent, if the other will do: that may
	 * be wanted again -- or the one waiting to go, let go of */
	int pick = -1;
	for (int i = 0; i < 2; i++) {
		if (r->state[i] == PIC_FREE && (pick < 0 || pick == r->last)) {
			pick = i;
		}
	}
	for (int i = 0; i < 2 && pick < 0; i++) {
		if (r->state[i] == PIC_READY) {
			pick = i;
		}
	}
	if (pick >= 0) {
		r->state[pick] = PIC_FILLING;
		if (pick == r->last) {
			r->last = -1;
		}
	}
	pthread_mutex_unlock(&r->lock);
	return pick >= 0 ? r->pic[pick] : NULL;
}

void desk_rec_frame_done(struct desk_rec *r, uint8_t *frame, bool ok)
{
	if (!r || !frame) {
		return;
	}
	pthread_mutex_lock(&r->lock);
	for (int i = 0; i < 2; i++) {
		if (r->pic[i] == frame && r->state[i] == PIC_FILLING) {
			r->state[i] = ok ? PIC_READY : PIC_FREE;
			r->seq[i] = ++r->seqs;
		}
	}
	pthread_cond_signal(&r->cond);
	pthread_mutex_unlock(&r->lock);
}

bool desk_rec_running(struct desk_rec *r)
{
	return r && !atomic_load(&r->dead);
}

void desk_rec_stop(struct desk_rec *r)
{
	if (!r) {
		return;
	}
	pthread_mutex_lock(&r->lock);
	r->stopping = true;
	bool free_now = r->done;
	pthread_cond_signal(&r->cond);
	pthread_mutex_unlock(&r->lock);
	if (free_now) {
		rec_free(r);   /* (its thread gone already: desk-record went first) */
	}
}
