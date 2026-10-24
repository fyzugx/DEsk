#include "compat.h"
#include "i18n.h"
#include "desk.h"
#include "lock.h"
#include "agenda.h"
#include "apps.h"
#include "panel.h"
#include "post.h"
#include "sound.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Every sound, every variant, as WAVs in dir; and all of them one after
 * another in all.wav, with what is when in all.txt -- to listen through. */
static int write_sounds(const char *dir)
{
	mkdir(dir, 0755);
	char path[1024];
	snprintf(path, sizeof path, "%s/all.wav", dir);
	FILE *all = fopen(path, "wbe");
	snprintf(path, sizeof path, "%s/all.txt", dir);
	FILE *idx = fopen(path, "we");
	if (!all || !idx) {
		fprintf(stderr, "cannot write into %s\n", dir);
		if (all) {
			fclose(all);
		}
		if (idx) {
			fclose(idx);
		}
		return 1;
	}
	const int rate = DESK_SFX_RATE;
	fseek(all, 44, SEEK_SET);
	long frames = 0;
	int files = 0;
	for (int s = 0; s < DESK_SND_COUNT; s++) {
		for (int v = 0; v < desk_sound_variants(s); v++) {
			snprintf(path, sizeof path, "%s/%s-%d.wav", dir, desk_sound_name(s), v + 1);
			if (desk_sound_write_wav(s, v, path)) {
				files++;
			}
			/* the first of each into the reel, with half a second after */
			if (v > 0) {
				continue;
			}
			int n = 0;
			float *x = desk_sfx_make(s, v, &n);
			fprintf(idx, "%7.2f s  %s\n", (double)frames / rate, desk_sound_name(s));
			for (int i = 0; i < n + rate / 2; i++) {
				double val = i < n ? x[i] * 32767.0 : 0.0;
				int16_t q = (int16_t)(val > 32767.0 ? 32767 : (val < -32768.0 ? -32768 : val));
				fwrite(&q, 2, 1, all);
			}
			frames += n + rate / 2;
			free(x);
		}
	}
	/* the reel's header, now its length is known */
	uint32_t data = (uint32_t)(frames * 2), riff = 36 + data, sixteen = 16, r = (uint32_t)rate;
	uint32_t bps = (uint32_t)rate * 2;
	uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
	fseek(all, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, all);
	fwrite(&riff, 4, 1, all);
	fwrite("WAVEfmt ", 1, 8, all);
	fwrite(&sixteen, 4, 1, all);
	fwrite(&fmt, 2, 1, all);
	fwrite(&ch, 2, 1, all);
	fwrite(&r, 4, 1, all);
	fwrite(&bps, 4, 1, all);
	fwrite(&align, 2, 1, all);
	fwrite(&bits, 2, 1, all);
	fwrite("data", 1, 4, all);
	fwrite(&data, 4, 1, all);
	fclose(all);
	fclose(idx);
	printf("wrote %d sounds into %s (and all.wav, %.1f s, with all.txt)\n", files, dir,
		(double)frames / rate);
	return 0;
}

static struct desk_cal_look cal_look(int view, int to, double st, int turn, double tt,
	int lift, double lt, int hover)
{
	struct desk_cal_look l = { 0 };
	l.view = view;
	l.to_view = to;
	l.switch_t = st;
	l.turn = turn;
	l.turn_t = tt;
	l.lift = lift;
	l.lift_t = lt;
	l.hover_tab = hover;
	return l;
}

/* The desk calendar close up, as a contact sheet: each section's page,
 * the pointer on a tab, bundles going over each way, a page turning, the
 * edges lifted -- without the room behind it, so nothing waits on the
 * tracer. */
static int write_calendar(const char *path, int w, int h)
{
	struct desk_cal_look looks[] = {
		cal_look(DESK_CAL_MONTH, DESK_CAL_MONTH, 0.0, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_YEAR, DESK_CAL_YEAR, 0.0, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_WEEK, DESK_CAL_WEEK, 0.0, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_DAY, DESK_CAL_DAY, 0.0, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_MONTH, DESK_CAL_MONTH, 0.0, 0, 0.0, 0, 0.0, DESK_CAL_WEEK),
		cal_look(DESK_CAL_MONTH, DESK_CAL_WEEK, 0.30, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_MONTH, DESK_CAL_WEEK, 0.55, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_DAY, DESK_CAL_YEAR, 0.50, 0, 0.0, 0, 0.0, -1),
		cal_look(DESK_CAL_WEEK, DESK_CAL_WEEK, 0.0, 1, 0.45, 0, 0.0, -1),
		cal_look(DESK_CAL_DAY, DESK_CAL_DAY, 0.0, 0, 0.0, 1, 1.0, -1),
		cal_look(DESK_CAL_YEAR, DESK_CAL_YEAR, 0.0, 0, 0.0, -1, 1.0, -1),
		cal_look(DESK_CAL_MONTH, DESK_CAL_DAY, 0.22, 0, 0.0, 0, 0.0, -1),
	};
	const char *what[] = { "month", "year", "week", "day", "pointer on WEEK",
		"month to week 30%", "month to week 55%", "day back to year 50%", "week on 45%",
		"day, foot lifted", "year, last drawn up", "month to day 22%" };
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	int cols = 4, n = (int)(sizeof looks / sizeof looks[0]), rows = (n + cols - 1) / cols;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * cols, h * rows);
	cairo_t *cr = cairo_create(surf);
	for (int i = 0; i < n; i++) {
		looks[i].year = tmv.tm_year + 1900;
		looks[i].month = tmv.tm_mon;
		looks[i].day = tmv.tm_mday;
		/* held up, as the compositor holds it on its own */
		looks[i].yaw = 0.30;
		looks[i].pitch = 0.12;
		cairo_save(cr);
		cairo_translate(cr, (i % cols) * w, (i / cols) * h);
		cairo_rectangle(cr, 0, 0, w, h);
		cairo_clip(cr);
		cairo_set_source_rgb(cr, 0.16, 0.11, 0.08);
		cairo_paint(cr);
		desk_draw_calendar(cr, w, h, &looks[i], now);
		cairo_set_source_rgb(cr, 0.9, 0.85, 0.7);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 18);
		cairo_move_to(cr, 12, h - 14);
		cairo_show_text(cr, what[i]);
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);
	printf("wrote %s\n", path);
	return 0;
}

/* The calendar with its binder of notes beside it, as a whole screen, and
 * the binder alone being written in -- a note on its way from the pad, and
 * one with the input method's candidates up. Notes for the pictures are
 * stuck on this month first, in whatever $XDG_DATA_HOME says: point it
 * somewhere to throw away. */
static int write_agenda(const char *path, int w, int h)
{
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	int y = tmv.tm_year + 1900, m = tmv.tm_mon, d = tmv.tm_mday;
	static const struct { int dd, kind, colour, minute, span; const char *text; } demo[] = {
		{ 0, DESK_STICK_PLAN, 0, 600, 60, "Dentist" },
		{ 0, DESK_STICK_PLAN, 2, 870, 90, "\xe6\x89\x93\xe3\x81\xa1\xe5\x90\x88\xe3\x82\x8f\xe3\x81\x9b (Room 2)" },
		{ 0, DESK_STICK_PLAN, 3, -1, 0, "Mum's birthday \xe2\x80\x94 call her in the evening" },
		{ 0, DESK_STICK_TODO, 1, -1, 0, "Buy milk" },
		{ 0, DESK_STICK_TODO, 4, -1, 0, "\xe8\xab\x8b\xe6\xb1\x82\xe6\x9b\xb8\xe3\x82\x92\xe9\x80\x81\xe3\x82\x8b" },
		{ 0, DESK_STICK_PLAN, 5, 900, 60, "Call with the bank" },
		{ 2, DESK_STICK_PLAN, 5, 1140, 120, "Concert" },
		{ 5, DESK_STICK_PLAN, 2, -1, 0, "Holiday" },
		{ 5, DESK_STICK_PLAN, 0, 540, 0, "Train 9:12" },
		{ 5, DESK_STICK_PLAN, 1, 720, 0, "Lunch with Ken" },
		{ -3, DESK_STICK_PLAN, 3, 990, 0, "Haircut" },
	};
	struct desk_stick probe[1];
	if (desk_agenda_day(y, m, d, DESK_STICK_PLAN, probe, 1) == 0) {
		for (unsigned i = 0; i < sizeof demo / sizeof demo[0]; i++) {
			int dd = d + demo[i].dd;
			if (dd < 1 || dd > 28) {
				continue;
			}
			struct desk_stick s = { 0 };
			s.y = y;
			s.m = m;
			s.d = dd;
			s.kind = demo[i].kind;
			s.colour = demo[i].colour;
			s.minute = demo[i].minute;
			s.span = demo[i].span;
			s.done = i == 3;
			snprintf(s.text, sizeof s.text, "%s", demo[i].text);
			desk_agenda_add(&s);
		}
	}
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * 2, h * 2);
	cairo_t *cr = cairo_create(surf);
	cairo_set_source_rgb(cr, 0.16, 0.11, 0.08);
	cairo_paint(cr);
	/* the whole screen: the calendar gone aside, the binder in */
	double bd[4], cal[3];
	desk_agenda_layout(w, h, bd, cal);
	struct desk_cal_look look = { 0 };
	look.view = look.to_view = DESK_CAL_MONTH;
	look.year = y;
	look.month = m;
	look.day = d;
	look.hover_tab = -1;
	/* held as the compositor holds them: the calendar turned a little
	 * towards the binder, the binder towards it, seen from over the middle
	 * of the screen */
	look.yaw = 0.10;
	look.pitch = 0.12;
	look.eye_x = (w * 0.5 - cal[1]) / cal[0];
	look.eye_y = (h * 0.5 - cal[2]) / cal[0];
	look.eye_f = h * 1.6 / cal[0];
	cairo_save(cr);
	cairo_translate(cr, cal[1], cal[2]);
	cairo_scale(cr, cal[0], cal[0]);
	desk_draw_calendar(cr, w, h, &look, now);
	cairo_restore(cr);
	struct desk_agenda_look al = { 0 };
	al.year = y;
	al.month = m;
	al.day = d;
	al.colour = 2;
	al.text = "";
	al.preedit = "";
	al.hover = DESK_AGENDA_HIT_SLOT_PLAN;
	al.peel_t = 1.0;
	al.when = -1;
	al.ruler_mark = -1;
	al.yaw = -0.22;
	al.pitch = 0.08;
	al.eye_x = w * 0.5 - bd[0];
	al.eye_y = h * 0.5 - bd[1];
	al.eye_f = h * 1.6;
	cairo_save(cr);
	cairo_translate(cr, bd[0], bd[1]);
	desk_draw_agenda(cr, bd[2], bd[3], &al);
	cairo_restore(cr);
	/* and a note half way over from the binder to its day */
	struct desk_stick fl[1];
	if (desk_agenda_day(y, m, d, DESK_STICK_PLAN, fl, 1) > 0) {
		struct desk_note_pose to, from = { bd[0] + bd[2] * 0.46, bd[1] + bd[3] * 0.30,
			bd[2] * 0.47, bd[3] * 0.085, 0.0, bd[3] * 0.027, DESK_NOTE_BINDER };
		if (desk_calendar_note_pose(w, h, &look, &fl[0], &to)) {
			double tx = to.x + to.w * 0.5, ty = to.y + to.h * 0.5;
			desk_calendar_turn_point(w, h, &look, &tx, &ty);
			to.x = tx - to.w * 0.5;
			to.y = ty - to.h * 0.5;
			to.x = to.x * cal[0] + cal[1];
			to.y = to.y * cal[0] + cal[2];
			to.w *= cal[0];
			to.h *= cal[0];
			to.font *= cal[0];
			double q = 0.5;
			struct desk_note_pose at = to;
			at.w = (from.w + (to.w - from.w) * q) * 1.10;
			at.h = (from.h + (to.h - from.h) * q) * 1.10;
			at.x = (from.x + from.w * 0.5 + to.x + to.w * 0.5) * 0.5 - at.w * 0.5;
			at.y = fmin(from.y, to.y) - at.h * 1.5;
			at.rot = 0.10;
			desk_draw_note_between(cr, &fl[0], &at, &from, &to, 0.5, 1.0, 1.0);
		}
	}
	/* the week and the day, with what is stuck on them, held on their own */
	look.yaw = 0.30;
	look.eye_f = 0.0;
	look.view = look.to_view = DESK_CAL_WEEK;
	cairo_save(cr);
	cairo_translate(cr, w, 0);
	desk_draw_calendar(cr, w, h, &look, now);
	cairo_restore(cr);
	look.view = look.to_view = DESK_CAL_DAY;
	cairo_save(cr);
	cairo_translate(cr, w, h);
	desk_draw_calendar(cr, w, h, &look, now);
	cairo_restore(cr);
	/* the binder being written in: a new plan with the input method's
	 * part and its candidates */
	static const char *const cands[] = { "\xe4\xbc\x9a\xe8\xad\xb0", "\xe9\x96\x8b\xe8\xad\xb0", "\xe5\xbf\xab\xe7\x96\x91", "\xe6\x80\xaa\xe7\x96\x91", "\xe3\x81\x8b\xe3\x81\x84\xe3\x81\x8e" };
	al.edit = DESK_AGENDA_NEW_PLAN;
	al.edit_colour = 4;
	al.colour = 4;
	al.text = "";
	al.caret = 0;
	al.preedit = "\xe4\xbc\x9a\xe8\xad\xb0";
	al.preedit_caret = 6;
	/* at a time set on the ruler, the pointer on it */
	al.when = 660;
	al.span = 90;
	al.ruler_t = 1.0;
	al.ruler_mark = 780;
	al.cands = cands;
	al.ncand = 5;
	al.cand_cursor = 0;
	al.cand_page = 9;
	al.hover = 0;
	al.eye_x = w * 0.5 - (w - bd[2]) * 0.5;
	al.eye_y = h * 0.5 - (h - bd[3]) * 0.5;
	al.yaw = 0.0;
	al.pitch = 0.10;
	cairo_save(cr);
	cairo_translate(cr, (w - bd[2]) * 0.5, h + (h - bd[3]) * 0.5);
	desk_draw_agenda(cr, bd[2], bd[3], &al);
	cairo_restore(cr);
	cairo_destroy(cr);
	cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);
	printf("wrote %s\n", path);
	return 0;
}

/* The book of every program, open: at its first spread with a line under
 * the hand, at a leaf going over, with something written on its line and
 * the first of what answers chosen, and at a thumb tab under the hand. */
static int write_book(const char *path, int w, int h)
{
	desk_apps_init(false);
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * 2, h * 2);
	cairo_t *cr = cairo_create(surf);
	cairo_set_source_rgb(cr, 0.16, 0.11, 0.08);
	cairo_paint(cr);
	const char *q[4] = { "", "", "term", "" };
	double page[4] = { 0.0, 0.45, 0.0, 1.0 };
	int hover[4] = { 3, -1, -1, -1 };
	for (int i = 0; i < 4; i++) {
		struct desk_book_look lk = { 0 };
		lk.query = q[i];
		lk.caret = (int)strlen(q[i]);
		lk.preedit = "";
		lk.page = page[i];
		lk.hover = hover[i];
		lk.sel = i == 2 ? 0 : -1;
		lk.hover_tab = i == 3 ? 6 : -1;
		lk.open_t = 1.0;
		cairo_save(cr);
		cairo_translate(cr, (i % 2) * w, (i / 2) * h);
		cairo_rectangle(cr, 0, 0, w, h);
		cairo_clip(cr);
		desk_draw_book(cr, w, h, &lk);
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);
	desk_apps_shutdown();
	printf("wrote %s\n", path);
	return 0;
}

/* The book (shelf -1) or binder shelf taken off the shelf and opened:
 * frames of it, three by three, at the times given (0 on the shelf, 1
 * open) or nine along the way. */
static int write_book_anim(const char *path, int w, int h, int shelf, const char *times)
{
	desk_apps_init(false);
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * 3, h * 3);
	cairo_t *cr = cairo_create(surf);
	cairo_set_source_rgb(cr, 0.16, 0.11, 0.08);
	cairo_paint(cr);
	double u[9] = { 0.02, 0.15, 0.28, 0.40, 0.52, 0.64, 0.76, 0.88, 1.0 };
	int nu = 9;
	if (times && *times) {
		nu = 0;
		for (const char *p = times; *p && nu < 9;) {
			char *end;
			double v = strtod(p, &end);
			if (end == p) {
				break;
			}
			u[nu++] = v;
			p = *end == ',' ? end + 1 : end;
		}
	}
	for (int i = 0; i < nu; i++) {
		struct desk_book_look lk = { 0 };
		lk.query = "";
		lk.preedit = "";
		lk.hover = -1;
		lk.sel = -1;
		lk.hover_tab = -1;
		lk.open_t = u[i];
		cairo_save(cr);
		cairo_translate(cr, (i % 3) * w, (i / 3) * h);
		cairo_rectangle(cr, 0, 0, w, h);
		cairo_clip(cr);
		cairo_surface_t *one = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cairo_t *c1 = cairo_create(one);
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		if (shelf < 0) {
			desk_draw_book(c1, w, h, &lk);
		} else {
			desk_draw_binder(c1, w, h, shelf, 0.0, -1, u[i], NULL, -1);
		}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		printf("u %.3f: %.1f ms\n", u[i],
			(t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
		cairo_destroy(c1);
		cairo_set_source_surface(cr, one, 0, 0);
		cairo_paint(cr);
		cairo_surface_destroy(one);
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);
	desk_apps_shutdown();
	printf("wrote %s\n", path);
	return 0;
}
/* The memo pad close up, as a contact sheet: a blank sheet, one written
 * on (in English and in Japanese, a long line wrapped), the input method
 * part way through a word with its candidates, a sheet going over the
 * wire and one coming back -- held as the compositor holds it. */
static int write_memo(const char *path, int w, int h)
{
	static const char *const cands[] = { "\xe4\xbc\x9a\xe8\xad\xb0",
		"\xe4\xbc\x9a\xe7\xbe\xa9", "\xe5\xbf\xab\xe6\x93\x8d",
		"\xe6\x87\x90\xe7\x96\x91" };
	const char *en = "Call the printer about the proofs\nBuy stamps (10)\n\n"
		"Ask Sam whether the meeting on Thursday can move to the afternoon, "
		"after the post has gone";
	const char *ja = "\xe7\x89\x9b\xe4\xb9\xb3\xe3\x80\x81\xe3\x83\x91\xe3\x83\xb3\n"
		"\xe9\x83\xb5\xe4\xbe\xbf\xe5\xb1\x80\xe3\x81\xab\xe5\xaf\x84\xe3\x82\x8b\n"
		"\xe6\x9c\xa8\xe6\x9b\x9c\xe3\x81\xae";
	struct desk_memo_look looks[6];
	memset(looks, 0, sizeof looks);
	const char *what[] = { "blank", "written on", "Japanese, IME", "going over 40%",
		"coming back 65%", "flat (yaw 0)" };
	for (int i = 0; i < 6; i++) {
		looks[i].sheets = 3;
		looks[i].sheet = 1;
		looks[i].yaw = 0.18;
		looks[i].pitch = 0.10;
		looks[i].caret = 0;
		looks[i].text = "";
	}
	looks[1].text = en;
	looks[1].caret = (int)strlen(en);
	looks[2].text = ja;
	looks[2].caret = (int)strlen(ja);
	looks[2].preedit = "\xe3\x81\x8b\xe3\x81\x84\xe3\x81\x8e";
	looks[2].preedit_caret = 9;
	looks[2].cands = cands;
	looks[2].ncand = 4;
	looks[2].cand_cursor = 0;
	looks[3].text = en;
	looks[3].turn = 1;
	looks[3].turn_t = 0.40;
	looks[3].other = "";
	looks[3].other_sheet = 2;
	looks[4].text = ja;
	looks[4].turn = -1;
	looks[4].turn_t = 0.65;
	looks[4].other = en;
	looks[4].other_sheet = 0;
	looks[5].text = en;
	looks[5].caret = 12;
	looks[5].yaw = looks[5].pitch = 0.0;
	int cols = 3, n = 6, rows = 2;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * cols, h * rows);
	cairo_t *cr = cairo_create(surf);
	for (int i = 0; i < n; i++) {
		cairo_save(cr);
		cairo_translate(cr, (i % cols) * w, (i / cols) * h);
		cairo_rectangle(cr, 0, 0, w, h);
		cairo_clip(cr);
		cairo_set_source_rgb(cr, 0.16, 0.11, 0.08);
		cairo_paint(cr);
		desk_draw_memo(cr, w, h, &looks[i]);
		cairo_set_source_rgb(cr, 0.9, 0.85, 0.7);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 18);
		cairo_move_to(cr, 12, h - 14);
		cairo_show_text(cr, what[i]);
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);
	printf("wrote %s\n", path);
	return 0;
}

/* The lamp coming up at login, as a contact sheet: the desk at each of the
 * given points through the room being made (0..1), dark as the lamp is for
 * it, the clock counting in the dark. */
static int write_boot(const char *path, int w, int h, const char *list)
{
	double u[16];
	int n = 0;
	const char *p = list && *list ? list : "0,0.15,0.3,0.45,0.6,0.75,0.9,1";
	while (*p && n < 16) {
		char *e;
		u[n++] = strtod(p, &e);
		p = *e == ',' ? e + 1 : e;
		if (e == p && *p) {
			break;
		}
	}
	desk_scene_warm(w, h, false);
	int cols = n > 4 ? 4 : n, rows = (n + cols - 1) / cols;
	int tw = w / 2, th = h / 2;
	cairo_surface_t *sheet = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw * cols, th * rows);
	cairo_t *sc = cairo_create(sheet);
	for (int i = 0; i < n; i++) {
		struct desk_clock_state st;
		memset(&st, 0, sizeof st);
		st.boot = u[i];
		st.boot_stage = u[i] < 0.45 ? 0 : u[i] < 0.583 ? 1 : u[i] < 0.717 ? 2 : u[i] < 0.85
			? 3 : u[i] < 0.999 ? 4 : 5;
		desk_clock_set(&st);
		cairo_surface_t *f = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cairo_t *cr = cairo_create(f);
		desk_draw_desktop(cr, w, h, time(NULL), false, 0.0);
		desk_draw_overlay(cr, DESK_OVL_CLOCK, w, h, time(NULL), 0);
		double light = desk_boot_light(u[i]);
		desk_draw_night(cr, w, h, light);
		double box[4];
		if (desk_clock_glow_box(w, h, box)) {
			double a = desk_night_alpha_at(w, h, light, box[0] + box[2] * 0.5,
				box[1] + box[3] * 0.5);
			cairo_push_group(cr);
			desk_draw_clock_glow(cr, w, h);
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, a);
		}
		cairo_destroy(cr);
		cairo_save(sc);
		cairo_translate(sc, (i % cols) * tw, (i / cols) * th);
		cairo_scale(sc, 0.5, 0.5);
		cairo_set_source_surface(sc, f, 0, 0);
		cairo_paint(sc);
		cairo_restore(sc);
		cairo_set_source_rgb(sc, 0.9, 0.85, 0.7);
		cairo_select_font_face(sc, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(sc, 16);
		char lab[64];
		snprintf(lab, sizeof lab, "%.0f%%  light %.2f", u[i] * 100.0, light);
		cairo_move_to(sc, (i % cols) * tw + 10, (i / cols) * th + th - 12);
		cairo_show_text(sc, lab);
		cairo_surface_destroy(f);
	}
	cairo_destroy(sc);
	cairo_surface_write_to_png(sheet, path);
	cairo_surface_destroy(sheet);
	printf("wrote %s\n", path);
	return 0;
}

/* The screen lock, as a contact sheet: walking up to it, waiting, letters
 * in, leaning in to the key while it is checked, told no, the key turned,
 * the door going in on the room, and through it -- the last frame the view
 * of the bookcase the compositor sits you down from. */
static int write_lock(const char *path, int w, int h)
{
	desk_scene_warm(w, h, false);
	desk_apps_init(false);
	cairo_surface_t *inside = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *ic = cairo_create(inside);
	desk_draw_shelf(ic, w, h, -1, 0.0, -1);
	desk_draw_shelf_gadgets(ic, w, h, time(NULL));
	cairo_destroy(ic);
	char login[64], name[128];
	desk_lock_user(login, sizeof login, name, sizeof name);
	cairo_surface_t *pic = desk_lock_picture();
	enum { N = 12 };
	struct desk_lock_look looks[N];
	memset(looks, 0, sizeof looks);
	const char *what[N];
	const char *what0[N] = { "walking up (back 0.6)", "waiting", "7 letters in", "leaning in 0.5",
		"checking, key tried", "told no", "caps lock", "key turned", "door half open",
		"door open", "through it 50%", "through it 85%" };
	memcpy(what, what0, sizeof what);
	for (int i = 0; i < N; i++) {
		looks[i].name = name;
		looks[i].picture = pic;
		looks[i].inside = inside;
		looks[i].now_s = 3.3;
	}
	looks[0].back = 0.6;
	looks[1].caret = true;
	looks[2].typed = 7;
	looks[2].caret = true;
	looks[3].typed = 7;
	looks[3].checking = true;
	looks[3].lean = 0.5;
	looks[4].typed = 7;
	looks[4].checking = true;
	looks[4].lean = 1.0;
	looks[4].turn = 0.12;
	looks[5].fail = 0.25;
	looks[5].lean = 0.6;
	looks[6].caps = true;
	looks[6].typed = 3;
	looks[7].lean = 1.0;
	looks[7].turn = 1.0;
	looks[8].lean = 0.3;
	looks[8].turn = 1.0;
	looks[8].open = 0.5;
	looks[9].turn = 1.0;
	looks[9].open = 1.0;
	looks[10].turn = 1.0;
	looks[10].open = 1.0;
	looks[10].push = 0.5;
	looks[11].turn = 1.0;
	looks[11].open = 1.0;
	looks[11].push = 0.85;
	if (getenv("DESK_LOCK_WALK")) {
		/* the walk in alone, push 0.15 to 1 (DESK_LOCK_WALK=console: into
		 * the Control Panel instead -- with the shelf's picture, for the room) */
		bool cc = !strcmp(getenv("DESK_LOCK_WALK"), "console");
		for (int i = 0; i < N; i++) {
			memset(&looks[i], 0, sizeof looks[i]);
			looks[i].name = name;
			looks[i].picture = pic;
			looks[i].inside = inside;
			looks[i].inside_view = cc ? DESK_VIEW_CONSOLE : DESK_VIEW_SHELF;
			looks[i].now_s = 3.3;
			looks[i].turn = 1.0;
			looks[i].open = 1.0;
			looks[i].push = 0.15 + 0.85 * i / (N - 1);
			static char lbl[N][32];
			snprintf(lbl[i], sizeof lbl[i], "push %.2f", looks[i].push);
			what[i] = lbl[i];
		}
		if (cc) {
			cairo_t *c2 = cairo_create(inside);
			struct desk_console_state cs = { .wifi_on = true, .volume = 0.5,
				.brightness = 0.80, .screen = 1.0, .warm = 1.0, .clock = 12.0, .pressed = -1,
				.antenna = 1.0, .choose_down = -1, .choose_open = -1 };
			desk_draw_console(c2, w, h, &cs, -1);
			cairo_destroy(c2);
		}
	}
	if (getenv("DESK_LOCK_DECOR") && !getenv("DESK_LOCK_MOODS")) {
		/* the usual sheet, at night, dressed for a day */
		const char *dc = getenv("DESK_LOCK_DECOR");
		for (int i = 0; i < N; i++) {
			looks[i].mood.set = true;
			looks[i].mood.sun_el = -30.0;
			looks[i].mood.moon = 0.8;
			looks[i].mood.decor = !strcmp(dc, "xmas") ? DESK_DECOR_XMAS
				: !strncmp(dc, "newyear", 7) ? DESK_DECOR_NEWYEAR
				: !strcmp(dc, "birthday") ? DESK_DECOR_BIRTHDAY : 0;
			looks[i].mood.year = 2027;
		}
	}
	if (getenv("DESK_LOCK_MOODS")) {
		/* the door waiting, at twelve hours and weathers (DESK_LOCK_DECOR=
		 * xmas, newyear or birthday dresses it for all of them) */
		static const struct { const char *what; double el, az; int wx; double heavy; } M[N] = {
			{ "night, clear", -30, 0, DESK_WX_CLEAR, 0 },
			{ "dawn", 1.5, 95, DESK_WX_CLEAR, 0 },
			{ "morning", 22, 115, DESK_WX_CLEAR, 0 },
			{ "summer noon", 72, 180, DESK_WX_CLEAR, 0 },
			{ "winter noon", 31, 180, DESK_WX_CLEAR, 0 },
			{ "evening, low sun", 6, 245, DESK_WX_CLEAR, 0 },
			{ "dusk", -5, 275, DESK_WX_CLEAR, 0 },
			{ "cloudy afternoon", 35, 215, DESK_WX_CLOUDS, 0 },
			{ "rain, day", 30, 160, DESK_WX_RAIN, 0.6 },
			{ "rain, night", -30, 0, DESK_WX_RAIN, 0.7 },
			{ "snow, day", 20, 170, DESK_WX_SNOW, 0.7 },
			{ "fog, morning", 9, 120, DESK_WX_FOG, 0 },
		};
		const char *dc = getenv("DESK_LOCK_DECOR");
		unsigned decor = !dc ? 0 : !strcmp(dc, "xmas") ? DESK_DECOR_XMAS
			: !strncmp(dc, "newyear", 7) ? DESK_DECOR_NEWYEAR
			: !strcmp(dc, "birthday") ? DESK_DECOR_BIRTHDAY : 0;
		for (int i = 0; i < N; i++) {
			memset(&looks[i], 0, sizeof looks[i]);
			looks[i].name = name;
			looks[i].picture = pic;
			looks[i].inside = inside;
			looks[i].now_s = 3.3 + i * 0.37;
			looks[i].caret = true;
			looks[i].mood.set = true;
			looks[i].mood.sun_el = M[i].el;
			looks[i].mood.sun_az = M[i].az;
			looks[i].mood.weather = M[i].wx;
			looks[i].mood.heavy = M[i].heavy;
			looks[i].mood.moon = 0.8;
			looks[i].mood.decor = decor;
			looks[i].mood.year = 2027;
			what[i] = M[i].what;
		}
	}
	const char *only = getenv("DESK_LOCK_FRAME");
	int cols = 4, rows = 3, tw = w / 2, th = h / 2;
	cairo_surface_t *sheet = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw * cols, th * rows);
	cairo_t *sc = cairo_create(sheet);
	for (int i = 0; i < N; i++) {
		cairo_surface_t *f = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cairo_t *cr = cairo_create(f);
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		desk_draw_lock(cr, w, h, &looks[i]);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double fine = 0.0;
		if (desk_lock_settling()) {
			/* as it will be once it has stood still a moment */
			struct timespec f0, f1;
			clock_gettime(CLOCK_MONOTONIC, &f0);
			desk_lock_settle();
			clock_gettime(CLOCK_MONOTONIC, &f1);
			fine = (f1.tv_sec - f0.tv_sec) * 1e3 + (f1.tv_nsec - f0.tv_nsec) * 1e-6;
			desk_draw_lock(cr, w, h, &looks[i]);
		}
		desk_draw_lock_fx(cr, w, h, &looks[i]);
		printf("%-24s %.1f ms (fine %.0f ms)\n", what[i],
			(t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6, fine);
		cairo_destroy(cr);
		if (only && atoi(only) == i) {
			cairo_surface_write_to_png(f, path);
			cairo_surface_destroy(f);
			printf("wrote %s (frame %d)\n", path, i);
			cairo_destroy(sc);
			cairo_surface_destroy(sheet);
			return 0;
		}
		cairo_save(sc);
		cairo_translate(sc, (i % cols) * tw, (i / cols) * th);
		cairo_scale(sc, 0.5, 0.5);
		cairo_set_source_surface(sc, f, 0, 0);
		cairo_paint(sc);
		cairo_restore(sc);
		cairo_set_source_rgb(sc, 0.95, 0.9, 0.75);
		cairo_select_font_face(sc, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(sc, 15);
		cairo_move_to(sc, (i % cols) * tw + 10, (i / cols) * th + th - 12);
		cairo_show_text(sc, what[i]);
		cairo_surface_destroy(f);
	}
	cairo_destroy(sc);
	cairo_surface_write_to_png(sheet, path);
	cairo_surface_destroy(sheet);
	cairo_surface_destroy(inside);
	if (pic) {
		cairo_surface_destroy(pic);
	}
	printf("wrote %s\n", path);
	return 0;
}

#define PREVIEW_USAGE "usage: desk-preview [-o file.png] [-s 1600x1000] [--no-window] " \
	"[--closed] [--apps] [--shelf] [--console] [--post] [--sounds DIR] [--calendar] " \
	"[--memo] [--lock] [--boot[=u,u,...]] [--agenda] [--book] [--book-anim[=u,u,...]] " \
	"[--lang en|ja|zh|ko|es|fr|de] [--binder-anim=N[:u,u,...]]\n"

int main(int argc, char **argv)
{
	desk_compat();
	const char *path = "desk-preview.png";
	int w = 1600, h = 1000;
	bool demo = true;
	bool shelf = false;
	bool console = false;
	bool post = false;
	bool calendar = false, memo = false;
	const char *boot = NULL;
	bool lock = false;
	bool agenda = false;
	bool book = false, book_anim = false;
	const char *anim_times = NULL;
	int anim_shelf = -1;
	double drawer = 1.0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			path = argv[++i];
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			if (sscanf(argv[++i], "%dx%d", &w, &h) != 2) {
				fprintf(stderr, "size must be WIDTHxHEIGHT\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--no-window") == 0) {
			demo = false;
		} else if (strcmp(argv[i], "--closed") == 0) {
			drawer = 0.0;
		} else if (strcmp(argv[i], "--shelf") == 0) {
			shelf = true;
		} else if (strcmp(argv[i], "--console") == 0) {
			console = true;
		} else if (strcmp(argv[i], "--post") == 0) {
			post = true;
		} else if (strcmp(argv[i], "--sounds") == 0 && i + 1 < argc) {
			return write_sounds(argv[i + 1]);
		} else if (strcmp(argv[i], "--calendar") == 0) {
			calendar = true;
		} else if (strcmp(argv[i], "--memo") == 0) {
			memo = true;
		} else if (strcmp(argv[i], "--lock") == 0) {
			lock = true;
		} else if (strncmp(argv[i], "--boot", 6) == 0
			&& (argv[i][6] == '\0' || argv[i][6] == '=')) {
			boot = argv[i][6] == '=' ? argv[i] + 7 : "";
		} else if (strcmp(argv[i], "--agenda") == 0) {
			agenda = true;
		} else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
			/* the desk's words in en, ja, zh, ko, es, fr or de (as shell.conf's language) */
			enum desk_lang lang = desk_lang_parse(argv[++i]);
			desk_lang_set(lang);
			desk_apps_set_language(desk_lang_code(lang));
		} else if (strcmp(argv[i], "--book") == 0) {
			book = true;
		} else if (strncmp(argv[i], "--book-anim", 11) == 0
			&& (argv[i][11] == '\0' || argv[i][11] == '=')) {
			book_anim = true;
			anim_times = argv[i][11] == '=' ? argv[i] + 12 : NULL;
		} else if (strncmp(argv[i], "--binder-anim=", 14) == 0) {
			/* --binder-anim=N or N:u,u,... */
			book_anim = true;
			anim_shelf = atoi(argv[i] + 14);
			const char *c = strchr(argv[i] + 14, ':');
			anim_times = c ? c + 1 : NULL;
		} else if (strcmp(argv[i], "--apps") == 0) {
			/* What went on which binder, and why. Look at this before
			 * believing any picture of a bookshelf. */
			desk_apps_init(false);
			const struct desk_app_list *l = desk_apps_acquire();
			printf("%d applications on the shelf, from %d files (%d not for a menu)\n\n",
				l->n, l->n_files, l->n_hidden);
			for (int sh = 0; sh < DESK_SHELF_COUNT; sh++) {
				const struct desk_shelf_info *si = &l->shelf[sh];
				printf("== %-12s %-16s %3d\n", si->label_en, si->label_ja, si->count);
				for (int k = 0; k < si->count; k++) {
					const struct desk_app *a = &l->apps[si->first + k];
					printf("   %-34s %-28s %s%s\n", a->name, a->id,
						a->icon_file[0] ? "icon" : "initial ",
						a->icon_file[0] ? "" : a->initial);
				}
			}
			desk_apps_release(l);
			desk_apps_shutdown();
			return 0;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf(PREVIEW_USAGE);
			return 0;
		} else {
			fprintf(stderr, PREVIEW_USAGE);
			return 1;
		}
	}
	if (calendar) {
		return write_calendar(path, w, h);
	}
	if (memo) {
		return write_memo(path, w, h);
	}
	if (boot) {
		return write_boot(path, w, h, boot);
	}
	if (lock) {
		return write_lock(path, w, h);
	}
	if (agenda) {
		return write_agenda(path, w, h);
	}
	if (book) {
		return write_book(path, w, h);
	}
	if (book_anim) {
		return write_book_anim(path, w, h, anim_shelf, anim_times);
	}
	/* a file has to be complete, so wait for the ray tracer */
	desk_scene_warm(w, h, false);
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(surf);
	if (post) {
		/* A post box with nothing in it says nothing about what a letter
		 * looks like, so the still gets some. */
		desk_apps_init(false);
		static struct desk_note notes[5] = {
			{ 1, "Kate", "Unsaved changes",
			  "draw.c has been modified since you last saved it.",
			  "", "org.kde.kate", 0, 1, false, false, 0, { "" }, { "" }, false },
			{ 2, "Firefox", "Download finished", "wlroots-0.18.2.tar.gz",
			  "", "firefox", 0, 2, false, false, 0, { "" }, { "" }, false },
			{ 3, "Calendar", "Standup in 10 minutes", "Room 2, as always.",
			  "", "org.gnome.Calendar", 0, 1, false, false, 0, { "" }, { "" }, false },
			{ 4, "Files", "Copy complete", "1,204 items to /backup",
			  "", "org.gnome.Nautilus", 0, 1, true, false, 0, { "" }, { "" }, false },
			{ 5, "Power", "Battery low", "12% remaining",
			  "", "", 0, 2, true, false, 0, { "" }, { "" }, false },
		};
		desk_draw_post(cr, w, h, notes, 5, 1, 1.0, 0.0, NULL);
	} else if (console) {
		struct desk_console_state st = { .wifi_on = true, .volume = 0.62,
			.brightness = 0.80, .screen = 1.0, .warm = 1.0, .clock = 12.0, .pressed = -1,
			.antenna = getenv("DESK_AERIAL") ? atof(getenv("DESK_AERIAL")) : 1.0,
			.choose_down = -1, .choose_open = -1 };
		/* the guarded set has four states worth photographing, and no way
		 * to click one up from here */
		const char *e;
		if ((e = getenv("DESK_KEY")) != NULL) {
			st.key = atof(e);
		}
		if ((e = getenv("DESK_GUARD")) != NULL) {
			st.guard = atof(e);
		}
		if ((e = getenv("DESK_PRESSED")) != NULL) {
			st.pressed = atoi(e);
		}
		if ((e = getenv("DESK_DYING")) != NULL) {
			st.dying = atof(e);
		}
		/* a laptop's screen, its light turned to this */
		if ((e = getenv("DESK_BACKLIGHT")) != NULL) {
			st.screen = atof(e);
			st.screen_ok = true;
		}
		/* The tube draws a history, and a history nobody has watched yet is
		 * a flat line -- which is not what the instrument looks like. Run
		 * the beam across it a few times before taking the picture. */
		cairo_surface_t *warm = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cairo_t *wc = cairo_create(warm);
		for (int i = 0; i < 56; i++) {
			st.clock = 12.0 + i * 0.11;
			desk_draw_console(wc, w, h, &st, -1);
		}
		cairo_destroy(wc);
		cairo_surface_destroy(warm);
		st.clock = 12.0 + 56 * 0.11;
		desk_draw_console(cr, w, h, &st, -1);
	} else if (shelf) {
		desk_apps_init(false);
		desk_draw_shelf(cr, w, h, 3, 1.0, -1);
		desk_draw_shelf_gadgets(cr, w, h, time(NULL));
	} else {
		desk_draw_desktop(cr, w, h, time(NULL), demo, drawer);
	}
	cairo_status_t st = cairo_surface_write_to_png(surf, path);
	cairo_destroy(cr);
	cairo_surface_destroy(surf);
	if (st != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "failed to write %s\n", path);
		return 1;
	}
	printf("wrote %s (%dx%d)\n", path, w, h);
	return 0;
}
