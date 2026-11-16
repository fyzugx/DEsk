/* What fingers are taken to have done (src/touch.c). */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "touch.h"

static int failed;

#define SAYS(call, want) do { \
	struct desk_touch_said said_ = (call); \
	if (said_.what != (want)) { \
		printf("line %d: said %d, not %d\n", __LINE__, said_.what, (want)); \
		failed++; \
	} \
} while (0)

int main(void)
{
	struct desk_touch t;

	/* a tap, with a finger's wobble in it */
	memset(&t, 0, sizeof t);
	SAYS(desk_touch_down(&t, 7, 100, 100, 1.0, 1.0), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_move(&t, 7, 104, 103, 1.05, 1.0), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_up(&t, 7, 1.1), DESK_TOUCH_TAP);

	/* held: once, and lifted with nothing more to say than that */
	desk_touch_down(&t, 1, 100, 100, 2.0, 1.0);
	SAYS(desk_touch_tick(&t, 2.2), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_tick(&t, 2.5), DESK_TOUCH_LONG);
	SAYS(desk_touch_tick(&t, 2.6), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_move(&t, 1, 200, 100, 2.7, 1.0), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_up(&t, 1, 2.8), DESK_TOUCH_LIFTED);

	/* a finger that has gone is not held */
	desk_touch_down(&t, 1, 100, 100, 3.0, 1.0);
	SAYS(desk_touch_move(&t, 1, 120, 100, 3.02, 1.0), DESK_TOUCH_SETS_OFF);
	SAYS(desk_touch_move(&t, 1, 140, 100, 3.04, 1.0), DESK_TOUCH_MOVES);
	SAYS(desk_touch_tick(&t, 4.0), DESK_TOUCH_NOTHING);
	struct desk_touch_said up = desk_touch_up(&t, 1, 4.1);
	if (up.what != DESK_TOUCH_LIFTED || up.vx != 0.0) {
		printf("still before it was lifted, and said to be going %f\n", up.vx);
		failed++;
	}

	/* and one lifted on the move is going */
	desk_touch_down(&t, 1, 100, 100, 5.0, 1.0);
	desk_touch_move(&t, 1, 140, 100, 5.02, 1.0);
	desk_touch_move(&t, 1, 180, 100, 5.04, 1.0);
	up = desk_touch_up(&t, 1, 5.05);
	if (up.vx < 1000.0) {
		printf("lifted on the move, and said to be going %f\n", up.vx);
		failed++;
	}

	/* points are bigger on a bigger screen */
	desk_touch_down(&t, 1, 100, 100, 6.0, 2.0);
	SAYS(desk_touch_move(&t, 1, 115, 100, 6.1, 2.0), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_up(&t, 1, 6.2), DESK_TOUCH_TAP);

	/* two fingers are two fingers */
	desk_touch_down(&t, 1, 100, 100, 7.0, 1.0);
	SAYS(desk_touch_down(&t, 2, 200, 100, 7.01, 1.0), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_move(&t, 2, 230, 100, 7.05, 1.0), DESK_TOUCH_SETS_OFF);
	desk_touch_up(&t, 1, 7.1);
	desk_touch_up(&t, 2, 7.1);

	/* a third long after the first two is a third finger */
	desk_touch_down(&t, 1, 100, 100, 8.0, 1.0);
	desk_touch_down(&t, 2, 200, 100, 8.0, 1.0);
	SAYS(desk_touch_down(&t, 3, 150, 200, 9.0, 1.0), DESK_TOUCH_NOTHING);
	desk_touch_up(&t, 1, 9.1);
	desk_touch_up(&t, 2, 9.1);
	desk_touch_up(&t, 3, 9.1);

	/* a hand, going somewhere: says which way once, then how far, and is lifted once */
	desk_touch_down(&t, 0, 400, 400, 10.0, 1.0);
	desk_touch_down(&t, 1, 500, 400, 10.02, 1.0);
	SAYS(desk_touch_down(&t, 2, 450, 500, 10.04, 1.0), DESK_TOUCH_HAND);
	SAYS(desk_touch_move(&t, 0, 400, 395, 10.1, 1.0), DESK_TOUCH_NOTHING);
	desk_touch_move(&t, 1, 500, 375, 10.12, 1.0);
	SAYS(desk_touch_move(&t, 2, 450, 475, 10.14, 1.0), DESK_TOUCH_HAND_SWIPE);
	SAYS(desk_touch_move(&t, 0, 400, 300, 10.16, 1.0), DESK_TOUCH_HAND_MOVES);
	desk_touch_move(&t, 1, 500, 300, 10.18, 1.0);
	struct desk_touch_said s = desk_touch_move(&t, 2, 450, 400, 10.2, 1.0);
	if (s.what != DESK_TOUCH_HAND_MOVES || s.dy > -90.0 || s.dy < -110.0) {
		printf("a hand gone 100 up, said %d, %f\n", s.what, s.dy);
		failed++;
	}
	SAYS(desk_touch_tick(&t, 10.3), DESK_TOUCH_NOTHING);
	s = desk_touch_up(&t, 0, 10.22);
	if (s.what != DESK_TOUCH_HAND_LIFTED || s.vy > -100.0) {
		printf("a hand lifted, said %d, going %f\n", s.what, s.vy);
		failed++;
	}
	SAYS(desk_touch_move(&t, 1, 500, 100, 10.3, 1.0), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_up(&t, 1, 11.1), DESK_TOUCH_NOTHING);
	SAYS(desk_touch_up(&t, 2, 11.1), DESK_TOUCH_NOTHING);

	/* closing up, and opening out */
	for (int i = 0; i < 3; i++) {
		double a = i * 2.0944;
		desk_touch_down(&t, i, 500 + 100 * cos(a), 500 + 100 * sin(a), 12.0, 1.0);
	}
	s = (struct desk_touch_said){ 0 };
	for (double r = 95; r > 30 && s.what == DESK_TOUCH_NOTHING; r -= 5) {
		for (int i = 0; i < 3 && s.what == DESK_TOUCH_NOTHING; i++) {
			double a = i * 2.0944;
			s = desk_touch_move(&t, i, 500 + r * cos(a), 500 + r * sin(a), 12.2, 1.0);
		}
	}
	if (s.what != DESK_TOUCH_HAND_GATHER) {
		printf("a hand closing up, said %d\n", s.what);
		failed++;
	}
	for (int i = 0; i < 3; i++) {
		double a = i * 2.0944;
		s = desk_touch_move(&t, i, 500 + 50 * cos(a), 500 + 50 * sin(a), 12.21 + i * 0.01, 1.0);
	}
	if (fabs(s.spread - 0.5) > 0.02) {
		printf("a hand closed to half, said %f\n", s.spread);
		failed++;
	}
	s = desk_touch_up(&t, 0, 12.24);
	if (s.what != DESK_TOUCH_HAND_LIFTED || s.vspread >= 0.0) {
		printf("a hand lifted closing up, said %d, %f\n", s.what, s.vspread);
		failed++;
	}
	for (int i = 1; i < 3; i++) {
		desk_touch_up(&t, i, 12.5);
	}
	for (int i = 0; i < 3; i++) {
		double a = i * 2.0944;
		desk_touch_down(&t, i, 500 + 50 * cos(a), 500 + 50 * sin(a), 13.0, 1.0);
	}
	s = (struct desk_touch_said){ 0 };
	for (double r = 55; r < 140 && s.what == DESK_TOUCH_NOTHING; r += 5) {
		for (int i = 0; i < 3 && s.what == DESK_TOUCH_NOTHING; i++) {
			double a = i * 2.0944;
			s = desk_touch_move(&t, i, 500 + r * cos(a), 500 + r * sin(a), 13.2, 1.0);
		}
	}
	if (s.what != DESK_TOUCH_HAND_SPREAD) {
		printf("a hand opening out, said %d\n", s.what);
		failed++;
	}
	for (int i = 0; i < 3; i++) {
		desk_touch_up(&t, i, 13.5);
	}

	/* six together, two hands: the keys asked for, once, and the hand going nowhere after */
	for (int i = 0; i < 5; i++) {
		SAYS(desk_touch_down(&t, 20 + i, 100 + i * 40, 300, 13.6 + i * 0.05, 1.0),
			i == 2 ? DESK_TOUCH_HAND : DESK_TOUCH_NOTHING);
	}
	SAYS(desk_touch_down(&t, 25, 700, 300, 13.95, 1.0), DESK_TOUCH_SIX);
	SAYS(desk_touch_move(&t, 25, 760, 300, 14.0, 1.0), DESK_TOUCH_NOTHING);
	for (int i = 0; i < 6; i++) {
		SAYS(desk_touch_up(&t, 20 + i, 14.1), DESK_TOUCH_NOTHING);
	}
	/* and six one after another, slowly, are not */
	for (int i = 0; i < 6; i++) {
		struct desk_touch_said s6 = desk_touch_down(&t, 30 + i, 100 + i * 40, 300, 14.5 + i * 0.2, 1.0);
		if (s6.what == DESK_TOUCH_SIX) {
			printf("six over a second, said to be together\n");
			failed++;
		}
	}
	for (int i = 0; i < 6; i++) {
		desk_touch_up(&t, 30 + i, 16.0);
	}

	/* a tap after a hand is a tap; a finger taken back is nothing; too many fingers break nothing */
	desk_touch_down(&t, 5, 10, 10, 14.0, 1.0);
	SAYS(desk_touch_up(&t, 5, 14.1), DESK_TOUCH_TAP);
	desk_touch_down(&t, 5, 10, 10, 15.0, 1.0);
	desk_touch_cancel(&t, 5);
	SAYS(desk_touch_up(&t, 5, 15.1), DESK_TOUCH_NOTHING);
	for (int i = 0; i < DESK_FINGERS + 3; i++) {
		desk_touch_down(&t, i, 10, 10, 16.0, 1.0);
	}
	for (int i = 0; i < DESK_FINGERS + 3; i++) {
		desk_touch_up(&t, i, 16.1);
	}
	if (t.n != 0 || t.hand) {
		printf("all lifted, and %d left down\n", t.n);
		failed++;
	}
	if (failed) {
		printf("%d wrong\n", failed);
	}
	return failed ? 1 : 0;
}
