#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/timer.h>

#include "../test.h"

#define TIMER_TEST_YEAR				126
#define TIMER_TEST_MONTH			10
#define TIMER_TEST_DAY				5
#define TIMER_TEST_HOUR				13
#define TIMER_TEST_MINUTE			45
#define TIMER_TEST_SECOND			9

#define TIMER_TEST_START_SECONDS	1
#define TIMER_TEST_START_NANOS		500000000L
#define TIMER_TEST_END_SECONDS		3
#define TIMER_TEST_END_NANOS		250000000L
#define TIMER_TEST_ELAPSED			1.75
#define TIMER_TEST_PRECISION		0.000001

#define TIMER_TEST_SLEEP			0.01

static const char *expected_time = { "13:45:09" };
static const char *expected_date = { "05/11/26" };
static const char *expected_date_and_time = { "05/11/26 - 13:45:09" };
static const char *year_format = { "%Y" };
static const char *expected_year = { "2026" };

static struct tm timer_test_tm (void) {

	struct tm timeinfo;
	(void) memset (&timeinfo, 0, sizeof (struct tm));

	timeinfo.tm_year = TIMER_TEST_YEAR;
	timeinfo.tm_mon = TIMER_TEST_MONTH;
	timeinfo.tm_mday = TIMER_TEST_DAY;
	timeinfo.tm_hour = TIMER_TEST_HOUR;
	timeinfo.tm_min = TIMER_TEST_MINUTE;
	timeinfo.tm_sec = TIMER_TEST_SECOND;

	return timeinfo;

}

static void test_timer_strings (void) {

	const struct tm timeinfo = timer_test_tm ();

	String *time_string = timer_time_to_string (&timeinfo);
	test_check_str_eq (time_string->str, expected_time, NULL);
	str_delete (time_string);

	String *date_string = timer_date_to_string (&timeinfo);
	test_check_str_eq (date_string->str, expected_date, NULL);
	str_delete (date_string);

	String *both = timer_date_and_time_to_string (&timeinfo);
	test_check_str_eq (both->str, expected_date_and_time, NULL);
	str_delete (both);

	String *custom = timer_time_to_string_custom (&timeinfo, year_format);
	test_check_str_eq (custom->str, expected_year, NULL);
	str_delete (custom);

	test_check_null_ptr (timer_time_to_string_custom (NULL, year_format));

}

static void test_timer_elapsed (void) {

	TimeSpec start = { TIMER_TEST_START_SECONDS, TIMER_TEST_START_NANOS };
	TimeSpec end = { TIMER_TEST_END_SECONDS, TIMER_TEST_END_NANOS };

	const double elapsed = timer_elapsed_time (&start, &end);
	test_check ((elapsed > (TIMER_TEST_ELAPSED - TIMER_TEST_PRECISION)) && (elapsed < (TIMER_TEST_ELAPSED + TIMER_TEST_PRECISION)), NULL);

	test_check (timer_elapsed_time (NULL, &end) < TIMER_TEST_PRECISION, NULL);

	// a real sleep is measured by the monotonic clock
	TimeSpec *before = timer_get_timespec ();
	timer_sleep_for_seconds (TIMER_TEST_SLEEP);
	TimeSpec *after = timer_get_timespec ();

	test_check (timer_elapsed_time (before, after) >= TIMER_TEST_SLEEP, "the sleep was not measured");

	timespec_delete (before);
	timespec_delete (after);

}

void utils_tests_timer (void) {

	(void) printf ("Testing UTILS timer...\n");

	test_timer_strings ();
	test_timer_elapsed ();

	(void) printf ("Done!\n");

}
