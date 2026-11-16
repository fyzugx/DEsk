/* ================================================================
 * A look's folder, read (looks.h): by the desk, and by desk-look, which
 * makes them and says what the desk will make of one.
 * ================================================================ */

#include "looks.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void desk_looks_own_dir(char *out, size_t n)
{
	const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (data && *data) {
		snprintf(out, n, "%s/desk/looks", data);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/share/desk/looks", home);
	} else {
		out[0] = '\0';
	}
}

/* How big a PNG is, from its header, without reading the rest. */
bool desk_png_size(const char *path, int *w, int *h)
{
	unsigned char b[24];
	FILE *f = fopen(path, "rbe");
	if (!f) {
		return false;
	}
	bool ok = fread(b, 1, sizeof b, f) == sizeof b && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)
		&& !memcmp(b + 12, "IHDR", 4);
	fclose(f);
	if (ok) {
		*w = (int)((uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | b[19]);
		*h = (int)((uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | b[23]);
	}
	return ok && *w > 0 && *h > 0;
}

bool desk_look_folder(const char *dir, const char *lang, char *path, size_t pn, char *name, size_t nn,
	double *along, double *thick, int *order)
{
	struct stat st;
	if ((size_t)snprintf(path, pn, "%s/look.png", dir) >= pn || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
		return false;
	}
	/* called what the folder is called, unless it says */
	const char *base = strrchr(dir, '/');
	snprintf(name, nn, "%s", base ? base + 1 : dir);
	double cm = 0.0, ppcm = 0.0, thick_cm = DESK_LOOK_THICK_CM;
	*order = 1000;
	char ini[PATH_MAX];
	snprintf(ini, sizeof ini, "%s/look.ini", dir);
	FILE *f = fopen(ini, "re");
	bool in_look = true, named_in_lang = false;
	char line[512], key[64];
	snprintf(key, sizeof key, "Name[%s]=", lang ? lang : "en");
	while (f && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			in_look = !strcmp(line, "[Look]");
			continue;
		}
		if (!in_look || line[0] == '#') {
			continue;
		}
		if (!strncmp(line, "Name=", 5) && line[5] && !named_in_lang) {
			snprintf(name, nn, "%s", line + 5);
		} else if (!strncmp(line, key, strlen(key)) && line[strlen(key)]) {
			snprintf(name, nn, "%s", line + strlen(key));
			named_in_lang = true;
		} else if (!strncmp(line, "Size=", 5)) {
			cm = strtod(line + 5, NULL);
		} else if (!strncmp(line, "PixelsPerCm=", 12)) {
			ppcm = strtod(line + 12, NULL);
		} else if (!strncmp(line, "Thick=", 6)) {
			thick_cm = strtod(line + 6, NULL);
		} else if (!strncmp(line, "Order=", 6)) {
			*order = atoi(line + 6);
		}
	}
	if (f) {
		fclose(f);
	}
	int w, h;
	if (!(cm > 0.0) && ppcm > 0.0 && desk_png_size(path, &w, &h)) {
		cm = (w > h ? w : h) / ppcm;
	}
	/* (nothing as small as a stamp or bigger than the drawer) */
	cm = cm > 0.0 ? cm : DESK_LOOK_CM;
	cm = cm < 2.0 ? 2.0 : cm > 40.0 ? 40.0 : cm;
	thick_cm = thick_cm < 0.0 ? 0.0 : thick_cm > 12.0 ? 12.0 : thick_cm;
	*along = cm / DESK_LOOK_CM_PER_UNIT;
	*thick = thick_cm / DESK_LOOK_CM_PER_UNIT;
	return true;
}

