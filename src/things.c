/* ================================================================
 * Every thing on the desk that is not paper (thing.h). A window is
 * asked for by each in turn, so the first that claims it has it.
 * ================================================================ */

#include "thing.h"

extern const struct desk_thing desk_thing_phone;
extern const struct desk_thing desk_thing_notebook;
extern const struct desk_thing desk_thing_calc;
extern const struct desk_thing desk_thing_keyboard;
extern const struct desk_thing desk_thing_mat;
extern const struct desk_thing desk_thing_clipboard;

const struct desk_thing *const desk_things[] = {
	&desk_thing_phone,
	&desk_thing_notebook,
	&desk_thing_calc,
	&desk_thing_keyboard,
	&desk_thing_mat,
	&desk_thing_clipboard,
	NULL,
};
