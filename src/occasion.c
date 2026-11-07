/* What day it is, and what it is like out at the front door (occasion.h). */

/* readlink is glibc's and the BSDs' */
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "occasion.h"
#include "run.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static struct desk_occasion_conf g_conf = { 0, 0, true, true };
static double g_offset;                  /* DESK_TODAY's, against the real clock */
static double g_lat = 35.0, g_lon = 135.0;

static pthread_mutex_t g_wx_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_wx = DESK_WX_CLEAR;       /* as last heard */
static double g_heavy;
static bool g_have_wx;
static int g_wx_test = -1;               /* DESK_WEATHER's */
static double g_heavy_test = 0.6;
static double g_asked_at = -1e9;         /* monotonic seconds */
static double g_started;
static atomic_bool g_busy;
static int g_wake[2] = { -1, -1 };
static void (*g_changed)(void *data);
static void *g_data;

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

time_t desk_occasion_now(void)
{
	return time(NULL) + (time_t)g_offset;
}

/* ---- where the house is ---- */

/* the zone a tzfile link points into: /usr/share/zoneinfo/Asia/Tokyo */
static bool zone_of_link(const char *path, char *out, size_t n)
{
	char buf[512];
	ssize_t k = readlink(path, buf, sizeof buf - 1);
	if (k <= 0) {
		return false;
	}
	buf[k] = '\0';
	const char *z = strstr(buf, "zoneinfo/");
	if (!z) {
		return false;
	}
	snprintf(out, n, "%s", z + 9);
	return out[0] != '\0';
}

/* the time zone's name: Asia/Tokyo */
static bool zone_name(char *out, size_t n)
{
	const char *tz = getenv("TZ");
	if (tz && *tz) {
		if (*tz == ':') {
			tz++;
		}
		const char *z = strstr(tz, "zoneinfo/");
		if (z || *tz != '/') {
			snprintf(out, n, "%s", z ? z + 9 : tz);
			return out[0] && out[0] != '/';
		}
		/* a tzfile of its own, as TZ=:/etc/localtime: where it points */
		if (zone_of_link(tz, out, n)) {
			return true;
		}
		if (strcmp(tz, "/etc/localtime") != 0) {
			return false;
		}
		/* the system's own: as if TZ were not set */
	}
	if (zone_of_link("/etc/localtime", out, n)) {
		return true;
	}
	FILE *f = fopen("/etc/timezone", "re");
	if (!f) {
		return false;
	}
	bool ok = fgets(out, (int)n, f) != NULL;
	fclose(f);
	if (ok) {
		out[strcspn(out, " \t\r\n")] = '\0';
	}
	return ok && out[0];
}

/* ISO 6709, as zone.tab has it: +DDMM[SS]+DDDMM[SS] */
static bool iso6709(const char *s, double *lat, double *lon)
{
	double v[2];
	for (int i = 0; i < 2; i++) {
		if (*s != '+' && *s != '-') {
			return false;
		}
		double sign = *s == '-' ? -1.0 : 1.0;
		s++;
		int dd = i == 0 ? 2 : 3;
		char dig[8];
		int n = 0;
		while (s[n] >= '0' && s[n] <= '9' && n < 7) {
			dig[n] = s[n];
			n++;
		}
		if (n != dd + 2 && n != dd + 4) {
			return false;
		}
		dig[n] = '\0';
		double deg = 0, min = 0, sec = 0;
		char part[4];
		memcpy(part, dig, (size_t)dd);
		part[dd] = '\0';
		deg = atof(part);
		memcpy(part, dig + dd, 2);
		part[2] = '\0';
		min = atof(part);
		if (n == dd + 4) {
			memcpy(part, dig + dd + 2, 2);
			part[2] = '\0';
			sec = atof(part);
		}
		v[i] = sign * (deg + min / 60.0 + sec / 3600.0);
		s += n;
	}
	*lat = v[0];
	*lon = v[1];
	return true;
}

static void locate(void)
{
	char zone[128];
	if (zone_name(zone, sizeof zone)) {
		static const char *const TAB[] = { "/usr/share/zoneinfo/zone1970.tab",
			"/usr/share/zoneinfo/zone.tab" };
		for (size_t i = 0; i < 2; i++) {
			FILE *f = fopen(TAB[i], "re");
			if (!f) {
				continue;
			}
			char line[512];
			bool found = false;
			while (!found && fgets(line, sizeof line, f)) {
				if (line[0] == '#') {
					continue;
				}
				char *save = NULL;
				strtok_r(line, "\t\n", &save);
				char *coord = strtok_r(NULL, "\t\n", &save);
				char *name = strtok_r(NULL, "\t\n", &save);
				if (coord && name && !strcmp(name, zone)) {
					found = iso6709(coord, &g_lat, &g_lon);
				}
			}
			fclose(f);
			if (found) {
				wlr_log(WLR_INFO, "occasion: the house is about %.1f, %.1f (%s)", g_lat, g_lon,
					zone);
				return;
			}
		}
	}
	/* somewhere along the time zone's middle, in the north */
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	g_lat = 35.0;
	g_lon = tm.tm_gmtoff / 3600.0 * 15.0;
}

/* ---- the sky ---- */

/* where the sun is, from the north through the east, and how high */
static void sun_at(time_t t, double lat, double lon, double *el, double *az)
{
	double d = t / 86400.0 + 2440587.5 - 2451545.0;
	double g = fmod(357.529 + 0.98560028 * d, 360.0) * M_PI / 180.0;
	double q = fmod(280.459 + 0.98564736 * d, 360.0);
	double L = (q + 1.915 * sin(g) + 0.020 * sin(2.0 * g)) * M_PI / 180.0;
	double e = (23.439 - 0.00000036 * d) * M_PI / 180.0;
	double ra = atan2(cos(e) * sin(L), cos(L));
	double dec = asin(sin(e) * sin(L));
	double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
	double ha = (gmst + lon) * M_PI / 180.0 - ra;
	double la = lat * M_PI / 180.0;
	*el = asin(sin(la) * sin(dec) + cos(la) * cos(dec) * cos(ha)) * 180.0 / M_PI;
	double a = atan2(-sin(ha), tan(dec) * cos(la) - sin(la) * cos(ha)) * 180.0 / M_PI;
	*az = fmod(a + 360.0, 360.0);
}

/* how full the moon is: from a new moon at 18:14 UTC on 6 January 2000 */
static double moon_full(time_t t)
{
	double ph = fmod((t - 947182440.0) / 86400.0 / 29.530588853, 1.0);
	if (ph < 0.0) {
		ph += 1.0;
	}
	return 0.5 - 0.5 * cos(2.0 * M_PI * ph);
}

/* ---- the weather ---- */

/* a WMO weather code, as the door has it */
static void wmo(int code, int *wx, double *heavy)
{
	static const struct { int code, wx; double heavy; } W[] = {
		{ 0, DESK_WX_CLEAR, 0 }, { 1, DESK_WX_CLEAR, 0 }, { 2, DESK_WX_CLOUDS, 0 },
		{ 3, DESK_WX_OVERCAST, 0 }, { 45, DESK_WX_FOG, 0 }, { 48, DESK_WX_FOG, 0 },
		{ 51, DESK_WX_RAIN, 0.15 }, { 53, DESK_WX_RAIN, 0.25 }, { 55, DESK_WX_RAIN, 0.35 },
		{ 56, DESK_WX_RAIN, 0.25 }, { 57, DESK_WX_RAIN, 0.40 },
		{ 61, DESK_WX_RAIN, 0.35 }, { 63, DESK_WX_RAIN, 0.60 }, { 65, DESK_WX_RAIN, 0.90 },
		{ 66, DESK_WX_RAIN, 0.50 }, { 67, DESK_WX_RAIN, 0.80 },
		{ 71, DESK_WX_SNOW, 0.30 }, { 73, DESK_WX_SNOW, 0.60 }, { 75, DESK_WX_SNOW, 0.90 },
		{ 77, DESK_WX_SNOW, 0.20 },
		{ 80, DESK_WX_RAIN, 0.45 }, { 81, DESK_WX_RAIN, 0.70 }, { 82, DESK_WX_RAIN, 1.0 },
		{ 85, DESK_WX_SNOW, 0.50 }, { 86, DESK_WX_SNOW, 0.90 },
		{ 95, DESK_WX_STORM, 0.80 }, { 96, DESK_WX_STORM, 1.0 }, { 99, DESK_WX_STORM, 1.0 },
	};
	for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
		if (W[i].code == code) {
			*wx = W[i].wx;
			*heavy = W[i].heavy;
			return;
		}
	}
	*wx = DESK_WX_CLOUDS;
	*heavy = 0.0;
}

/* what prog prints to its standard output, n at most; false if it could
 * not be run or said nothing */
static bool run_out(char *const argv[], char *out, size_t n)
{
	size_t got;
	unsigned char *b = desk_run_capture(argv, n - 1, &got, NULL);
	if (!b) {
		return false;
	}
	memcpy(out, b, got);
	out[got] = '\0';
	free(b);
	return got > 0;
}

static void *fetch(void *arg)
{
	double *ll = arg;
	char url[256];
	snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.2f"
		"&longitude=%.2f&current=weather_code", ll[0], ll[1]);
	free(ll);
	char out[8192];
	char *curl[] = { "curl", "-fsS", "--max-time", "20", url, NULL };
	char *wget[] = { "wget", "-q", "-T", "20", "-O", "-", url, NULL };
	bool ok = run_out(curl, out, sizeof out) || run_out(wget, out, sizeof out);
	const char *cur = ok ? strstr(out, "\"current\":{") : NULL;
	const char *code = cur ? strstr(cur, "\"weather_code\":") : NULL;
	if (code) {
		int wx;
		double heavy;
		wmo(atoi(code + 15), &wx, &heavy);
		pthread_mutex_lock(&g_wx_lock);
		bool changed = !g_have_wx || wx != g_wx || heavy != g_heavy;
		g_wx = wx;
		g_heavy = heavy;
		g_have_wx = true;
		pthread_mutex_unlock(&g_wx_lock);
		if (changed) {
			char c = 1;
			if (write(g_wake[1], &c, 1) < 0) {
				/* told already */
			}
		}
	}
	atomic_store(&g_busy, false);
	return NULL;
}

static void ask(void)
{
	if (atomic_exchange(&g_busy, true)) {
		return;
	}
	g_asked_at = mono();
	double *ll = malloc(2 * sizeof *ll);
	if (!ll) {
		atomic_store(&g_busy, false);
		return;
	}
	ll[0] = g_lat;
	ll[1] = g_lon;
	sigset_t all, old;
	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old);
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	pthread_t t;
	if (pthread_create(&t, &at, fetch, ll) != 0) {
		free(ll);
		atomic_store(&g_busy, false);
	}
	pthread_attr_destroy(&at);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
}

static int woken(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;
	char buf[16];
	while (read(fd, buf, sizeof buf) > 0) {
	}
	static const char *const NAME[DESK_WX_N] = { "clear", "cloudy", "overcast", "foggy",
		"raining", "snowing", "stormy" };
	pthread_mutex_lock(&g_wx_lock);
	int wx = g_wx;
	double heavy = g_heavy;
	pthread_mutex_unlock(&g_wx_lock);
	wlr_log(WLR_INFO, "occasion: it is %s out there (%.2f)", NAME[wx], heavy);
	if (g_changed) {
		g_changed(g_data);
	}
	return 0;
}

void desk_occasion_tick(void)
{
	if (!g_conf.weather || g_wx_test >= 0 || g_wake[1] < 0) {
		return;
	}
	double now = mono();
	/* the first a little after the desk is up, then every half hour */
	bool first = g_asked_at < 0.0;
	if ((first && now - g_started > 15.0) || (!first && now - g_asked_at > 1800.0)) {
		ask();
	}
}

/* ---- the day ---- */

static bool leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

unsigned desk_occasion_today(void)
{
	if (!g_conf.celebrations) {
		return 0;
	}
	time_t t = desk_occasion_now();
	struct tm tm;
	localtime_r(&t, &tm);
	unsigned o = 0;
	if (tm.tm_mon == 11 && (tm.tm_mday == 24 || tm.tm_mday == 25)) {
		o |= DESK_OCC_XMAS;
	}
	if (tm.tm_mon == 0 && tm.tm_mday <= 3) {
		o |= DESK_OCC_NEWYEAR;
	}
	if (tm.tm_mon == 9 && tm.tm_mday == 31) {
		o |= DESK_OCC_HALLOWEEN;
	}
	int bm = g_conf.bday_month, bd = g_conf.bday_day;
	if (bm >= 1 && bm <= 12 && bd >= 1) {
		if (tm.tm_mon + 1 == bm && tm.tm_mday == bd) {
			o |= DESK_OCC_BIRTHDAY;
		}
		/* the 29th of February, in a year without one: the day before */
		if (bm == 2 && bd == 29 && tm.tm_mon == 1 && tm.tm_mday == 28
			&& !leap(tm.tm_year + 1900)) {
			o |= DESK_OCC_BIRTHDAY;
		}
	}
	return o;
}

void desk_occasion_mood(struct desk_door_mood *m)
{
	memset(m, 0, sizeof *m);
	m->set = true;
	time_t t = desk_occasion_now();
	sun_at(t, g_lat, g_lon, &m->sun_el, &m->sun_az);
	m->moon = moon_full(t);
	if (g_wx_test >= 0) {
		m->weather = g_wx_test;
		m->heavy = g_heavy_test;
	} else if (g_conf.weather) {
		pthread_mutex_lock(&g_wx_lock);
		if (g_have_wx) {
			m->weather = g_wx;
			m->heavy = g_heavy;
		}
		pthread_mutex_unlock(&g_wx_lock);
	}
	unsigned occ = desk_occasion_today();
	m->decor = (occ & DESK_OCC_XMAS ? DESK_DECOR_XMAS : 0)
		| (occ & DESK_OCC_NEWYEAR ? DESK_DECOR_NEWYEAR : 0)
		| (occ & DESK_OCC_BIRTHDAY ? DESK_DECOR_BIRTHDAY : 0)
		| (occ & DESK_OCC_HALLOWEEN ? DESK_DECOR_HALLOWEEN : 0);
	struct tm tm;
	localtime_r(&t, &tm);
	m->year = tm.tm_year + 1900;
}

void desk_occasion_conf(const struct desk_occasion_conf *c)
{
	bool was = g_conf.weather;
	g_conf = *c;
	if (was && !c->weather) {
		pthread_mutex_lock(&g_wx_lock);
		g_have_wx = false;
		pthread_mutex_unlock(&g_wx_lock);
	} else if (!was && c->weather) {
		g_asked_at = -1e9;
		g_started = mono() - 14.0;
	}
}

/* DESK_TODAY: 2026-12-31, or 2026-12-31T23:59:50 */
static void test_clock(void)
{
	const char *s = getenv("DESK_TODAY");
	if (!s || !*s) {
		return;
	}
	struct tm tm;
	memset(&tm, 0, sizeof tm);
	int y, mo, d, h = 12, mi = 0, se = 0;
	int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se);
	if (n < 3) {
		return;
	}
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = se;
	tm.tm_isdst = -1;
	time_t want = mktime(&tm);
	if (want != (time_t)-1) {
		g_offset = difftime(want, time(NULL));
	}
}

static void test_weather(void)
{
	const char *s = getenv("DESK_WEATHER");
	if (!s || !*s) {
		return;
	}
	static const char *const NAME[DESK_WX_N] = { "clear", "clouds", "overcast", "fog", "rain",
		"snow", "storm" };
	for (int i = 0; i < DESK_WX_N; i++) {
		size_t k = strlen(NAME[i]);
		if (!strncmp(s, NAME[i], k) && (s[k] == '\0' || s[k] == ':')) {
			g_wx_test = i;
			if (s[k] == ':') {
				g_heavy_test = atof(s + k + 1);
			}
		}
	}
}

void desk_occasion_init(struct wl_event_loop *loop, void (*changed)(void *data), void *data)
{
	g_started = mono();
	locate();
	test_clock();
	test_weather();
	g_changed = changed;
	g_data = data;
	if (pipe2(g_wake, O_NONBLOCK | O_CLOEXEC) == 0) {
		wl_event_loop_add_fd(loop, g_wake[0], WL_EVENT_READABLE, woken, NULL);
	} else {
		g_wake[0] = g_wake[1] = -1;
	}
}
