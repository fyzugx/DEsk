/* The desk's sounds, made from nothing.
 *
 * Nothing here is a recording. Each sound is built the way the thing makes
 * it, from a handful of pieces:
 *
 *   - something struck rings at a few frequencies of its own, each dying
 *     away at its own rate (mode): wood low and quickly, steel higher and
 *     slowly, a plastic key high and at once;
 *   - something rubbed or let go is noise, coloured by what it is and
 *     shaped by how hard (burst, friction);
 *   - paper, close to, is a shower of tiny crackles -- fibres letting go,
 *     one at a time and then in a rush (crackle) -- behind a breath of air
 *     the sheet pushes in front of it as it moves (swish);
 *   - a hinge is stick and slip, a buzz with a pitch that wanders (creak);
 *   - a valve set going out is mains hum dying away under a whine that
 *     falls, and the glass ticking as it cools.
 *
 * Every sound is made a few times over with a different throw of the dice
 * (variant), so that nothing on the desk sounds exactly the same twice, and
 * is brought to a loudness of its own: a click is a short sharp thing and a
 * rustle a long soft one, and they are measured differently (finish). */

#include "sound.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SR ((double)DESK_SFX_RATE)
#define PI 3.14159265358979323846
#define TAU (2.0 * PI)

/* ---- the dice ---- */

struct rng {
	uint64_t s;
};

static uint64_t rng_next(struct rng *r)
{
	r->s ^= r->s >> 12;
	r->s ^= r->s << 25;
	r->s ^= r->s >> 27;
	return r->s * 2685821657736338717ull;
}

static double rnd(struct rng *r)
{
	return (double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

static double rnd2(struct rng *r)
{
	return rnd(r) * 2.0 - 1.0;
}

/* a value near x: within a fraction j either way */
static double near(struct rng *r, double x, double j)
{
	return x * (1.0 + j * rnd2(r));
}

/* ---- filters: the cookbook's biquads ---- */

enum { F_NONE = -1, F_LP, F_HP, F_BP, F_PK };

struct flt {
	int type;
	double f, q, g;
};

#define NOF ((struct flt){ F_NONE, 0.0, 0.0, 0.0 })
#define LP(f, q) ((struct flt){ F_LP, (f), (q), 0.0 })
#define HP(f, q) ((struct flt){ F_HP, (f), (q), 0.0 })
#define BP(f, q) ((struct flt){ F_BP, (f), (q), 0.0 })

struct bq {
	double b0, b1, b2, a1, a2, z1, z2;
};

static void bq_set(struct bq *f, struct flt s, bool keep)
{
	double fr = s.f < 10.0 ? 10.0 : (s.f > SR * 0.45 ? SR * 0.45 : s.f);
	double q = s.q > 0.05 ? s.q : 0.05;
	double w = TAU * fr / SR, cw = cos(w), sw = sin(w), al = sw / (2.0 * q);
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
	switch (s.type) {
	case F_LP:
		b0 = (1.0 - cw) * 0.5;
		b1 = 1.0 - cw;
		b2 = b0;
		a0 = 1.0 + al;
		a1 = -2.0 * cw;
		a2 = 1.0 - al;
		break;
	case F_HP:
		b0 = (1.0 + cw) * 0.5;
		b1 = -(1.0 + cw);
		b2 = b0;
		a0 = 1.0 + al;
		a1 = -2.0 * cw;
		a2 = 1.0 - al;
		break;
	case F_BP:
		b0 = al;
		b1 = 0.0;
		b2 = -al;
		a0 = 1.0 + al;
		a1 = -2.0 * cw;
		a2 = 1.0 - al;
		break;
	case F_PK: {
		double A = pow(10.0, s.g / 40.0);
		b0 = 1.0 + al * A;
		b1 = -2.0 * cw;
		b2 = 1.0 - al * A;
		a0 = 1.0 + al / A;
		a1 = -2.0 * cw;
		a2 = 1.0 - al / A;
		break;
	}
	default:
		break;
	}
	f->b0 = b0 / a0;
	f->b1 = b1 / a0;
	f->b2 = b2 / a0;
	f->a1 = a1 / a0;
	f->a2 = a2 / a0;
	if (!keep) {
		f->z1 = f->z2 = 0.0;
	}
}

static double bq_run(struct bq *f, double x)
{
	double y = f->b0 * x + f->z1;
	f->z1 = f->b1 * x - f->a1 * y + f->z2;
	f->z2 = f->b2 * x - f->a2 * y;
	return y;
}

/* ---- the sheet of sound being made ---- */

struct buf {
	float *x;
	int n;
};

static int at(double t)
{
	return (int)lrint(t * SR);
}

static void put(struct buf *b, int i, double v)
{
	if (i >= 0 && i < b->n) {
		b->x[i] += (float)v;
	}
}

/* noise through f1 and f2, len samples, brought to an RMS of 0.3 whatever
 * the filters took out of it: so that amp means much the same however
 * narrow the band */
static float *coloured(struct rng *r, int len, struct flt f1, struct flt f2, struct flt f3)
{
	float *t = calloc((size_t)len + 1, sizeof *t);
	if (!t) {
		return NULL;
	}
	struct bq a, b, c;
	if (f1.type != F_NONE) {
		bq_set(&a, f1, false);
	}
	if (f2.type != F_NONE) {
		bq_set(&b, f2, false);
	}
	if (f3.type != F_NONE) {
		bq_set(&c, f3, false);
	}
	double ss = 0.0;
	for (int i = 0; i < len; i++) {
		double s = rnd2(r);
		if (f1.type != F_NONE) {
			s = bq_run(&a, s);
		}
		if (f2.type != F_NONE) {
			s = bq_run(&b, s);
		}
		if (f3.type != F_NONE) {
			s = bq_run(&c, s);
		}
		t[i] = (float)s;
		ss += s * s;
	}
	double rms = sqrt(ss / (len > 0 ? len : 1));
	double g = rms > 1e-9 ? 0.3 / rms : 0.0;
	for (int i = 0; i < len; i++) {
		t[i] *= (float)g;
	}
	return t;
}

/* One resonance of something struck at t0: ringing at f, dying to 1/e in
 * decay seconds. A quarter of a millisecond to rise, so a low one does not
 * start with a click it does not have. */
static void mode(struct buf *b, double t0, double f, double amp, double decay)
{
	int i0 = at(t0);
	double w = TAU * f / SR;
	double k = exp(-1.0 / (decay * SR));
	double e = amp;
	int na = (int)(0.00025 * SR) + 1;
	for (int i = i0, j = 0; i < b->n && e > 1e-5; i++, j++) {
		double a = j < na ? 0.5 - 0.5 * cos(PI * j / na) : 1.0;
		put(b, i, e * a * sin(w * j));
		e *= k;
	}
}

/* Struck: a set of resonances at once, with the click of the blow on top --
 * a sliver of bright noise. f[] and d[] end at a 0. */
static void strike(struct buf *b, struct rng *r, double t0, double amp, double click,
	const double *f, const double *a, const double *d, double detune)
{
	for (int k = 0; f[k] > 0.0; k++) {
		mode(b, t0, f[k] * (1.0 + detune * rnd2(r)), amp * a[k], d[k] * near(r, 1.0, 0.1));
	}
	if (click > 0.0) {
		int len = at(0.004);
		float *n = coloured(r, len, HP(2500.0, 0.7), NOF, NOF);
		if (n) {
			for (int j = 0; j < len; j++) {
				put(b, at(t0) + j, n[j] * click * exp(-j / (0.0007 * SR)));
			}
			free(n);
		}
	}
}

/* Noise, coloured, rising over attack and falling away over decay. */
static void burst(struct buf *b, struct rng *r, double t0, double attack, double decay,
	double amp, struct flt f1, struct flt f2)
{
	int len = at(attack + decay * 7.0) + 1;
	float *n = coloured(r, len, f1, f2, NOF);
	if (!n) {
		return;
	}
	int i0 = at(t0);
	for (int j = 0; j < len; j++) {
		double t = j / SR, e;
		if (t < attack) {
			double u = sin(0.5 * PI * t / attack);
			e = u * u;
		} else {
			e = exp(-(t - attack) / decay);
		}
		put(b, i0 + j, n[j] * amp * e);
	}
	free(n);
}

/* The shape most things that move have: up from nothing to its peak at
 * fraction p of the way, and down to nothing again. */
static double hump(double u, double p)
{
	if (u <= 0.0 || u >= 1.0) {
		return 0.0;
	}
	double s = u < p ? sin(0.5 * PI * u / p) : cos(0.5 * PI * (u - p) / (1.0 - p));
	return s * s;
}

/* A shower of crackles: fibres letting go, one at a time and then in a
 * rush. rate a second at the height of a hump over dur, peaking at p; most
 * small and a few much louder (the sizes fall off as a power, which is
 * what makes it paper rather than rain); kept to lo..hi. */
static void crackle(struct buf *b, struct rng *r, double t0, double dur, double p,
	double rate, double amp, double lo, double hi)
{
	int len = at(dur);
	if (len < 2) {
		return;
	}
	float *t = calloc((size_t)len + 16, sizeof *t);
	if (!t) {
		return;
	}
	double sum = 0.0;
	int count = 0;
	for (int j = 0; j < len; j++) {
		double e = hump((double)j / len, p);
		if (rnd(r) < rate * e / SR) {
			double u = rnd(r);
			double a = pow(1.0 - 0.999 * u, -1.0 / 1.6);
			a = a > 14.0 ? 14.0 : a;
			a *= rnd(r) < 0.5 ? -1.0 : 1.0;
			int w = 2 + (int)(rnd(r) * 5.0);
			for (int k = 0; k < w; k++) {
				t[j + k] += (float)(a * exp(-k * 1.1) * (k & 1 ? -0.55 : 1.0));
			}
			sum += fabs(a);
			count++;
		}
	}
	struct bq h, l, h2;
	bq_set(&h, HP(lo, 0.7), false);
	bq_set(&h2, HP(lo, 0.7), false);
	bq_set(&l, LP(hi, 0.7), false);
	double mean = count ? sum / count : 1.0;
	int i0 = at(t0);
	for (int j = 0; j < len + 16; j++) {
		double s = bq_run(&l, bq_run(&h2, bq_run(&h, t[j])));
		put(b, i0 + j, s * amp * 0.35 / mean);
	}
	free(t);
}

/* Air pushed along by something moving: noise through a band that slides
 * from f0 to f1, loudest at fraction p of dur. */
static void swish(struct buf *b, struct rng *r, double t0, double dur, double p, double f0,
	double f1, double q, double amp)
{
	int len = at(dur);
	if (len < 2) {
		return;
	}
	float *t = calloc((size_t)len, sizeof *t);
	if (!t) {
		return;
	}
	struct bq f;
	bq_set(&f, BP(f0, q), false);
	double ss = 0.0;
	for (int j = 0; j < len; j++) {
		if ((j & 15) == 0) {
			double u = (double)j / len;
			bq_set(&f, BP(f0 * pow(f1 / f0, u), q), true);
		}
		double s = bq_run(&f, rnd2(r));
		t[j] = (float)s;
		ss += s * s;
	}
	double g = 0.3 / fmax(sqrt(ss / len), 1e-9);
	int i0 = at(t0);
	for (int j = 0; j < len; j++) {
		put(b, i0 + j, t[j] * g * amp * hump((double)j / len, p));
	}
	free(t);
}

/* Something drawn along a surface: noise in the bands of what is rubbing,
 * roughened by the grain of it -- a slow random wobble in how hard it
 * catches -- under the envelope env (a function of time from t0). */
static void friction(struct buf *b, struct rng *r, double t0, double dur, const double *bands,
	const double *qs, const double *gains, int nb, double grain_hz, double grain,
	double amp, double (*env)(double t, double dur))
{
	int len = at(dur);
	float *n = calloc((size_t)len, sizeof *n);
	if (!n) {
		return;
	}
	struct bq f[6];
	for (int k = 0; k < nb && k < 6; k++) {
		bq_set(&f[k], BP(bands[k], qs[k]), false);
	}
	struct bq g1, g2;
	bq_set(&g1, LP(grain_hz, 0.7), false);
	bq_set(&g2, LP(grain_hz, 0.7), false);
	double ss = 0.0, gs = 0.0;
	float *gr = calloc((size_t)len, sizeof *gr);
	if (!gr) {
		free(n);
		return;
	}
	for (int j = 0; j < len; j++) {
		double w = rnd2(r), s = 0.0;
		for (int k = 0; k < nb && k < 6; k++) {
			s += bq_run(&f[k], w) * gains[k];
		}
		n[j] = (float)s;
		ss += s * s;
		double c = bq_run(&g2, bq_run(&g1, rnd2(r)));
		gr[j] = (float)c;
		gs += c * c;
	}
	double gn = 0.3 / fmax(sqrt(ss / len), 1e-9);
	double gg = 1.0 / fmax(sqrt(gs / len), 1e-9);
	int i0 = at(t0);
	for (int j = 0; j < len; j++) {
		double t = j / SR;
		double rough = 1.0 - grain + grain * fmin(fabs(gr[j] * gg) * 0.8, 1.6);
		put(b, i0 + j, n[j] * gn * rough * amp * env(t, dur));
	}
	free(gr);
	free(n);
}

/* Stick and slip: a hinge, a key's pins. A train of little catches, its
 * rate wandering from f0 towards f1, each one ringing the metal at a few
 * resonances. */
static void creak(struct buf *b, struct rng *r, double t0, double dur, double f0, double f1,
	double amp, const double *res, int nres)
{
	int len = at(dur);
	float *t = calloc((size_t)len + 1, sizeof *t);
	if (!t) {
		return;
	}
	double ph = 0.0, wob = 0.0;
	for (int j = 0; j < len; j++) {
		double u = (double)j / len;
		wob += (rnd2(r) * 0.08 - wob * 0.002);
		double f = (f0 + (f1 - f0) * u) * (1.0 + 0.04 * wob + 0.06 * sin(TAU * 4.3 * u));
		ph += f / SR;
		if (ph >= 1.0) {
			ph -= 1.0;
			t[j] += (float)(0.7 + 0.3 * rnd(r));
		}
	}
	struct bq f[6];
	for (int k = 0; k < nres && k < 6; k++) {
		bq_set(&f[k], BP(res[k], 6.0 + 2.0 * k), false);
	}
	int i0 = at(t0);
	for (int j = 0; j < len; j++) {
		double s = 0.0;
		for (int k = 0; k < nres && k < 6; k++) {
			s += bq_run(&f[k], t[j]) / (1.0 + 0.5 * k);
		}
		double e = hump((double)j / len, 0.25);
		put(b, i0 + j, s * amp * 3.0 * sqrt(e));
	}
	free(t);
}

/* A stroke of a pencil: graphite on paper, a scratch with the grain of the
 * paper in it, rising and falling with the pressure of the hand. */
static void pencil(struct buf *b, struct rng *r, double t0, double dur, double fc,
	double amp, double wobble_hz)
{
	int len = at(dur);
	float *n = coloured(r, len, BP(fc, 0.9), HP(1500.0, 0.7), NOF);
	float *g = coloured(r, len, LP(260.0, 0.7), NOF, NOF);
	if (!n || !g) {
		free(n);
		free(g);
		return;
	}
	int i0 = at(t0);
	for (int j = 0; j < len; j++) {
		double u = (double)j / len;
		double e = hump(u, 0.35);
		double grain = 0.45 + 0.55 * fmin(fabs(g[j]) * 3.0, 1.4);
		double turn = wobble_hz > 0.0 ? 0.65 + 0.35 * cos(TAU * wobble_hz * j / SR) : 1.0;
		put(b, i0 + j, n[j] * amp * e * grain * turn);
	}
	free(n);
	free(g);
}

/* ---- finishing: rumble out, a level of its own, no clicks at the ends ---- */

/* rms_db: the loudest 30 ms of it, as RMS; peak_db: no sample higher. The
 * quieter of the two wins: a click is held by its peak, a rustle by its
 * body. */
static void finish(struct buf *b, double rms_db, double peak_db)
{
	struct bq hp;
	bq_set(&hp, HP(28.0, 0.7), false);
	for (int i = 0; i < b->n; i++) {
		b->x[i] = (float)bq_run(&hp, b->x[i]);
	}
	int win = at(0.03);
	double ss = 0.0, best = 0.0, peak = 0.0;
	for (int i = 0; i < b->n; i++) {
		double v = b->x[i];
		ss += v * v;
		if (i >= win) {
			double o = b->x[i - win];
			ss -= o * o;
		}
		best = fmax(best, ss / win);
		peak = fmax(peak, fabs(v));
	}
	if (best <= 1e-12 || peak <= 1e-9) {
		return;
	}
	double g = fmin(pow(10.0, rms_db / 20.0) / sqrt(best), pow(10.0, peak_db / 20.0) / peak);
	for (int i = 0; i < b->n; i++) {
		b->x[i] *= (float)g;
	}
	/* the tail that is past hearing goes, and what is left fades out */
	double floor_ = pow(10.0, -78.0 / 20.0);
	int last = b->n - 1;
	while (last > 0 && fabs(b->x[last]) < floor_) {
		last--;
	}
	int n = last + at(0.01);
	b->n = n < b->n ? n : b->n;
	int fo = at(0.008);
	for (int i = 0; i < fo && i < b->n; i++) {
		b->x[b->n - 1 - i] *= (float)(i / (double)fo);
	}
	int fi = at(0.0004);
	for (int i = 0; i < fi && i < b->n; i++) {
		b->x[i] *= (float)(i / (double)fi);
	}
}

/* ================================================================ */
/* The sounds. Each is given its sheet, its dice, and which time this is. */

/* -- paper -- */

static void s_paper_swing(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the flap going over: air, rising as it gathers speed and falling as
	 * it comes down; the band rises with it. Played as long as the flap
	 * takes, faster for a quicker one. */
	swish(b, r, 0.0, 0.40, 0.5, near(r, 520.0, 0.1), near(r, 1900.0, 0.1), 0.85, 0.9);
	swish(b, r, 0.06, 0.30, 0.55, 2400.0, 4200.0, 1.2, 0.18);
	crackle(b, r, 0.02, 0.10, 0.3, 700.0, 0.25, 2000.0, 8000.0);
	finish(b, -31.0, -14.0);
}

static void s_paper_press(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* down: the soft pat of the flap on the stack, then the crease run
	 * under a thumbnail -- a short, dense rush of crackle */
	burst(b, r, 0.0, 0.002, 0.012, 0.9, BP(near(r, 700.0, 0.1), 0.8), NOF);
	mode(b, 0.0, near(r, 175.0, 0.08), 0.10, 0.025);
	crackle(b, r, 0.004, 0.075, 0.25, near(r, 5200.0, 0.15), 0.9, 1800.0, 8500.0);
	crackle(b, r, 0.03, 0.09, 0.5, 1600.0, 0.35, 3000.0, 10000.0);
	finish(b, -27.0, -11.0);
}

static void s_paper_unpress(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a crease letting go, and the flap lifting off */
	crackle(b, r, 0.0, 0.06, 0.3, near(r, 2600.0, 0.2), 0.6, 1500.0, 7000.0);
	swish(b, r, 0.02, 0.22, 0.4, 2200.0, 800.0, 0.8, 0.45);
	finish(b, -31.0, -14.0);
}

static void s_paper_settle(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a packet laid on the desk: a soft "fwap" and the wood under it */
	swish(b, r, 0.0, 0.05, 0.8, 600.0, 1500.0, 0.8, 0.3);
	burst(b, r, 0.045, 0.003, 0.022, 0.8, BP(near(r, 1000.0, 0.1), 0.7), NOF);
	mode(b, 0.045, near(r, 150.0, 0.06), 0.16, 0.03);
	mode(b, 0.045, near(r, 330.0, 0.06), 0.06, 0.02);
	crackle(b, r, 0.045, 0.03, 0.2, 1800.0, 0.25, 2000.0, 7000.0);
	finish(b, -29.0, -12.0);
}

static void s_paper_unfold(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a sheet opening out: air, and the folds letting go one after
	 * another; it lies down flat at the end */
	swish(b, r, 0.0, 0.46, 0.45, 480.0, 1700.0, 0.7, 0.75);
	double t = 0.02;
	for (int k = 0; k < 3; k++) {
		crackle(b, r, t, 0.07, 0.3, near(r, 2200.0, 0.2), near(r, 0.55, 0.3), 1500.0, 7500.0);
		t += near(r, 0.12, 0.25);
	}
	burst(b, r, 0.43, 0.004, 0.02, 0.45, BP(900.0, 0.7), NOF);
	mode(b, 0.43, 160.0, 0.06, 0.03);
	finish(b, -31.0, -14.0);
}

static void s_paper_crumple(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* screwed up in the hand: rushes of crackle as it is squeezed, each
	 * with the crunch of the whole sheet giving under it */
	double t = 0.0;
	int n = 5 + (int)(rnd(r) * 3.0);
	for (int k = 0; k < n; k++) {
		double d = near(r, 0.075, 0.3);
		double a = 0.5 + 0.5 * rnd(r);
		crackle(b, r, t, d, near(r, 0.35, 0.3), near(r, 7500.0, 0.2), a, 1300.0, 9500.0);
		burst(b, r, t + d * 0.25, 0.006, 0.035, a * 0.45, BP(near(r, 650.0, 0.2), 0.6), NOF);
		t += d * near(r, 0.8, 0.2);
	}
	/* and the last squeeze, the ball made */
	crackle(b, r, t, 0.05, 0.3, 9000.0, 0.8, 2000.0, 10000.0);
	finish(b, -23.0, -8.0);
}

static void s_paper_toss(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a ball of paper in the air: a whoosh, rising and falling past */
	swish(b, r, 0.0, 0.42, 0.35, near(r, 320.0, 0.1), near(r, 1100.0, 0.1), 1.1, 0.8);
	finish(b, -35.0, -20.0);
}

static void s_paper_land(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* landing on the floor, past the edge of the desk: muffled by the
	 * distance, a little bounce */
	static const double f[] = { 210.0, 470.0, 980.0, 0.0 };
	static const double a[] = { 1.0, 0.5, 0.25 };
	static const double d[] = { 0.03, 0.02, 0.012 };
	strike(b, r, 0.0, 0.3, 0.0, f, a, d, 0.06);
	burst(b, r, 0.0, 0.002, 0.02, 0.5, BP(650.0, 0.7), LP(1600.0, 0.7));
	strike(b, r, near(r, 0.11, 0.15), 0.1, 0.0, f, a, d, 0.06);
	burst(b, r, 0.11, 0.002, 0.015, 0.15, BP(700.0, 0.7), LP(1500.0, 0.7));
	finish(b, -40.0, -24.0);
}

static void s_corner_lift(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a corner turning up off the page: a flick of air and a crackle */
	swish(b, r, 0.0, 0.09, 0.4, 1600.0, 4200.0, 0.9, 0.6);
	crackle(b, r, 0.005, 0.04, 0.3, 1500.0, 0.4, 2500.0, 9000.0);
	finish(b, -39.0, -21.0);
}

static void s_corner_tap(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a finger on the back of a turned-up corner */
	burst(b, r, 0.0, 0.0015, 0.010, 0.9, BP(near(r, 1500.0, 0.1), 0.8), NOF);
	mode(b, 0.0, near(r, 340.0, 0.08), 0.12, 0.012);
	crackle(b, r, 0.0, 0.025, 0.2, 1200.0, 0.2, 2500.0, 8000.0);
	finish(b, -33.0, -15.0);
}

/* -- the drawer -- */

static double env_pull(double t, double dur)
{
	/* pulled: at speed at once, slowing as it runs out */
	double up = 1.0 - exp(-t / 0.018);
	double down = 0.30 + 0.70 * exp(-t / 0.28);
	double end = t > dur - 0.08 ? (dur - t) / 0.08 : 1.0;
	return up * down * (end > 0.0 ? end : 0.0);
}

static double env_push(double t, double dur)
{
	/* pushed home: it picks up speed and keeps it */
	double up = 1.0 - exp(-t / 0.035);
	double down = 0.55 + 0.45 * exp(-t / 0.40);
	double end = t > dur - 0.08 ? (dur - t) / 0.08 : 1.0;
	return up * down * (end > 0.0 ? end : 0.0);
}

/* the small things in it shifting on the felt: soft, muffled clicks */
static void contents(struct buf *b, struct rng *r, double t0, double span, int n, double amp)
{
	for (int k = 0; k < n; k++) {
		double t = t0 + span * rnd(r);
		double f = near(r, 2300.0, 0.3);
		mode(b, t, f, amp * near(r, 0.7, 0.4), 0.004);
		mode(b, t, near(r, 850.0, 0.2), amp * 0.6, 0.008);
		burst(b, r, t, 0.001, 0.006, amp * 0.5, LP(2600.0, 0.7), NOF);
	}
}

static void s_drawer_out(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* Wood on wood along the runners: a low rumble of the drawer's box,
	 * the rub of the runners above it, and a hiss of the fibres, all
	 * roughened by the grain. Long enough for the slowest pull: it is
	 * stopped when the drawer is. */
	static const double bands[] = { 165.0, 430.0, 1250.0, 3200.0 };
	static const double qs[] = { 1.1, 1.6, 0.9, 0.8 };
	static const double gains[] = { 0.8, 0.55, 0.22, 0.06 };
	friction(b, r, 0.0, 1.0, bands, qs, gains, 4, 32.0, 0.55, 0.9, env_pull);
	crackle(b, r, 0.0, 0.6, 0.1, 55.0, 0.18, 600.0, 3000.0);
	contents(b, r, 0.015, 0.08, 3, 0.10);
	finish(b, -29.0, -15.0);
}

static void s_drawer_in(struct buf *b, struct rng *r, int v)
{
	(void)v;
	static const double bands[] = { 140.0, 380.0, 1100.0, 3000.0 };
	static const double qs[] = { 1.1, 1.5, 0.9, 0.8 };
	static const double gains[] = { 0.85, 0.5, 0.2, 0.05 };
	friction(b, r, 0.0, 1.0, bands, qs, gains, 4, 28.0, 0.5, 0.9, env_push);
	crackle(b, r, 0.0, 0.7, 0.4, 45.0, 0.15, 600.0, 3000.0);
	contents(b, r, 0.02, 0.06, 2, 0.07);
	finish(b, -30.0, -16.0);
}

static void s_drawer_stop_out(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* out against its stop: a knock in the wood, the box rattling once in
	 * its runners, and the things in it sliding on and settling */
	static const double f[] = { 190.0, 420.0, 960.0, 2150.0, 3700.0, 0.0 };
	static const double a[] = { 1.0, 0.7, 0.45, 0.25, 0.12 };
	static const double d[] = { 0.045, 0.03, 0.02, 0.009, 0.005 };
	strike(b, r, 0.0, 0.45, 0.35, f, a, d, 0.04);
	burst(b, r, 0.0, 0.001, 0.012, 0.5, BP(600.0, 0.8), NOF);
	strike(b, r, near(r, 0.014, 0.2), 0.08, 0.08, f, a, d, 0.05);
	contents(b, r, 0.025, 0.07, 4, 0.12);
	finish(b, -22.0, -7.0);
}

static void s_drawer_stop_in(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* home: the face meeting the carcass -- deeper, with the clack of the
	 * edges, and a shiver of the things inside */
	static const double f[] = { 112.0, 245.0, 560.0, 1250.0, 2600.0, 0.0 };
	static const double a[] = { 1.0, 0.75, 0.45, 0.25, 0.10 };
	static const double d[] = { 0.068, 0.045, 0.028, 0.013, 0.006 };
	strike(b, r, 0.0, 0.5, 0.3, f, a, d, 0.04);
	burst(b, r, 0.0, 0.001, 0.015, 0.5, BP(450.0, 0.8), NOF);
	mode(b, 0.001, near(r, 820.0, 0.05), 0.12, 0.012);
	contents(b, r, 0.02, 0.06, 2, 0.08);
	finish(b, -21.0, -6.0);
}

static void s_felt_pick(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* lifted off the felt: the nap brushed, and a tiny tick */
	burst(b, r, 0.0, 0.012, 0.035, 0.8, BP(near(r, 700.0, 0.15), 0.7), LP(2000.0, 0.7));
	mode(b, 0.004, near(r, 2600.0, 0.2), 0.04, 0.004);
	finish(b, -37.0, -22.0);
}

static void s_felt_drop(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* laid on the felt: a muffled thud, the wood of the drawer under it */
	burst(b, r, 0.0, 0.002, 0.018, 0.9, LP(near(r, 900.0, 0.1), 0.7), NOF);
	mode(b, 0.0, near(r, 160.0, 0.08), 0.30, 0.035);
	mode(b, 0.0, near(r, 345.0, 0.08), 0.14, 0.02);
	finish(b, -30.0, -14.0);
}

/* -- the bookcase -- */

static void s_binder_lift(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* drawn out of the row: board rubbing board */
	static const double bands[] = { 420.0, 1300.0, 2900.0 };
	static const double qs[] = { 1.0, 0.9, 0.9 };
	static const double gains[] = { 0.5, 0.35, 0.12 };
	friction(b, r, 0.0, 0.22, bands, qs, gains, 3, 45.0, 0.5, 0.8, env_pull);
	finish(b, -36.0, -22.0);
}

/* the ring they hang on: a little steel, rattling */
static void ring_rattle(struct buf *b, struct rng *r, double t0, double amp, int hits)
{
	static const double f[] = { 2900.0, 4350.0, 6200.0, 7900.0, 0.0 };
	static const double a[] = { 1.0, 0.7, 0.45, 0.25 };
	static const double d[] = { 0.12, 0.09, 0.06, 0.04 };
	double t = t0;
	for (int k = 0; k < hits; k++) {
		strike(b, r, t, amp, amp * 0.6, f, a, d, 0.015);
		t += near(r, 0.035, 0.3);
		amp *= 0.45;
	}
}

static void s_binder_open(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* laid open on the desk: the board coming down flat on the wood, the
	 * ring rattling, the sheets settling after it */
	static const double f[] = { 140.0, 310.0, 720.0, 1500.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.35, 0.15 };
	static const double d[] = { 0.05, 0.035, 0.02, 0.01 };
	strike(b, r, 0.0, 0.5, 0.15, f, a, d, 0.05);
	burst(b, r, 0.0, 0.002, 0.016, 0.6, BP(near(r, 1200.0, 0.1), 0.7), NOF);
	ring_rattle(b, r, 0.008, 0.10, 3);
	crackle(b, r, 0.05, 0.12, 0.3, 1100.0, 0.25, 2000.0, 7500.0);
	finish(b, -24.0, -8.0);
}

static void s_binder_close(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* shut: the boards meeting over the sheets, and the ring */
	static const double f[] = { 180.0, 400.0, 900.0, 0.0 };
	static const double a[] = { 1.0, 0.55, 0.3 };
	static const double d[] = { 0.04, 0.025, 0.015 };
	swish(b, r, 0.0, 0.08, 0.8, 500.0, 1400.0, 0.8, 0.35);
	strike(b, r, 0.075, 0.4, 0.12, f, a, d, 0.05);
	burst(b, r, 0.075, 0.002, 0.014, 0.6, BP(1500.0, 0.7), NOF);
	ring_rattle(b, r, 0.09, 0.07, 2);
	finish(b, -25.0, -9.0);
}

static void s_page_turn(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a sheet swung up over the ring: its hole ticking round the steel,
	 * the air it moves, and a flap as it comes down on the others */
	static const double f[] = { 4600.0, 6900.0, 0.0 };
	static const double a[] = { 1.0, 0.5 };
	static const double d[] = { 0.02, 0.012 };
	strike(b, r, 0.008, 0.05, 0.0, f, a, d, 0.03);
	crackle(b, r, 0.0, 0.04, 0.3, 1400.0, 0.3, 2200.0, 8000.0);
	swish(b, r, 0.0, 0.17, 0.45, near(r, 850.0, 0.12), near(r, 3100.0, 0.12), 0.8, 0.8);
	burst(b, r, near(r, 0.15, 0.08), 0.003, 0.018, 0.45, BP(near(r, 1700.0, 0.12), 0.8), NOF);
	finish(b, -31.0, -15.0);
}

static void s_sheet_take(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* off the ring: a tick of steel, the paper giving at the hole, and the
	 * sheet lifted away */
	static const double f[] = { 3900.0, 5800.0, 0.0 };
	static const double a[] = { 1.0, 0.6 };
	static const double d[] = { 0.03, 0.02 };
	strike(b, r, 0.0, 0.12, 0.08, f, a, d, 0.03);
	crackle(b, r, 0.0, 0.05, 0.25, 6000.0, 0.6, 2000.0, 9000.0);
	swish(b, r, 0.03, 0.22, 0.4, 1100.0, 2600.0, 0.8, 0.5);
	finish(b, -28.0, -12.0);
}

static void s_sheet_down(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* put down flat on the desk: the air under it, then "fwap" */
	swish(b, r, 0.0, 0.07, 0.85, 500.0, 1500.0, 0.8, 0.5);
	burst(b, r, 0.06, 0.006, 0.03, 1.0, BP(near(r, 900.0, 0.1), 0.6), NOF);
	mode(b, 0.06, near(r, 170.0, 0.06), 0.25, 0.04);
	crackle(b, r, 0.06, 0.04, 0.2, 1500.0, 0.25, 2000.0, 7000.0);
	finish(b, -27.0, -10.0);
}

/* -- the post box -- */

/* painted sheet steel, struck */
static void clank(struct buf *b, struct rng *r, double t0, double amp, double pitch)
{
	static const double f[] = { 245.0, 580.0, 1215.0, 2650.0, 3980.0, 5300.0, 0.0 };
	static const double a[] = { 1.0, 0.75, 0.55, 0.35, 0.22, 0.12 };
	static const double d[] = { 0.12, 0.10, 0.075, 0.045, 0.032, 0.022 };
	double ff[7];
	for (int k = 0; k < 7; k++) {
		ff[k] = f[k] * pitch;
	}
	strike(b, r, t0, amp, amp * 0.8, ff, a, d, 0.02);
	/* the panel's own thud: it is a box, not a bell */
	burst(b, r, t0, 0.001, 0.03, amp * 1.4, BP(420.0 * pitch, 0.8), NOF);
}

static void s_post_open(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* its door on its hinge -- a short creak -- and the door at the end of
	 * its swing, a dull clank of painted steel, once more as it rocks */
	static const double res[] = { 950.0, 2300.0, 3650.0 };
	creak(b, r, 0.0, 0.20, near(r, 250.0, 0.1), near(r, 330.0, 0.1), 0.7, res, 3);
	clank(b, r, 0.23, 0.45, near(r, 1.0, 0.03));
	clank(b, r, 0.23 + near(r, 0.07, 0.2), 0.12, near(r, 1.02, 0.03));
	finish(b, -22.0, -8.0);
}

static void s_post_close(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* swung shut: a squeak of the hinge, the clank, and the latch */
	static const double res[] = { 1100.0, 2600.0, 3900.0 };
	creak(b, r, 0.0, 0.09, 380.0, 300.0, 0.5, res, 3);
	clank(b, r, 0.10, 0.5, near(r, 0.92, 0.03));
	static const double lf[] = { 3400.0, 5100.0, 0.0 };
	static const double la[] = { 1.0, 0.6 };
	static const double ld[] = { 0.015, 0.01 };
	strike(b, r, 0.112, 0.25, 0.25, lf, la, ld, 0.03);
	finish(b, -21.0, -7.0);
}

static void s_letter_arrive(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* A letter through the slot in the front: the brass flap pushed
	 * open, the paper sliding in, the letter dropping onto the others, and
	 * the flap snapping shut behind it. */
	static const double tf[] = { 2100.0, 3900.0, 0.0 };
	static const double ta[] = { 1.0, 0.6 };
	static const double td[] = { 0.03, 0.02 };
	strike(b, r, 0.0, 0.10, 0.05, tf, ta, td, 0.03);
	swish(b, r, 0.02, 0.21, 0.5, 1100.0, 2500.0, 0.8, 0.55);
	crackle(b, r, 0.02, 0.2, 0.5, 900.0, 0.25, 2000.0, 8000.0);
	burst(b, r, 0.235, 0.003, 0.025, 0.7, BP(1300.0, 0.7), NOF);
	mode(b, 0.235, 200.0, 0.15, 0.03);
	/* the flap: brass, and the box it is in */
	static const double ff[] = { 640.0, 1650.0, 3300.0, 4950.0, 0.0 };
	static const double fa[] = { 0.7, 1.0, 0.6, 0.35 };
	static const double fd[] = { 0.035, 0.045, 0.03, 0.02 };
	strike(b, r, 0.30, 0.45, 0.4, ff, fa, fd, 0.02);
	strike(b, r, 0.30 + near(r, 0.035, 0.15), 0.12, 0.1, ff, fa, fd, 0.02);
	finish(b, -20.0, -6.0);
}

static void s_letter_take(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* drawn out from among the others */
	swish(b, r, 0.0, 0.28, 0.35, 2400.0, 900.0, 0.7, 0.75);
	crackle(b, r, 0.0, 0.12, 0.3, 1500.0, 0.3, 2000.0, 8000.0);
	burst(b, r, 0.25, 0.003, 0.02, 0.3, BP(1100.0, 0.7), NOF);
	finish(b, -30.0, -14.0);
}

static void s_letter_flick(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the letters turned over, one */
	swish(b, r, 0.0, 0.11, 0.45, near(r, 1400.0, 0.12), near(r, 3500.0, 0.12), 0.8, 0.8);
	burst(b, r, 0.095, 0.002, 0.014, 0.4, BP(near(r, 2000.0, 0.12), 0.8), NOF);
	finish(b, -34.0, -18.0);
}

/* -- keys, switches, the set -- */

static void s_key_press(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a hard plastic key going down: the click of it, and the thock of
	 * it meeting the bottom */
	static const double f[] = { 2250.0, 3650.0, 5200.0, 0.0 };
	static const double a[] = { 1.0, 0.65, 0.3 };
	static const double d[] = { 0.005, 0.004, 0.0025 };
	strike(b, r, 0.0, 0.35, 0.6, f, a, d, 0.04);
	static const double bf[] = { 330.0, 780.0, 1500.0, 0.0 };
	static const double ba[] = { 1.0, 0.55, 0.3 };
	static const double bd[] = { 0.008, 0.006, 0.004 };
	strike(b, r, 0.004, 0.35, 0.0, bf, ba, bd, 0.05);
	finish(b, -25.0, -11.0);
}

static void s_key_release(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* and back up: higher and lighter */
	static const double f[] = { 2650.0, 4200.0, 0.0 };
	static const double a[] = { 1.0, 0.6 };
	static const double d[] = { 0.004, 0.003 };
	strike(b, r, 0.0, 0.25, 0.35, f, a, d, 0.04);
	mode(b, 0.0, near(r, 420.0, 0.06), 0.12, 0.006);
	finish(b, -30.0, -15.0);
}

static void s_toggle(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a bat lever: it slides over its spring, and snaps */
	static const double sf[] = { 5200.0, 0.0 };
	static const double sa[] = { 1.0 };
	static const double sd[] = { 0.003 };
	strike(b, r, 0.0, 0.06, 0.1, sf, sa, sd, 0.05);
	static const double f[] = { 1850.0, 3350.0, 5700.0, 8100.0, 0.0 };
	static const double a[] = { 1.0, 0.8, 0.5, 0.2 };
	static const double d[] = { 0.025, 0.018, 0.012, 0.007 };
	strike(b, r, 0.007, 0.32, 0.9, f, a, d, 0.03);
	mode(b, 0.007, near(r, 230.0, 0.06), 0.35, 0.03);
	finish(b, -22.0, -8.0);
}

static void s_knob_tick(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a detent going by under the fingers */
	static const double f[] = { 3600.0, 6100.0, 900.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.35 };
	static const double d[] = { 0.004, 0.0025, 0.006 };
	strike(b, r, 0.0, 0.3, 0.3, f, a, d, 0.05);
	finish(b, -34.0, -19.0);
}

static void s_key_in(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the key going in: steel past the pins, each one ticking over a cut */
	static const double bands[] = { 4800.0, 7200.0 };
	static const double qs[] = { 2.0, 1.5 };
	static const double gains[] = { 0.6, 0.3 };
	friction(b, r, 0.0, 0.26, bands, qs, gains, 2, 60.0, 0.6, 0.35, env_push);
	static const double f[] = { 4200.0, 6800.0, 0.0 };
	static const double a[] = { 1.0, 0.5 };
	static const double d[] = { 0.006, 0.004 };
	for (int k = 0; k < 5; k++) {
		strike(b, r, 0.03 + k * near(r, 0.045, 0.15), 0.12, 0.1, f, a, d, 0.05);
	}
	finish(b, -31.0, -15.0);
}

static void s_lock_clunk(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* turned: the cylinder going round and the bolt thrown */
	static const double cf[] = { 3300.0, 5000.0, 0.0 };
	static const double ca[] = { 1.0, 0.5 };
	static const double cd[] = { 0.008, 0.005 };
	strike(b, r, 0.0, 0.1, 0.1, cf, ca, cd, 0.04);
	static const double f[] = { 600.0, 1450.0, 3100.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.3 };
	static const double d[] = { 0.05, 0.03, 0.015 };
	strike(b, r, 0.03, 0.4, 0.4, f, a, d, 0.03);
	mode(b, 0.03, near(r, 170.0, 0.05), 0.35, 0.04);
	finish(b, -24.0, -9.0);
}

static void s_guard_lift(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the glass over the buttons, its hinge letting go */
	static const double f[] = { 1150.0, 2600.0, 4500.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.3 };
	static const double d[] = { 0.02, 0.014, 0.008 };
	strike(b, r, 0.0, 0.25, 0.3, f, a, d, 0.04);
	swish(b, r, 0.01, 0.18, 0.4, 700.0, 1600.0, 0.9, 0.25);
	finish(b, -29.0, -13.0);
}

static void s_guard_stop(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* and against its stop: acrylic on steel, a tock */
	static const double f[] = { 900.0, 2200.0, 3800.0, 0.0 };
	static const double a[] = { 1.0, 0.55, 0.25 };
	static const double d[] = { 0.015, 0.010, 0.006 };
	strike(b, r, 0.0, 0.4, 0.3, f, a, d, 0.04);
	mode(b, 0.0, near(r, 260.0, 0.05), 0.25, 0.014);
	burst(b, r, 0.0, 0.001, 0.008, 0.4, BP(1200.0, 0.8), NOF);
	finish(b, -25.0, -10.0);
}

static void s_lid_lift(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the console's walnut lid on its brass hinge: the knuckle turning, a short dry creak
	 * lower than a door's, and the air the board moves */
	static const double res[] = { 780.0, 1900.0, 3100.0 };
	creak(b, r, 0.0, 0.16, near(r, 210.0, 0.1), near(r, 260.0, 0.1), 0.45, res, 3);
	swish(b, r, 0.02, 0.20, 0.35, 380.0, 900.0, 0.8, 0.18);
	finish(b, -30.0, -14.0);
}

static void s_lid_stop(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* and at either end of its swing: walnut meeting the plate or its stop, a dull knock low
	 * in the wood, and the brass of the hinge once */
	static const double f[] = { 230.0, 520.0, 1180.0, 2600.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.35, 0.15 };
	static const double d[] = { 0.035, 0.022, 0.012, 0.006 };
	strike(b, r, 0.0, 0.42, 0.3, f, a, d, 0.04);
	burst(b, r, 0.0, 0.001, 0.010, 0.45, BP(700.0, 0.8), NOF);
	static const double hf[] = { 3300.0, 5200.0, 0.0 };
	static const double ha[] = { 1.0, 0.5 };
	static const double hd[] = { 0.010, 0.006 };
	strike(b, r, near(r, 0.008, 0.2), 0.07, 0.05, hf, ha, hd, 0.03);
	finish(b, -24.0, -9.0);
}

static void s_power_press(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the big button, all the way down, and the relay behind the panel
	 * dropping out a moment later */
	static const double f[] = { 1400.0, 2600.0, 0.0 };
	static const double a[] = { 1.0, 0.5 };
	static const double d[] = { 0.01, 0.006 };
	strike(b, r, 0.0, 0.3, 0.4, f, a, d, 0.04);
	mode(b, 0.002, 210.0, 0.45, 0.035);
	static const double rf[] = { 2500.0, 4300.0, 7000.0, 0.0 };
	static const double ra[] = { 1.0, 0.6, 0.3 };
	static const double rd[] = { 0.012, 0.008, 0.005 };
	strike(b, r, 0.075, 0.35, 0.5, rf, ra, rd, 0.03);
	mode(b, 0.075, 120.0, 0.3, 0.03);
	finish(b, -21.0, -7.0);
}

static void s_power_down(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* The set going out. The mains hum of its transformer dies away; the
	 * whine of the tube's deflection falls through the octaves to nothing;
	 * there is a crackle of the charge on the glass going, and as it cools
	 * the glass ticks. */
	int n = b->n;
	double ph[5] = { 0 }, wp = 0.0;
	for (int i = 0; i < n; i++) {
		double t = i / SR;
		double hum_e = exp(-t / 0.9) * (1.0 - exp(-t / 0.01));
		double h = 0.0;
		for (int k = 0; k < 5; k++) {
			double f = 100.0 * (k + 1) * (1.0 - 0.04 * (1.0 - exp(-t / 1.2)));
			ph[k] += f / SR;
			h += sin(TAU * ph[k]) / (1.0 + k * 1.3);
		}
		double fw = 70.0 + 1150.0 * exp(-t / 0.55);
		wp += fw / SR;
		double w = sin(TAU * wp) + 0.3 * sin(TAU * 2.0 * wp) + 0.12 * sin(TAU * 3.0 * wp);
		double we = exp(-t / 0.75) * (1.0 - exp(-t / 0.02));
		put(b, i, 0.20 * hum_e * h + 0.10 * we * w);
	}
	crackle(b, r, 0.0, 0.18, 0.2, 4500.0, 0.5, 2500.0, 12000.0);
	for (int k = 0; k < 6; k++) {
		mode(b, 0.9 + 1.7 * rnd(r), near(r, 4800.0, 0.3), 0.03, 0.003);
	}
	finish(b, -27.0, -12.0);
}

static void s_card_slide(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a card drawn out from under the console, card on wood */
	static const double bands[] = { 1800.0, 3600.0, 700.0 };
	static const double qs[] = { 0.9, 1.0, 1.0 };
	static const double gains[] = { 0.6, 0.3, 0.25 };
	friction(b, r, 0.0, 0.3, bands, qs, gains, 3, 70.0, 0.6, 0.8, env_pull);
	finish(b, -35.0, -19.0);
}

/* the transformer's hum: mains, rectified, its harmonics thinning upward,
 * loud as env says at each moment */
static void hum(struct buf *b, double t0, double dur, double amp, double (*env)(double t))
{
	int i0 = at(t0), len = at(dur);
	double ph = 0.0;
	for (int j = 0; j < len; j++) {
		double t = j / SR;
		ph += 100.0 / SR;
		double h = 0.0;
		for (int k = 0; k < 5; k++) {
			h += sin(TAU * ph * (k + 1) + k * 0.7) / (1.0 + k * 1.3);
		}
		put(b, i0 + j, amp * env(t) * h);
	}
}

static double env_warm(double t)
{
	/* up as the filaments come up to heat, and back down to nothing:
	 * an atmosphere, not a drone */
	return hump(t / 2.5, 0.45);
}

static double env_off(double t)
{
	return exp(-t / 0.12) * (1.0 - exp(-t / 0.004));
}

static void s_set_warm(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* switched on: the relay pulling in, and the hum of the set coming
	 * up and dying away as it settles */
	static const double f[] = { 2400.0, 4100.0, 6600.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.3 };
	static const double d[] = { 0.010, 0.007, 0.004 };
	strike(b, r, 0.0, 0.3, 0.45, f, a, d, 0.03);
	mode(b, 0.0, near(r, 140.0, 0.05), 0.25, 0.025);
	hum(b, 0.02, 2.5, 0.10, env_warm);
	crackle(b, r, 0.3, 1.6, 0.4, 60.0, 0.15, 3000.0, 11000.0);
	finish(b, -31.0, -14.0);
}

static void s_set_off(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* and off: the relay dropping out, the hum cut */
	static const double f[] = { 2200.0, 3800.0, 0.0 };
	static const double a[] = { 1.0, 0.5 };
	static const double d[] = { 0.008, 0.005 };
	strike(b, r, 0.0, 0.3, 0.35, f, a, d, 0.03);
	hum(b, 0.0, 0.5, 0.18, env_off);
	finish(b, -31.0, -15.0);
}

/* -- the wireless aerial -- */

static void aerial(struct buf *b, struct rng *r, bool up)
{
	/* A little geared motor driving a nylon cable: a buzz with a fast
	 * rattle in it from the gears, pitched up as the load comes off going
	 * up (and down going down), soft at both ends where it gets going and
	 * runs down; then the stop, the cable snatching against the end. */
	const double dur = 1.55;
	int len = at(dur);
	double ph = 0.0, rp = 0.0;
	double f0 = near(r, up ? 178.0 : 214.0, 0.03), f1 = up ? f0 * 1.20 : f0 * 0.86;
	for (int j = 0; j < len; j++) {
		double t = j / SR, u = t / dur;
		double f = f0 + (f1 - f0) * u;
		ph += f / SR;
		rp += f * 0.21 / SR;
		double env = (1.0 - exp(-t / 0.05)) * (1.0 - exp(-(dur - t) / 0.06));
		double buzz = 0.0;
		for (int k = 1; k <= 7; k++) {
			buzz += sin(TAU * ph * k + k * 1.3) / (k * 0.9);
		}
		double rattle = 0.55 + 0.45 * pow(0.5 + 0.5 * sin(TAU * rp), 3.0);
		double hiss = rnd2(r) * 0.10;
		put(b, j, env * (buzz * 0.18 * rattle + hiss * 0.5));
	}
	static const double f[] = { 1800.0, 3300.0, 5200.0, 0.0 };
	static const double a[] = { 1.0, 0.55, 0.25 };
	static const double d[] = { 0.012, 0.007, 0.004 };
	strike(b, r, dur - 0.01, 0.35, 0.40, f, a, d, 0.02);
}

static void s_aerial_up(struct buf *b, struct rng *r, int v)
{
	(void)v;
	aerial(b, r, true);
	finish(b, -34.0, -18.0);
}

static void s_aerial_down(struct buf *b, struct rng *r, int v)
{
	(void)v;
	aerial(b, r, false);
	finish(b, -34.0, -18.0);
}

/* -- writing -- */

static void s_pencil_tick(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a tick: a short stroke down, and a longer one up and away */
	pencil(b, r, 0.0, near(r, 0.055, 0.1), near(r, 3600.0, 0.1), 0.8, 0.0);
	pencil(b, r, near(r, 0.07, 0.1), near(r, 0.12, 0.1), near(r, 4200.0, 0.1), 1.0, 0.0);
	finish(b, -32.0, -16.0);
}

static void s_pencil_ring(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a ring drawn round a word: round once and a bit, the stroke
	 * changing as the pencil changes direction */
	pencil(b, r, 0.0, near(r, 0.34, 0.08), near(r, 3900.0, 0.1), 1.0, near(r, 5.6, 0.1));
	finish(b, -33.0, -17.0);
}

/* ================================================================ */

/* -- the days kept -- */

static void s_beep(struct buf *b, struct rng *r, int v)
{
	(void)r;
	(void)v;
	/* The clock's buzzer playing a tune: a soft square of a note, odd
	 * harmonics rolled off, struck and held a moment and let go -- the
	 * way an alarm clock that knows a tune plays it. C5. */
	const double f = 523.25;
	for (int i = 0; i < b->n; i++) {
		double t = i / SR;
		double env = (t < 0.006 ? t / 0.006 : 1.0) * (t < 0.16 ? 1.0 : exp(-(t - 0.16) / 0.16));
		double w = TAU * f * t;
		double x = sin(w) + 0.28 * sin(3.0 * w) + 0.10 * sin(5.0 * w) + 0.04 * sin(7.0 * w);
		put(b, i, 0.25 * env * x);
	}
	finish(b, -21.0, -7.0);
}

static void metronome(struct buf *b, struct rng *r, bool accent)
{
	/* a pendulum metronome's escapement: a hard little knock in wood */
	static const double f[] = { 2150.0, 3380.0, 5300.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.25 };
	static const double d[] = { 0.012, 0.008, 0.004 };
	static const double fa[] = { 1250.0, 2350.0, 3900.0, 0.0 };
	static const double aa[] = { 1.0, 0.55, 0.25 };
	static const double da[] = { 0.020, 0.012, 0.006 };
	if (accent) {
		strike(b, r, 0.0, 0.45, 0.5, fa, aa, da, 0.02);
	} else {
		strike(b, r, 0.0, 0.35, 0.4, f, a, d, 0.02);
	}
	mode(b, 0.0, near(r, accent ? 380.0 : 520.0, 0.03), 0.20, 0.018);
}

static void s_tick(struct buf *b, struct rng *r, int v)
{
	(void)v;
	metronome(b, r, false);
	finish(b, -30.0, -14.0);
}

static void s_tock(struct buf *b, struct rng *r, int v)
{
	(void)v;
	metronome(b, r, true);
	finish(b, -27.0, -11.0);
}

static void s_bell(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a glockenspiel bar, C6: its own inharmonic partials, the high ones
	 * gone first, and the little knock of the mallet */
	static const double f[] = { 1046.5, 2884.0, 5651.0, 9345.0, 0.0 };
	static const double a[] = { 1.0, 0.42, 0.18, 0.07 };
	static const double d[] = { 1.10, 0.45, 0.18, 0.08 };
	strike(b, r, 0.0, 0.5, 0.25, f, a, d, 0.0);
	finish(b, -24.0, -8.0);
}

static void s_jingle(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a strap of sleigh bells shaken once: a shower of tiny bells, each
	 * with its pea rattling inside */
	int n = 18 + (int)(rnd(r) * 8.0);
	for (int i = 0; i < n; i++) {
		double t0 = 0.01 + pow(rnd(r), 1.6) * 0.20;
		double f0 = 3600.0 + rnd(r) * 3200.0;
		double fr[] = { f0, f0 * 1.58, f0 * 2.31, 0.0 };
		double am[] = { 1.0, 0.5, 0.25 };
		double de[] = { 0.06 + rnd(r) * 0.07, 0.035, 0.02 };
		strike(b, r, t0, 0.10 + rnd(r) * 0.12, 0.25, fr, am, de, 0.0);
	}
	finish(b, -28.0, -12.0);
}

static void s_fw_whistle(struct buf *b, struct rng *r, int v)
{
	/* a rocket going up: air tearing past it -- a breathy rush whose
	 * band climbs as it does, not a tone (a clean rising tone is an
	 * alarm) -- and gone at the top */
	double dur = 1.0 + 0.2 * v;
	swish(b, r, 0.0, dur, 0.85, near(r, 900.0, 0.08), near(r, 3200.0, 0.08), 3.5, 0.9);
	swish(b, r, 0.0, dur, 0.80, 2500.0, 6000.0, 1.0, 0.25);
	finish(b, -32.0, -18.0);
}

/* a hard knee: loud things squashed round, as a microphone near a bang
 * does, never cut flat */
static void squash(struct buf *b, double drive)
{
	double n = tanh(drive);
	for (int i = 0; i < b->n; i++) {
		b->x[i] = (float)(tanh(b->x[i] * drive) / n);
	}
}

static void s_fw_boom(struct buf *b, struct rng *r, int v)
{
	/* A shell bursting overhead: the crack of it, the deep body of the
	 * bang that is felt as much as heard, the roll of it going off down
	 * the sky, and the houses giving it back twice. No pitch anywhere. */
	burst(b, r, 0.0, 0.0004, 0.025, 0.85, HP(200.0, 0.7), LP(5000.0, 0.7));
	burst(b, r, 0.0, 0.0008, 0.120, 1.0 + 0.05 * v, LP(240.0, 0.8), HP(45.0, 0.7));
	burst(b, r, 0.002, 0.002, 0.060, 0.45, BP(110.0, 0.9), NOF);
	burst(b, r, 0.02, 0.05, 0.90, 0.22, LP(500.0, 0.7), HP(35.0, 0.7));
	for (int e = 1; e <= 2; e++) {
		double t0 = e == 1 ? near(r, 0.35, 0.1) : near(r, 0.80, 0.1);
		double k = e == 1 ? 0.30 : 0.14;
		burst(b, r, t0, 0.003, 0.08, k, LP(900.0, 0.7), HP(60.0, 0.7));
	}
	squash(b, 1.3);
	finish(b, -17.0, -2.5);
}

static void s_fw_crackle(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the stars of a crackler going off as they fall: each a tiny dry
	 * snap, thick at first and thinning out */
	crackle(b, r, 0.04, 1.5, 0.15, 220.0, 0.5, 1200.0, 4500.0);
	finish(b, -34.0, -18.0);
}

static void s_popper(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a party popper: a sharp snap, the card tube's knock behind it, and
	 * the paper it throws pattering down */
	burst(b, r, 0.0, 0.0003, 0.007, 1.0, HP(900.0, 0.7), LP(7000.0, 0.7));
	mode(b, 0.0005, near(r, 640.0, 0.08), 0.08, 0.005);
	crackle(b, r, 0.12, 1.0, 0.25, 1500.0, 0.22, 2800.0, 8000.0);
	squash(b, 1.4);
	finish(b, -24.0, -5.0);
}

/* -- the camera -- */

static void s_shutter(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* A reflex camera going off. The mirror flung up against its stop --
	 * a slap, and the thud of the body round it; the shutter's two
	 * blinds, one and then the other, ticks of thin steel; the mirror
	 * falling back; and the gears winding the shutter for the next. */
	static const double mf[] = { 820.0, 1900.0, 3400.0, 5600.0, 0.0 };
	static const double ma[] = { 1.0, 0.8, 0.5, 0.25 };
	static const double md[] = { 0.012, 0.009, 0.006, 0.004 };
	strike(b, r, 0.0, 0.45, 0.7, mf, ma, md, 0.04);
	mode(b, 0.0, near(r, 150.0, 0.05), 0.40, 0.022);
	burst(b, r, 0.0, 0.0008, 0.010, 0.5, BP(1100.0, 0.8), NOF);
	static const double tf[] = { 4300.0, 6900.0, 9200.0, 0.0 };
	static const double ta[] = { 1.0, 0.6, 0.3 };
	static const double td[] = { 0.004, 0.003, 0.002 };
	double t1 = near(r, 0.008, 0.15);
	strike(b, r, t1, 0.22, 0.4, tf, ta, td, 0.03);
	strike(b, r, t1 + near(r, 0.017, 0.1), 0.26, 0.45, tf, ta, td, 0.03);
	static const double df[] = { 700.0, 1650.0, 3000.0, 5000.0, 0.0 };
	double t2 = near(r, 0.062, 0.08);
	strike(b, r, t2, 0.32, 0.5, df, ma, md, 0.05);
	mode(b, t2, near(r, 135.0, 0.05), 0.30, 0.020);
	static const double gears[] = { 1700.0, 3100.0, 5200.0 };
	creak(b, r, t2 + 0.006, near(r, 0.075, 0.1), near(r, 430.0, 0.06), near(r, 370.0, 0.06),
		2.0, gears, 3);
	finish(b, -21.0, -7.0);
}

/* -- the camera's tape -- */

/* A little motor: a cassette's reels taken up, its capstan got going -- a
 * soft buzz with its gears' rattle in it, pitched from f0 to f1 over dur,
 * getting going and running down at its ends. */
static void motor(struct buf *b, struct rng *r, double t0, double dur, double f0, double f1,
	double amp)
{
	int i0 = at(t0), len = at(dur);
	double ph = 0.0, rp = 0.0;
	for (int j = 0; j < len; j++) {
		double t = j / SR, u = t / dur;
		double f = f0 + (f1 - f0) * u;
		ph += f / SR;
		rp += f * 0.17 / SR;
		double env = (1.0 - exp(-t / 0.03)) * (1.0 - exp(-(dur - t) / 0.04));
		double buzz = 0.0;
		for (int k = 1; k <= 6; k++) {
			buzz += sin(TAU * ph * k + k * 0.9) / (k * 1.1);
		}
		double rattle = 0.6 + 0.4 * pow(0.5 + 0.5 * sin(TAU * rp), 3.0);
		put(b, i0 + j, amp * env * (buzz * 0.16 * rattle + rnd2(r) * 0.04));
	}
}

static void s_tape_in(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* A cassette pushed into its slot: its shell sliding past the flap,
	 * the catch taking it with a click; then the mechanism drawing it
	 * home -- a clunk -- and its motor a moment, the tape taken up. */
	static const double bands[] = { 2600.0, 5200.0, 900.0 };
	static const double qs[] = { 1.1, 1.3, 1.0 };
	static const double gains[] = { 0.6, 0.3, 0.3 };
	friction(b, r, 0.0, 0.20, bands, qs, gains, 3, 50.0, 0.5, 0.45, env_push);
	static const double cf[] = { 2900.0, 4700.0, 7400.0, 0.0 };
	static const double ca[] = { 1.0, 0.55, 0.25 };
	static const double cd[] = { 0.006, 0.004, 0.003 };
	strike(b, r, 0.19, 0.30, 0.35, cf, ca, cd, 0.04);
	static const double kf[] = { 420.0, 980.0, 2100.0, 0.0 };
	static const double ka[] = { 1.0, 0.6, 0.3 };
	static const double kd[] = { 0.035, 0.022, 0.012 };
	strike(b, r, 0.25, 0.45, 0.25, kf, ka, kd, 0.03);
	mode(b, 0.25, near(r, 165.0, 0.05), 0.30, 0.035);
	motor(b, r, 0.30, 0.34, near(r, 300.0, 0.03), 330.0, 0.5);
	finish(b, -27.0, -11.0);
}

static void s_tape_out(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* The eject: the catch let go and the spring throwing the cassette
	 * out against the hand -- a clack, the spring's ring, the shell
	 * sliding. */
	static const double kf[] = { 760.0, 1700.0, 3600.0, 0.0 };
	static const double ka[] = { 1.0, 0.7, 0.35 };
	static const double kd[] = { 0.025, 0.015, 0.008 };
	strike(b, r, 0.0, 0.45, 0.5, kf, ka, kd, 0.03);
	mode(b, 0.004, near(r, 1350.0, 0.04), 0.10, 0.06);
	static const double bands[] = { 2400.0, 4800.0 };
	static const double qs[] = { 1.0, 1.2 };
	static const double gains[] = { 0.6, 0.3 };
	friction(b, r, 0.02, 0.14, bands, qs, gains, 2, 50.0, 0.5, 0.35, env_pull);
	finish(b, -27.0, -11.0);
}

/* A camcorder's beep: a thin, pure note, on and off. */
static void pip(struct buf *b, double t0, double dur, double f, double amp)
{
	int i0 = at(t0), len = at(dur);
	for (int j = 0; j < len; j++) {
		double t = j / SR;
		double env = fmin(1.0, t / 0.003) * fmin(1.0, (dur - t) / 0.006);
		put(b, i0 + j, amp * env * sin(TAU * f * t));
	}
}

static void s_rec_start(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* Recording: the beep, and the transport getting going -- a soft
	 * clunk, the motor winding up. */
	pip(b, 0.0, 0.07, 2350.0, 0.35);
	static const double kf[] = { 380.0, 900.0, 0.0 };
	static const double ka[] = { 1.0, 0.5 };
	static const double kd[] = { 0.03, 0.02 };
	strike(b, r, 0.09, 0.30, 0.15, kf, ka, kd, 0.03);
	motor(b, r, 0.10, 0.45, near(r, 230.0, 0.03), 330.0, 0.45);
	finish(b, -27.0, -11.0);
}

static void s_rec_stop(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* Stopped: two beeps, the motor running down, the clunk of the
	 * transport letting go. */
	pip(b, 0.0, 0.06, 2350.0, 0.35);
	pip(b, 0.11, 0.06, 2350.0, 0.35);
	motor(b, r, 0.0, 0.30, 330.0, near(r, 210.0, 0.03), 0.40);
	static const double kf[] = { 380.0, 900.0, 0.0 };
	static const double ka[] = { 1.0, 0.5 };
	static const double kd[] = { 0.03, 0.02 };
	strike(b, r, 0.30, 0.30, 0.15, kf, ka, kd, 0.03);
	finish(b, -27.0, -11.0);
}

static void s_flash(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* A flash going off: the tube's pop -- a hard tick, a breath of bright
	 * noise -- and the capacitor charging again, a thin whine rising and
	 * fading away. */
	static const double f[] = { 3800.0, 6100.0, 9000.0, 0.0 };
	static const double a[] = { 1.0, 0.6, 0.3 };
	static const double d[] = { 0.004, 0.003, 0.002 };
	strike(b, r, 0.0, 0.30, 0.6, f, a, d, 0.03);
	burst(b, r, 0.0, 0.0005, 0.012, 0.35, HP(2500.0, 0.7), NOF);
	int i0 = at(0.06), len = at(1.1);
	double ph = 0.0;
	for (int j = 0; j < len; j++) {
		double t = j / SR, u = t / 1.1;
		double fr = 2600.0 + 5200.0 * (1.0 - (1.0 - u) * (1.0 - u));
		ph += fr / SR;
		double env = (1.0 - exp(-t / 0.08)) * (1.0 - u) * (1.0 - u);
		put(b, i0 + j, 0.035 * env * sin(TAU * ph));
	}
	finish(b, -30.0, -12.0);
}

struct sfx_def {
	const char *name;
	double secs;
	int variants;
	void (*make)(struct buf *b, struct rng *r, int v);
};

static void s_type_strike(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* a long lever going down, then the type through the ribbon on the platen, hard rubber under
	 * it and the whole frame ringing a little; the bar falling back on its felt */
	static const double lf[] = { 1250.0, 2400.0, 0.0 };
	static const double la[] = { 1.0, 0.5 };
	static const double ld[] = { 0.006, 0.004 };
	strike(b, r, 0.0, 0.12, 0.10, lf, la, ld, 0.05);
	static const double f[] = { 1750.0, 2950.0, 4400.0, 6600.0, 0.0 };
	static const double a[] = { 1.0, 0.85, 0.55, 0.30 };
	static const double d[] = { 0.016, 0.011, 0.007, 0.004 };
	strike(b, r, 0.012, 0.42, 0.85, f, a, d, 0.05);
	static const double tf[] = { 190.0, 360.0, 720.0, 0.0 };
	static const double ta[] = { 1.0, 0.7, 0.35 };
	static const double td[] = { 0.024, 0.016, 0.009 };
	strike(b, r, 0.012, 0.50, 0.0, tf, ta, td, 0.06);
	static const double bf[] = { 900.0, 1700.0, 0.0 };
	static const double ba[] = { 1.0, 0.5 };
	static const double bd[] = { 0.010, 0.006 };
	strike(b, r, 0.075, 0.10, 0.06, bf, ba, bd, 0.05);
	finish(b, -22.0, -8.0);
}

static void s_type_release(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the lever back up, and the escapement letting the carriage go on a tooth */
	static const double f[] = { 1500.0, 3100.0, 0.0 };
	static const double a[] = { 1.0, 0.6 };
	static const double d[] = { 0.006, 0.004 };
	strike(b, r, 0.0, 0.2, 0.12, f, a, d, 0.05);
	static const double ef[] = { 3600.0, 5400.0, 0.0 };
	static const double ea[] = { 1.0, 0.5 };
	static const double ed[] = { 0.003, 0.002 };
	strike(b, r, 0.012, 0.16, 0.2, ef, ea, ed, 0.05);
	finish(b, -33.0, -18.0);
}

static void s_type_space(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the bar going down on its two arms: no type, only the escapement */
	static const double f[] = { 260.0, 560.0, 1300.0, 0.0 };
	static const double a[] = { 1.0, 0.7, 0.3 };
	static const double d[] = { 0.016, 0.012, 0.006 };
	strike(b, r, 0.0, 0.4, 0.15, f, a, d, 0.05);
	static const double ef[] = { 3400.0, 5200.0, 0.0 };
	static const double ea[] = { 1.0, 0.5 };
	static const double ed[] = { 0.004, 0.002 };
	strike(b, r, 0.03, 0.2, 0.3, ef, ea, ed, 0.05);
	finish(b, -25.0, -11.0);
}

static void s_type_shift(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the whole basket of type lifting on its springs */
	static const double f[] = { 135.0, 310.0, 860.0, 1900.0, 0.0 };
	static const double a[] = { 1.0, 0.75, 0.35, 0.15 };
	static const double d[] = { 0.055, 0.035, 0.016, 0.008 };
	strike(b, r, 0.0, 0.5, 0.12, f, a, d, 0.04);
	finish(b, -24.0, -10.0);
}

static void s_type_bell(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the little bell under the carriage, struck by its hammer */
	static const double f[] = { 2650.0, 4310.0, 6900.0, 10100.0, 0.0 };
	static const double a[] = { 1.0, 0.5, 0.26, 0.09 };
	static const double d[] = { 0.42, 0.26, 0.11, 0.05 };
	strike(b, r, 0.0, 0.5, 0.2, f, a, d, 0.0);
	finish(b, -25.0, -9.0);
}

static void s_type_return(struct buf *b, struct rng *r, int v)
{
	(void)v;
	/* the lever taking up the line, the platen's ratchet going round a tooth, and the carriage
	 * running home, tooth after tooth, faster, to its stop */
	static const double lf[] = { 700.0, 1600.0, 3000.0, 0.0 };
	static const double la[] = { 1.0, 0.6, 0.3 };
	static const double ld[] = { 0.012, 0.008, 0.004 };
	strike(b, r, 0.0, 0.3, 0.3, lf, la, ld, 0.04);
	strike(b, r, 0.045, 0.25, 0.3, lf, la, ld, 0.04);
	double t = 0.09, gap = 0.014;
	static const double tf[] = { 2600.0, 4300.0, 0.0 };
	static const double ta[] = { 1.0, 0.5 };
	static const double td[] = { 0.0025, 0.0015 };
	while (t < 0.31) {
		strike(b, r, t, 0.09 * near(r, 1.0, 0.2), 0.05, tf, ta, td, 0.05);
		t += gap * near(r, 1.0, 0.08);
		gap = gap > 0.007 ? gap * 0.965 : gap;
	}
	burst(b, r, 0.09, 0.05, 0.08, 0.05, BP(1800.0, 0.6), NOF);
	static const double sf[] = { 170.0, 380.0, 950.0, 2100.0, 0.0 };
	static const double sa[] = { 1.0, 0.8, 0.4, 0.2 };
	static const double sd[] = { 0.05, 0.03, 0.014, 0.007 };
	strike(b, r, 0.315, 0.5, 0.3, sf, sa, sd, 0.03);
	finish(b, -23.0, -8.0);
}

static const struct sfx_def SFX[DESK_SND_COUNT] = {
	[DESK_SND_PAPER_SWING] = { "paper-swing", 0.42, 3, s_paper_swing },
	[DESK_SND_PAPER_PRESS] = { "paper-press", 0.16, 4, s_paper_press },
	[DESK_SND_PAPER_UNPRESS] = { "paper-unpress", 0.26, 3, s_paper_unpress },
	[DESK_SND_PAPER_SETTLE] = { "paper-settle", 0.16, 3, s_paper_settle },
	[DESK_SND_PAPER_UNFOLD] = { "paper-unfold", 0.62, 2, s_paper_unfold },
	[DESK_SND_PAPER_CRUMPLE] = { "paper-crumple", 0.75, 3, s_paper_crumple },
	[DESK_SND_PAPER_TOSS] = { "paper-toss", 0.44, 2, s_paper_toss },
	[DESK_SND_PAPER_LAND] = { "paper-land", 0.3, 2, s_paper_land },
	[DESK_SND_CORNER_LIFT] = { "corner-lift", 0.12, 3, s_corner_lift },
	[DESK_SND_CORNER_TAP] = { "corner-tap", 0.08, 3, s_corner_tap },
	[DESK_SND_DRAWER_OUT] = { "drawer-out", 1.02, 3, s_drawer_out },
	[DESK_SND_DRAWER_IN] = { "drawer-in", 1.02, 3, s_drawer_in },
	[DESK_SND_DRAWER_STOP_OUT] = { "drawer-stop-out", 0.32, 3, s_drawer_stop_out },
	[DESK_SND_DRAWER_STOP_IN] = { "drawer-stop-in", 0.4, 3, s_drawer_stop_in },
	[DESK_SND_FELT_PICK] = { "felt-pick", 0.14, 3, s_felt_pick },
	[DESK_SND_FELT_DROP] = { "felt-drop", 0.16, 3, s_felt_drop },
	[DESK_SND_BINDER_LIFT] = { "binder-lift", 0.24, 2, s_binder_lift },
	[DESK_SND_BINDER_OPEN] = { "binder-open", 0.45, 2, s_binder_open },
	[DESK_SND_BINDER_CLOSE] = { "binder-close", 0.45, 2, s_binder_close },
	[DESK_SND_PAGE_TURN] = { "page-turn", 0.24, 4, s_page_turn },
	[DESK_SND_SHEET_TAKE] = { "sheet-take", 0.28, 2, s_sheet_take },
	[DESK_SND_SHEET_DOWN] = { "sheet-down", 0.2, 3, s_sheet_down },
	/* (baked in art/sound, nothing made here: how many takes there are) */
	[DESK_SND_SHEET_DOWN_SOFT] = { "sheet-down-soft", 0.0, 3, NULL },
	[DESK_SND_SLIDE_WOOD_SLOW] = { "slide-wood-slow", 0.0, 1, NULL },
	[DESK_SND_SLIDE_WOOD_QUICK] = { "slide-wood-quick", 0.0, 1, NULL },
	[DESK_SND_SLIDE_SOFT_SLOW] = { "slide-soft-slow", 0.0, 1, NULL },
	[DESK_SND_SLIDE_SOFT_QUICK] = { "slide-soft-quick", 0.0, 1, NULL },
	[DESK_SND_SLIDE_CLEAR_SLOW] = { "slide-clear-slow", 0.0, 1, NULL },
	[DESK_SND_SLIDE_CLEAR_QUICK] = { "slide-clear-quick", 0.0, 1, NULL },
	[DESK_SND_POST_OPEN] = { "post-open", 0.9, 2, s_post_open },
	[DESK_SND_POST_CLOSE] = { "post-close", 0.8, 2, s_post_close },
	[DESK_SND_LETTER_ARRIVE] = { "letter-arrive", 0.62, 2, s_letter_arrive },
	[DESK_SND_LETTER_TAKE] = { "letter-take", 0.32, 3, s_letter_take },
	[DESK_SND_LETTER_FLICK] = { "letter-flick", 0.15, 4, s_letter_flick },
	[DESK_SND_KEY_PRESS] = { "key-press", 0.07, 3, s_key_press },
	[DESK_SND_KEY_RELEASE] = { "key-release", 0.05, 3, s_key_release },
	[DESK_SND_TOGGLE] = { "toggle", 0.16, 3, s_toggle },
	[DESK_SND_KNOB_TICK] = { "knob-tick", 0.03, 3, s_knob_tick },
	[DESK_SND_KEY_IN] = { "key-in", 0.3, 2, s_key_in },
	[DESK_SND_LOCK_CLUNK] = { "lock-clunk", 0.2, 2, s_lock_clunk },
	[DESK_SND_GUARD_LIFT] = { "guard-lift", 0.22, 2, s_guard_lift },
	[DESK_SND_GUARD_STOP] = { "guard-stop", 0.12, 2, s_guard_stop },
	[DESK_SND_LID_LIFT] = { "lid-lift", 0.24, 2, s_lid_lift },
	[DESK_SND_LID_STOP] = { "lid-stop", 0.16, 2, s_lid_stop },
	[DESK_SND_POWER_PRESS] = { "power-press", 0.25, 1, s_power_press },
	[DESK_SND_POWER_DOWN] = { "power-down", 2.9, 1, s_power_down },
	[DESK_SND_CARD_SLIDE] = { "card-slide", 0.32, 2, s_card_slide },
	[DESK_SND_SET_WARM] = { "set-warm", 2.6, 1, s_set_warm },
	[DESK_SND_SET_OFF] = { "set-off", 0.5, 2, s_set_off },
	[DESK_SND_PENCIL_TICK] = { "pencil-tick", 0.22, 3, s_pencil_tick },
	[DESK_SND_PENCIL_RING] = { "pencil-ring", 0.4, 3, s_pencil_ring },
	[DESK_SND_AERIAL_UP] = { "aerial-up", 1.75, 1, s_aerial_up },
	[DESK_SND_AERIAL_DOWN] = { "aerial-down", 1.75, 1, s_aerial_down },
	[DESK_SND_CC_CAP_ON] = { "cc-cap-on", 0.0, 3, NULL },
	[DESK_SND_CC_CAP_OFF] = { "cc-cap-off", 0.0, 3, NULL },
	[DESK_SND_CC_CAP] = { "cc-cap", 0.0, 3, NULL },
	[DESK_SND_CC_CAP_SLEEP] = { "cc-cap-sleep", 0.0, 3, NULL },
	[DESK_SND_CC_AIR_ON] = { "cc-air-on", 0.0, 2, NULL },
	[DESK_SND_CC_AIR_OFF] = { "cc-air-off", 0.0, 2, NULL },
	[DESK_SND_CC_LEVER_UP] = { "cc-lever-up", 0.0, 3, NULL },
	[DESK_SND_CC_LEVER_DOWN] = { "cc-lever-down", 0.0, 3, NULL },
	[DESK_SND_CC_KNOB] = { "cc-knob", 0.0, 3, NULL },
	[DESK_SND_CC_KEY_IN] = { "cc-key-in", 0.0, 2, NULL },
	[DESK_SND_CC_KEY_TURN] = { "cc-key-turn", 0.0, 2, NULL },
	[DESK_SND_CC_KEY_BACK] = { "cc-key-back", 0.0, 2, NULL },
	[DESK_SND_CC_KEY_OUT] = { "cc-key-out", 0.0, 2, NULL },
	[DESK_SND_CC_LID_UP] = { "cc-lid-up", 0.0, 2, NULL },
	[DESK_SND_CC_LID_UP_STOP] = { "cc-lid-up-stop", 0.0, 2, NULL },
	[DESK_SND_CC_LID_DOWN] = { "cc-lid-down", 0.0, 2, NULL },
	[DESK_SND_CC_LID_DOWN_STOP] = { "cc-lid-down-stop", 0.0, 2, NULL },
	[DESK_SND_CC_POWER] = { "cc-power", 0.0, 1, NULL },
	[DESK_SND_BEEP] = { "beep", 1.2, 1, s_beep },
	[DESK_SND_TICK] = { "tick", 0.10, 2, s_tick },
	[DESK_SND_TOCK] = { "tock", 0.12, 2, s_tock },
	[DESK_SND_BELL] = { "bell", 2.4, 1, s_bell },
	[DESK_SND_JINGLE] = { "jingle", 0.45, 3, s_jingle },
	[DESK_SND_FW_WHISTLE] = { "firework-whistle", 1.3, 2, s_fw_whistle },
	[DESK_SND_FW_BOOM] = { "firework-boom", 3.0, 3, s_fw_boom },
	[DESK_SND_FW_CRACKLE] = { "firework-crackle", 1.8, 3, s_fw_crackle },
	[DESK_SND_POPPER] = { "popper", 1.4, 2, s_popper },
	[DESK_SND_SHUTTER] = { "shutter", 0.22, 3, s_shutter },
	[DESK_SND_TAPE_IN] = { "tape-in", 0.75, 2, s_tape_in },
	[DESK_SND_TAPE_OUT] = { "tape-out", 0.25, 2, s_tape_out },
	[DESK_SND_REC_START] = { "rec-start", 0.6, 1, s_rec_start },
	[DESK_SND_REC_STOP] = { "rec-stop", 0.45, 1, s_rec_stop },
	[DESK_SND_FLASH] = { "flash", 1.25, 2, s_flash },
	[DESK_SND_TYPE_STRIKE] = { "type-strike", 0.16, 4, s_type_strike },
	[DESK_SND_TYPE_RELEASE] = { "type-release", 0.05, 3, s_type_release },
	[DESK_SND_TYPE_SPACE] = { "type-space", 0.09, 3, s_type_space },
	[DESK_SND_TYPE_SHIFT] = { "type-shift", 0.18, 2, s_type_shift },
	[DESK_SND_TYPE_BELL] = { "type-bell", 0.9, 1, s_type_bell },
	[DESK_SND_TYPE_RETURN] = { "type-return", 0.48, 2, s_type_return },
};

const char *desk_sound_name(enum desk_sound s)
{
	return s >= 0 && s < DESK_SND_COUNT ? SFX[s].name : "?";
}

int desk_sound_variants(enum desk_sound s)
{
	return s >= 0 && s < DESK_SND_COUNT ? SFX[s].variants : 0;
}

float *desk_sfx_make(enum desk_sound s, int variant, int *frames)
{
	*frames = 0;
	if (s < 0 || s >= DESK_SND_COUNT || !SFX[s].make) {
		return NULL;
	}
	struct buf b;
	b.n = at(SFX[s].secs) + 1;
	b.x = calloc((size_t)b.n, sizeof *b.x);
	if (!b.x) {
		return NULL;
	}
	/* the same dice for the same sound and variant, every time */
	struct rng r = { 0x9E3779B97F4A7C15ull ^ ((uint64_t)(s + 1) * 0xD1B54A32D192ED03ull)
		^ ((uint64_t)(variant + 1) * 0x94D049BB133111EBull) };
	for (int k = 0; k < 4; k++) {
		rng_next(&r);
	}
	SFX[s].make(&b, &r, variant);
	*frames = b.n;
	return b.x;
}
