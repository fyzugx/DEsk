/* ================================================================
 * What has been copied (clips.h). The desk is the one the clipboard passes through:
 * whenever a program sets it, what it offers is read, as a program pasting it would
 * (on the event loop, a pipe at a time, never waited for), and kept. Copying a slip
 * again offers what was kept, written out the same way to whoever asks for it.
 * ================================================================ */

#include "clips.h"
#include "thing.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

/* the most read of words, and of a picture; and of pictures kept, all told */
#define CLIP_TEXT_MOST (1u << 20)
#define CLIP_IMAGE_MOST (32u << 20)
#define CLIP_IMAGES_MOST (96u << 20)
/* a picture is kept no bigger than this across, for its slip */
#define CLIP_PICTURE_PX 560

static struct desk_clip g_clips[DESK_CLIPS_MAX];
static int g_n;
static unsigned g_gen = 1, g_next_id = 1;
static struct desk_server *g_server;
static struct wl_listener g_on_selection;
static bool g_listening;

/* the source the newest was read from, or our own for it, while it is the clipboard */
static struct wlr_data_source *g_front_src;
static struct wl_listener g_front_gone;

static const struct wlr_data_source_impl clip_source_impl;

static void clip_free(struct desk_clip *c)
{
	free(c->data);
	if (c->picture) {
		cairo_surface_destroy(c->picture);
	}
	memset(c, 0, sizeof *c);
}

static void front_forget(void)
{
	if (g_front_src) {
		wl_list_remove(&g_front_gone.link);
		g_front_src = NULL;
	}
}

static void front_gone(struct wl_listener *l, void *data)
{
	(void)l;
	(void)data;
	front_forget();
	g_gen++;
}

static void front_is(struct wlr_data_source *src)
{
	front_forget();
	if (src) {
		g_front_src = src;
		g_front_gone.notify = front_gone;
		wl_signal_add(&src->events.destroy, &g_front_gone);
	}
}

/* ---- what is kept ---- */

static void take_out(int i)
{
	memmove(&g_clips[i], &g_clips[i + 1], (size_t)(g_n - i - 1) * sizeof g_clips[0]);
	g_n--;
	memset(&g_clips[g_n], 0, sizeof g_clips[0]);
}

/* the same as one kept already: that one, or -1 */
static int kept_as(int kind, const unsigned char *data, size_t len)
{
	for (int i = 0; i < g_n; i++) {
		if (g_clips[i].kind == kind && g_clips[i].len == len && !memcmp(g_clips[i].data, data, len)) {
			return i;
		}
	}
	return -1;
}

struct png_read {
	const unsigned char *data;
	size_t len, at;
};

static cairo_status_t png_chunk(void *closure, unsigned char *out, unsigned int n)
{
	struct png_read *r = closure;
	if (r->at + n > r->len) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy(out, r->data + r->at, n);
	r->at += n;
	return CAIRO_STATUS_SUCCESS;
}

/* A PNG, made small enough for a slip; NULL if it is not one. */
static cairo_surface_t *picture_of(const unsigned char *data, size_t len, int *w, int *h)
{
	*w = *h = 0;
	static const unsigned char SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	if (len < 8 || memcmp(data, SIG, 8)) {
		return NULL;
	}
	struct png_read r = { data, len, 0 };
	cairo_surface_t *full = cairo_image_surface_create_from_png_stream(png_chunk, &r);
	if (cairo_surface_status(full) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(full);
		return NULL;
	}
	*w = cairo_image_surface_get_width(full);
	*h = cairo_image_surface_get_height(full);
	double k = fmin(1.0, (double)CLIP_PICTURE_PX / fmax(*w, *h));
	if (k >= 1.0 || *w < 1 || *h < 1) {
		return full;
	}
	int sw = (int)fmax(1.0, lround(*w * k)), sh = (int)fmax(1.0, lround(*h * k));
	cairo_surface_t *small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	cairo_t *cr = cairo_create(small);
	cairo_scale(cr, k, k);
	cairo_set_source_surface(cr, full, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(full);
	return small;
}

/* A new one, newest: taken over (data is the clip's now). */
static void keep(int kind, unsigned char *data, size_t len, const char *mime)
{
	int was = kept_as(kind, data, len);
	if (was >= 0) {
		/* copied again: it moves to the front */
		struct desk_clip c = g_clips[was];
		take_out(was);
		memmove(&g_clips[1], &g_clips[0], (size_t)g_n * sizeof g_clips[0]);
		g_clips[0] = c;
		g_clips[0].at = time(NULL);
		g_n++;
		free(data);
		g_gen++;
		return;
	}
	if (g_n == DESK_CLIPS_MAX) {
		clip_free(&g_clips[g_n - 1]);
		g_n--;
	}
	memmove(&g_clips[1], &g_clips[0], (size_t)g_n * sizeof g_clips[0]);
	g_n++;
	struct desk_clip *c = &g_clips[0];
	memset(c, 0, sizeof *c);
	c->kind = kind;
	c->data = data;
	c->len = len;
	snprintf(c->mime, sizeof c->mime, "%s", mime);
	c->at = time(NULL);
	c->id = g_next_id++;
	if (kind == DESK_CLIP_IMAGE) {
		c->picture = picture_of(data, len, &c->img_w, &c->img_h);
		/* and not more pictures than the desk should hold for them */
		size_t all = 0;
		for (int i = 0; i < g_n; i++) {
			if (g_clips[i].kind == DESK_CLIP_IMAGE) {
				all += g_clips[i].len;
				if (all > CLIP_IMAGES_MOST && i > 0) {
					clip_free(&g_clips[i]);
					take_out(i);
					i--;
				}
			}
		}
	}
	g_gen++;
}

/* ---- reading what has been copied ---- */

static struct {
	int fd;
	struct wl_event_source *src;
	unsigned char *buf;
	size_t len, cap, most;
	int kind;
	char mime[64];
	struct wlr_data_source *from;
} g_rd;

static void read_end(void)
{
	if (g_rd.src) {
		wl_event_source_remove(g_rd.src);
	}
	if (g_rd.fd > 0) {
		close(g_rd.fd);
	}
	free(g_rd.buf);
	memset(&g_rd, 0, sizeof g_rd);
}

static int read_some(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;
	for (;;) {
		if (g_rd.len + 65536 + 1 > g_rd.cap) {
			size_t cap = g_rd.cap ? g_rd.cap * 2 : 65536 * 2;
			unsigned char *n = realloc(g_rd.buf, cap);
			if (!n) {
				read_end();
				return 0;
			}
			g_rd.buf = n;
			g_rd.cap = cap;
		}
		ssize_t n = read(fd, g_rd.buf + g_rd.len, 65536);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && errno == EAGAIN) {
			return 0;
		}
		if (n > 0) {
			g_rd.len += (size_t)n;
			if (g_rd.len > g_rd.most) {
				wlr_log(WLR_INFO, "clipboard: more than %zu bytes copied; not kept", g_rd.most);
				read_end();
				return 0;
			}
			continue;
		}
		break;
	}
	/* all of it, or the program has gone */
	if (g_rd.len == 0) {
		read_end();
		return 0;
	}
	unsigned char *bytes = g_rd.buf;
	size_t len = g_rd.len;
	int kind = g_rd.kind;
	char mime[64];
	snprintf(mime, sizeof mime, "%s", g_rd.mime);
	struct wlr_data_source *from = g_rd.from;
	g_rd.buf = NULL;
	read_end();
	if (kind == DESK_CLIP_TEXT) {
		bytes[len] = '\0';   /* (there is room: the buffer is a read ahead of it) */
		/* nothing but blanks is nothing to keep */
		size_t i = 0;
		while (i < len && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\n' || bytes[i] == '\r')) {
			i++;
		}
		if (i == len) {
			free(bytes);
			return 0;
		}
		len++;   /* the 0 is kept with it */
	}
	/* (what it was, never what it says) */
	wlr_log(WLR_INFO, "clipboard: kept %s, %zu bytes", kind == DESK_CLIP_TEXT ? "words" : mime, len);
	keep(kind, bytes, len, mime);
	if (g_server && desk_seat(g_server)->selection_source == from) {
		front_is(from);
	}
	if (g_server) {
		server_schedule_frames(g_server);
	}
	return 0;
}

static bool offers(const struct wlr_data_source *src, const char *mime)
{
	char **p;
	wl_array_for_each(p, &src->mime_types) {
		if (!strcmp(*p, mime)) {
			return true;
		}
	}
	return false;
}

static void on_selection(struct wl_listener *l, void *data)
{
	(void)l;
	struct wlr_seat *seat = data;
	struct wlr_data_source *src = seat->selection_source;
	read_end();
	front_forget();
	g_gen++;
	if (!src) {
		return;
	}
	if (src->impl == &clip_source_impl) {
		/* one of ours, copied again from a slip: it is at the front already */
		front_is(src);
		return;
	}
	if (offers(src, "x-kde-passwordManagerHint")) {
		/* a secret: a password manager's, which it will take back itself */
		wlr_log(WLR_INFO, "clipboard: a secret was copied; not kept");
		return;
	}
	static const char *const TEXT[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain",
		"STRING", "TEXT" };
	static const char *const IMAGE[] = { "image/png", "image/jpeg", "image/webp", "image/bmp",
		"image/gif" };
	const char *mime = NULL;
	int kind = DESK_CLIP_TEXT;
	for (size_t i = 0; i < sizeof TEXT / sizeof TEXT[0] && !mime; i++) {
		if (offers(src, TEXT[i])) {
			mime = TEXT[i];
		}
	}
	for (size_t i = 0; i < sizeof IMAGE / sizeof IMAGE[0] && !mime; i++) {
		if (offers(src, IMAGE[i])) {
			mime = IMAGE[i];
			kind = DESK_CLIP_IMAGE;
		}
	}
	int fds[2];
	if (!mime || !g_server || pipe(fds) != 0) {
		return;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	g_rd.fd = fds[0];
	g_rd.kind = kind;
	g_rd.most = kind == DESK_CLIP_TEXT ? CLIP_TEXT_MOST : CLIP_IMAGE_MOST;
	g_rd.from = src;
	snprintf(g_rd.mime, sizeof g_rd.mime, "%s", kind == DESK_CLIP_TEXT ? "text/plain;charset=utf-8" : mime);
	g_rd.src = wl_event_loop_add_fd(wl_display_get_event_loop(desk_display(g_server)), fds[0],
		WL_EVENT_READABLE, read_some, NULL);
	wlr_data_source_send(src, mime, fds[1]);
}

/* ---- offering one again ---- */

struct clip_source {
	struct wlr_data_source base;
	unsigned char *data;
	size_t len;
};

/* What is being written out to one who asked: as much as the pipe takes each time. */
struct clip_send {
	int fd;
	struct wl_event_source *src;
	unsigned char *data;
	size_t len, at;
};

static void send_end(struct clip_send *s)
{
	if (s->src) {
		wl_event_source_remove(s->src);
	}
	close(s->fd);
	free(s->data);
	free(s);
}

static int send_some(int fd, uint32_t mask, void *data)
{
	struct clip_send *s = data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		send_end(s);
		return 0;
	}
	while (s->at < s->len) {
		ssize_t n = write(fd, s->data + s->at, s->len - s->at);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && errno == EAGAIN) {
			return 0;
		}
		if (n <= 0) {
			break;
		}
		s->at += (size_t)n;
	}
	send_end(s);
	return 0;
}

static void clip_source_send(struct wlr_data_source *source, const char *mime, int32_t fd)
{
	(void)mime;
	struct clip_source *cs = wl_container_of(source, cs, base);
	struct clip_send *s = calloc(1, sizeof *s);
	if (!s || !g_server || !(s->data = malloc(cs->len ? cs->len : 1))) {
		free(s);
		close(fd);
		return;
	}
	memcpy(s->data, cs->data, cs->len);
	s->len = cs->len;
	s->fd = fd;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	s->src = wl_event_loop_add_fd(wl_display_get_event_loop(desk_display(g_server)), fd,
		WL_EVENT_WRITABLE, send_some, s);
	if (!s->src) {
		send_end(s);
	}
}

static void clip_source_destroy(struct wlr_data_source *source)
{
	struct clip_source *cs = wl_container_of(source, cs, base);
	free(cs->data);
	free(cs);
}

static const struct wlr_data_source_impl clip_source_impl = {
	.send = clip_source_send,
	.destroy = clip_source_destroy,
};

static void offer_as(struct clip_source *cs, const char *mime)
{
	char **p = wl_array_add(&cs->base.mime_types, sizeof *p);
	if (p) {
		*p = strdup(mime);
	}
}

bool desk_clips_copy(struct desk_server *server, int i)
{
	if (i < 0 || i >= g_n) {
		return false;
	}
	struct clip_source *cs = calloc(1, sizeof *cs);
	if (!cs) {
		return false;
	}
	wlr_data_source_init(&cs->base, &clip_source_impl);
	const struct desk_clip *c = &g_clips[i];
	/* the words without the 0 kept after them */
	cs->len = c->kind == DESK_CLIP_TEXT && c->len ? c->len - 1 : c->len;
	cs->data = malloc(cs->len ? cs->len : 1);
	if (!cs->data) {
		wlr_data_source_destroy(&cs->base);
		return false;
	}
	memcpy(cs->data, c->data, cs->len);
	if (c->kind == DESK_CLIP_TEXT) {
		static const char *const TEXT[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING",
			"STRING", "TEXT" };
		for (size_t k = 0; k < sizeof TEXT / sizeof TEXT[0]; k++) {
			offer_as(cs, TEXT[k]);
		}
	} else {
		offer_as(cs, c->mime);
	}
	if (i > 0) {
		struct desk_clip front = g_clips[i];
		take_out(i);
		memmove(&g_clips[1], &g_clips[0], (size_t)g_n * sizeof g_clips[0]);
		g_clips[0] = front;
		g_n++;
	}
	g_clips[0].at = time(NULL);
	g_gen++;
	wlr_seat_set_selection(desk_seat(server), &cs->base, wl_display_next_serial(desk_display(server)));
	return true;
}

/* ---- the rest ---- */

void desk_clips_start(struct desk_server *server)
{
	if (g_listening) {
		return;
	}
	g_server = server;
	g_on_selection.notify = on_selection;
	wl_signal_add(&desk_seat(server)->events.set_selection, &g_on_selection);
	g_listening = true;
}

void desk_clips_stop(void)
{
	read_end();
	front_forget();
	if (g_listening) {
		wl_list_remove(&g_on_selection.link);
		g_listening = false;
	}
	desk_clips_clear();
	g_server = NULL;
}

int desk_clips_n(void)
{
	return g_n;
}

const struct desk_clip *desk_clips_at(int i)
{
	return i >= 0 && i < g_n ? &g_clips[i] : NULL;
}

unsigned desk_clips_generation(void)
{
	return g_gen;
}

bool desk_clips_current(void)
{
	return g_n > 0 && g_front_src && g_server && desk_seat(g_server)->selection_source == g_front_src;
}

void desk_clips_remove(int i)
{
	if (i < 0 || i >= g_n) {
		return;
	}
	if (i == 0) {
		front_forget();
	}
	clip_free(&g_clips[i]);
	take_out(i);
	g_gen++;
}

void desk_clips_clear(void)
{
	front_forget();
	for (int i = 0; i < g_n; i++) {
		clip_free(&g_clips[i]);
	}
	g_n = 0;
	g_gen++;
}
