/* ================================================================
 * What the instruments are reading.
 *
 * Small reads off /proc and /sys, and the mixer through whatever the
 * machine has. Nothing here blocks for long and nothing here guesses: an
 * instrument with nothing behind it says so.
 * ================================================================ */

#include "panel.h"

#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool read_first_line(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "re");
	if (!f) {
		return false;
	}
	bool ok = fgets(buf, (int)n, f) != NULL;
	fclose(f);
	return ok;
}

static double read_number(const char *path, double fallback)
{
	char buf[128];
	if (!read_first_line(path, buf, sizeof buf)) {
		return fallback;
	}
	return atof(buf);
}

/* ---- rates over a steady window -------------------------------------
 *
 * The processor's busy fraction and the network's traffic are what has
 * gone by since some earlier look, and the meters are looked at as often as
 * the Control Panel is drawn -- which a knob being turned asks for sixty
 * times a second. Since the last look, that was a sixtieth of a second:
 * one or two of the kernel's ticks, so 0, 50 and 100 per cent in turn, and
 * a needle that shook. So each is taken from the older of two marks, the
 * newer moved up every quarter second: over between a quarter and half a
 * second, whenever it is asked. */
struct rate_marks {
	double at[2], a[2], b[2];   /* [0] the older mark, [1] the newer */
	int n;
};

/* the counters a and b now: the change in each since the older mark, and
 * the seconds since it -- or, settled, between the two marks, which moves
 * only when the newer one does: a needle that need not be drawn again
 * more than four times a second; false until there is a mark to go from */
static bool rate_since(struct rate_marks *r, double now, double a, double b, bool settled,
	double *da, double *db, double *dt)
{
	if (r->n == 0) {
		r->at[0] = r->at[1] = now;
		r->a[0] = r->a[1] = a;
		r->b[0] = r->b[1] = b;
		r->n = 1;
		return false;
	}
	if (now - r->at[1] >= 0.25) {
		r->at[0] = r->at[1];
		r->a[0] = r->a[1];
		r->b[0] = r->b[1];
		r->at[1] = now;
		r->a[1] = a;
		r->b[1] = b;
	}
	if (settled) {
		*da = r->a[1] - r->a[0];
		*db = r->b[1] - r->b[0];
		*dt = r->at[1] - r->at[0];
	} else {
		*da = a - r->a[0];
		*db = b - r->b[0];
		*dt = now - r->at[0];
	}
	return *dt > 0.0;
}

/* ---- the processor ---------------------------------------------- */

static struct rate_marks g_cpu_marks;

static double cpu_busy(void)
{
	char buf[256];
	if (!read_first_line("/proc/stat", buf, sizeof buf)) {
		return 0.0;
	}
	unsigned long long v[10] = { 0 };
	int n = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
		&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
	if (n < 4) {
		return 0.0;
	}
	double total = 0.0;
	for (int i = 0; i < n; i++) {
		total += (double)v[i];
	}
	double idle = (double)v[3] + (double)v[4];
	double busy = total - idle;
	double dt, db, secs;
	if (!rate_since(&g_cpu_marks, now_sec(), total, busy, false, &dt, &db, &secs)
		|| dt <= 0.0) {
		return 0.0;
	}
	double f = db / dt;
	return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

static double mem_used(void)
{
	FILE *f = fopen("/proc/meminfo", "re");
	if (!f) {
		return 0.0;
	}
	double total = 0.0, avail = 0.0;
	char line[128];
	while (fgets(line, sizeof line, f)) {
		if (!total && strncmp(line, "MemTotal:", 9) == 0) {
			total = atof(line + 9);
		} else if (!avail && strncmp(line, "MemAvailable:", 13) == 0) {
			avail = atof(line + 13);
		}
		if (total > 0.0 && avail > 0.0) {
			break;
		}
	}
	fclose(f);
	if (total <= 0.0) {
		return 0.0;
	}
	double used = (total - avail) / total;
	return used < 0.0 ? 0.0 : (used > 1.0 ? 1.0 : used);
}

/* The warmest thing that will tell us how warm it is. A thermal zone if
 * there is one, otherwise whatever hwmon offers -- on a machine like this
 * that turns out to be the drive. */
static double temperature(void)
{
	double best = -1000.0;
	for (int i = 0; i < 16; i++) {
		char p[96];
		snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/temp", i);
		if (access(p, R_OK) != 0) {
			continue;
		}
		double t = read_number(p, -1e9) / 1000.0;
		if (t > best && t > -50.0 && t < 200.0) {
			best = t;
		}
	}
	if (best > -100.0) {
		return best;
	}
	for (int i = 0; i < 8; i++) {
		for (int k = 1; k <= 3; k++) {
			char p[96];
			snprintf(p, sizeof p, "/sys/class/hwmon/hwmon%d/temp%d_input", i, k);
			if (access(p, R_OK) != 0) {
				continue;
			}
			double t = read_number(p, -1e9) / 1000.0;
			if (t > best && t > -50.0 && t < 200.0) {
				best = t;
			}
		}
	}
	return best;
}

/* ---- the line ---------------------------------------------------- */

static struct rate_marks g_net_marks;

static void network(struct desk_meters *m)
{
	m->net_up = false;
	m->net_routed = false;
	m->net_rate = 0.0;
	m->net_if[0] = '\0';
	m->net_wireless = false;
	m->net_signal = 0.0;

	/* the interface the default route goes out of, if we can see one */
	FILE *f = fopen("/proc/net/route", "re");
	if (f) {
		char line[256];
		bool header = fgets(line, sizeof line, f) != NULL;
		while (header && fgets(line, sizeof line, f)) {
			char iface[32];
			unsigned long dest = 1;
			if (sscanf(line, "%31s %lx", iface, &dest) == 2 && dest == 0) {
				snprintf(m->net_if, sizeof m->net_if, "%s", iface);
				m->net_routed = true;
				break;
			}
		}
		fclose(f);
	}
	if (!m->net_if[0]) {
		/* no route: name the first interface that is not the loopback */
		DIR *d = opendir("/sys/class/net");
		struct dirent *e;
		while (d && (e = readdir(d)) != NULL) {
			if (e->d_name[0] == '.' || strcmp(e->d_name, "lo") == 0) {
				continue;
			}
			snprintf(m->net_if, sizeof m->net_if, "%.31s", e->d_name);
			break;
		}
		if (d) {
			closedir(d);
		}
	}
	if (!m->net_if[0]) {
		return;
	}

	char p[320], buf[64];
	snprintf(p, sizeof p, "/sys/class/net/%s/operstate", m->net_if);
	if (read_first_line(p, buf, sizeof buf) && strncmp(buf, "up", 2) == 0) {
		m->net_up = true;
	}
	snprintf(p, sizeof p, "/sys/class/net/%s/statistics/rx_bytes", m->net_if);
	double rx = read_number(p, 0.0);
	snprintf(p, sizeof p, "/sys/class/net/%s/statistics/tx_bytes", m->net_if);
	double tx = read_number(p, 0.0);
	double total = rx + tx, bytes, unused, secs;
	if (rate_since(&g_net_marks, now_sec(), total, 0.0, true, &bytes, &unused, &secs)) {
		double bps = bytes / secs;
		/* a needle, not a graph: log scale from a trickle to ten megabytes */
		if (bps > 1.0) {
			double r = log10(bps) / 7.0;
			m->net_rate = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
		}
	}

	/* A radio or a cable. The plate on the console shows dots, and dots
	 * are a radio's idea, so it matters which this is: a cable has no
	 * signal to be weak, it is either plugged in or it is not. */
	snprintf(p, sizeof p, "/sys/class/net/%s/wireless", m->net_if);
	struct stat st;
	m->net_wireless = stat(p, &st) == 0 && S_ISDIR(st.st_mode);
	if (!m->net_wireless) {
		return;
	}
	/* "iface: status link level noise ..." -- link is the driver's own
	 * quality figure, which in practice it reports out of seventy. */
	FILE *w = fopen("/proc/net/wireless", "re");
	if (!w) {
		return;
	}
	char line[256];
	if (fgets(line, sizeof line, w) && fgets(line, sizeof line, w)) {
		while (fgets(line, sizeof line, w)) {
			char iface[64];
			double status = 0.0, link = 0.0;
			if (sscanf(line, " %63[^:]: %lf %lf", iface, &status, &link) == 3
				&& strcmp(iface, m->net_if) == 0) {
				double q = link / 70.0;
				m->net_signal = q < 0.0 ? 0.0 : (q > 1.0 ? 1.0 : q);
				break;
			}
		}
	}
	fclose(w);
}

/* ---- the cell ---------------------------------------------------- */

static void battery(struct desk_meters *m)
{
	m->have_battery = false;
	m->battery = 1.0;
	m->on_mains = true;
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		char p[512], buf[64];
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
		if (!read_first_line(p, buf, sizeof buf)) {
			continue;
		}
		if (strncmp(buf, "Battery", 7) == 0) {
			/* a mouse's or a headset's is not the machine's */
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/scope", e->d_name);
			if (read_first_line(p, buf, sizeof buf) && strncmp(buf, "Device", 6) == 0) {
				continue;
			}
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
			double c = read_number(p, -1.0);
			if (c >= 0.0) {
				m->have_battery = true;
				m->battery = c / 100.0;
			}
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
			if (read_first_line(p, buf, sizeof buf)) {
				m->on_mains = strncmp(buf, "Discharging", 11) != 0;
			}
		} else if (strncmp(buf, "Mains", 5) == 0) {
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/online", e->d_name);
			m->on_mains = read_number(p, 1.0) > 0.5;
		}
	}
	closedir(d);
}

/* /sys, or the stand-in for it a test points DESK_SYSFS at */
static const char *sys_root(void);

/* ---- the radios -------------------------------------------------- */

static void radios(struct desk_meters *m)
{
	m->have_radio = false;
	m->wifi_blocked = false;
	m->bt_blocked = false;
	char dir[512];
	snprintf(dir, sizeof dir, "%s/class/rfkill", sys_root());
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		char p[1024], type[32];
		snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
		if (!read_first_line(p, type, sizeof type)) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/soft", dir, e->d_name);
		bool blocked = read_number(p, 0.0) > 0.5;
		m->have_radio = true;
		if (strncmp(type, "wlan", 4) == 0) {
			m->wifi_blocked = blocked;
		} else if (strncmp(type, "bluetooth", 9) == 0) {
			m->bt_blocked = blocked;
		}
	}
	closedir(d);
}

/* Throwing the lever for real, where there is a radio to throw it at. The
 * soft-block file is usually root's, so this is allowed to fail: on a
 * machine with nothing to switch -- or nothing it is allowed to switch --
 * the lever on the panel is still a lever, and says so by returning false
 * rather than by pretending. */
bool desk_meters_set_radio(bool wifi, bool on)
{
	char dir[512];
	snprintf(dir, sizeof dir, "%s/class/rfkill", sys_root());
	DIR *d = opendir(dir);
	if (!d) {
		return false;
	}
	bool done = false;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		char p[1024], type[32];
		snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
		if (!read_first_line(p, type, sizeof type)) {
			continue;
		}
		bool match = wifi ? strncmp(type, "wlan", 4) == 0
			: strncmp(type, "bluetooth", 9) == 0;
		if (!match) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/soft", dir, e->d_name);
		FILE *f = fopen(p, "we");
		if (!f) {
			continue;
		}
		/* soft is the block, so it is the other way up from the lever */
		if (fputs(on ? "0\n" : "1\n", f) >= 0) {
			done = true;
		}
		fclose(f);
	}
	closedir(d);
	return done;
}

/* ---- the mixer --------------------------------------------------- */

static int g_have_wpctl = -1;
static double g_vol_at, g_vol_cached = -1.0;
static double g_vol_set_at;   /* when it was last set from here */
static bool g_muted_cached;
static pthread_mutex_t g_vol_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool g_vol_asking;

/* Asking the mixer costs a process, and a process is a fork of whoever
 * asks. On the compositor's thread that was ten milliseconds of a frozen
 * desktop every other second, for a knob nobody was looking at. So the
 * question goes to a thread of its own, and the reading is always whatever
 * the last answer was; the knob is a second behind the mixer at worst,
 * which is as current as it ever was. */
static void *volume_ask(void *arg)
{
	(void)arg;
	double asked = now_sec();
	FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	if (f) {
		char buf[128] = { 0 };
		if (fgets(buf, sizeof buf, f)) {
			double v = 0.0;
			const char *p = strstr(buf, "Volume:");
			if (p) {
				v = atof(p + 7);
			}
			pthread_mutex_lock(&g_vol_lock);
			/* an answer to a question asked before the last change made
			 * here is about the mixer as it was, not as it is */
			if (g_vol_set_at < asked) {
				g_vol_cached = v < 0.0 ? 0.0 : (v > 1.5 ? 1.5 : v);
				g_muted_cached = strstr(buf, "MUTED") != NULL;
			}
			pthread_mutex_unlock(&g_vol_lock);
		}
		pclose(f);
	}
	atomic_store(&g_vol_asking, false);
	return NULL;
}

static void volume(struct desk_meters *m)
{
	if (g_have_wpctl < 0) {
		g_have_wpctl = access("/usr/bin/wpctl", X_OK) == 0;
	}
	m->have_volume = g_have_wpctl != 0;
	if (!m->have_volume) {
		m->volume = 0.0;
		m->muted = true;
		return;
	}
	double at = now_sec();
	pthread_mutex_lock(&g_vol_lock);
	bool stale = g_vol_cached < 0.0 || at - g_vol_at >= 1.20;
	if (stale) {
		g_vol_at = at;
	}
	m->volume = g_vol_cached < 0.0 ? 0.0 : g_vol_cached;
	m->muted = g_muted_cached;
	pthread_mutex_unlock(&g_vol_lock);
	if (stale && !atomic_exchange(&g_vol_asking, true)) {
		pthread_t th;
		if (pthread_create(&th, NULL, volume_ask, NULL) == 0) {
			pthread_detach(th);
		} else {
			atomic_store(&g_vol_asking, false);
		}
	}
}

/* Setting the mixer costs a process too, and a hand on a volume control
 * sends a new value every time it moves. So the value goes to a thread of
 * its own that sets whatever is the latest when it gets round to it: a
 * drag is a handful of processes, not one per pointer event, and none of
 * them on the compositor's thread. */
static pthread_mutex_t g_set_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_set_cond = PTHREAD_COND_INITIALIZER;
static bool g_set_running;
static double g_set_vol = -1.0;    /* to set; < 0 nothing waiting */
static int g_set_mute = -1;        /* 0 unmute, 1 mute, 2 toggle; < 0 nothing */

static void *volume_setter(void *arg)
{
	(void)arg;
	for (;;) {
		pthread_mutex_lock(&g_set_lock);
		while (g_set_vol < 0.0 && g_set_mute < 0) {
			pthread_cond_wait(&g_set_cond, &g_set_lock);
		}
		double v = g_set_vol;
		int mute = g_set_mute;
		g_set_vol = -1.0;
		g_set_mute = -1;
		pthread_mutex_unlock(&g_set_lock);
		char cmd[128];
		if (v >= 0.0) {
			snprintf(cmd, sizeof cmd,
				"wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f >/dev/null 2>&1", v);
			if (system(cmd) == -1) {
				/* nothing to do about it here; the reading will say */
			}
		}
		if (mute >= 0) {
			snprintf(cmd, sizeof cmd, "wpctl set-mute @DEFAULT_AUDIO_SINK@ %s >/dev/null 2>&1",
				mute == 2 ? "toggle" : mute ? "1" : "0");
			if (system(cmd) == -1) {
			}
		}
	}
	return NULL;
}

static bool setter_kick(void)
{
	/* under g_set_lock */
	if (!g_set_running) {
		pthread_t th;
		if (pthread_create(&th, NULL, volume_setter, NULL) != 0) {
			return false;
		}
		pthread_detach(th);
		g_set_running = true;
	}
	pthread_cond_signal(&g_set_cond);
	return true;
}

bool desk_meters_set_volume(double v)
{
	if (g_have_wpctl < 0) {
		g_have_wpctl = access("/usr/bin/wpctl", X_OK) == 0;
	}
	if (!g_have_wpctl) {
		return false;
	}
	if (v < 0.0) {
		v = 0.0;
	}
	if (v > 1.0) {
		v = 1.0;
	}
	pthread_mutex_lock(&g_set_lock);
	g_set_vol = v;
	bool ok = setter_kick();
	pthread_mutex_unlock(&g_set_lock);
	if (ok) {
		pthread_mutex_lock(&g_vol_lock);
		g_vol_cached = v;
		g_vol_at = g_vol_set_at = now_sec();
		pthread_mutex_unlock(&g_vol_lock);
	}
	return ok;
}

bool desk_meters_set_muted(int how)
{
	if (g_have_wpctl < 0) {
		g_have_wpctl = access("/usr/bin/wpctl", X_OK) == 0;
	}
	if (!g_have_wpctl) {
		return false;
	}
	pthread_mutex_lock(&g_set_lock);
	g_set_mute = how;
	bool ok = setter_kick();
	pthread_mutex_unlock(&g_set_lock);
	if (ok) {
		pthread_mutex_lock(&g_vol_lock);
		g_muted_cached = how == 2 ? !g_muted_cached : how != 0;
		g_vol_at = g_vol_set_at = now_sec();
		pthread_mutex_unlock(&g_vol_lock);
	}
	return ok;
}

void desk_meters_read_volume(struct desk_meters *out)
{
	volume(out);
}

static pthread_mutex_t g_latest_lock = PTHREAD_MUTEX_INITIALIZER;
static struct desk_meters g_latest;
static bool g_latest_have;
static double g_latest_at = -1.0;
static atomic_bool g_latest_asking;

static void *meters_ask(void *arg)
{
	(void)arg;
	struct desk_meters m;
	desk_meters_read(&m);
	pthread_mutex_lock(&g_latest_lock);
	g_latest = m;
	g_latest_have = true;
	pthread_mutex_unlock(&g_latest_lock);
	atomic_store(&g_latest_asking, false);
	return NULL;
}

void desk_meters_latest(struct desk_meters *out)
{
	if (!out) {
		return;
	}
	double at = now_sec();
	pthread_mutex_lock(&g_latest_lock);
	bool have = g_latest_have;
	if (have) {
		*out = g_latest;
	}
	bool stale = g_latest_at < 0.0 || at - g_latest_at >= 0.15;
	if (stale) {
		g_latest_at = at;
	}
	pthread_mutex_unlock(&g_latest_lock);
	if (!have) {
		/* the very first: there is nothing to show but a real reading */
		desk_meters_read(out);
		pthread_mutex_lock(&g_latest_lock);
		g_latest = *out;
		g_latest_have = true;
		pthread_mutex_unlock(&g_latest_lock);
		return;
	}
	if (stale && !atomic_exchange(&g_latest_asking, true)) {
		pthread_t th;
		if (pthread_create(&th, NULL, meters_ask, NULL) == 0) {
			pthread_detach(th);
		} else {
			atomic_store(&g_latest_asking, false);
		}
	}
}

void desk_meters_read(struct desk_meters *out)
{
	if (!out) {
		return;
	}
	memset(out, 0, sizeof *out);
	out->cpu = cpu_busy();
	out->mem = mem_used();
	out->temp_c = temperature();
	out->load = 0.0;
	char buf[128];
	if (read_first_line("/proc/loadavg", buf, sizeof buf)) {
		long n = sysconf(_SC_NPROCESSORS_ONLN);
		out->load = atof(buf) / (double)(n > 0 ? n : 1);
	}
	network(out);
	battery(out);
	radios(out);
	volume(out);
}

/* ---- what the machine is drawing -------------------------------- *
 *
 * A battery says what goes in and out of it. What the whole machine draws
 * off the wall is another matter: nothing on a desktop measures that. The
 * nearest things are the processor's own energy counter (RAPL, which the
 * kernel shows to root only unless somebody has opened it up) and a
 * graphics card that reports its own power. Whatever there is is used and
 * named; where there is nothing, it says so. DESK_SYSFS points all of
 * this at another tree, for trying it on a machine that has none of it. */

static const char *sys_root(void)
{
	const char *r = getenv("DESK_SYSFS");
	return r && *r ? r : "/sys";
}

static double g_rapl_uj = -1.0, g_rapl_at, g_rapl_w = -1.0;

/* The processor package's -- or the whole platform's, where there is a
 * "psys" domain -- energy counter, as watts since the last look. */
static double rapl_watts(void)
{
	char dir[512], p[1024], buf[64];
	snprintf(dir, sizeof dir, "%s/class/powercap", sys_root());
	DIR *d = opendir(dir);
	if (!d) {
		return -1.0;
	}
	double psys = -1.0, pkg = 0.0, range = 0.0;
	bool any = false;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.' || strchr(e->d_name, ':') == NULL) {
			continue;
		}
		/* the top level domains only: intel-rapl:0, not intel-rapl:0:0 */
		const char *c = strchr(e->d_name, ':');
		if (strchr(c + 1, ':')) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/name", dir, e->d_name);
		if (!read_first_line(p, buf, sizeof buf)) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/energy_uj", dir, e->d_name);
		double uj = read_number(p, -1.0);
		if (uj < 0.0) {
			continue;   /* root's only, as it usually is */
		}
		snprintf(p, sizeof p, "%s/%s/max_energy_range_uj", dir, e->d_name);
		double r = read_number(p, 0.0);
		if (strncmp(buf, "psys", 4) == 0) {
			psys = uj;
			range = r;
		} else if (strncmp(buf, "package", 7) == 0) {
			pkg += uj;
			range = r > range ? r : range;
			any = true;
		}
	}
	closedir(d);
	double uj = psys >= 0.0 ? psys : (any ? pkg : -1.0);
	if (uj < 0.0) {
		return -1.0;
	}
	double at = now_sec();
	if (g_rapl_uj >= 0.0 && at - g_rapl_at > 0.2) {
		double du = uj - g_rapl_uj;
		if (du < 0.0 && range > 0.0) {
			du += range;   /* the counter went round */
		}
		if (du >= 0.0) {
			g_rapl_w = du / 1e6 / (at - g_rapl_at);
		}
		g_rapl_uj = uj;
		g_rapl_at = at;
	} else if (g_rapl_uj < 0.0) {
		g_rapl_uj = uj;
		g_rapl_at = at;
	}
	return g_rapl_w;
}

/* A graphics card that says what it is drawing (amdgpu does, to anyone). */
static double gpu_watts(void)
{
	char dir[512], p[1024], buf[64];
	snprintf(dir, sizeof dir, "%s/class/hwmon", sys_root());
	DIR *d = opendir(dir);
	if (!d) {
		return -1.0;
	}
	double w = 0.0;
	bool any = false;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/name", dir, e->d_name);
		if (!read_first_line(p, buf, sizeof buf)) {
			continue;
		}
		if (strncmp(buf, "amdgpu", 6) != 0 && strncmp(buf, "radeon", 6) != 0
			&& strncmp(buf, "nouveau", 7) != 0 && strncmp(buf, "xe", 2) != 0) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/power1_average", dir, e->d_name);
		double uw = read_number(p, -1.0);
		if (uw < 0.0) {
			snprintf(p, sizeof p, "%s/%s/power1_input", dir, e->d_name);
			uw = read_number(p, -1.0);
		}
		if (uw >= 0.0) {
			w += uw / 1e6;
			any = true;
		}
	}
	closedir(d);
	return any ? w : -1.0;
}

/* How bright the screen is, where the machine sets that itself -- a
 * laptop's panel. Of several, the one that is the firmware's to set comes
 * first, then the platform's, then the graphics driver's own register:
 * the order logind and the desktops choose in. */
bool desk_backlight_find(char *name, size_t n, double *max_out, double *now_out)
{
	char dir[512], p[1024], buf[64];
	snprintf(dir, sizeof dir, "%s/class/backlight", sys_root());
	DIR *d = opendir(dir);
	if (!d) {
		return false;
	}
	int best = -1;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
		int rank = 0;
		if (read_first_line(p, buf, sizeof buf)) {
			rank = strncmp(buf, "firmware", 8) == 0 ? 3 : strncmp(buf, "platform", 8) == 0 ? 2 : 1;
		}
		if (rank <= best) {
			continue;
		}
		snprintf(p, sizeof p, "%s/%s/max_brightness", dir, e->d_name);
		double max = read_number(p, -1.0);
		/* actual_brightness is what the panel is at; brightness what was asked */
		snprintf(p, sizeof p, "%s/%s/actual_brightness", dir, e->d_name);
		double now = read_number(p, -1.0);
		if (now < 0.0) {
			snprintf(p, sizeof p, "%s/%s/brightness", dir, e->d_name);
			now = read_number(p, -1.0);
		}
		if (max <= 0.0 || now < 0.0) {
			continue;
		}
		best = rank;
		snprintf(name, n, "%s", e->d_name);
		*max_out = max;
		*now_out = now;
	}
	closedir(d);
	return best >= 0;
}

static void backlight(struct desk_supply *out)
{
	char name[256];
	double max, now;
	if (desk_backlight_find(name, sizeof name, &max, &now)) {
		out->have_backlight = true;
		out->backlight = now / max > 1.0 ? 1.0 : now / max;
	}
}

const char *desk_sys_root(void)
{
	return sys_root();
}

void desk_supply_read(struct desk_supply *out)
{
	memset(out, 0, sizeof *out);
	out->batt_w = -1.0;
	out->hours = -1.0;
	out->draw_w = -1.0;
	out->on_mains = true;
	backlight(out);
	char dir[512], p[1024], buf[64];
	snprintf(dir, sizeof dir, "%s/class/power_supply", sys_root());
	DIR *d = opendir(dir);
	double e_now = 0.0, e_full = 0.0, watts = 0.0;
	double cap_sum = 0.0;
	int n_bat = 0, n_cap = 0;
	bool w_known = false, mains_seen = false, mains_on = false;
	bool charging = false, discharging = false, full = true;
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != NULL) {
			if (e->d_name[0] == '.') {
				continue;
			}
			snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
			if (!read_first_line(p, buf, sizeof buf)) {
				continue;
			}
			if (strncmp(buf, "Mains", 5) == 0 || strncmp(buf, "USB", 3) == 0) {
				snprintf(p, sizeof p, "%s/%s/online", dir, e->d_name);
				double on = read_number(p, -1.0);
				if (on >= 0.0) {
					mains_seen = true;
					mains_on = mains_on || on > 0.5;
				}
				continue;
			}
			if (strncmp(buf, "Battery", 7) != 0) {
				continue;
			}
			snprintf(p, sizeof p, "%s/%s/scope", dir, e->d_name);
			if (read_first_line(p, buf, sizeof buf) && strncmp(buf, "Device", 6) == 0) {
				continue;
			}
			snprintf(p, sizeof p, "%s/%s/present", dir, e->d_name);
			if (read_number(p, 1.0) < 0.5) {
				continue;
			}
			n_bat++;
			snprintf(p, sizeof p, "%s/%s/capacity", dir, e->d_name);
			double cap = read_number(p, -1.0);
			if (cap >= 0.0) {
				cap_sum += cap;
				n_cap++;
			}
			snprintf(p, sizeof p, "%s/%s/status", dir, e->d_name);
			if (read_first_line(p, buf, sizeof buf)) {
				if (strncmp(buf, "Charging", 8) == 0) {
					charging = true;
					full = false;
				} else if (strncmp(buf, "Discharging", 11) == 0) {
					discharging = true;
					full = false;
				} else if (strncmp(buf, "Full", 4) != 0) {
					full = false;
				}
			}
			/* energy in uWh, or charge in uAh and a voltage in uV */
			double v = -1.0;
			snprintf(p, sizeof p, "%s/%s/voltage_now", dir, e->d_name);
			v = read_number(p, -1.0);
			snprintf(p, sizeof p, "%s/%s/energy_now", dir, e->d_name);
			double en = read_number(p, -1.0);
			snprintf(p, sizeof p, "%s/%s/energy_full", dir, e->d_name);
			double ef = read_number(p, -1.0);
			if (en < 0.0 && v > 0.0) {
				snprintf(p, sizeof p, "%s/%s/charge_now", dir, e->d_name);
				double cn = read_number(p, -1.0);
				snprintf(p, sizeof p, "%s/%s/charge_full", dir, e->d_name);
				double cf = read_number(p, -1.0);
				if (cn >= 0.0 && cf > 0.0) {
					en = cn * v / 1e6;
					ef = cf * v / 1e6;
				}
			}
			if (en >= 0.0 && ef > 0.0) {
				e_now += en;
				e_full += ef;
			}
			snprintf(p, sizeof p, "%s/%s/power_now", dir, e->d_name);
			double pw = read_number(p, -1.0);
			if (pw < 0.0 && v > 0.0) {
				snprintf(p, sizeof p, "%s/%s/current_now", dir, e->d_name);
				double cur = read_number(p, -1.0);
				if (cur >= 0.0) {
					pw = cur * v / 1e6;
				}
			}
			if (pw >= 0.0) {
				watts += fabs(pw) / 1e6;
				w_known = true;
			}
		}
		closedir(d);
	}
	out->have_battery = n_bat > 0;
	if (out->have_battery) {
		out->battery = e_full > 0.0 ? e_now / e_full : (n_cap ? cap_sum / n_cap / 100.0 : 0.0);
		if (out->battery > 1.0) {
			out->battery = 1.0;
		}
		out->charging = charging;
		out->discharging = discharging && !charging;
		out->full = full && !charging && !discharging;
		out->on_mains = mains_seen ? mains_on : !out->discharging;
		if (w_known) {
			out->batt_w = watts;
			if (watts > 0.05 && e_full > 0.0) {
				out->hours = out->discharging ? (e_now / 1e6) / watts
					: out->charging ? ((e_full - e_now) / 1e6) / watts : -1.0;
			}
		}
	} else {
		out->on_mains = mains_seen ? mains_on : true;
	}

	/* What the machine draws: on its battery, what comes out of it is all
	 * of it. Off the wall, whatever measures some of it. */
	if (out->have_battery && out->discharging && out->batt_w >= 0.0) {
		out->draw_w = out->batt_w;
		snprintf(out->source, sizeof out->source, "BATTERY");
		return;
	}
	double cpu = rapl_watts(), gpu = gpu_watts();
	if (cpu >= 0.0 && gpu >= 0.0) {
		out->draw_w = cpu + gpu;
		snprintf(out->source, sizeof out->source, "CPU+GPU");
	} else if (cpu >= 0.0) {
		out->draw_w = cpu;
		snprintf(out->source, sizeof out->source, "CPU");
	} else if (gpu >= 0.0) {
		out->draw_w = gpu;
		snprintf(out->source, sizeof out->source, "GPU");
	}
}
