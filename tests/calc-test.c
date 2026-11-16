/* meson test: the calculator's workings, keyed as somebody would (src/calc.c). */

#include "calc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

/* keys: one letter or mark each */
static void key(const char *keys)
{
	static const struct { char c; enum desk_calc_key k; } MAP[] = {
		{ '.', DESK_CK_DOT }, { '+', DESK_CK_ADD }, { '-', DESK_CK_SUB }, { '*', DESK_CK_MUL },
		{ '/', DESK_CK_DIV }, { '=', DESK_CK_EQ }, { '%', DESK_CK_PCT }, { 'n', DESK_CK_NEG },
		{ 'C', DESK_CK_AC }, { 'c', DESK_CK_MC }, { 'r', DESK_CK_MR }, { 'm', DESK_CK_MMINUS },
		{ 'p', DESK_CK_MPLUS }, { 'R', DESK_CK_SQRT }, { 'Q', DESK_CK_SQ }, { '^', DESK_CK_POW },
		{ 'I', DESK_CK_INV }, { 'P', DESK_CK_PI }, { 'E', DESK_CK_EUL }, { 'L', DESK_CK_LN },
		{ 'G', DESK_CK_LOG }, { 'S', DESK_CK_SIN }, { 'O', DESK_CK_COS }, { 'T', DESK_CK_TAN },
		{ '!', DESK_CK_FACT }, { 'Z', DESK_CK_DBLZERO },
	};
	for (; *keys; keys++) {
		if (*keys >= '0' && *keys <= '9') {
			desk_calc_press(DESK_CK_0 + (*keys - '0'));
			continue;
		}
		for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
			if (MAP[i].c == *keys) {
				desk_calc_press(MAP[i].k);
			}
		}
	}
}

static void expect(const char *keys, const char *shown)
{
	desk_calc_press(DESK_CK_AC);
	key(keys);
	char s[64];
	desk_calc_shown(s, sizeof s);
	if (strcmp(s, shown)) {
		printf("FAIL %s: shows %s, not %s\n", keys, s, shown);
		g_failed++;
	}
}

int main(void)
{
	setenv("XDG_DATA_HOME", "/nonexistent", 1);
	setenv("HOME", "/nonexistent", 1);

	expect("", "0.");
	expect("123", "123.");
	expect("007", "7.");
	expect("1.50", "1.50");
	expect(".5", "0.5");
	expect("1.2.3", "1.23");
	expect("1Z", "100.");
	expect("123456789012345", "123456789012.");

	/* in the order they are keyed */
	expect("1+2*3=", "9.");
	expect("2+3=", "5.");
	expect("9-12=", "-3.");
	expect("7/2=", "3.5");
	expect("1250+380=*2=-60=", "3200.");

	/* the decimals that were meant */
	expect(".1+.2=", "0.3");
	expect("1.1*1.1=", "1.21");
	expect(".1*3=", "0.3");
	expect("1/3*3=", "1.");
	expect("100-99.9=", "0.1");

	/* operations changing their minds, and going on */
	expect("5+-3=", "2.");
	expect("2+3+", "5.");
	expect("5*=", "25.");
	/* = again */
	expect("5+2===", "11.");
	expect("2*3==", "18.");

	/* percent */
	expect("200+10%", "220.");
	expect("200-10%", "180.");
	expect("200*10%", "20.");
	expect("200/10%", "2000.");
	expect("50%", "0.5");

	/* sign */
	expect("5n", "-5.");
	expect("5nn", "5.");
	expect("5n+3=", "-2.");
	expect("2+3=n", "-5.");
	expect("2+n3=", "5.");

	/* memory */
	expect("Ccc5p3pC r", "8.");
	expect("c5p2mCr", "3.");
	expect("c2+3pr", "5.");
	expect("cCr", "0.");

	/* one number */
	expect("9R", "3.");
	expect("3Q", "9.");
	expect("8I", "0.125");
	expect("2^10=", "1024.");
	expect("P", "3.14159265359");
	expect("E", "2.71828182846");
	expect("100G", "2.");
	expect("EL", "1.");
	expect("30S", "0.5");
	expect("60O", "0.5");
	expect("45T", "1.");
	expect("180S", "0.");
	expect("90O", "0.");
	expect("5!", "120.");
	expect("0!", "1.");
	expect("2+9R=", "5.");
	expect("9R+1=", "4.");

	/* what cannot be */
	expect("1/0=", "Error");
	expect("1nR", "Error");
	expect("0I", "Error");
	expect("0L", "Error");
	expect("90T", "Error");
	expect("1.5!", "Error");
	expect("171!", "Error");
	expect("1/0=5", "5.");
	expect("1/0=C", "0.");
	expect("1/0=+3=", "3.");

	/* too big or small to write out */
	expect("999999999999*2=", "2E12");
	expect("20!", "2.43290201E18");
	expect("170!", "7.2574156E306");
	expect("1/999999999999=", "1E-12");
	expect("999999999999*999999999999=", "1E24");
	expect("1n/3=", "-0.3333333333");

	/* rubbing out */
	desk_calc_press(DESK_CK_AC);
	key("123");
	desk_calc_rub_out();
	char s[64];
	desk_calc_shown(s, sizeof s);
	if (strcmp(s, "12.")) {
		printf("FAIL rub out: %s\n", s);
		g_failed++;
	}

	/* a number from elsewhere */
	static const struct { const char *in, *shown; } PASTES[] = {
		{ "42", "42." }, { " 12.5\n", "12.5" }, { "1,250", "1250." }, { "12,5", "12.5" },
		{ "1,234,567.5", "1234567.5" }, { "1.234.567,5", "1234567.5" }, { "-3", "-3." },
		{ "1e3", "1000." },
	};
	for (size_t i = 0; i < sizeof PASTES / sizeof PASTES[0]; i++) {
		if (!desk_calc_paste(PASTES[i].in)) {
			printf("FAIL paste %s refused\n", PASTES[i].in);
			g_failed++;
			continue;
		}
		desk_calc_shown(s, sizeof s);
		if (strcmp(s, PASTES[i].shown)) {
			printf("FAIL paste %s: %s\n", PASTES[i].in, s);
			g_failed++;
		}
	}
	if (desk_calc_paste("hello") || desk_calc_paste("") || desk_calc_paste("1 2")) {
		printf("FAIL paste: took what is not a number\n");
		g_failed++;
	}

	/* the paper */
	desk_calc_tear();
	expect("1250+380=", "1630.");
	const struct desk_calc_line *l = desk_calc_line(desk_calc_lines() - 1);
	if (!l || strcmp(l->sum, "1,250 + 380") || strcmp(l->answer, "1,630")) {
		printf("FAIL paper: %s = %s\n", l ? l->sum : "?", l ? l->answer : "?");
		g_failed++;
	}
	expect("3200R", "56.5685424949");
	l = desk_calc_line(desk_calc_lines() - 1);
	if (!l || strcmp(l->sum, "\xe2\x88\x9a 3,200")) {
		printf("FAIL paper: %s\n", l ? l->sum : "?");
		g_failed++;
	}
	desk_calc_recall(0);
	desk_calc_shown(s, sizeof s);
	if (strcmp(s, "1630.")) {
		printf("FAIL recall: %s\n", s);
		g_failed++;
	}
	/* only so many lines */
	for (int i = 0; i < DESK_CALC_LINES + 10; i++) {
		expect("1+1=", "2.");
	}
	if (desk_calc_lines() != DESK_CALC_LINES) {
		printf("FAIL lines: %d\n", desk_calc_lines());
		g_failed++;
	}
	/* what is being worked out, over the figures */
	static const struct { const char *keys, *working; } WORK[] = {
		{ "", "" }, { "12", "" }, { "12+", "12 +" }, { "12+3", "12 +" }, { "12+3=", "12 + 3 =" },
		{ "12+3=5", "" }, { "2R", "\xe2\x88\x9a 2 =" }, { "30S", "sin 30 =" }, { "12*", "12 \xc3\x97" },
		{ "1/0=", "" },
	};
	for (size_t i = 0; i < sizeof WORK / sizeof WORK[0]; i++) {
		char w[96];
		desk_calc_press(DESK_CK_AC);
		key(WORK[i].keys);
		desk_calc_working(w, sizeof w);
		if (strcmp(w, WORK[i].working)) {
			printf("FAIL working %s: \"%s\", not \"%s\"\n", WORK[i].keys, w, WORK[i].working);
			g_failed++;
		}
	}
	if (g_failed) {
		printf("%d failed\n", g_failed);
		return 1;
	}
	printf("all as they should be\n");
	return 0;
}
