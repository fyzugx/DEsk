/* The faces DEsk sets its text in (fonts.h).
 *
 * The toy font API cairo has takes one family and gives every letter of a
 * string from that one face: there is no falling back to another face for
 * a letter it has not got. "sans-serif" under a Japanese locale is a
 * gothic whose Latin letters are all one width, as on a typewriter, and a
 * Latin face asked for Japanese draws boxes. So each role has two lists,
 * and a string is set in the Latin face only if that face has every one of
 * its letters; anything else -- Japanese, an arrow it lacks -- goes to the
 * second, which is chosen for having nearly everything.
 *
 * Chinese, Japanese and Korean share most of their characters but print
 * them differently, so the wide faces are chosen again for each: Noto's
 * and Source Han's own cut for the region, and the faces only one of them
 * has (a Japanese hand, a Korean gothic) only for that one. A Latin face
 * made in Japan (Klee One) has the Japanese shapes too: for Chinese and
 * Korean it sets only what has no such characters in it.
 *
 * And a string with letters neither face has -- Arabic, Hebrew, Thai,
 * Devanagari -- is set in a face fontconfig finds for them, the role's
 * wide face asked for with those letters wanted (desk_font_family_wide):
 * readable, if not joined up or set right to left, which the toy API
 * cannot do. */

#define DESK_TEXT_SHAPING_IMPL
#include "fonts.h"
#include <pango/pangocairo.h>
#include <math.h>

#include <fontconfig/fontconfig.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

struct role {
	const char *name;
	const char *const *latin;
	const char *const *wide;
};

/* Each list in order of preference, ending in a generic name fontconfig
 * always answers. The URW faces (fonts-urw-base35: Palatino, Helvetica,
 * Courier) and Noto CJK (fonts-noto-cjk) are what DEsk is drawn with;
 * the rest are near enough. */
#define WIDE_SANS "Noto Sans CJK JP", "Source Han Sans JP", "Noto Sans CJK SC", \
	"IPAexGothic", "IPAPGothic", "VL PGothic", "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", \
	"NanumGothic", "UnDotum", "Droid Sans Fallback", "sans-serif"
#define WIDE_SERIF "Noto Serif CJK JP", "Source Han Serif JP", "Noto Serif CJK SC", \
	"IPAexMincho", "IPAPMincho", "AR PL UMing CN", "NanumMyeongjo", "UnBatang", \
	"Noto Sans CJK JP", "serif"

static const char *const UI_L[] = { "Noto Sans CJK JP", "Source Han Sans JP", "Noto Sans",
	"Source Sans 3", "Lato", "DejaVu Sans", "sans-serif", NULL };
static const char *const UI_W[] = { WIDE_SANS, NULL };
static const char *const PRINT_L[] = { "Noto Serif CJK JP", "Source Han Serif JP",
	"Noto Serif", "Source Serif 4", "C059", "DejaVu Serif", "serif", NULL };
static const char *const PRINT_W[] = { WIDE_SERIF, NULL };
static const char *const DISPLAY_L[] = { "P052", "TeX Gyre Pagella", "Palatino Linotype",
	"URW Palladio L", "C059", "Noto Serif", "DejaVu Serif", "serif", NULL };
static const char *const ENGRAVE_L[] = { "Nimbus Sans", "TeX Gyre Heros", "Liberation Sans",
	"Arimo", "Noto Sans", "DejaVu Sans", "sans-serif", NULL };
static const char *const TYPE_L[] = { "Nimbus Mono PS", "Courier Prime", "TeX Gyre Cursor",
	"Liberation Mono", "Noto Sans Mono", "DejaVu Sans Mono", "monospace", NULL };
static const char *const TYPE_W[] = { "Noto Sans Mono CJK JP", WIDE_SANS, NULL };
static const char *const SCREEN_L[] = { "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono",
	"monospace", NULL };
static const char *const HAND_L[] = { "Klee One", "Caveat", "Kalam", "Patrick Hand",
	"Noto Serif CJK JP", "Source Han Serif JP", "Noto Serif", "serif", NULL };
static const char *const HAND_W[] = { "Klee One", "Zen Kurenaido", "Yomogi", "LXGW WenKai",
	"Nanum Pen Script", "Gaegu", WIDE_SERIF, NULL };

static const struct role ROLES[DESK_FONT_COUNT] = {
	[DESK_FONT_UI] = { "ui", UI_L, UI_W },
	[DESK_FONT_PRINT] = { "print", PRINT_L, PRINT_W },
	[DESK_FONT_DISPLAY] = { "display", DISPLAY_L, PRINT_W },
	[DESK_FONT_ENGRAVE] = { "engrave", ENGRAVE_L, UI_W },
	[DESK_FONT_TYPE] = { "type", TYPE_L, TYPE_W },
	[DESK_FONT_SCREEN] = { "screen", SCREEN_L, TYPE_W },
	[DESK_FONT_HAND] = { "hand", HAND_L, HAND_W },
};

/* the regions the wide faces are cut for */
enum { REG_JP, REG_SC, REG_KR, REGIONS };

/* faces made for one region's way of writing, and left out of the others'
 * wide lists */
static const struct {
	const char *face;
	int region;
} ONLY_FOR[] = {
	{ "IPAexGothic", REG_JP }, { "IPAPGothic", REG_JP }, { "VL PGothic", REG_JP },
	{ "IPAexMincho", REG_JP }, { "IPAPMincho", REG_JP }, { "Klee One", REG_JP },
	{ "Zen Kurenaido", REG_JP }, { "Yomogi", REG_JP },
	{ "WenQuanYi Zen Hei", REG_SC }, { "WenQuanYi Micro Hei", REG_SC },
	{ "AR PL UMing CN", REG_SC }, { "LXGW WenKai", REG_SC },
	{ "NanumGothic", REG_KR }, { "UnDotum", REG_KR }, { "NanumMyeongjo", REG_KR },
	{ "UnBatang", REG_KR }, { "Nanum Pen Script", REG_KR }, { "Gaegu", REG_KR },
};

/* a family's Japanese cut, and the same in each region's */
static const char *const CUTS[][REGIONS] = {
	{ "Noto Sans CJK JP", "Noto Sans CJK SC", "Noto Sans CJK KR" },
	{ "Noto Serif CJK JP", "Noto Serif CJK SC", "Noto Serif CJK KR" },
	{ "Noto Sans Mono CJK JP", "Noto Sans Mono CJK SC", "Noto Sans Mono CJK KR" },
	{ "Source Han Sans JP", "Source Han Sans SC", "Source Han Sans K" },
	{ "Source Han Serif JP", "Source Han Serif SC", "Source Han Serif K" },
};

struct chosen {
	const char *latin, *wide;
	FcCharSet *cs;          /* the letters the Latin face has */
	FcCharSet *wcs;         /* and the wide one */
};
static struct chosen g_chosen[REGIONS][DESK_FONT_COUNT];
static atomic_bool g_ready[REGIONS];
static pthread_mutex_t g_choosing = PTHREAD_MUTEX_INITIALIZER;
/* the region of the desk's language: Japanese's for any that is not
 * Chinese or Korean */
static atomic_int g_region = REG_JP;
/* text whose words are not known when its face is chosen: in the desk's
 * language, which may be one the Latin faces have no letters for */
static bool g_wide_default;

void desk_font_set_wide_default(bool wide)
{
	g_wide_default = wide;
}

void desk_font_set_region(enum desk_lang lang)
{
	atomic_store(&g_region, lang == DESK_LANG_ZH ? REG_SC : lang == DESK_LANG_KO ? REG_KR
		: REG_JP);
}

/* A family as it is for region r: its cut for there, or (in a wide list)
 * NULL if it is not for there at all. */
static const char *regional(const char *f, int r, bool wide)
{
	for (size_t i = 0; wide && i < sizeof ONLY_FOR / sizeof ONLY_FOR[0]; i++) {
		if (!strcmp(f, ONLY_FOR[i].face)) {
			return ONLY_FOR[i].region == r ? f : NULL;
		}
	}
	for (size_t i = 0; i < sizeof CUTS / sizeof CUTS[0]; i++) {
		if (!strcmp(f, CUTS[i][REG_JP])) {
			return CUTS[i][r];
		}
	}
	return f;
}

static bool generic(const char *f)
{
	return strcmp(f, "sans-serif") == 0 || strcmp(f, "serif") == 0
		|| strcmp(f, "monospace") == 0;
}

/* Is a face of this family installed? Not "what would fontconfig give for
 * it", which is always something. */
static bool installed(const char *family)
{
	FcPattern *p = FcPatternCreate();
	FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
	FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, (char *)NULL);
	FcFontSet *fs = FcFontList(NULL, p, os);
	bool ok = fs && fs->nfont > 0;
	if (fs) {
		FcFontSetDestroy(fs);
	}
	FcObjectSetDestroy(os);
	FcPatternDestroy(p);
	return ok;
}

static const char *first_installed(const char *const *list, int r, bool wide)
{
	for (int i = 0; list[i]; i++) {
		const char *f = regional(list[i], r, wide);
		if (f && (generic(f) || installed(f))) {
			return f;
		}
	}
	return "sans-serif";
}

static FcCharSet *charset_of(const char *family)
{
	FcPattern *p = FcPatternCreate();
	FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
	FcConfigSubstitute(NULL, p, FcMatchPattern);
	FcDefaultSubstitute(p);
	FcResult r;
	FcPattern *m = FcFontMatch(NULL, p, &r);
	FcCharSet *cs = NULL, *got = NULL;
	if (m && FcPatternGetCharSet(m, FC_CHARSET, 0, &got) == FcResultMatch && got) {
		cs = FcCharSetCopy(got);
	}
	if (m) {
		FcPatternDestroy(m);
	}
	FcPatternDestroy(p);
	return cs;
}

/* The desk's region's faces, chosen the first time they are wanted. */
static const struct chosen *chosen(int r)
{
	if (!atomic_load(&g_ready[r])) {
		pthread_mutex_lock(&g_choosing);
		if (!atomic_load(&g_ready[r])) {
			FcInit();
			struct chosen *c = g_chosen[r];
			for (int i = 0; i < DESK_FONT_COUNT; i++) {
				c[i].latin = first_installed(ROLES[i].latin, r, false);
				c[i].wide = first_installed(ROLES[i].wide, r, true);
				c[i].cs = strcmp(c[i].latin, c[i].wide) == 0 ? NULL
					: charset_of(c[i].latin);
				c[i].wcs = charset_of(c[i].wide);
			}
			atomic_store(&g_ready[r], true);
		}
		pthread_mutex_unlock(&g_choosing);
	}
	return g_chosen[r];
}

/* the next letter of s, and where the one after it starts */
static unsigned next_letter(const unsigned char **s)
{
	const unsigned char *p = *s;
	unsigned c = p[0];
	int n = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
	if (n) {
		c &= 0x3f >> n;
	}
	p++;
	for (int i = 0; i < n && (*p & 0xc0) == 0x80; i++, p++) {
		c = (c << 6) | (*p & 0x3f);
	}
	*s = p;
	return c;
}

/* What is in text but not for seeing: joiners and marks of direction,
 * variation selectors, tags -- no face need have them. */
static bool ignorable(unsigned c)
{
	return c < 0x20 || (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e)
		|| (c >= 0x2060 && c <= 0x206f) || (c >= 0xfe00 && c <= 0xfe0f) || c == 0xfeff
		|| c >= 0xe0000;
}

/* The first letter of text a face has not got; 0 if it has them all. */
static unsigned first_missing(const FcCharSet *cs, const char *text)
{
	if (!cs || !text) {
		return 0;
	}
	for (const unsigned char *p = (const unsigned char *)text; *p;) {
		unsigned c = next_letter(&p);
		if (!ignorable(c) && !FcCharSetHasChar(cs, c)) {
			return c;
		}
	}
	return 0;
}

/* Faces found for letters the chosen ones have not got, by the role and
 * the block of 128 the first such letter is in (or that none was found). */
#define FALLBACKS 64
static struct {
	int region, role;
	unsigned block;
	bool none;
	char family[96];
} g_fall[FALLBACKS];
static int g_fall_n;

/* The language a letter is written in, for the scripts the wide faces
 * have not got: fontconfig keeps its best faces for each by language. */
static const char *lang_of(unsigned c)
{
	static const struct {
		unsigned from, to;
		const char *lang;
	} SCRIPTS[] = {
		{ 0x0530, 0x058f, "hy" }, { 0x0590, 0x05ff, "he" }, { 0x0600, 0x06ff, "ar" },
		{ 0x0700, 0x074f, "syr" }, { 0x0750, 0x077f, "ar" }, { 0x0780, 0x07bf, "dv" },
		{ 0x08a0, 0x08ff, "ar" }, { 0x0900, 0x097f, "hi" }, { 0x0980, 0x09ff, "bn" },
		{ 0x0a00, 0x0a7f, "pa" }, { 0x0a80, 0x0aff, "gu" }, { 0x0b00, 0x0b7f, "or" },
		{ 0x0b80, 0x0bff, "ta" }, { 0x0c00, 0x0c7f, "te" }, { 0x0c80, 0x0cff, "kn" },
		{ 0x0d00, 0x0d7f, "ml" }, { 0x0d80, 0x0dff, "si" }, { 0x0e00, 0x0e7f, "th" },
		{ 0x0e80, 0x0eff, "lo" }, { 0x0f00, 0x0fff, "bo" }, { 0x1000, 0x109f, "my" },
		{ 0x10a0, 0x10ff, "ka" }, { 0x1200, 0x139f, "am" }, { 0x1780, 0x17ff, "km" },
		{ 0x1800, 0x18af, "mn" }, { 0xfb1d, 0xfb4f, "he" }, { 0xfb50, 0xfdff, "ar" },
		{ 0xfe70, 0xfeff, "ar" },
	};
	for (size_t i = 0; i < sizeof SCRIPTS / sizeof SCRIPTS[0]; i++) {
		if (c >= SCRIPTS[i].from && c <= SCRIPTS[i].to) {
			return SCRIPTS[i].lang;
		}
	}
	return NULL;
}

/* A face's English name, else its first. */
static const char *english_family(FcPattern *font)
{
	FcChar8 *name = NULL, *lang = NULL, *first = NULL;
	for (int i = 0; FcPatternGetString(font, FC_FAMILY, i, &name) == FcResultMatch; i++) {
		if (!first) {
			first = name;
		}
		if (FcPatternGetString(font, FC_FAMILYLANG, i, &lang) == FcResultMatch
			&& strcmp((const char *)lang, "en") == 0) {
			return (const char *)name;
		}
	}
	return (const char *)first;
}

/* A face that has the letters of text, for its role (style: serif,
 * sans-serif, monospace) and the language of the letter missed, as
 * fontconfig orders them: those with the letter missed and as many of the
 * rest as there are first. Unifont -- every letter there is, drawn in
 * pixels -- only when nothing else has it. */
static bool fall_find(const char *style, const char *text, unsigned missed, char *out, size_t n)
{
	FcCharSet *want = FcCharSetCreate();
	if (!want) {
		return false;
	}
	for (const unsigned char *p = (const unsigned char *)text; *p;) {
		unsigned c = next_letter(&p);
		if (!ignorable(c)) {
			FcCharSetAddChar(want, c);
		}
	}
	FcPattern *pat = FcPatternCreate();
	FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)style);
	FcPatternAddCharSet(pat, FC_CHARSET, want);
	FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
	const char *lang = lang_of(missed);
	if (lang) {
		FcPatternAddString(pat, FC_LANG, (const FcChar8 *)lang);
	}
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult r;
	FcFontSet *fs = FcFontSort(NULL, pat, FcFalse, NULL, &r);
	const char *best = NULL, *last_resort = NULL;
	for (int i = 0; fs && i < fs->nfont && !best; i++) {
		FcCharSet *got = NULL;
		if (FcPatternGetCharSet(fs->fonts[i], FC_CHARSET, 0, &got) != FcResultMatch || !got
			|| !FcCharSetHasChar(got, missed)) {
			continue;
		}
		const char *family = english_family(fs->fonts[i]);
		if (!family) {
			continue;
		}
		if (strncmp(family, "Unifont", 7) == 0) {
			last_resort = last_resort ? last_resort : family;
		} else {
			best = family;
		}
	}
	bool ok = false;
	if (best || last_resort) {
		snprintf(out, n, "%s", best ? best : last_resort);
		ok = true;
	}
	if (fs) {
		FcFontSetDestroy(fs);
	}
	FcPatternDestroy(pat);
	FcCharSetDestroy(want);
	return ok;
}

/* the kind of face each role is, for finding another of the kind */
static const char *const STYLE[DESK_FONT_COUNT] = {
	[DESK_FONT_UI] = "sans-serif", [DESK_FONT_PRINT] = "serif", [DESK_FONT_DISPLAY] = "serif",
	[DESK_FONT_ENGRAVE] = "sans-serif", [DESK_FONT_TYPE] = "monospace",
	[DESK_FONT_SCREEN] = "monospace", [DESK_FONT_HAND] = "serif",
};

static const char *fall_back(int r, int f, unsigned missed, const char *text, const char *near)
{
	unsigned block = missed >> 7;
	const char *found = NULL;
	pthread_mutex_lock(&g_choosing);
	for (int i = 0; i < g_fall_n; i++) {
		if (g_fall[i].region == r && g_fall[i].role == f && g_fall[i].block == block) {
			found = g_fall[i].none ? near : g_fall[i].family;
			break;
		}
	}
	if (!found && g_fall_n < FALLBACKS) {
		int i = g_fall_n++;
		g_fall[i].region = r;
		g_fall[i].role = f;
		g_fall[i].block = block;
		g_fall[i].none = !fall_find(STYLE[f], text, missed, g_fall[i].family,
			sizeof g_fall[i].family);
		found = g_fall[i].none ? near : g_fall[i].family;
	}
	pthread_mutex_unlock(&g_choosing);
	return found ? found : near;
}

const char *desk_font_family_wide(enum desk_font f, const char *text)
{
	int r = atomic_load(&g_region);
	const struct chosen *ch = chosen(r);
	if (f < 0 || f >= DESK_FONT_COUNT) {
		f = DESK_FONT_UI;
	}
	unsigned missed = first_missing(ch[f].wcs, text);
	return missed ? fall_back(r, f, missed, text, ch[f].wide) : ch[f].wide;
}

const char *desk_font_family(enum desk_font f, const char *text)
{
	int r = atomic_load(&g_region);
	const struct chosen *ch = chosen(r);
	if (f < 0 || f >= DESK_FONT_COUNT) {
		f = DESK_FONT_UI;
	}
	if (!text) {
		return g_wide_default ? ch[f].wide : ch[f].latin;
	}
	if (!ch[f].cs) {
		return desk_font_family_wide(f, text);
	}
	for (const unsigned char *p = (const unsigned char *)text; *p;) {
		unsigned c = next_letter(&p);
		/* (from U+2E80 on: the CJK radicals, kana, hangul, ideographs) */
		if (c >= 0x20 && !ignorable(c)
			&& (!FcCharSetHasChar(ch[f].cs, c) || (r != REG_JP && c >= 0x2e80))) {
			return desk_font_family_wide(f, text);
		}
	}
	return ch[f].latin;
}

void desk_font_select(cairo_t *cr, enum desk_font f, const char *text, bool bold,
	bool italic)
{
	cairo_select_font_face(cr, desk_font_family(f, text),
		italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,
		bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
}

void desk_font_describe(char *buf, size_t n)
{
	const struct chosen *ch = chosen(atomic_load(&g_region));
	size_t o = 0;
	buf[0] = '\0';
	for (int i = 0; i < DESK_FONT_COUNT && o < n; i++) {
		int k = snprintf(buf + o, n - o, "%s%s=%s/%s", i ? ", " : "", ROLES[i].name,
			ch[i].latin, ch[i].wide);
		if (k < 0) {
			break;
		}
		o += (size_t)k;
	}
}


/* -------------------------------------------------------------------------
 * Complex-script shaping for Cairo-based DEsk text.
 * Preserve the original Cairo rendering for Latin and CJK strings; use
 * Pango for Arabic joining, bidi runs, and font fallback when required.
 * The current drawing transform and font selection stay in effect. */
bool desk_text_needs_shaping(const char *text)
{
    if (!text) return false;
    for (const unsigned char *q = (const unsigned char *)text; *q; q++) {
        if (*q & 0x80) {
            if (!g_utf8_validate(text, -1, NULL)) return false;
            for (const char *p = text; *p; p = g_utf8_next_char(p)) {
                gunichar u = g_utf8_get_char(p);
                /* Hebrew, Arabic, related RTL blocks and presentation forms. */
                if ((u >= 0x0590 && u <= 0x08ff) ||
                    (u >= 0xfb1d && u <= 0xfeff) ||
                    (u >= 0x10e60 && u <= 0x10e7f) ||
                    (u >= 0x1ee00 && u <= 0x1eeff) ||
                    /* Indic and Southeast Asian combining scripts. */
                    (u >= 0x0900 && u <= 0x0e7f) ||
                    (u >= 0x0f00 && u <= 0x109f) ||
                    (u >= 0x1780 && u <= 0x17ff) ||
                    (u >= 0x1a20 && u <= 0x1aaf)) return true;
            }
            return false;
        }
    }
    return false;
}

static PangoLayout *desk_shaped_layout(cairo_t *cr, const char *text)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_new();
    cairo_font_face_t *face = cairo_get_font_face(cr);
    const char *family = cairo_font_face_get_type(face) == CAIRO_FONT_TYPE_TOY
        ? cairo_toy_font_face_get_family(face) : "sans-serif";
    cairo_matrix_t matrix;
    cairo_get_font_matrix(cr, &matrix);
    double size = hypot(matrix.xx, matrix.yx);
    if (!(size > 0.0) || !isfinite(size)) size = 12.0;
    pango_font_description_set_family(font, family);
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    if (cairo_font_face_get_type(face) == CAIRO_FONT_TYPE_TOY) {
        pango_font_description_set_weight(font,
            cairo_toy_font_face_get_weight(face) == CAIRO_FONT_WEIGHT_BOLD
            ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        pango_font_description_set_style(font,
            cairo_toy_font_face_get_slant(face) == CAIRO_FONT_SLANT_ITALIC
            ? PANGO_STYLE_ITALIC :
            cairo_toy_font_face_get_slant(face) == CAIRO_FONT_SLANT_OBLIQUE
            ? PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL);
    }
    pango_layout_set_font_description(layout, font);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_auto_dir(layout, TRUE);
    pango_layout_set_text(layout, text, -1);
    pango_font_description_free(font);
    return layout;
}

void desk_cairo_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext)
{
    if (!desk_text_needs_shaping(text)) {
        cairo_text_extents(cr, text, ext);
        return;
    }
    PangoLayout *layout = desk_shaped_layout(cr, text);
    PangoRectangle ink, logical;
    pango_layout_get_extents(layout, &ink, &logical);
    ext->x_bearing = (double)ink.x / PANGO_SCALE;
    ext->y_bearing = (double)(ink.y - pango_layout_get_baseline(layout)) / PANGO_SCALE;
    ext->width = (double)ink.width / PANGO_SCALE;
    ext->height = (double)ink.height / PANGO_SCALE;
    ext->x_advance = (double)logical.width / PANGO_SCALE;
    ext->y_advance = 0.0;
    g_object_unref(layout);
}

void desk_cairo_show_text(cairo_t *cr, const char *text)
{
    if (!desk_text_needs_shaping(text)) {
        cairo_show_text(cr, text);
        return;
    }
    PangoLayout *layout = desk_shaped_layout(cr, text);
    int width = 0;
    pango_layout_get_size(layout, &width, NULL);
    double x = 0.0, y = 0.0;
    cairo_get_current_point(cr, &x, &y);
    cairo_save(cr);
    cairo_move_to(cr, x, y - (double)pango_layout_get_baseline(layout) / PANGO_SCALE);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
    cairo_move_to(cr, x + (double)width / PANGO_SCALE, y);
    g_object_unref(layout);
}

void desk_cairo_text_path(cairo_t *cr, const char *text)
{
    if (!desk_text_needs_shaping(text)) {
        cairo_text_path(cr, text);
        return;
    }
    PangoLayout *layout = desk_shaped_layout(cr, text);
    int width = 0;
    pango_layout_get_size(layout, &width, NULL);
    double x = 0.0, y = 0.0;
    cairo_get_current_point(cr, &x, &y);
    cairo_save(cr);
    cairo_move_to(cr, x, y - (double)pango_layout_get_baseline(layout) / PANGO_SCALE);
    pango_cairo_layout_path(cr, layout);
    cairo_restore(cr);
    cairo_move_to(cr, x + (double)width / PANGO_SCALE, y);
    g_object_unref(layout);
}
