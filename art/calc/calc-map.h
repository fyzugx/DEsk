/* calc-map.h: made by calc.py with the pictures -- do not edit, make again.
 * Where everything is on the calculator, in pixels of its canvas (the
 * pictures' whole, from its top left), CALC_BAKE_PX to the centimetre. A box
 * is x0, y0, x1, y1. */

#define CALC_BAKE_PX 72
#define CALC_CANVAS 1860
#define CALC_OPEN_X 40
#define CALC_OPEN_Y 282
#define CALC_SHUT_X 667
#define CALC_SHUT_Y 91
#define CALC_OPEN_BOX { 121, 379, 1687, 1639 }
#define CALC_SHUT_BOX { 923, 335, 1699, 1639 }
#define CALC_RIGHT_BOX { 923, 379, 1687, 1639 }
#define CALC_LID_BOX { 121, 379, 887, 1639 }
#define CALC_LID_TRAY { 165, 427, 848, 1589 }
#define CALC_HINGE_BOX { 885, 375, 924, 1639 }
#define CALC_DISP_BOX { 1002, 518, 1590, 699 }
#define CALC_PAPER_BOX { 211, 465, 812, 1092 }
/* the outside's display, shut, and its figures: the first one's lower left, how far
 * to the next and up it (the inside's display, CALC_DISP_BOX, is a matrix of dots
 * DEsk draws) */
#define CALC_ODISP_BOX { 1003, 535, 1601, 698 }
#define CALC_FIG_X 1022.63
#define CALC_FIG_Y 670.30
#define CALC_FIG_PITCH 47.95
#define CALC_FIG_RISE 98.29
#define CALC_FIG_W 29.72
#define CALC_FIG_T 7.83
#define CALC_FIG_SLANT 0.06
#define CALC_FIGURES 12
/* the paper: its rules, down it, and where the sums start and the answers end */
#define CALC_RULES 9
static const double CALC_RULE_Y[CALC_RULES] = { 525.50, 588.89, 652.47, 716.24, 780.19, 844.34, 908.67, 973.19, 1037.91 };
#define CALC_TEXT_X0 241.08
#define CALC_TEXT_X1 785.91
#define CALC_TEXT_SIZE 40.59

/* the keys: which, on which face, where a hand finds it, and its piece held
 * down: where that goes, how big it is, and where it is on the sheet */
static const struct calc_key_map {
	enum desk_calc_key key;
	unsigned char face;     /* 0 open, the inside; 1 shut, the outside */
	short hit[4];
	short at[4];
	short sx, sy;
} CALC_KEYS[] = {
	{ DESK_CK_0, 0, { 998, 1421, 1138, 1549 }, { 990, 1413, 156, 144 }, 0, 0 },
	{ DESK_CK_DBLZERO, 0, { 1154, 1421, 1295, 1549 }, { 1146, 1413, 157, 144 }, 163, 0 },
	{ DESK_CK_DOT, 0, { 1311, 1421, 1453, 1549 }, { 1303, 1413, 158, 144 }, 326, 0 },
	{ DESK_CK_EQ, 0, { 1467, 1421, 1611, 1548 }, { 1459, 1413, 160, 143 }, 489, 0 },
	{ DESK_CK_1, 0, { 997, 1276, 1138, 1404 }, { 989, 1268, 157, 144 }, 652, 0 },
	{ DESK_CK_2, 0, { 1154, 1276, 1295, 1403 }, { 1146, 1268, 157, 143 }, 815, 0 },
	{ DESK_CK_3, 0, { 1309, 1276, 1451, 1403 }, { 1301, 1268, 158, 143 }, 978, 0 },
	{ DESK_CK_ADD, 0, { 1465, 1276, 1608, 1404 }, { 1457, 1268, 159, 144 }, 1141, 0 },
	{ DESK_CK_4, 0, { 997, 1132, 1136, 1259 }, { 989, 1124, 155, 143 }, 0, 146 },
	{ DESK_CK_5, 0, { 1153, 1132, 1293, 1259 }, { 1145, 1124, 156, 143 }, 163, 146 },
	{ DESK_CK_6, 0, { 1308, 1132, 1450, 1259 }, { 1300, 1124, 158, 143 }, 326, 146 },
	{ DESK_CK_SUB, 0, { 1463, 1132, 1606, 1260 }, { 1455, 1124, 159, 144 }, 489, 146 },
	{ DESK_CK_7, 0, { 997, 989, 1137, 1115 }, { 989, 981, 156, 142 }, 652, 146 },
	{ DESK_CK_8, 0, { 1152, 989, 1292, 1116 }, { 1144, 981, 156, 143 }, 815, 146 },
	{ DESK_CK_9, 0, { 1307, 989, 1448, 1116 }, { 1299, 981, 157, 143 }, 978, 146 },
	{ DESK_CK_MUL, 0, { 1462, 989, 1604, 1116 }, { 1454, 981, 158, 143 }, 1141, 146 },
	{ DESK_CK_AC, 0, { 997, 847, 1136, 973 }, { 989, 839, 155, 142 }, 0, 292 },
	{ DESK_CK_NEG, 0, { 1151, 847, 1291, 974 }, { 1143, 839, 156, 143 }, 163, 292 },
	{ DESK_CK_PCT, 0, { 1305, 847, 1446, 973 }, { 1297, 839, 157, 142 }, 326, 292 },
	{ DESK_CK_DIV, 0, { 1461, 846, 1602, 973 }, { 1453, 838, 157, 143 }, 489, 292 },
	{ DESK_CK_MC, 0, { 996, 705, 1135, 832 }, { 988, 697, 156, 143 }, 652, 292 },
	{ DESK_CK_MR, 0, { 1151, 706, 1290, 832 }, { 1143, 698, 155, 142 }, 815, 292 },
	{ DESK_CK_MMINUS, 0, { 1304, 706, 1445, 832 }, { 1296, 698, 157, 142 }, 978, 292 },
	{ DESK_CK_MPLUS, 0, { 1459, 706, 1600, 832 }, { 1451, 698, 157, 142 }, 1141, 292 },
	{ DESK_CK_FOLD, 0, { 1380, 449, 1479, 497 }, { 1372, 441, 115, 64 }, 0, 438 },
	{ DESK_CK_OFF, 0, { 1491, 449, 1590, 498 }, { 1483, 441, 115, 65 }, 163, 438 },
	{ DESK_CK_SIN, 0, { 198, 1421, 342, 1548 }, { 190, 1413, 160, 143 }, 326, 438 },
	{ DESK_CK_COS, 0, { 355, 1421, 498, 1549 }, { 347, 1413, 159, 144 }, 489, 438 },
	{ DESK_CK_TAN, 0, { 513, 1421, 655, 1549 }, { 505, 1413, 158, 144 }, 652, 438 },
	{ DESK_CK_FACT, 0, { 671, 1421, 812, 1549 }, { 663, 1413, 157, 144 }, 815, 438 },
	{ DESK_CK_PI, 0, { 200, 1276, 343, 1403 }, { 192, 1268, 159, 143 }, 978, 438 },
	{ DESK_CK_EUL, 0, { 358, 1276, 500, 1404 }, { 350, 1268, 158, 144 }, 1141, 438 },
	{ DESK_CK_LN, 0, { 515, 1276, 656, 1403 }, { 507, 1268, 157, 143 }, 0, 584 },
	{ DESK_CK_LOG, 0, { 671, 1276, 812, 1403 }, { 663, 1268, 157, 143 }, 163, 584 },
	{ DESK_CK_SQRT, 0, { 203, 1132, 346, 1259 }, { 195, 1124, 159, 143 }, 326, 584 },
	{ DESK_CK_SQ, 0, { 359, 1132, 501, 1260 }, { 351, 1124, 158, 144 }, 489, 584 },
	{ DESK_CK_POW, 0, { 516, 1132, 656, 1259 }, { 508, 1124, 156, 143 }, 652, 584 },
	{ DESK_CK_INV, 0, { 672, 1132, 812, 1259 }, { 664, 1124, 156, 143 }, 815, 584 },
	{ DESK_CK_0, 1, { 998, 1385, 1141, 1513 }, { 990, 1377, 159, 143 }, 978, 584 },
	{ DESK_CK_DBLZERO, 1, { 1158, 1385, 1301, 1513 }, { 1150, 1377, 159, 143 }, 1141, 584 },
	{ DESK_CK_DOT, 1, { 1317, 1385, 1461, 1513 }, { 1309, 1377, 160, 143 }, 0, 730 },
	{ DESK_CK_EQ, 1, { 1476, 1385, 1621, 1513 }, { 1468, 1377, 161, 143 }, 163, 730 },
	{ DESK_CK_1, 1, { 998, 1217, 1140, 1345 }, { 990, 1209, 158, 144 }, 326, 730 },
	{ DESK_CK_2, 1, { 1157, 1217, 1300, 1345 }, { 1149, 1209, 159, 144 }, 489, 730 },
	{ DESK_CK_3, 1, { 1315, 1217, 1459, 1345 }, { 1307, 1209, 160, 144 }, 652, 730 },
	{ DESK_CK_ADD, 1, { 1474, 1217, 1618, 1345 }, { 1466, 1209, 160, 144 }, 815, 730 },
	{ DESK_CK_4, 1, { 998, 1052, 1140, 1179 }, { 990, 1044, 158, 143 }, 978, 730 },
	{ DESK_CK_5, 1, { 1156, 1051, 1298, 1179 }, { 1148, 1043, 158, 144 }, 1141, 730 },
	{ DESK_CK_6, 1, { 1314, 1052, 1458, 1179 }, { 1306, 1044, 160, 143 }, 0, 876 },
	{ DESK_CK_SUB, 1, { 1472, 1052, 1616, 1179 }, { 1464, 1044, 160, 143 }, 163, 876 },
	{ DESK_CK_7, 1, { 998, 887, 1139, 1014 }, { 990, 879, 157, 142 }, 326, 876 },
	{ DESK_CK_8, 1, { 1155, 886, 1297, 1013 }, { 1147, 878, 158, 142 }, 489, 876 },
	{ DESK_CK_9, 1, { 1312, 887, 1455, 1014 }, { 1304, 879, 159, 142 }, 652, 876 },
	{ DESK_CK_MUL, 1, { 1470, 886, 1614, 1013 }, { 1462, 878, 160, 142 }, 815, 876 },
	{ DESK_CK_AC, 1, { 998, 723, 1139, 849 }, { 990, 715, 157, 142 }, 978, 876 },
	{ DESK_CK_NEG, 1, { 1155, 723, 1296, 849 }, { 1147, 715, 157, 142 }, 1141, 876 },
	{ DESK_CK_PCT, 1, { 1311, 723, 1453, 849 }, { 1303, 715, 158, 142 }, 0, 1022 },
	{ DESK_CK_DIV, 1, { 1468, 724, 1611, 850 }, { 1460, 716, 159, 142 }, 163, 1022 },
	{ DESK_CK_FOLD, 1, { 1387, 404, 1488, 453 }, { 1379, 396, 117, 65 }, 326, 1022 },
	{ DESK_CK_OFF, 1, { 1500, 405, 1601, 454 }, { 1492, 397, 117, 65 }, 489, 1022 },
};

/* the lid on its way (calc-lid-NN.png): where each goes */
#include "calc-film.h"

/* the calculator lying in the drawer (calc-top.png), on its side: how many
 * pixels it is along, and its middle */
#define CALC_TOP_ALONG_PX 387.20
#define CALC_TOP_MID_X 204.50
#define CALC_TOP_MID_Y 134.00
