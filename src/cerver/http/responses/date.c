#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cerver/http/responses/date.h"

static const char *http_date_days[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *http_date_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

#define HTTP_DATE_DAYS				7
#define HTTP_DATE_MONTHS			12
#define HTTP_DATE_NAME_LENGTH		3

// tm_year counts the years since 1900
#define HTTP_DATE_TM_YEAR_BASE		1900

#define HTTP_DATE_FORMAT_STRING		"%s, %02d %s %04d %02d:%02d:%02d GMT"

#define HTTP_DATE_DECIMAL_BASE		10

// the positions of the fields of an IMF-fixdate, "Sun, 06 Nov 1994 08:49:37 GMT"
#define HTTP_DATE_DAY_NAME_POSITION		0
#define HTTP_DATE_COMMA_POSITION		3
#define HTTP_DATE_DAY_POSITION			5
#define HTTP_DATE_MONTH_POSITION		8
#define HTTP_DATE_YEAR_POSITION			12
#define HTTP_DATE_HOURS_POSITION		17
#define HTTP_DATE_MINUTES_POSITION		20
#define HTTP_DATE_SECONDS_POSITION		23
#define HTTP_DATE_ZONE_POSITION			26

#define HTTP_DATE_TWO_DIGITS			2
#define HTTP_DATE_FOUR_DIGITS			4

static const char *http_date_zone = { "GMT" };

static const char http_date_space = ' ';
static const char http_date_time_separator = ':';
static const char http_date_comma = ',';

#define HTTP_DATE_MAX_HOURS				23
#define HTTP_DATE_MAX_MINUTES			59
// a leap second
#define HTTP_DATE_MAX_SECONDS			60
#define HTTP_DATE_MAX_DAY				31

size_t http_date_format (const time_t seconds, char *date) {

	size_t length = 0;

	struct tm parts;
	(void) memset (&parts, 0, sizeof (struct tm));
	if (date && gmtime_r (&seconds, &parts)) {
		const int written = snprintf (
			date, HTTP_DATE_SIZE, HTTP_DATE_FORMAT_STRING,
			http_date_days[parts.tm_wday % HTTP_DATE_DAYS],
			parts.tm_mday,
			http_date_months[parts.tm_mon % HTTP_DATE_MONTHS],
			parts.tm_year + HTTP_DATE_TM_YEAR_BASE,
			parts.tm_hour, parts.tm_min, parts.tm_sec
		);

		if (written == HTTP_DATE_LENGTH) length = (size_t) written;
	}

	return length;

}

// the date of the current second of each thread
static __thread time_t http_date_current_time = 0;
static __thread char http_date_current_value[HTTP_DATE_SIZE] = { 0 };

const char *http_date_current (void) {

	const time_t now = time (NULL);
	if ((now != http_date_current_time) || !http_date_current_value[0]) {
		if (http_date_format (now, http_date_current_value)) {
			http_date_current_time = now;
		}
	}

	return http_date_current_value;

}

// returns the index of the name in the names, -1 if it is not there
static int http_date_parse_name (
	const char *value, const char **names, const int n_names
) {

	int index = -1;

	for (int i = 0; i < n_names; i++) {
		if (!strncmp (value, names[i], HTTP_DATE_NAME_LENGTH)) {
			index = i;
			break;
		}
	}

	return index;

}

// returns the number of the digits, -1 if any of them is not a digit
static int http_date_parse_number (const char *value, const int n_digits) {

	int number = 0;

	for (int i = 0; i < n_digits; i++) {
		if (!isdigit ((unsigned char) value[i])) return -1;
		number = (number * HTTP_DATE_DECIMAL_BASE) + (value[i] - '0');
	}

	return number;

}

static bool http_date_parse_separators (const char *value) {

	return (value[HTTP_DATE_COMMA_POSITION] == http_date_comma)
		&& (value[HTTP_DATE_COMMA_POSITION + 1] == http_date_space)
		&& (value[HTTP_DATE_MONTH_POSITION - 1] == http_date_space)
		&& (value[HTTP_DATE_YEAR_POSITION - 1] == http_date_space)
		&& (value[HTTP_DATE_HOURS_POSITION - 1] == http_date_space)
		&& (value[HTTP_DATE_MINUTES_POSITION - 1] == http_date_time_separator)
		&& (value[HTTP_DATE_SECONDS_POSITION - 1] == http_date_time_separator)
		&& (value[HTTP_DATE_ZONE_POSITION - 1] == http_date_space)
		&& !strncmp (value + HTTP_DATE_ZONE_POSITION, http_date_zone, HTTP_DATE_NAME_LENGTH);

}

static bool http_date_parse_parts (const char *value, struct tm *parts) {

	const int day = http_date_parse_number (value + HTTP_DATE_DAY_POSITION, HTTP_DATE_TWO_DIGITS);
	const int month = http_date_parse_name (
		value + HTTP_DATE_MONTH_POSITION, http_date_months, HTTP_DATE_MONTHS
	);

	const int year = http_date_parse_number (value + HTTP_DATE_YEAR_POSITION, HTTP_DATE_FOUR_DIGITS);
	const int hours = http_date_parse_number (value + HTTP_DATE_HOURS_POSITION, HTTP_DATE_TWO_DIGITS);
	const int minutes = http_date_parse_number (value + HTTP_DATE_MINUTES_POSITION, HTTP_DATE_TWO_DIGITS);
	const int seconds = http_date_parse_number (value + HTTP_DATE_SECONDS_POSITION, HTTP_DATE_TWO_DIGITS);

	const bool valid = (day >= 1) && (day <= HTTP_DATE_MAX_DAY)
		&& (month >= 0)
		&& (year >= HTTP_DATE_TM_YEAR_BASE)
		&& (hours >= 0) && (hours <= HTTP_DATE_MAX_HOURS)
		&& (minutes >= 0) && (minutes <= HTTP_DATE_MAX_MINUTES)
		&& (seconds >= 0) && (seconds <= HTTP_DATE_MAX_SECONDS)
		&& (http_date_parse_name (value + HTTP_DATE_DAY_NAME_POSITION, http_date_days, HTTP_DATE_DAYS) >= 0);

	if (valid) {
		parts->tm_mday = day;
		parts->tm_mon = month;
		parts->tm_year = year - HTTP_DATE_TM_YEAR_BASE;
		parts->tm_hour = hours;
		parts->tm_min = minutes;
		parts->tm_sec = seconds;
	}

	return valid;

}

bool http_date_parse (
	const char *value, const size_t length, time_t *seconds
) {

	bool parsed = false;

	if (value && seconds && (length == HTTP_DATE_LENGTH) && http_date_parse_separators (value)) {
		struct tm parts;
		(void) memset (&parts, 0, sizeof (struct tm));
		if (http_date_parse_parts (value, &parts)) {
			*seconds = timegm (&parts);
			parsed = true;
		}
	}

	return parsed;

}
