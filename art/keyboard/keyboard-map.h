/* keyboard-map.h: made by keyboard.py with the pictures -- do not edit, make again.
 * the keyboard, as DEsk lays it out (src/keyboardart.c) */

enum {
	KB_CASE_W = 5936,
	KB_CASE_H = 2820,
	KB_BODY_X = 863,  /* its body, in the pictures: the left of its front */
	KB_BODY_FOOT = 2536,  /* where its front meets the desk */
	KB_PLATEN_TOP = 595,  /* the top of the platen, where paper comes out */
	KB_PLATEN_X = 1079,  /* and its left end */
	KB_PAPER_W = 3542,  /* a sheet in it */
	KB_PLATE_Y = 712,  /* the plate paper goes in behind */
	KB_BAIL_Y = 687,  /* and the bail over it */
	KB_BODY_TOP = 501,  /* the back of its body */
	KB_LEVER_X0 = 947,  /* the lever, in the pictures */
	KB_LEVER_X1 = 1447,
	KB_LEVER_Y0 = 223,
	KB_COLS = 15,
	KB_K_REF = 13200,  /* pixels to the centimetre where the keys are rendered, in hundredths */
	KB_U_REF = 275,  /* a key across, there */
	KB_ROW_REF = 237,  /* a full row, there */
	KB_SLIM_REF = 206,  /* the top row, as it would be there */
	KB_TILE_TOP = 31,  /* how far above its row a tile begins */
	KB_SLIM_TOP = 30,
	KB_TILE_LEFT = 62,  /* and how far to the left of a key */
	KB_LIGHT_X = 4818,
	KB_LIGHT_Y = 993,
	KB_SHOW_FOOT = 2571,  /* lying at the front of the desk, as much as shows */
	KB_KNOB_X = 829,  /* the left of its knob */
	KB_WELL_FX = 878,  /* the well: its front left corner */
	KB_WELL_FY = 2467,
	KB_WELL_BX = 848,  /* and its back left corner */
	KB_WELL_BY = 1035,
	KB_ROW_MID_0 = 1145,  /* the middle of each row */
	KB_ROW_MID_1 = 1352,
	KB_ROW_MID_2 = 1591,
	KB_ROW_MID_3 = 1828,
	KB_ROW_MID_4 = 2064,
	KB_ROW_MID_5 = 2300,
	KB_ROW_TOP_0 = 1042,  /* each row, down the picture */
	KB_ROW_TOP_1 = 1248,
	KB_ROW_TOP_2 = 1471,
	KB_ROW_TOP_3 = 1709,
	KB_ROW_TOP_4 = 1946,
	KB_ROW_TOP_5 = 2182,
	KB_ROW_TOP_6 = 2417,
	KB_ROW_U_0 = 2764,  /* a key across in each, in tenths of a pixel */
	KB_ROW_U_1 = 2759,
	KB_ROW_U_2 = 2752,
	KB_ROW_U_3 = 2746,
	KB_ROW_U_4 = 2739,
	KB_ROW_U_5 = 2733,
};
