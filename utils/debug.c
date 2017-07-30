/*
 * debug routines for uftrace
 *
 * Copyright (C) 2014-2017, LG Electronics, Namhyung Kim <namhyung@gmail.com>
 *
 * Released under the GPL v2.
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <inttypes.h>

#include "utils/utils.h"

#define TERM_COLOR_NORMAL	""
#define TERM_COLOR_RESET	"\033[0m"
#define TERM_COLOR_BOLD		"\033[1m"
#define TERM_COLOR_RED		"\033[1;31m"  /* bright red */
#define TERM_COLOR_GREEN	"\033[32m"
#define TERM_COLOR_YELLOW	"\033[33m"
#define TERM_COLOR_BLUE		"\033[1;34m"  /* bright blue */
#define TERM_COLOR_MAGENTA	"\033[35m"
#define TERM_COLOR_CYAN		"\033[36m"
#define TERM_COLOR_GRAY		"\033[37m"

int debug;
FILE *logfp;
FILE *outfp;
enum color_setting log_color;
enum color_setting out_color;
int dbg_domain[DBG_DOMAIN_MAX];

static const struct color_code {
	char		code;
	const char	*color;
} colors[] = {
	{ COLOR_CODE_RED,	TERM_COLOR_RED },
	{ COLOR_CODE_GREEN,	TERM_COLOR_GREEN },
	{ COLOR_CODE_BLUE,	TERM_COLOR_BLUE },
	{ COLOR_CODE_YELLOW,	TERM_COLOR_YELLOW },
	{ COLOR_CODE_MAGENTA,	TERM_COLOR_MAGENTA },
	{ COLOR_CODE_CYAN,	TERM_COLOR_CYAN },
	{ COLOR_CODE_GRAY,	TERM_COLOR_GRAY },
	{ COLOR_CODE_BOLD,	TERM_COLOR_BOLD },
};

static void color(const char *code, FILE *fp)
{
	size_t len = strlen(code);

	if ((fp == logfp && log_color == COLOR_OFF) ||
	    (fp == outfp && out_color == COLOR_OFF))
		return;

	if (fwrite(code, 1, len, fp) == len)
		return;  /* ok */

	/* disable color */
	log_color = COLOR_OFF;
	out_color = COLOR_OFF;

	len = sizeof(TERM_COLOR_RESET) - 1;
	if (fwrite(TERM_COLOR_RESET, 1, len, fp) != len)
		pr_err("resetting terminal color failed");
}

void setup_color(enum color_setting color)
{
	if (likely(color == COLOR_AUTO)) {
		log_color = isatty(fileno(logfp)) ? COLOR_ON : COLOR_OFF;
		out_color = isatty(fileno(outfp)) ? COLOR_ON : COLOR_OFF;
	}
	else {
		log_color = color;
		out_color = color;
	}
}

void __pr_dbg(const char *fmt, ...)
{
	va_list ap;

	color(TERM_COLOR_GRAY, logfp);

	va_start(ap, fmt);
	vfprintf(logfp, fmt, ap);
	va_end(ap);

	color(TERM_COLOR_RESET, logfp);
}

void __pr_err(const char *fmt, ...)
{
	va_list ap;

	color(TERM_COLOR_RED, logfp);

	va_start(ap, fmt);
	vfprintf(logfp, fmt, ap);
	va_end(ap);

	color(TERM_COLOR_RESET, logfp);

	exit(1);
}

void __pr_err_s(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;
	char buf[512];

	color(TERM_COLOR_RED, logfp);

	va_start(ap, fmt);
	vfprintf(logfp, fmt, ap);
	va_end(ap);

	fprintf(logfp, ": %s\n", strerror_r(saved_errno, buf, sizeof(buf)));

	color(TERM_COLOR_RESET, logfp);

	exit(1);
}

void __pr_warn(const char *fmt, ...)
{
	va_list ap;

	color(TERM_COLOR_YELLOW, logfp);

	va_start(ap, fmt);
	vfprintf(logfp, fmt, ap);
	va_end(ap);

	color(TERM_COLOR_RESET, logfp);
}

void __pr_out(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(outfp, fmt, ap);
	va_end(ap);
}

void __pr_color(char code, const char *fmt, ...)
{
	size_t i;
	va_list ap;
	const char *cs = TERM_COLOR_NORMAL;

	for (i = 0; i < ARRAY_SIZE(colors); i++) {
		if (code == colors[i].code)
			cs = colors[i].color;
	}

	color(cs, outfp);

	va_start(ap, fmt);
	vfprintf(outfp, fmt, ap);
	va_end(ap);

	color(TERM_COLOR_RESET, outfp);
}

static void __print_time_unit(int64_t delta_nsec, bool needs_sign)
{
	uint64_t delta = abs(delta_nsec);
	uint64_t delta_small = 0;
	char *units[] = { "us", "ms", " s", " m", " h", };
	char *color_units[] = {
		TERM_COLOR_NORMAL "us" TERM_COLOR_RESET,
		TERM_COLOR_GREEN  "ms" TERM_COLOR_RESET,
		TERM_COLOR_YELLOW " s" TERM_COLOR_RESET,
		TERM_COLOR_RED    " m" TERM_COLOR_RESET,
		TERM_COLOR_RED    " h" TERM_COLOR_RESET,
	};
	char *unit;
	unsigned limit[] = { 1000, 1000, 1000, 60, 24, INT_MAX, };
	unsigned idx;

	if (delta_nsec == 0UL) {
		if (needs_sign)
			pr_out(" ");
		pr_out("%7s %2s", "", "");
		return;
	}

	for (idx = 0; idx < ARRAY_SIZE(units); idx++) {
		delta_small = delta % limit[idx];
		delta = delta / limit[idx];

		if (delta < limit[idx+1])
			break;
	}

	assert(idx < ARRAY_SIZE(units));

	/* for some error cases */
	if (delta > 999)
		delta = delta_small = 999;

	if (out_color == COLOR_ON)
		unit = color_units[idx];
	else
		unit = units[idx];

	if (needs_sign) {
		const char *signs[] = { "+", "-" };
		const char *color_signs[] = {
			TERM_COLOR_RED     "+",
			TERM_COLOR_MAGENTA "+",
			TERM_COLOR_NORMAL  "+",
			TERM_COLOR_BLUE    "-",
			TERM_COLOR_CYAN    "-",
			TERM_COLOR_NORMAL  "-",
		};
		int sign_idx = (delta_nsec > 0);
		int indent = (delta >= 100) ? 0 : (delta >= 10) ? 1 : 2;
		const char *sign = signs[sign_idx];
		const char *ends = TERM_COLOR_NORMAL;

		if (out_color == COLOR_ON) {
			if (delta_nsec >= 100000)
				sign_idx = 0;
			else if (delta_nsec >= 5000)
				sign_idx = 1;
			else if (delta_nsec > 0)
				sign_idx = 2;
			else if (delta_nsec <= -100000)
				sign_idx = 3;
			else if (delta_nsec <= -5000)
				sign_idx = 4;
			else
				sign_idx = 5;

			sign = color_signs[sign_idx];
			ends = TERM_COLOR_RESET;
		}

		pr_out("%*s%s%"PRId64".%03"PRIu64"%s %s", indent, "",
		       sign, delta, delta_small, ends, unit);
	}
	else
		pr_out("%3"PRIu64".%03"PRIu64" %s", delta, delta_small, unit);
}

void print_time_unit(uint64_t delta_nsec)
{
	__print_time_unit(delta_nsec, false);
}

void print_diff_percent(uint64_t base_nsec, uint64_t pair_nsec)
{
	double percent = 999.99;
	const char *sc = TERM_COLOR_NORMAL;
	const char *ec = TERM_COLOR_NORMAL;

	if (base_nsec)
		percent = 100.0 * (int64_t)(pair_nsec - base_nsec) / base_nsec;

	/* for some error cases */
	if (percent > 999.99)
		percent = 999.99;
	else if (percent < -999.99)
		percent = -999.99;

	if (out_color == COLOR_ON) {
		sc = percent > 30 ? TERM_COLOR_RED :
			percent > 3 ? TERM_COLOR_MAGENTA :
			percent < -30 ? TERM_COLOR_BLUE :
			percent < -3 ? TERM_COLOR_CYAN : TERM_COLOR_NORMAL;
		ec = TERM_COLOR_RESET;
	}

	pr_out("%s%+7.2f%s%%", sc, percent, ec);
}

void print_diff_time_unit(uint64_t base_nsec, uint64_t pair_nsec)
{
	if (base_nsec == pair_nsec)
		pr_out("%11s", "0 us");
	else
		__print_time_unit(pair_nsec - base_nsec, true);
}
