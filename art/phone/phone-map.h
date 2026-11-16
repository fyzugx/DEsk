/* phone-map.h: made by phone.py with the pictures -- do not edit, make again.
 * What src/phone.c lays the pictures by, in pixels on the screen. */

enum {
	PH_PAD = 44,  /* how far the slab's picture goes past it, for its shadow */
	PH_L = 52,  /* the window, from the slab's edge */
	PH_R = 52,
	PH_T = 26,
	PH_B = 26,
	PH_LIGHT_X = 26,  /* the lights' middles, from the slab's right */
	PH_LIGHT_Y = 44,  /* and from its top and its foot */
	PH_EAR_X0 = 4,  /* ph-ear.png, from the slab's left */
	PH_EAR_ABOVE = 101,  /* and from its middle */
	PH_DECK_W = 560,  /* the deck's plate */
	PH_DECK_H = 186,
	PH_DECK_SIDE = 30,  /* picture past it, at each side and under it */
	PH_DECK_FOOT = 40,
	PH_SLIDER_Y = 62,  /* the slider's middle, from the plate's top */
	PH_SLIDER_END = 102,  /* and how far in from each end */
	PH_TIME_L = 32,  /* a window for a time, from the plate's end */
	PH_TIME_W = 62,
	PH_ROW_Y = 90,  /* the row of keys */
	PH_ROW_H = 68,
	PH_ROW_LEAD = 32,  /* the first, from the plate's left, and how wide each is */
	PH_ROW_WIDE = 158,
	PH_CELL_W = 176,  /* the cells of ph-keys.png */
	PH_CELL_H = 92,
	PH_CELL_CLOSE = 3,
	PH_CELL_DOWN = 4,
	PH_CELL_UP = 5,
	PH_CELL_KNOB = 6,
	PH_CELL_HOME = 7,
};
