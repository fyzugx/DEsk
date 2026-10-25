#pragma once

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>

#include "i18n.h"

/* What a piece of text is, as far as the face it is set in goes. Each has
 * a face for the Latin alphabet and one for Japanese, Chinese or Korean
 * (and anything else the Latin face has no letters for), chosen from what
 * is installed: the first of a list that is there, so that a machine
 * without the faces DEsk is drawn with still gets the nearest thing it
 * has. */
enum desk_font {
	DESK_FONT_UI,       /* small working text: menus, settings, captions */
	DESK_FONT_PRINT,    /* printed on paper: sheets, slips, labels, the index */
	DESK_FONT_DISPLAY,  /* titles and gilt: lettering on a binding, headings */
	DESK_FONT_ENGRAVE,  /* cut in brass and printed on machines: plates, keys,
	                       * dials, a desk calendar's figures */
	DESK_FONT_TYPE,     /* typed: a letter's body, a card left out */
	DESK_FONT_SCREEN,   /* lit on a tube */
	DESK_FONT_HAND,     /* written by hand: the memo */
	DESK_FONT_COUNT
};

/* The family to set text in role f in: the Latin one, or (text with
 * letters the Latin one does not have) the other -- or, for letters
 * neither has (Arabic, Hebrew, Thai...), one fontconfig finds that has
 * them. text may be NULL. */
const char *desk_font_family(enum desk_font f, const char *text);
/* The same, always from the wide face (the one with Japanese, Chinese and
 * Korean as well as Latin letters), but for letters it has not got:
 * names in any script, in one face whatever the script. */
const char *desk_font_family_wide(enum desk_font f, const char *text);

/* cairo_select_font_face with that family. */
void desk_font_select(cairo_t *cr, enum desk_font f, const char *text, bool bold,
	bool italic);

/* Text set without saying what it is (text NULL) is in the desk's
 * language: wide, if that is one the Latin faces cannot set (i18n.c). */
void desk_font_set_wide_default(bool wide);
/* The wide faces in the cut for the desk's language: Chinese's, Korean's,
 * and Japanese's for any other (i18n.c). */
void desk_font_set_region(enum desk_lang lang);

/* What was chosen for each role, for the log: "ui=Noto Sans CJK JP/..." */
void desk_font_describe(char *buf, size_t n);
/* Complex-script fallback for text DEsk draws itself with Cairo. Pango
 * supplies contextual glyph shaping and bidirectional text ordering.
 * Does not intercept text inside other Wayland applications. */
bool desk_text_needs_shaping(const char *text);
void desk_cairo_show_text(cairo_t *cr, const char *text);
void desk_cairo_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext);
void desk_cairo_text_path(cairo_t *cr, const char *text);

/* Preserve the original Cairo typography for ordinary Latin and CJK text.
 * The wrappers selectively forward complex scripts to PangoCairo. */
#ifndef DESK_TEXT_SHAPING_IMPL
#define cairo_show_text(cr, text) desk_cairo_show_text((cr), (text))
#define cairo_text_extents(cr, text, ext) desk_cairo_text_extents((cr), (text), (ext))
#define cairo_text_path(cr, text) desk_cairo_text_path((cr), (text))
#endif

