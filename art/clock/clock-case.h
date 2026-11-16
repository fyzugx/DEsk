/* clock-case.h: made by clock.py with clock-case.bin -- do not edit, make again.
 * The clock's case and its shadow, rendered through the room's camera for each
 * of these shapes of screen (width over height) and desks (half their
 * width, in room units): where its picture is in clock-case.bin (a PNG),
 * and where that goes in the frame of the room it was rendered in
 * (room_cam_plan_at; 0..1 from its top left). In rows of
 * CLOCK_CASE_ASPECTS shapes, a row to a desk; room_cases_paint in draw.c. */

#define CLOCK_CASE_ASPECTS 12
#define CLOCK_CASE_HXS 1
#define CLOCK_CASE_BIN_SIZE 861466u

static const struct room_case_bake CLOCK_CASE[CLOCK_CASE_ASPECTS * CLOCK_CASE_HXS] = {
	{ 0.300000, 16.0000, 0u, 91634u, 1323, 325, 0.30276609, 0.00000000, 0.70054119, 0.02931361 },
	{ 0.420000, 16.0000, 91634u, 74542u, 1322, 268, 0.30598341, 0.00000000, 0.69757109, 0.03334163 },
	{ 0.562500, 16.0000, 166176u, 70972u, 1319, 242, 0.30995343, 0.00000000, 0.69383003, 0.03962017 },
	{ 0.750000, 16.0000, 237148u, 78946u, 1318, 221, 0.31465149, 0.00000000, 0.68961593, 0.04715170 },
	{ 1.000000, 16.0000, 316094u, 66317u, 1310, 206, 0.32182320, 0.00000000, 0.68370166, 0.05690608 },
	{ 1.333000, 16.0000, 382411u, 67039u, 1299, 197, 0.33058511, 0.00000000, 0.67606383, 0.06983339 },
	{ 1.600000, 16.0000, 449450u, 69471u, 1272, 192, 0.34168388, 0.00000000, 0.67019628, 0.07933884 },
	{ 1.778000, 16.0000, 518921u, 67226u, 1318, 191, 0.33392450, 0.00000000, 0.66784900, 0.08603604 },
	{ 2.100000, 16.0000, 586147u, 69773u, 1242, 189, 0.35301323, 0.00000000, 0.65727585, 0.09722222 },
	{ 2.500000, 16.0000, 655920u, 68444u, 1311, 197, 0.34588235, 0.00000000, 0.65435294, 0.11588235 },
	{ 3.000000, 16.0000, 724364u, 65893u, 1228, 205, 0.35403587, 0.00000000, 0.62937220, 0.13786147 },
	{ 4.000000, 16.0000, 790257u, 71209u, 1174, 221, 0.37745902, 0.00000000, 0.61803279, 0.18114754 },
};
