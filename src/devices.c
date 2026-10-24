/* ================================================================
 * What is behind the levers and the knob (devices.h).
 *
 * One thread does all of it, a job at a time, so that two things asked of
 * the same radio happen in the order they were asked. Each program is run
 * straight, never through a shell -- an SSID is whatever its owner typed --
 * and in the C locale, so what it prints is what is parsed here.
 * ================================================================ */

#define _GNU_SOURCE   /* pipe2 */

#include "devices.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>

extern char **environ;

struct dev_job {
	enum desk_dev_kind kind;
	enum desk_dev_action action;
	bool act;          /* false: only read the list again */
	char id[160];
};

#define JOBS_MAX 16

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static struct desk_dev_list g_lists[DESK_DEV_KINDS];
static struct dev_job g_jobs[JOBS_MAX];
static int g_njobs;
static int g_pending[DESK_DEV_KINDS];   /* jobs queued or running, per list */
static bool g_bt_scanned;               /* show what a scan found, not yet paired */
static int g_wake[2] = { -1, -1 };
static void (*g_changed)(void *);
static void *g_changed_data;
static bool g_started;

/* ---- running a program ------------------------------------------- */

static bool on_path(const char *name)
{
	const char *path = getenv("PATH");
	char buf[1024];
	for (const char *p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin"; *p;) {
		const char *e = strchr(p, ':');
		size_t k = e ? (size_t)(e - p) : strlen(p);
		if (k > 0 && (size_t)snprintf(buf, sizeof buf, "%.*s/%s", (int)k, p, name)
				< sizeof buf && access(buf, X_OK) == 0) {
			return true;
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return false;
}

/* argv run with what it prints to out (n bytes, ended), the C locale, and
 * no more than secs seconds (through timeout(1)). True if it exited 0. */
static bool run(const char *const argv[], int secs, char *out, size_t n)
{
	const char *full[24];
	char limit[16];
	int k = 0;
	snprintf(limit, sizeof limit, "%d", secs);
	full[k++] = "timeout";
	full[k++] = limit;
	for (int i = 0; argv[i] && k < 23; i++) {
		full[k++] = argv[i];
	}
	full[k] = NULL;

	/* the environment as it is, but in the C locale */
	size_t ne = 0;
	while (environ[ne]) {
		ne++;
	}
	char **env = calloc(ne + 2, sizeof *env);
	if (!env) {
		return false;
	}
	size_t m = 0;
	for (size_t i = 0; i < ne; i++) {
		if (strncmp(environ[i], "LC_ALL=", 7) && strncmp(environ[i], "LANGUAGE=", 9)) {
			env[m++] = environ[i];
		}
	}
	env[m++] = "LC_ALL=C";
	env[m] = NULL;

	int fd[2];
	if (pipe2(fd, O_CLOEXEC) != 0) {
		free(env);
		return false;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	pid_t pid;
	int err = posix_spawnp(&pid, "timeout", &fa, NULL, (char *const *)full, env);
	posix_spawn_file_actions_destroy(&fa);
	free(env);
	close(fd[1]);
	if (err != 0) {
		close(fd[0]);
		return false;
	}
	size_t got = 0;
	char sink[512];
	for (;;) {
		char *to = out && got + 1 < n ? out + got : sink;
		size_t room = out && got + 1 < n ? n - 1 - got : sizeof sink;
		ssize_t r = read(fd[0], to, room);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			break;
		}
		if (to != sink) {
			got += (size_t)r;
		}
	}
	close(fd[0]);
	if (out && n) {
		out[got] = '\0';
	}
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
		;
	}
	return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

/* The next line of *p, ended, or NULL at the end. */
static char *next_line(char **p)
{
	char *s = *p;
	if (!s || !*s) {
		return NULL;
	}
	char *e = strchr(s, '\n');
	if (e) {
		*e = '\0';
		*p = e + 1;
	} else {
		*p = s + strlen(s);
	}
	return s;
}

/* One field of nmcli's terse form (-t -e yes), where ':' and '\' inside a
 * field come escaped: up to the next bare ':', unescaped into out. */
static const char *nm_field(const char *s, char *out, size_t n)
{
	size_t k = 0;
	for (; *s && *s != ':'; s++) {
		if (*s == '\\' && s[1]) {
			s++;
		}
		if (k + 1 < n) {
			out[k++] = *s;
		}
	}
	out[k] = '\0';
	return *s == ':' ? s + 1 : s;
}

/* s into to, as much as fits, cut between characters rather than in one */
static void copy(char *to, size_t n, const char *s)
{
	if (!n) {
		return;
	}
	size_t k = s ? strlen(s) : 0;
	if (k >= n) {
		k = n - 1;
		while (k > 0 && ((unsigned char)s[k] & 0xc0) == 0x80) {
			k--;
		}
	}
	if (k) {
		memcpy(to, s, k);
	}
	to[k] = '\0';
}

/* ---- reading the lists ------------------------------------------- */

static void read_wifi(struct desk_dev_list *L)
{
	static char buf[65536];
	L->have = on_path("nmcli");
	if (!L->have) {
		return;
	}
	const char *radio[] = { "nmcli", "radio", "wifi", NULL };
	char r[64];
	L->off = run(radio, 5, r, sizeof r) && !strncmp(r, "disabled", 8);
	if (L->off) {
		return;
	}

	/* the saved ones, and the one in use, by name */
	char saved[DESK_DEV_MAX][128];
	int nsaved = 0;
	char active[128] = "";
	const char *conns[] = { "nmcli", "-t", "-e", "yes", "-f", "NAME,TYPE,ACTIVE",
		"connection", "show", NULL };
	if (run(conns, 8, buf, sizeof buf)) {
		char *p = buf, *line;
		while ((line = next_line(&p))) {
			char name[128], type[64], on[16];
			const char *s = nm_field(line, name, sizeof name);
			s = nm_field(s, type, sizeof type);
			nm_field(s, on, sizeof on);
			if (strcmp(type, "802-11-wireless")) {
				continue;
			}
			if (nsaved < DESK_DEV_MAX) {
				copy(saved[nsaved++], 128, name);
			}
			if (!strcmp(on, "yes")) {
				copy(active, sizeof active, name);
			}
		}
	}

	/* what NetworkManager already knows, at once: a scan is asked for on
	 * its own (DESK_DEV_SCAN), and this read again after it */
	const char *list[] = { "nmcli", "-t", "-e", "yes", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
		"device", "wifi", "list", "--rescan", "no", NULL };
	if (!run(list, 20, buf, sizeof buf)) {
		L->failed = true;
		return;
	}
	char *p = buf, *line;
	while ((line = next_line(&p))) {
		char use[8], ssid[128], sig[8], sec[64];
		const char *s = nm_field(line, use, sizeof use);
		s = nm_field(s, ssid, sizeof ssid);
		s = nm_field(s, sig, sizeof sig);
		nm_field(s, sec, sizeof sec);
		if (!ssid[0]) {
			continue;   /* a hidden network: nothing to call it by */
		}
		bool in_use = use[0] == '*';
		int signal = atoi(sig);
		/* one network on two bands is one line on the card: the
		 * stronger, or the one in use */
		int at = -1;
		for (int i = 0; i < L->n; i++) {
			if (!strcmp(L->d[i].id, ssid)) {
				at = i;
				break;
			}
		}
		if (at < 0) {
			if (L->n >= DESK_DEV_MAX) {
				continue;
			}
			at = L->n++;
			memset(&L->d[at], 0, sizeof L->d[at]);
			copy(L->d[at].id, sizeof L->d[at].id, ssid);
			copy(L->d[at].label, sizeof L->d[at].label, ssid);
			L->d[at].signal = -1;
			for (int i = 0; i < nsaved; i++) {
				if (!strcmp(saved[i], ssid)) {
					L->d[at].known = true;
					copy(L->d[at].conn, sizeof L->d[at].conn, saved[i]);
				}
			}
		}
		struct desk_dev *d = &L->d[at];
		if (signal > d->signal) {
			d->signal = signal;
		}
		d->secure = d->secure || (sec[0] && strcmp(sec, "--"));
		if (in_use) {
			d->active = true;
			if (active[0]) {
				copy(d->conn, sizeof d->conn, active);
			}
		}
	}
	/* the one in use first, then the saved ones, then by signal */
	for (int i = 1; i < L->n; i++) {
		struct desk_dev t = L->d[i];
		int j = i - 1;
		while (j >= 0) {
			const struct desk_dev *o = &L->d[j];
			int ko = o->active * 4 + o->known * 2, kt = t.active * 4 + t.known * 2;
			if (ko > kt || (ko == kt && o->signal >= t.signal)) {
				break;
			}
			L->d[j + 1] = L->d[j];
			j--;
		}
		L->d[j + 1] = t;
	}
}

/* "Device AA:BB:CC:DD:EE:FF Name" lines, into addresses and names. */
static int bt_devices(const char *filter, char addr[][24], char name[][128], int max)
{
	static char buf[16384];
	const char *argv[] = { "bluetoothctl", "devices", filter, NULL };
	if (!filter) {
		argv[2] = NULL;
	}
	if (!run(argv, 5, buf, sizeof buf)) {
		return 0;
	}
	int n = 0;
	char *p = buf, *line;
	while ((line = next_line(&p)) && n < max) {
		if (strncmp(line, "Device ", 7) || strlen(line) < 24) {
			continue;
		}
		snprintf(addr[n], 24, "%.17s", line + 7);
		const char *nm = line + 7 + 17;
		while (*nm == ' ') {
			nm++;
		}
		copy(name[n], 128, nm);
		n++;
	}
	return n;
}

static void read_bt(struct desk_dev_list *L)
{
	L->have = on_path("bluetoothctl");
	if (!L->have) {
		return;
	}
	char buf[4096];
	const char *show[] = { "bluetoothctl", "show", NULL };
	if (!run(show, 5, buf, sizeof buf) || !strstr(buf, "Controller")) {
		L->have = false;   /* no adapter at all */
		return;
	}
	L->off = strstr(buf, "Powered: yes") == NULL;
	if (L->off) {
		return;
	}
	static char addr[DESK_DEV_MAX][24], name[DESK_DEV_MAX][128];
	static char caddr[DESK_DEV_MAX][24], cname[DESK_DEV_MAX][128];
	int np = bt_devices("Paired", addr, name, DESK_DEV_MAX);
	int nc = bt_devices("Connected", caddr, cname, DESK_DEV_MAX);
	for (int i = 0; i < np && L->n < DESK_DEV_MAX; i++) {
		struct desk_dev *d = &L->d[L->n++];
		memset(d, 0, sizeof *d);
		copy(d->id, sizeof d->id, addr[i]);
		copy(d->label, sizeof d->label, name[i]);
		d->signal = -1;
		d->known = true;
		for (int j = 0; j < nc; j++) {
			d->active = d->active || !strcmp(caddr[j], addr[i]);
		}
	}
	if (!g_bt_scanned) {
		return;
	}
	/* and what the last scan found that is not paired: only the ones
	 * with a name, since an address is nothing anybody recognises */
	static char aaddr[DESK_DEV_MAX][24], aname[DESK_DEV_MAX][128];
	int na = bt_devices(NULL, aaddr, aname, DESK_DEV_MAX);
	for (int i = 0; i < na && L->n < DESK_DEV_MAX; i++) {
		bool paired = false;
		for (int j = 0; j < np; j++) {
			paired = paired || !strcmp(addr[j], aaddr[i]);
		}
		char dashed[24];
		copy(dashed, sizeof dashed, aaddr[i]);
		for (char *c = dashed; *c; c++) {
			*c = *c == ':' ? '-' : *c;
		}
		if (paired || !strcmp(aname[i], dashed) || !aname[i][0]) {
			continue;
		}
		struct desk_dev *d = &L->d[L->n++];
		memset(d, 0, sizeof *d);
		copy(d->id, sizeof d->id, aaddr[i]);
		copy(d->label, sizeof d->label, aname[i]);
		d->signal = -1;
	}
}

/* pactl's long list: "Sink #N" / "Source #N" blocks with "Name:" and
 * "Description:" lines in them. */
static void read_sound(struct desk_dev_list *L, bool sink)
{
	static char buf[262144];
	L->have = on_path("pactl");
	if (!L->have) {
		return;
	}
	char def[160] = "";
	const char *getdef[] = { "pactl", sink ? "get-default-sink" : "get-default-source", NULL };
	if (run(getdef, 5, def, sizeof def)) {
		def[strcspn(def, "\n")] = '\0';
	}
	const char *list[] = { "pactl", "list", sink ? "sinks" : "sources", NULL };
	if (!run(list, 8, buf, sizeof buf)) {
		L->failed = true;
		return;
	}
	struct desk_dev *d = NULL;
	char *p = buf, *line;
	while ((line = next_line(&p))) {
		if (!strncmp(line, sink ? "Sink #" : "Source #", sink ? 6 : 8)) {
			d = L->n < DESK_DEV_MAX ? &L->d[L->n++] : NULL;
			if (d) {
				memset(d, 0, sizeof *d);
				d->signal = -1;
			}
			continue;
		}
		if (!d) {
			continue;
		}
		while (*line == ' ' || *line == '\t') {
			line++;
		}
		if (!strncmp(line, "Name: ", 6)) {
			copy(d->id, sizeof d->id, line + 6);
			d->active = def[0] && !strcmp(d->id, def);
		} else if (!strncmp(line, "Description: ", 13)) {
			copy(d->label, sizeof d->label, line + 13);
		}
	}
	/* a sink's monitor is not something anybody records from on purpose */
	int k = 0;
	for (int i = 0; i < L->n; i++) {
		size_t len = strlen(L->d[i].id);
		bool monitor = len > 8 && !strcmp(L->d[i].id + len - 8, ".monitor");
		if (!L->d[i].id[0] || (!sink && monitor)) {
			continue;
		}
		if (!L->d[i].label[0]) {
			copy(L->d[i].label, sizeof L->d[i].label, L->d[i].id);
		}
		L->d[k++] = L->d[i];
	}
	L->n = k;
}

/* ---- doing things ------------------------------------------------- */

static bool act(const struct dev_job *j)
{
	char out[512];
	switch (j->kind) {
	case DESK_DEV_WIFI: {
		if (j->action == DESK_DEV_SCAN) {
			const char *argv[] = { "nmcli", "device", "wifi", "rescan", NULL };
			bool ok = run(argv, 15, out, sizeof out);
			/* the scan takes a moment to come back after it is asked for */
			sleep(3);
			return ok;
		}
		struct desk_dev_list L;
		pthread_mutex_lock(&g_lock);
		L = g_lists[DESK_DEV_WIFI];
		pthread_mutex_unlock(&g_lock);
		const struct desk_dev *d = NULL;
		for (int i = 0; i < L.n; i++) {
			if (!strcmp(L.d[i].id, j->id)) {
				d = &L.d[i];
			}
		}
		if (j->action == DESK_DEV_DISCONNECT) {
			if (!d || !d->conn[0]) {
				return false;
			}
			const char *argv[] = { "nmcli", "connection", "down", "id", d->conn, NULL };
			return run(argv, 20, out, sizeof out);
		}
		if (d && d->known && d->conn[0]) {
			const char *argv[] = { "nmcli", "connection", "up", "id", d->conn, NULL };
			return run(argv, 60, out, sizeof out);
		}
		/* a new one: NetworkManager asks its agent (nm-applet) for the
		 * password, which is a dialog on the desk */
		const char *argv[] = { "nmcli", "device", "wifi", "connect", j->id, NULL };
		return run(argv, 90, out, sizeof out);
	}
	case DESK_DEV_BT: {
		if (j->action == DESK_DEV_SCAN) {
			const char *argv[] = { "bluetoothctl", "--timeout", "8", "scan", "on", NULL };
			run(argv, 12, out, sizeof out);
			pthread_mutex_lock(&g_lock);
			g_bt_scanned = true;
			pthread_mutex_unlock(&g_lock);
			return true;
		}
		if (j->action == DESK_DEV_DISCONNECT) {
			const char *argv[] = { "bluetoothctl", "disconnect", j->id, NULL };
			return run(argv, 15, out, sizeof out);
		}
		struct desk_dev_list L;
		pthread_mutex_lock(&g_lock);
		L = g_lists[DESK_DEV_BT];
		pthread_mutex_unlock(&g_lock);
		bool paired = false;
		for (int i = 0; i < L.n; i++) {
			paired = paired || (!strcmp(L.d[i].id, j->id) && L.d[i].known);
		}
		if (!paired) {
			/* paired (confirmed by the agent, blueman's), and trusted, so
			 * that it may connect again by itself next time */
			const char *pair[] = { "bluetoothctl", "pair", j->id, NULL };
			if (!run(pair, 40, out, sizeof out)) {
				return false;
			}
			const char *trust[] = { "bluetoothctl", "trust", j->id, NULL };
			run(trust, 10, out, sizeof out);
		}
		const char *argv[] = { "bluetoothctl", "connect", j->id, NULL };
		return run(argv, 30, out, sizeof out);
	}
	case DESK_DEV_SINK:
	case DESK_DEV_SOURCE: {
		if (j->action != DESK_DEV_DEFAULT) {
			return true;
		}
		const char *argv[] = { "pactl",
			j->kind == DESK_DEV_SINK ? "set-default-sink" : "set-default-source", j->id, NULL };
		return run(argv, 5, out, sizeof out);
	}
	default:
		return false;
	}
}

static void read_list(enum desk_dev_kind kind, struct desk_dev_list *L)
{
	memset(L, 0, sizeof *L);
	switch (kind) {
	case DESK_DEV_WIFI:
		read_wifi(L);
		break;
	case DESK_DEV_BT:
		read_bt(L);
		break;
	default:
		read_sound(L, kind == DESK_DEV_SINK);
		break;
	}
	L->read = true;
}

static void *worker(void *arg)
{
	(void)arg;
	static struct desk_dev_list fresh[2];
	for (;;) {
		pthread_mutex_lock(&g_lock);
		while (g_njobs == 0) {
			pthread_cond_wait(&g_cond, &g_lock);
		}
		struct dev_job j = g_jobs[0];
		memmove(g_jobs, g_jobs + 1, (size_t)(g_njobs - 1) * sizeof g_jobs[0]);
		g_njobs--;
		pthread_mutex_unlock(&g_lock);

		bool failed = j.act && !act(&j);
		/* the sound lists go together: a default is one of each */
		bool sound = j.kind == DESK_DEV_SINK || j.kind == DESK_DEV_SOURCE;
		read_list(sound ? DESK_DEV_SINK : j.kind, &fresh[0]);
		if (sound) {
			read_list(DESK_DEV_SOURCE, &fresh[1]);
		}

		pthread_mutex_lock(&g_lock);
		fresh[0].failed = fresh[0].failed || failed;
		if (sound) {
			fresh[1].failed = fresh[1].failed || failed;
			g_lists[DESK_DEV_SINK] = fresh[0];
			g_lists[DESK_DEV_SOURCE] = fresh[1];
			g_pending[DESK_DEV_SINK]--;
			g_pending[DESK_DEV_SOURCE]--;
		} else {
			g_lists[j.kind] = fresh[0];
			g_pending[j.kind]--;
		}
		for (int k = 0; k < DESK_DEV_KINDS; k++) {
			g_lists[k].busy = g_pending[k] > 0;
		}
		pthread_mutex_unlock(&g_lock);
		char c = 1;
		if (write(g_wake[1], &c, 1) < 0) {
			/* full: a wake is already waiting, which will do */
		}
	}
	return NULL;
}

static int woken(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;
	char buf[64];
	while (read(fd, buf, sizeof buf) > 0) {
		;
	}
	if (g_changed) {
		g_changed(g_changed_data);
	}
	return 0;
}

void desk_devices_init(struct wl_event_loop *loop, void (*changed)(void *data), void *data)
{
	if (g_started || pipe2(g_wake, O_CLOEXEC | O_NONBLOCK) != 0) {
		return;
	}
	g_changed = changed;
	g_changed_data = data;
	wl_event_loop_add_fd(loop, g_wake[0], WL_EVENT_READABLE, woken, NULL);
	pthread_t th;
	if (pthread_create(&th, NULL, worker, NULL) != 0) {
		return;
	}
	pthread_detach(th);
	g_started = true;
}

static void queue(enum desk_dev_kind kind, enum desk_dev_action action, bool doit,
	const char *id)
{
	if (!g_started || kind < 0 || kind >= DESK_DEV_KINDS) {
		return;
	}
	bool sound = kind == DESK_DEV_SINK || kind == DESK_DEV_SOURCE;
	pthread_mutex_lock(&g_lock);
	/* a read already waiting for this list will do for another */
	bool dup = false;
	for (int i = 0; i < g_njobs && !doit; i++) {
		dup = dup || (!g_jobs[i].act && g_jobs[i].kind == kind);
	}
	if (!dup && g_njobs < JOBS_MAX) {
		struct dev_job *j = &g_jobs[g_njobs++];
		memset(j, 0, sizeof *j);
		j->kind = kind;
		j->action = action;
		j->act = doit;
		copy(j->id, sizeof j->id, id);
		if (sound) {
			g_pending[DESK_DEV_SINK]++;
			g_pending[DESK_DEV_SOURCE]++;
		} else {
			g_pending[kind]++;
		}
		for (int k = 0; k < DESK_DEV_KINDS; k++) {
			g_lists[k].busy = g_pending[k] > 0;
		}
		pthread_cond_signal(&g_cond);
	}
	pthread_mutex_unlock(&g_lock);
}

void desk_devices_refresh(enum desk_dev_kind kind)
{
	queue(kind, DESK_DEV_SCAN, false, NULL);
}

void desk_devices_act(enum desk_dev_kind kind, enum desk_dev_action action, const char *id)
{
	queue(kind, action, true, id);
}

void desk_devices_get(enum desk_dev_kind kind, struct desk_dev_list *out)
{
	memset(out, 0, sizeof *out);
	if (kind < 0 || kind >= DESK_DEV_KINDS) {
		return;
	}
	pthread_mutex_lock(&g_lock);
	*out = g_lists[kind];
	pthread_mutex_unlock(&g_lock);
}
