#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/http/responses/date.h>

#include "../../test.h"

// the example of RFC 9110 section 5.6.7
#define TEST_DATE_EXAMPLE_TIME			784111777
static const char *test_date_example = { "Sun, 06 Nov 1994 08:49:37 GMT" };

// the first second of 2000, a leap year
#define TEST_DATE_LEAP_TIME				951782400
static const char *test_date_leap = { "Tue, 29 Feb 2000 00:00:00 GMT" };

static void test_date_format (void) {

	char date[HTTP_DATE_SIZE] = { 0 };

	test_check_unsigned_eq (http_date_format (TEST_DATE_EXAMPLE_TIME, date), HTTP_DATE_LENGTH, NULL);
	test_check_str_eq (date, test_date_example, NULL);

	test_check_unsigned_eq (http_date_format (TEST_DATE_LEAP_TIME, date), HTTP_DATE_LENGTH, NULL);
	test_check_str_eq (date, test_date_leap, NULL);

	test_check_unsigned_eq (http_date_format (TEST_DATE_EXAMPLE_TIME, NULL), 0, NULL);

}

// the date of the thread changes only once per second
static void test_date_current (void) {

	const time_t before = time (NULL);
	const char *date = http_date_current ();
	const time_t after = time (NULL);

	test_check_ptr (date);
	test_check_unsigned_eq (strlen (date), HTTP_DATE_LENGTH, NULL);

	time_t parsed = 0;
	test_check_true (http_date_parse (date, strlen (date), &parsed));
	test_check (((parsed >= before) && (parsed <= after)), "the current date is not now");

	test_check_ptr_eq (http_date_current (), date);

}

static void test_date_parse_valid (void) {

	time_t parsed = 0;
	test_check_true (http_date_parse (test_date_example, strlen (test_date_example), &parsed));
	test_check_long_int_eq ((long) parsed, (long) TEST_DATE_EXAMPLE_TIME, NULL);

	test_check_true (http_date_parse (test_date_leap, strlen (test_date_leap), &parsed));
	test_check_long_int_eq ((long) parsed, (long) TEST_DATE_LEAP_TIME, NULL);

}

// only IMF-fixdate is accepted, the obsolete formats and broken dates are ignored
static void test_date_parse_invalid (void) {

	static const char *invalid[] = {
		"Sunday, 06-Nov-94 08:49:37 GMT",
		"Sun Nov  6 08:49:37 1994",
		"Sun, 06 Nov 1994 08:49:37 UTC",
		"Sun, 06 Nov 1994 08:49:37 GM",
		"Sun, 06 Nov 1994 08:49:37 GMTX",
		"Sun, 32 Nov 1994 08:49:37 GMT",
		"Sun, 06 Nob 1994 08:49:37 GMT",
		"Sun, 06 Nov 1994 24:49:37 GMT",
		"Sun, 06 Nov 1994 08:60:37 GMT",
		"Sun, 06 Nov 1994 08:49:61 GMT",
		"Xyz, 06 Nov 1994 08:49:37 GMT",
		"Sun, 0a Nov 1994 08:49:37 GMT",
		"Sun,  6 Nov 1994 08:49:37 GMT",
		""
	};

	time_t parsed = 0;
	for (size_t i = 0; i < (sizeof (invalid) / sizeof (const char *)); i++) {
		test_check_false (http_date_parse (invalid[i], strlen (invalid[i]), &parsed));
	}

	test_check_false (http_date_parse (NULL, HTTP_DATE_LENGTH, &parsed));
	test_check_false (http_date_parse (test_date_example, HTTP_DATE_LENGTH, NULL));

	// a length that is not the one of the date
	test_check_false (http_date_parse (test_date_example, HTTP_DATE_LENGTH - 1, &parsed));

}

void http_tests_date (void) {

	(void) printf ("Testing HTTP date...\n");

	test_date_format ();

	test_date_current ();

	test_date_parse_valid ();

	test_date_parse_invalid ();

	(void) printf ("Done!\n");

}
