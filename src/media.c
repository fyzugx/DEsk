/* ================================================================
 * What is playing.
 *
 * Every player that wants to be driven from outside takes a name under
 * org.mpris.MediaPlayer2 on the session bus and keeps two objects at
 * /org/mpris/MediaPlayer2: the player itself (Identity, Raise) and its
 * transport (PlaybackStatus, Metadata, Position, Next, PlayPause...).
 * They come and go with NameOwnerChanged, say what changed with
 * PropertiesChanged, and say nothing at all as the position runs on, so
 * that is worked out here from the last they said and the rate.
 * ================================================================ */

#define _GNU_SOURCE
#include "media.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

#define MEDIA_MAX 8
#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_ROOT "org.mpris.MediaPlayer2"
#define MPRIS_PLAYER "org.mpris.MediaPlayer2.Player"
#define PROPS "org.freedesktop.DBus.Properties"

enum { ST_STOPPED = 0, ST_PAUSED, ST_PLAYING };

struct player {
	bool used;
	unsigned id;
	char name[128];          /* org.mpris.MediaPlayer2.vlc */
	char owner[64];          /* :1.42, what its signals come from */
	char identity[64];
	char title[128], artist[128];
	char track[256];         /* what tells one track from the next */
	char track_id[128];      /* what it calls the track, to be sent somewhere in it */
	int status;
	double length;           /* seconds; < 0 unknown */
	double pos, pos_at;      /* seconds, and when that was so; pos < 0 unknown */
	double stale;            /* what it goes on adding from the last track; 0 none */
	double rate;
	bool can_prev, can_next, can_play, can_pause, can_raise, can_seek;
	double active_at;        /* when it last started playing, or changed track */
	double since;            /* when the track it has came up */
	double asked_at;
	sd_bus_slot *call;       /* the GetAll on its way */
	bool again;              /* and another wanted when it comes */
	bool got;                /* it has answered once */
};

static struct wl_event_loop *g_loop;
static void (*g_changed)(void *);
static void *g_user;
static sd_bus *g_bus;
static struct wl_event_source *g_src;
static sd_bus_slot *g_match[3];
static struct player g_players[MEDIA_MAX];
static unsigned g_next_id = 1;

static double now_mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void str_copy(char *dst, size_t n, const char *src)
{
	if (!n) {
		return;
	}
	if (!src) {
		src = "";
	}
	size_t l = strlen(src);
	if (l >= n) {
		l = n - 1;
		/* not in the middle of a character */
		while (l > 0 && ((unsigned char)src[l] & 0xC0) == 0x80) {
			l--;
		}
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static void changed(void)
{
	if (g_changed) {
		g_changed(g_user);
	}
}

static struct player *player_by_id(unsigned id)
{
	for (int i = 0; i < MEDIA_MAX; i++) {
		if (g_players[i].used && g_players[i].id == id) {
			return &g_players[i];
		}
	}
	return NULL;
}

static struct player *player_by_name(const char *name)
{
	for (int i = 0; i < MEDIA_MAX; i++) {
		if (g_players[i].used && strcmp(g_players[i].name, name) == 0) {
			return &g_players[i];
		}
	}
	return NULL;
}

static void player_drop(struct player *p)
{
	if (p->call) {
		sd_bus_slot_unref(p->call);
	}
	memset(p, 0, sizeof *p);
}

/* ---- reading what it says ------------------------------------------ */

/* A variant holding a number of any of the kinds players use for one. */
static bool read_number(sd_bus_message *m, double *out)
{
	char type;
	const char *contents;
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v' || !contents) {
		return false;
	}
	if (strlen(contents) != 1 || !strchr("xtiudnqy", contents[0])) {
		sd_bus_message_skip(m, "v");
		return false;
	}
	if (sd_bus_message_enter_container(m, 'v', contents) < 0) {
		return false;
	}
	bool ok = true;
	switch (contents[0]) {
	case 'x': { int64_t v; ok = sd_bus_message_read_basic(m, 'x', &v) >= 0; *out = (double)v; break; }
	case 't': { uint64_t v; ok = sd_bus_message_read_basic(m, 't', &v) >= 0; *out = (double)v; break; }
	case 'i': { int32_t v; ok = sd_bus_message_read_basic(m, 'i', &v) >= 0; *out = v; break; }
	case 'u': { uint32_t v; ok = sd_bus_message_read_basic(m, 'u', &v) >= 0; *out = v; break; }
	case 'd': { double v; ok = sd_bus_message_read_basic(m, 'd', &v) >= 0; *out = v; break; }
	case 'n': { int16_t v; ok = sd_bus_message_read_basic(m, 'n', &v) >= 0; *out = v; break; }
	case 'q': { uint16_t v; ok = sd_bus_message_read_basic(m, 'q', &v) >= 0; *out = v; break; }
	case 'y': { uint8_t v; ok = sd_bus_message_read_basic(m, 'y', &v) >= 0; *out = v; break; }
	default: ok = false; break;
	}
	sd_bus_message_exit_container(m);
	return ok;
}

/* A variant holding a string (or an object path, which some players give
 * a track's id as a string instead of). */
static bool read_string(sd_bus_message *m, char *out, size_t n)
{
	char type;
	const char *contents;
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v' || !contents) {
		return false;
	}
	if (strcmp(contents, "s") != 0 && strcmp(contents, "o") != 0) {
		sd_bus_message_skip(m, "v");
		return false;
	}
	const char *s = NULL;
	if (sd_bus_message_enter_container(m, 'v', contents) < 0) {
		return false;
	}
	bool ok = sd_bus_message_read_basic(m, contents[0], &s) >= 0;
	sd_bus_message_exit_container(m);
	if (ok) {
		str_copy(out, n, s);
	}
	return ok;
}

static bool read_bool(sd_bus_message *m, bool *out)
{
	char type;
	const char *contents;
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v' || !contents) {
		return false;
	}
	if (strcmp(contents, "b") != 0) {
		sd_bus_message_skip(m, "v");
		return false;
	}
	int b = 0;
	if (sd_bus_message_enter_container(m, 'v', "b") < 0) {
		return false;
	}
	bool ok = sd_bus_message_read_basic(m, 'b', &b) >= 0;
	sd_bus_message_exit_container(m);
	if (ok) {
		*out = b != 0;
	}
	return ok;
}

/* xesam:artist is a list of names; a few players send one string. */
static void read_artists(sd_bus_message *m, char *out, size_t n)
{
	char type;
	const char *contents;
	out[0] = '\0';
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v' || !contents) {
		return;
	}
	if (strcmp(contents, "s") == 0) {
		read_string(m, out, n);
		return;
	}
	if (strcmp(contents, "as") != 0 || sd_bus_message_enter_container(m, 'v', "as") < 0) {
		sd_bus_message_skip(m, "v");
		return;
	}
	if (sd_bus_message_enter_container(m, 'a', "s") >= 0) {
		const char *s;
		while (sd_bus_message_read_basic(m, 's', &s) > 0) {
			if (!s || !*s) {
				continue;
			}
			size_t l = strlen(out);
			if (l && l + 2 < n) {
				strcat(out, ", ");
				l += 2;
			}
			str_copy(out + l, n - l, s);
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
}

/* The last part of a file's address, with its %-escapes undone and its
 * extension off: what to call a track that has no title. */
static void title_from_url(const char *url, char *out, size_t n)
{
	const char *slash = strrchr(url, '/');
	const char *s = slash ? slash + 1 : url;
	size_t k = 0;
	for (; *s && *s != '?' && *s != '#' && k + 1 < n; s++) {
		if (*s == '%' && s[1] && s[2]) {
			char hex[3] = { s[1], s[2], 0 };
			char *end;
			long v = strtol(hex, &end, 16);
			if (*end == '\0') {
				out[k++] = (char)v;
				s += 2;
				continue;
			}
		}
		out[k++] = *s;
	}
	out[k] = '\0';
	char *dot = strrchr(out, '.');
	if (dot && dot != out && strlen(dot) <= 5) {
		*dot = '\0';
	}
}

/* Where it is by our own count: what it last said, and the time since if
 * it is playing. < 0 if it has not said. */
static double where(const struct player *p, double now)
{
	if (p->pos < 0.0) {
		return -1.0;
	}
	double s = p->pos;
	if (p->status == ST_PLAYING && now > p->pos_at) {
		s += (now - p->pos_at) * p->rate;
	}
	return s;
}

/* Where the player says it is. Some say less than the truth: Spotify, the
 * track just changed, goes on counting from where the last one had got
 * to -- what is said is that and the time since, and that is taken off;
 * and some (Spotify before) say 0 all along while playing, which says
 * nothing. Anything else is believed. */
static void told_position(struct player *p, double s, double now)
{
	double ours = where(p, now);
	if (ours >= 0.0) {
		if (p->stale > 0.0 && fabs(s - p->stale - ours) < 2.0) {
			s -= p->stale;
		} else if (s == 0.0 && p->status == ST_PLAYING && ours > 3.0) {
			return;
		} else {
			p->stale = 0.0;
		}
	}
	p->pos = s;
	p->pos_at = now;
}

static void read_metadata(struct player *p, sd_bus_message *m, double now)
{
	char type;
	const char *contents;
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v' || !contents
		|| strcmp(contents, "a{sv}") != 0 || sd_bus_message_enter_container(m, 'v', "a{sv}") < 0) {
		sd_bus_message_skip(m, "v");
		return;
	}
	char title[128] = { 0 }, artist[128] = { 0 }, url[512] = { 0 }, id[256] = { 0 };
	double length = -1.0;
	if (sd_bus_message_enter_container(m, 'a', "{sv}") >= 0) {
		while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
			const char *key = NULL;
			if (sd_bus_message_read_basic(m, 's', &key) < 0 || !key) {
				sd_bus_message_exit_container(m);
				break;
			}
			if (strcmp(key, "xesam:title") == 0) {
				read_string(m, title, sizeof title);
			} else if (strcmp(key, "xesam:artist") == 0) {
				read_artists(m, artist, sizeof artist);
			} else if (strcmp(key, "xesam:url") == 0) {
				read_string(m, url, sizeof url);
			} else if (strcmp(key, "mpris:trackid") == 0) {
				read_string(m, id, sizeof id);
			} else if (strcmp(key, "mpris:length") == 0) {
				double us;
				if (read_number(m, &us) && us > 0.0) {
					length = us / 1e6;
				}
			} else {
				sd_bus_message_skip(m, "v");
			}
			sd_bus_message_exit_container(m);
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
	if (!title[0] && url[0]) {
		title_from_url(url, title, sizeof title);
	}
	char track[256];
	snprintf(track, sizeof track, "%.100s|%.100s|%.50s", id, title, url);
	if (strcmp(track, p->track) != 0) {
		/* another track: from its beginning, whatever the player says
		 * next, and how far the last had got kept, for a player that
		 * goes on counting from there */
		if (p->track[0]) {
			/* (and a player that went on counting from the track
			 * before goes on from where it says this one got to) */
			double said = p->stale + fmax(where(p, now), 0.0);
			p->stale = said > 2.0 ? said : 0.0;
			p->pos = 0.0;
			p->pos_at = now;
		}
		str_copy(p->track, sizeof p->track, track);
		p->since = now;
		p->active_at = now;
	}
	str_copy(p->track_id, sizeof p->track_id, id);
	str_copy(p->title, sizeof p->title, title);
	str_copy(p->artist, sizeof p->artist, artist);
	p->length = length;
}

/* One property, its name read and the variant next. */
static void apply(struct player *p, const char *name, sd_bus_message *m, double now)
{
	if (strcmp(name, "PlaybackStatus") == 0) {
		char s[32] = { 0 };
		read_string(m, s, sizeof s);
		int st = strcmp(s, "Playing") == 0 ? ST_PLAYING : strcmp(s, "Paused") == 0 ? ST_PAUSED
			: ST_STOPPED;
		if (st != p->status) {
			if (st == ST_PLAYING) {
				p->active_at = now;
			}
			/* the position was running; hold it where it had got to */
			if (p->status == ST_PLAYING && p->pos >= 0.0) {
				p->pos += (now - p->pos_at) * p->rate;
				p->pos_at = now;
			}
			p->status = st;
		}
	} else if (strcmp(name, "Metadata") == 0) {
		read_metadata(p, m, now);
	} else if (strcmp(name, "Position") == 0) {
		double us;
		if (read_number(m, &us)) {
			told_position(p, us / 1e6, now);
		}
	} else if (strcmp(name, "Rate") == 0) {
		double r;
		if (read_number(m, &r) && r > 0.0 && r < 100.0) {
			p->rate = r;
		}
	} else if (strcmp(name, "CanGoNext") == 0) {
		read_bool(m, &p->can_next);
	} else if (strcmp(name, "CanGoPrevious") == 0) {
		read_bool(m, &p->can_prev);
	} else if (strcmp(name, "CanPlay") == 0) {
		read_bool(m, &p->can_play);
	} else if (strcmp(name, "CanPause") == 0) {
		read_bool(m, &p->can_pause);
	} else if (strcmp(name, "CanRaise") == 0) {
		read_bool(m, &p->can_raise);
	} else if (strcmp(name, "CanSeek") == 0) {
		read_bool(m, &p->can_seek);
	} else if (strcmp(name, "Identity") == 0) {
		read_string(m, p->identity, sizeof p->identity);
	} else {
		sd_bus_message_skip(m, "v");
	}
}

/* An a{sv} of properties, as GetAll and PropertiesChanged both have. */
static void apply_all(struct player *p, sd_bus_message *m)
{
	double now = now_mono();
	if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) {
		return;
	}
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char *name = NULL;
		if (sd_bus_message_read_basic(m, 's', &name) < 0 || !name) {
			sd_bus_message_exit_container(m);
			break;
		}
		apply(p, name, m, now);
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
}

/* ---- asking it ----------------------------------------------------- */

static void fetch(struct player *p);

static int on_ignore(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)m;
	(void)user;
	(void)err;
	return 0;
}

/* The player's own object: its name, and whether it can be brought up. */
static int on_root(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	struct player *p = player_by_id((unsigned)(uintptr_t)user);
	if (!p || sd_bus_message_is_method_error(m, NULL)) {
		return 0;
	}
	apply_all(p, m);
	changed();
	return 0;
}

/* One property asked for by itself, for a player that could not give them
 * all at once: which one is in the low bits of user. */
static const char *const ONE_PROP[] = { "PlaybackStatus", "Metadata", "Position" };

static int on_one(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	uintptr_t u = (uintptr_t)user;
	struct player *p = player_by_id((unsigned)(u >> 2));
	if (!p || sd_bus_message_is_method_error(m, NULL) || (u & 3) > 2) {
		return 0;
	}
	apply(p, ONE_PROP[u & 3], m, now_mono());
	p->got = true;
	changed();
	return 0;
}

static int on_player(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	struct player *p = player_by_id((unsigned)(uintptr_t)user);
	if (!p) {
		return 0;
	}
	p->call = sd_bus_slot_unref(p->call);
	if (sd_bus_message_is_method_error(m, NULL)) {
		/* One getter that throws spoils GetAll with some bindings; the
		 * ones that matter, one at a time. */
		for (uintptr_t k = 0; k < 3; k++) {
			sd_bus_call_method_async(g_bus, NULL, p->name, MPRIS_PATH, PROPS, "Get", on_one,
				(void *)(uintptr_t)(p->id << 2 | k), "ss", MPRIS_PLAYER, ONE_PROP[k]);
		}
	} else {
		apply_all(p, m);
		p->got = true;
		changed();
	}
	if (p->again) {
		p->again = false;
		fetch(p);
	}
	return 0;
}

/* Everything it has to say about what it is playing: once when it comes,
 * and again whenever it says something changed -- one at a time. */
static void fetch(struct player *p)
{
	if (p->call) {
		p->again = true;
		return;
	}
	p->asked_at = now_mono();
	if (sd_bus_call_method_async(g_bus, &p->call, p->name, MPRIS_PATH, PROPS, "GetAll",
			on_player, (void *)(uintptr_t)p->id, "s", MPRIS_PLAYER) < 0) {
		p->call = NULL;
	}
}

static struct player *player_add(const char *name, const char *owner)
{
	struct player *p = player_by_name(name);
	if (!p) {
		for (int i = 0; i < MEDIA_MAX && !p; i++) {
			if (!g_players[i].used) {
				p = &g_players[i];
			}
		}
		if (!p) {
			return NULL;   /* eight players at once is plenty */
		}
		memset(p, 0, sizeof *p);
		p->used = true;
		p->id = g_next_id++;
		str_copy(p->name, sizeof p->name, name);
		p->length = -1.0;
		p->pos = -1.0;
		p->rate = 1.0;
		p->active_at = now_mono();
	}
	str_copy(p->owner, sizeof p->owner, owner);
	/* until it says otherwise, what the name says */
	if (!p->identity[0]) {
		const char *s = name + strlen(MPRIS_PREFIX);
		size_t l = strcspn(s, ".");
		char id[64];
		str_copy(id, l + 1 < sizeof id ? l + 1 : sizeof id, s);
		str_copy(p->identity, sizeof p->identity, id);
	}
	sd_bus_call_method_async(g_bus, NULL, p->name, MPRIS_PATH, PROPS, "GetAll", on_root,
		(void *)(uintptr_t)p->id, "s", MPRIS_ROOT);
	fetch(p);
	return p;
}

/* playerctld is a player standing in for whichever other one was last
 * used: it would only be the same thing twice. */
static bool wanted(const char *name)
{
	return name && strncmp(name, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) == 0
		&& strncmp(name, MPRIS_PREFIX "playerctld", strlen(MPRIS_PREFIX "playerctld")) != 0;
}

static int on_owner(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	char *name = user;
	const char *owner = NULL;
	if (!sd_bus_message_is_method_error(m, NULL) && sd_bus_message_read(m, "s", &owner) >= 0
		&& owner && g_bus) {
		player_add(name, owner);
	}
	free(name);
	return 0;
}

static int on_names(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	if (sd_bus_message_is_method_error(m, NULL)
		|| sd_bus_message_enter_container(m, 'a', "s") < 0) {
		return 0;
	}
	const char *s;
	while (sd_bus_message_read_basic(m, 's', &s) > 0) {
		if (!wanted(s)) {
			continue;
		}
		char *name = strdup(s);
		if (name && sd_bus_call_method_async(g_bus, NULL, "org.freedesktop.DBus",
				"/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", on_owner, name,
				"s", name) < 0) {
			free(name);
		}
	}
	sd_bus_message_exit_container(m);
	return 0;
}

static int on_name_owner(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *name = NULL, *old = NULL, *owner = NULL;
	if (sd_bus_message_read(m, "sss", &name, &old, &owner) < 0 || !wanted(name)) {
		return 0;
	}
	struct player *p = player_by_name(name);
	if (!owner || !*owner) {
		if (p) {
			player_drop(p);
			changed();
		}
		return 0;
	}
	player_add(name, owner);
	return 0;
}

/* Something about the transport changed: what it says changed is taken as
 * it is, and everything asked for again besides -- some players only say
 * that a property is stale, not what it now is. */
static int on_props(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *sender = sd_bus_message_get_sender(m);
	const char *iface = NULL;
	if (!sender || sd_bus_message_read_basic(m, 's', &iface) < 0 || !iface) {
		return 0;
	}
	bool player = strcmp(iface, MPRIS_PLAYER) == 0, root = strcmp(iface, MPRIS_ROOT) == 0;
	if (!player && !root) {
		return 0;
	}
	for (int i = 0; i < MEDIA_MAX; i++) {
		struct player *p = &g_players[i];
		if (!p->used || strcmp(p->owner, sender) != 0) {
			continue;
		}
		sd_bus_message_rewind(m, true);
		sd_bus_message_skip(m, "s");
		apply_all(p, m);
		if (player) {
			fetch(p);
		}
	}
	changed();
	return 0;
}

static int on_seeked(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *sender = sd_bus_message_get_sender(m);
	int64_t us = 0;
	if (!sender || sd_bus_message_read(m, "x", &us) < 0) {
		return 0;
	}
	double now = now_mono();
	for (int i = 0; i < MEDIA_MAX; i++) {
		struct player *p = &g_players[i];
		if (p->used && strcmp(p->owner, sender) == 0) {
			/* a jump it says it made is where it is, all of it */
			p->pos = us / 1e6;
			p->pos_at = now;
			p->stale = 0.0;
		}
	}
	changed();
	return 0;
}

/* ---- the one to show ----------------------------------------------- */

static int standing(const struct player *p)
{
	if (p->status == ST_PLAYING) {
		return 3;
	}
	if (p->status == ST_PAUSED) {
		return 2;
	}
	return p->title[0] ? 1 : 0;
}

static struct player *current(void)
{
	struct player *best = NULL;
	for (int i = 0; i < MEDIA_MAX; i++) {
		struct player *p = &g_players[i];
		if (!p->used || !p->got) {
			continue;
		}
		if (!best || standing(p) > standing(best)
			|| (standing(p) == standing(best) && p->active_at > best->active_at)) {
			best = p;
		}
	}
	return best;
}

/* The player of a program, by what it calls itself: a browser's name on the bus has
 * less in it than its window's id does (org.mozilla.firefox: firefox; Google-chrome:
 * chrome), and it may be running several. */
static struct player *player_of(const char *app)
{
	static const char *const KNOWN[] = { "firefox", "chromium", "chrome", "brave", "edge",
		"vivaldi", "opera", "librewolf", "zen", "waterfox", "epiphany", "falkon" };
	char want[64] = "";
	if (app) {
		for (size_t i = 0; i < sizeof KNOWN / sizeof KNOWN[0] && !want[0]; i++) {
			if (strcasestr(app, KNOWN[i])) {
				str_copy(want, sizeof want, KNOWN[i]);
			}
		}
		if (!want[0]) {
			const char *dot = strrchr(app, '.');
			str_copy(want, sizeof want, dot ? dot + 1 : app);
		}
	}
	struct player *best = NULL;
	for (int i = 0; want[0] && i < MEDIA_MAX; i++) {
		struct player *p = &g_players[i];
		if (!p->used || !p->got || !(strcasestr(p->name, want) || strcasestr(p->identity, want))) {
			continue;
		}
		if (!best || standing(p) > standing(best)
			|| (standing(p) == standing(best) && p->active_at > best->active_at)) {
			best = p;
		}
	}
	return best ? best : current();
}

static void read_player(const struct player *p, struct desk_media *out, double now)
{
	memset(out, 0, sizeof *out);
	out->length = -1.0;
	out->position = -1.0;
	if (!p) {
		return;
	}
	out->have = true;
	out->playing = p->status == ST_PLAYING;
	out->stopped = p->status == ST_STOPPED;
	str_copy(out->title, sizeof out->title, p->title);
	str_copy(out->artist, sizeof out->artist, p->artist);
	str_copy(out->player, sizeof out->player, p->identity);
	out->length = p->length;
	if (p->pos >= 0.0 && p->status != ST_STOPPED) {
		double pos = p->pos;
		if (p->status == ST_PLAYING && now > p->pos_at) {
			pos += (now - p->pos_at) * p->rate;
		}
		if (p->length > 0.0 && pos > p->length) {
			pos = p->length;
		}
		out->position = pos;
	}
	out->since = p->since;
	out->can_prev = p->can_prev;
	out->can_next = p->can_next;
	out->can_play = p->can_play;
	out->can_pause = p->can_pause;
	out->can_raise = p->can_raise;
	out->can_seek = p->can_seek;
}

void desk_media_read(struct desk_media *out, double now)
{
	read_player(current(), out, now);
}

void desk_media_read_for(const char *app, struct desk_media *out, double now)
{
	read_player(player_of(app), out, now);
}

static void command(struct player *p, int what)
{
	if (!p || !g_bus) {
		return;
	}
	const char *iface = MPRIS_PLAYER, *method;
	switch (what) {
	case DESK_MEDIA_PREV: method = "Previous"; break;
	case DESK_MEDIA_NEXT: method = "Next"; break;
	case DESK_MEDIA_PLAY_PAUSE: method = "PlayPause"; break;
	case DESK_MEDIA_RAISE: iface = MPRIS_ROOT; method = "Raise"; break;
	default: return;
	}
	/* the one pressed stays the one shown, even paused */
	p->active_at = now_mono();
	sd_bus_call_method_async(g_bus, NULL, p->name, MPRIS_PATH, iface, method, on_ignore, NULL, "");
	sd_bus_flush(g_bus);
}

void desk_media_command(int what)
{
	command(current(), what);
}

void desk_media_command_for(const char *app, int what)
{
	command(player_of(app), what);
}

/* Sent somewhere: what it says next is what is so, but it is shown going there now. */
static void went(struct player *p, double at, double now)
{
	if (p->length > 0.0 && at > p->length) {
		at = p->length;
	}
	p->pos = at > 0.0 ? at : 0.0;
	p->pos_at = now;
	p->stale = 0.0;
	p->active_at = now;
	changed();
}

void desk_media_seek_for(const char *app, double by)
{
	struct player *p = player_of(app);
	if (!p || !g_bus || !p->can_seek) {
		return;
	}
	double now = now_mono();
	sd_bus_call_method_async(g_bus, NULL, p->name, MPRIS_PATH, MPRIS_PLAYER, "Seek", on_ignore, NULL,
		"x", (int64_t)(by * 1e6));
	sd_bus_flush(g_bus);
	double here = where(p, now);
	if (here >= 0.0) {
		went(p, here + by, now);
	}
}

void desk_media_goto_for(const char *app, double at)
{
	struct player *p = player_of(app);
	if (!p || !g_bus || !p->can_seek) {
		return;
	}
	double now = now_mono();
	if (p->track_id[0] == '/') {
		sd_bus_call_method_async(g_bus, NULL, p->name, MPRIS_PATH, MPRIS_PLAYER, "SetPosition", on_ignore,
			NULL, "ox", p->track_id, (int64_t)(at * 1e6));
	} else {
		/* (a player that gives its tracks no name can still be sent by so much) */
		double here = where(p, now);
		sd_bus_call_method_async(g_bus, NULL, p->name, MPRIS_PATH, MPRIS_PLAYER, "Seek", on_ignore, NULL,
			"x", (int64_t)((at - (here > 0.0 ? here : 0.0)) * 1e6));
	}
	sd_bus_flush(g_bus);
	went(p, at, now);
}

/* ---- the bus in the compositor's loop ------------------------------ */

static void pump(void)
{
	if (!g_bus) {
		return;
	}
	for (int k = 0; k < 256; k++) {
		if (sd_bus_process(g_bus, NULL) <= 0) {
			break;
		}
	}
	if (g_bus) {
		sd_bus_flush(g_bus);
	}
}

static int bus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		desk_media_finish();
		changed();
		return 0;
	}
	pump();
	return 0;
}

bool desk_media_init(struct wl_event_loop *loop, void (*on_changed)(void *), void *user)
{
	if (g_bus) {
		return true;
	}
	g_loop = loop;
	g_changed = on_changed;
	g_user = user;
	if (sd_bus_open_user(&g_bus) < 0) {
		g_bus = NULL;
		return false;
	}
	sd_bus_add_match(g_bus, &g_match[0],
		"type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
		"member='NameOwnerChanged',arg0namespace='org.mpris.MediaPlayer2'",
		on_name_owner, NULL);
	sd_bus_add_match(g_bus, &g_match[1],
		"type='signal',interface='" PROPS "',member='PropertiesChanged',path='" MPRIS_PATH "'",
		on_props, NULL);
	sd_bus_add_match(g_bus, &g_match[2],
		"type='signal',interface='" MPRIS_PLAYER "',member='Seeked',path='" MPRIS_PATH "'",
		on_seeked, NULL);
	sd_bus_call_method_async(g_bus, NULL, "org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "ListNames", on_names, NULL, "");
	int fd = sd_bus_get_fd(g_bus);
	if (fd >= 0 && loop) {
		g_src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, bus_ready, NULL);
	}
	pump();
	return true;
}

void desk_media_finish(void)
{
	for (int i = 0; i < MEDIA_MAX; i++) {
		if (g_players[i].used) {
			player_drop(&g_players[i]);
		}
	}
	if (g_src) {
		wl_event_source_remove(g_src);
		g_src = NULL;
	}
	for (int i = 0; i < 3; i++) {
		g_match[i] = sd_bus_slot_unref(g_match[i]);
	}
	if (g_bus) {
		sd_bus_flush_close_unref(g_bus);
		g_bus = NULL;
	}
}

void desk_media_tick(void)
{
	pump();
	/* A player that is playing is asked where it is now and then, in case
	 * it has jumped without saying so. */
	struct player *p = current();
	if (p && p->status == ST_PLAYING && now_mono() - p->asked_at > 5.0) {
		fetch(p);
		pump();
	}
}
