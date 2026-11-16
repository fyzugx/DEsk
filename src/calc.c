/* The calculator's workings (calc.h). */

#include "calc.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.7182818284590452354
#endif

/* Numbers are kept to this many decimal digits, after every step: what a
 * double cannot hold exactly comes back as the decimal it was meant to be, so
 * that 0.1 + 0.2 is 0.3, and 1.1 x 1.1 is 1.21. */
#define KEPT_DIGITS 15

static struct {
	bool loaded;
	unsigned gen;
	char entry[DESK_CALC_FIGURES + 4]; /* what is being keyed */
	bool keying;
	double x;                 /* what is on the display */
	bool fresh;               /* x is new since the last operation took one */
	double acc;               /* what the pending operation works on */
	enum desk_calc_key op;    /* the pending operation, DESK_CK_COUNT for none */
	enum desk_calc_key again_op; /* = again does this, by that */
	double again_by;
	bool failed;              /* the display says Error */
	bool summed;              /* what is shown is the answer of the last sum */
	double memory;
	bool has_memory;
	struct desk_calc_line line[DESK_CALC_LINES];
	int lines;
} g = { .gen = 1, .op = DESK_CK_COUNT, .again_op = DESK_CK_COUNT };

static void calc_path(char *out, size_t n)
{
	const char *dh = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	if (dh && *dh) {
		snprintf(out, n, "%s/desk/calc", dh);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/share/desk/calc", home);
	} else {
		out[0] = '\0';
	}
}

static void add_line(const char *sum, const char *answer, double value)
{
	if (g.lines == DESK_CALC_LINES) {
		memmove(&g.line[0], &g.line[1], sizeof g.line[0] * (DESK_CALC_LINES - 1));
		g.lines--;
	}
	struct desk_calc_line *l = &g.line[g.lines++];
	snprintf(l->sum, sizeof l->sum, "%s", sum);
	snprintf(l->answer, sizeof l->answer, "%s", answer);
	l->value = value;
	g.summed = g.loaded;
}

/* The file: "m <memory>", then a line for each sum, the value (as C reads it),
 * the answer and the sum, tabs between. */
static void load(void)
{
	if (g.loaded) {
		return;
	}
	g.loaded = true;
	char path[512];
	calc_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	char line[256];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == 'm' && line[1] == ' ') {
			char *end;
			double m = strtod(line + 2, &end);
			if (end != line + 2 && isfinite(m)) {
				g.memory = m;
				g.has_memory = true;
			}
			continue;
		}
		char *t1 = strchr(line, '\t');
		char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
		if (!t2) {
			continue;
		}
		*t1 = *t2 = '\0';
		char *end;
		double v = strtod(line, &end);
		if (end != line && !*end && isfinite(v)) {
			add_line(t2 + 1, t1 + 1, v);
		}
	}
	fclose(f);
	/* (what was worked out before is on the paper, not on the display) */
	g.summed = false;
}

void desk_calc_keep(void)
{
	load();
	char path[512], tmp[540];
	calc_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	char dir[512];
	snprintf(dir, sizeof dir, "%s", path);
	for (char *p = strchr(dir + 1, '/'); p; p = strchr(p + 1, '/')) {
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		return;
	}
	if (g.has_memory) {
		fprintf(f, "m %.17g\n", g.memory);
	}
	for (int i = 0; i < g.lines; i++) {
		fprintf(f, "%.17g\t%s\t%s\n", g.line[i].value, g.line[i].answer, g.line[i].sum);
	}
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

/* v as the decimal it is, to the digits kept. */
static double decimal(double v)
{
	if (!isfinite(v) || v == 0.0) {
		return v;
	}
	char s[40];
	snprintf(s, sizeof s, "%.*e", KEPT_DIGITS - 1, v);
	return strtod(s, NULL);
}

/* v to at most `room` figures, a sign and an exponent counted. */
static void format_number(double v, char *out, size_t n, int room)
{
	if (v == 0.0) {
		snprintf(out, n, "0");
		return;
	}
	double a = fabs(v);
	int digits = room - (v < 0.0 ? 1 : 0);
	if (a < 1e12 && a >= 1e-7) {
		int whole = a < 1.0 ? 1 : (int)floor(log10(a)) + 1;
		int dec = digits - whole;
		if (dec < 0) {
			dec = 0;
		}
		char s[64];
		snprintf(s, sizeof s, "%.*f", dec, v);
		if (strchr(s, '.')) {
			size_t k = strlen(s);
			while (k > 0 && s[k - 1] == '0') {
				s[--k] = '\0';
			}
			if (k > 0 && s[k - 1] == '.') {
				s[--k] = '\0';
			}
		}
		int count = 0;
		for (const char *p = s; *p; p++) {
			count += isdigit((unsigned char)*p) != 0;
		}
		if (count <= digits) {
			snprintf(out, n, "%s", s);
			return;
		}
	}
	/* too long, or too small, to write out: a shorter number and its exponent */
	int ex = (int)floor(log10(a));
	char tail[16];
	snprintf(tail, sizeof tail, "E%d", ex);
	int mant = digits - (int)strlen(tail);
	if (mant < 1) {
		mant = 1;
	}
	char s[64];
	snprintf(s, sizeof s, "%.*e", mant - 1, v);
	char *e = strchr(s, 'e');
	if (!e) {
		snprintf(out, n, "%s", s);
		return;
	}
	ex = atoi(e + 1);   /* (rounding may have moved it) */
	*e = '\0';
	if (strchr(s, '.')) {
		size_t k = strlen(s);
		while (k > 0 && s[k - 1] == '0') {
			s[--k] = '\0';
		}
		if (k > 0 && s[k - 1] == '.') {
			s[--k] = '\0';
		}
	}
	snprintf(out, n, "%sE%d", s, ex);
}

/* v as written on the paper: with thousands marks. */
static void format_paper(double v, char *out, size_t n)
{
	char s[32];
	format_number(v, s, sizeof s, DESK_CALC_FIGURES);
	if (strchr(s, 'E')) {
		snprintf(out, n, "%s", s);
		return;
	}
	const char *p = s;
	size_t w = 0;
	if (*p == '-') {
		if (w + 4 < n) {
			memcpy(out + w, "\xe2\x88\x92", 3);
			w += 3;
		}
		p++;
	}
	size_t whole = strcspn(p, ".");
	for (size_t i = 0; i < whole && w + 2 < n; i++) {
		out[w++] = p[i];
		if ((whole - i - 1) % 3 == 0 && i + 1 < whole) {
			out[w++] = ',';
		}
	}
	snprintf(out + w, n - w, "%s", p + whole);
}

static void fail(void)
{
	g.failed = true;
	g.keying = false;
	g.fresh = false;
	g.op = DESK_CK_COUNT;
	g.again_op = DESK_CK_COUNT;
}

/* What is being keyed goes in as a number. */
static void take_entry(void)
{
	if (g.keying) {
		g.x = decimal(strtod(g.entry, NULL));
		g.keying = false;
		g.fresh = true;
	}
}

static const char *op_mark(enum desk_calc_key op)
{
	switch (op) {
	case DESK_CK_ADD: return "+";
	case DESK_CK_SUB: return "\xe2\x88\x92";
	case DESK_CK_MUL: return "\xc3\x97";
	case DESK_CK_DIV: return "\xc3\xb7";
	default: return "^";
	}
}

/* a op b, kept on the paper, as x; false if it could not be worked out */
static bool work_out(double a, enum desk_calc_key op, double b, const char *b_note)
{
	double r;
	switch (op) {
	case DESK_CK_ADD: r = a + b; break;
	case DESK_CK_SUB: r = a - b; break;
	case DESK_CK_MUL: r = a * b; break;
	case DESK_CK_DIV:
		if (b == 0.0) {
			fail();
			return false;
		}
		r = a / b;
		break;
	default:
		r = pow(a, b);
		break;
	}
	r = decimal(r);
	if (!isfinite(r)) {
		fail();
		return false;
	}
	char sa[32], sb[32], sum[72], ans[40];
	format_paper(a, sa, sizeof sa);
	format_paper(b, sb, sizeof sb);
	snprintf(sum, sizeof sum, "%s %s %s%s", sa, op_mark(op), sb, b_note ? b_note : "");
	format_paper(r, ans, sizeof ans);
	add_line(sum, ans, r);
	g.x = r;
	return true;
}

/* The key that acts on what is on the display, at once. */
static void work_on_one(enum desk_calc_key k)
{
	take_entry();
	double v = g.x, r = 0.0;
	char sv[32], sum[72], ans[40];
	format_paper(v, sv, sizeof sv);
	/* the angle's sine and tangent are exact where they should be */
	double deg = fmod(v, 360.0);
	switch (k) {
	case DESK_CK_SQRT:
		if (v < 0.0) {
			fail();
			return;
		}
		r = sqrt(v);
		snprintf(sum, sizeof sum, "\xe2\x88\x9a %s", sv);
		break;
	case DESK_CK_SQ:
		r = v * v;
		snprintf(sum, sizeof sum, "%s\xc2\xb2", sv);
		break;
	case DESK_CK_INV:
		if (v == 0.0) {
			fail();
			return;
		}
		r = 1.0 / v;
		snprintf(sum, sizeof sum, "1 \xc3\xb7 %s", sv);
		break;
	case DESK_CK_LN:
	case DESK_CK_LOG:
		if (v <= 0.0) {
			fail();
			return;
		}
		r = k == DESK_CK_LN ? log(v) : log10(v);
		snprintf(sum, sizeof sum, "%s %s", k == DESK_CK_LN ? "ln" : "log", sv);
		break;
	case DESK_CK_SIN:
		r = fmod(deg, 180.0) == 0.0 ? 0.0 : sin(deg * M_PI / 180.0);
		snprintf(sum, sizeof sum, "sin %s", sv);
		break;
	case DESK_CK_COS:
		r = fmod(deg - 90.0, 180.0) == 0.0 ? 0.0 : cos(deg * M_PI / 180.0);
		snprintf(sum, sizeof sum, "cos %s", sv);
		break;
	case DESK_CK_TAN:
		if (fmod(deg - 90.0, 180.0) == 0.0) {
			fail();
			return;
		}
		r = fmod(deg, 180.0) == 0.0 ? 0.0 : tan(deg * M_PI / 180.0);
		snprintf(sum, sizeof sum, "tan %s", sv);
		break;
	case DESK_CK_FACT:
		if (v < 0.0 || v > 170.0 || v != floor(v)) {
			fail();
			return;
		}
		r = 1.0;
		for (int i = 2; i <= (int)v; i++) {
			r *= i;
		}
		snprintf(sum, sizeof sum, "%s!", sv);
		break;
	default:
		return;
	}
	r = decimal(r);
	if (!isfinite(r)) {
		fail();
		return;
	}
	format_paper(r, ans, sizeof ans);
	add_line(sum, ans, r);
	g.x = r;
	g.fresh = true;
}

/* A digit, or the point: onto what is being keyed. */
static void key_figure(const char *what)
{
	if (g.failed) {
		g.failed = false;
		g.x = 0.0;
	}
	if (!g.keying) {
		g.keying = true;
		g.summed = false;
		g.entry[0] = '\0';
		g.fresh = true;
		if (g.op == DESK_CK_COUNT) {
			g.again_op = DESK_CK_COUNT;
		}
	}
	for (; *what; what++) {
		size_t k = strlen(g.entry);
		int count = 0;
		for (const char *p = g.entry; *p; p++) {
			count += isdigit((unsigned char)*p) != 0;
		}
		bool neg = g.entry[0] == '-';
		if (*what == '.') {
			if (strchr(g.entry, '.')) {
				continue;
			}
			if (k == (neg ? 1u : 0u)) {
				snprintf(g.entry + k, sizeof g.entry - k, "0.");
			} else {
				snprintf(g.entry + k, sizeof g.entry - k, ".");
			}
			continue;
		}
		if (count >= DESK_CALC_FIGURES - (neg ? 1 : 0)) {
			continue;
		}
		/* no 007 */
		const char *num = g.entry + (neg ? 1 : 0);
		if (!strcmp(num, "0")) {
			g.entry[neg ? 1 : 0] = *what;
			g.entry[(neg ? 1 : 0) + 1] = '\0';
			continue;
		}
		g.entry[k] = *what;
		g.entry[k + 1] = '\0';
	}
}

static void key_operation(enum desk_calc_key k)
{
	take_entry();
	if (g.op != DESK_CK_COUNT && g.fresh) {
		if (!work_out(g.acc, g.op, g.x, NULL)) {
			return;
		}
	}
	g.acc = g.x;
	g.op = k;
	g.fresh = false;
	g.again_op = DESK_CK_COUNT;
}

static void key_equals(void)
{
	take_entry();
	if (g.op != DESK_CK_COUNT) {
		enum desk_calc_key op = g.op;
		double a = g.acc, b = g.x;
		g.op = DESK_CK_COUNT;
		if (work_out(a, op, b, NULL)) {
			g.again_op = op;
			g.again_by = b;
		}
	} else if (g.again_op != DESK_CK_COUNT) {
		enum desk_calc_key op = g.again_op;
		work_out(g.x, op, g.again_by, NULL);
	}
	g.fresh = false;
}

/* With a sum pending, the percentage of what it works on (200 + 10% = 220,
 * 200 x 10% = 20); alone, a hundredth. */
static void key_percent(void)
{
	take_entry();
	if (g.op == DESK_CK_COUNT) {
		char sv[32], sum[72], ans[40];
		format_paper(g.x, sv, sizeof sv);
		snprintf(sum, sizeof sum, "%s%%", sv);
		double r = decimal(g.x / 100.0);
		format_paper(r, ans, sizeof ans);
		add_line(sum, ans, r);
		g.x = r;
		g.fresh = true;
		return;
	}
	enum desk_calc_key op = g.op;
	double a = g.acc, shown = g.x;
	double b = decimal(op == DESK_CK_ADD || op == DESK_CK_SUB ? a * shown / 100.0 : shown / 100.0);
	g.op = DESK_CK_COUNT;
	double r;
	switch (op) {
	case DESK_CK_ADD: r = a + b; break;
	case DESK_CK_SUB: r = a - b; break;
	case DESK_CK_MUL: r = a * b; break;
	case DESK_CK_DIV:
		if (b == 0.0) {
			fail();
			return;
		}
		r = a / b;
		break;
	default: r = pow(a, b); break;
	}
	r = decimal(r);
	if (!isfinite(r)) {
		fail();
		return;
	}
	char sa[32], sb[32], sum[72], ans[40];
	format_paper(a, sa, sizeof sa);
	format_paper(shown, sb, sizeof sb);
	snprintf(sum, sizeof sum, "%s %s %s%%", sa, op_mark(op), sb);
	format_paper(r, ans, sizeof ans);
	add_line(sum, ans, r);
	g.x = r;
	g.fresh = false;
}

static void key_sign(void)
{
	if (g.keying) {
		int count = 0;
		for (const char *p = g.entry; *p; p++) {
			count += isdigit((unsigned char)*p) != 0;
		}
		if (g.entry[0] == '-') {
			memmove(g.entry, g.entry + 1, strlen(g.entry));
			return;
		}
		if (count < DESK_CALC_FIGURES) {
			memmove(g.entry + 1, g.entry, strlen(g.entry) + 1);
			g.entry[0] = '-';
			return;
		}
		take_entry();
	}
	g.x = -g.x;
	g.fresh = true;
}

/* M+ and M-: what is on the display, any sum pending finished first. */
static void key_memory(double sign)
{
	key_equals();
	if (g.failed) {
		return;
	}
	g.memory = decimal(g.memory + sign * g.x);
	g.has_memory = true;
}

void desk_calc_press(enum desk_calc_key k)
{
	if (k >= DESK_CK_COUNT) {
		return;
	}
	load();
	g.gen++;
	if (k == DESK_CK_AC) {
		g.failed = false;
		g.summed = false;
		g.keying = false;
		g.fresh = false;
		g.x = g.acc = 0.0;
		g.op = g.again_op = DESK_CK_COUNT;
		return;
	}
	if (k <= DESK_CK_9) {
		char d[2] = { (char)('0' + (k - DESK_CK_0)), '\0' };
		key_figure(d);
		return;
	}
	if (k == DESK_CK_DBLZERO) {
		key_figure("00");
		return;
	}
	if (k == DESK_CK_DOT) {
		key_figure(".");
		return;
	}
	if (g.failed && k != DESK_CK_MC) {
		return;
	}
	switch (k) {
	case DESK_CK_ADD:
	case DESK_CK_SUB:
	case DESK_CK_MUL:
	case DESK_CK_DIV:
	case DESK_CK_POW:
		key_operation(k);
		break;
	case DESK_CK_EQ:
		key_equals();
		break;
	case DESK_CK_PCT:
		key_percent();
		break;
	case DESK_CK_NEG:
		key_sign();
		break;
	case DESK_CK_MC:
		g.memory = 0.0;
		g.has_memory = false;
		break;
	case DESK_CK_MR:
		g.summed = false;
		g.keying = false;
		g.x = g.memory;
		g.fresh = true;
		break;
	case DESK_CK_MMINUS:
		key_memory(-1.0);
		break;
	case DESK_CK_MPLUS:
		key_memory(1.0);
		break;
	case DESK_CK_PI:
		g.summed = false;
		g.keying = false;
		g.x = decimal(M_PI);
		g.fresh = true;
		break;
	case DESK_CK_EUL:
		g.summed = false;
		g.keying = false;
		g.x = decimal(M_E);
		g.fresh = true;
		break;
	default:
		work_on_one(k);
		break;
	}
}

bool desk_calc_rub_out(void)
{
	load();
	if (!g.keying || g.failed) {
		return false;
	}
	size_t k = strlen(g.entry);
	if (k > 0) {
		g.entry[k - 1] = '\0';
	}
	if (!strcmp(g.entry, "-")) {
		g.entry[0] = '\0';
	}
	g.gen++;
	if (!g.entry[0]) {
		snprintf(g.entry, sizeof g.entry, "0");
	}
	return true;
}

bool desk_calc_paste(const char *text)
{
	load();
	char s[64];
	size_t w = 0;
	while (isspace((unsigned char)*text)) {
		text++;
	}
	/* whichever mark comes last is the decimal one, if only one of the kind,
	 * or there are other marks before it */
	const char *last_dot = strrchr(text, '.');
	const char *last_comma = strrchr(text, ',');
	const char *mark = NULL;
	if (last_dot && last_comma) {
		mark = last_dot > last_comma ? last_dot : last_comma;
	} else if (last_dot) {
		mark = strchr(text, '.') == last_dot ? last_dot : NULL;
	} else if (last_comma) {
		/* 1,250 is a thousand and a quarter; 12,5 twelve and a half */
		size_t after = strcspn(last_comma + 1, " \t\r\n");
		mark = strchr(text, ',') == last_comma && after != 3 ? last_comma : NULL;
	}
	for (const char *p = text; *p && !isspace((unsigned char)*p); p++) {
		if (w + 2 >= sizeof s) {
			return false;
		}
		if (p == mark) {
			s[w++] = '.';
		} else if (*p == '.' || *p == ',' || *p == '\'') {
			continue;
		} else if (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == 'e' || *p == 'E') {
			s[w++] = *p;
		} else {
			return false;
		}
	}
	s[w] = '\0';
	const char *rest = text + strcspn(text, " \t\r\n");
	while (isspace((unsigned char)*rest)) {
		rest++;
	}
	char *end;
	errno = 0;
	double v = strtod(s, &end);
	if (w == 0 || end == s || *end || *rest || !isfinite(v)) {
		return false;
	}
	g.failed = false;
	g.keying = false;
	g.x = decimal(v);
	g.fresh = true;
	g.gen++;
	return true;
}

void desk_calc_shown(char *out, unsigned n)
{
	load();
	if (g.failed) {
		snprintf(out, n, "Error");
		return;
	}
	char s[64];
	if (g.keying) {
		snprintf(s, sizeof s, "%s", g.entry);
	} else {
		format_number(g.x, s, sizeof s, DESK_CALC_FIGURES);
	}
	if (!strchr(s, '.') && !strchr(s, 'E')) {
		size_t k = strlen(s);
		if (k + 2 < sizeof s) {
			s[k] = '.';
			s[k + 1] = '\0';
		}
	}
	snprintf(out, n, "%s", s);
}

void desk_calc_shown_text(char *out, unsigned n)
{
	load();
	if (g.failed) {
		if (n > 0) {
			out[0] = '\0';
		}
		return;
	}
	char s[64];
	double v = g.keying ? decimal(strtod(g.entry, NULL)) : g.x;
	/* all the digits that are kept, not only those that fit */
	snprintf(s, sizeof s, "%.*g", KEPT_DIGITS, v);
	snprintf(out, n, "%s", s);
}

void desk_calc_working(char *out, unsigned n)
{
	load();
	char a[32];
	if (n == 0) {
		return;
	}
	out[0] = '\0';
	if (g.failed) {
		return;
	}
	if (g.op != DESK_CK_COUNT) {
		format_paper(g.acc, a, sizeof a);
		snprintf(out, n, "%s %s", a, op_mark(g.op));
	} else if (g.summed && g.lines > 0) {
		snprintf(out, n, "%s =", g.line[g.lines - 1].sum);
	}
}

bool desk_calc_has_memory(void)
{
	load();
	return g.has_memory && g.memory != 0.0;
}

int desk_calc_lines(void)
{
	load();
	return g.lines;
}

const struct desk_calc_line *desk_calc_line(int i)
{
	load();
	return i >= 0 && i < g.lines ? &g.line[i] : NULL;
}

void desk_calc_recall(int i)
{
	load();
	if (i < 0 || i >= g.lines) {
		return;
	}
	g.failed = false;
	g.keying = false;
	g.summed = false;
	g.x = g.line[i].value;
	g.fresh = true;
	g.gen++;
}

void desk_calc_tear(void)
{
	load();
	g.lines = 0;
	g.summed = false;
	g.gen++;
}

unsigned desk_calc_generation(void)
{
	return g.gen;
}
