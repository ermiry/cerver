#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <errno.h>

#include <sys/time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/timer.h"

static TimeSpec *timespec_new (void) {

	TimeSpec *t = (TimeSpec *) malloc (sizeof (TimeSpec));
	if (t) {
		t->tv_nsec = 0;
		t->tv_sec = 0;
	}

	return t;

}

void timespec_delete (void *timespec_ptr) {

	if (timespec_ptr) free (timespec_ptr);

}

TimeSpec *timer_get_timespec (void) {

	TimeSpec *t = timespec_new ();
	if (t) {
		clock_gettime (4, t);
	}

	return t;

}

double timer_elapsed_time (TimeSpec *start, TimeSpec *end) {

	return (start && end) ?
		(double) end->tv_sec + (double) end->tv_nsec / 1000000000
		- (double) start->tv_sec - (double) start->tv_nsec / 1000000000
		: 0;

}

void timer_sleep_for_seconds (double seconds) {

	TimeSpec timespc = { 0 };
	timespc.tv_sec = (time_t) seconds;
	timespc.tv_nsec = (long) ((seconds - timespc.tv_sec) * 1e+9);

	int result;
	do {
		result = nanosleep (&timespc, &timespc);
	} while (result == -1 && errno == EINTR);

}

double timer_get_current_time (void) {

	struct timeval time = { 0 };
	return !gettimeofday (&time, NULL) ?
		(double) time.tv_sec + (double) time.tv_usec * .000001 : 0;

}

struct tm *timer_get_gmt_time (void) {

	time_t rawtime = 0;
	(void) time (&rawtime);
	return gmtime (&rawtime);

}

struct tm *timer_get_local_time (void) {

	time_t rawtime = 0;
	(void) time (&rawtime);
	return localtime (&rawtime);

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

// 24h time representation
void timer_time_to_string_actual (
	const struct tm *timeinfo, char *buffer
) {

	(void) strftime (buffer, CERVER_TIMER_BUFFER_SIZE, "%T", timeinfo);

}

// returns a string representing the 24h time
String *timer_time_to_string (
	const struct tm *timeinfo
) {

	char buffer[CERVER_TIMER_BUFFER_SIZE] = { 0 };

	String *retval = NULL;

	if (timeinfo) {
		timer_time_to_string_actual (timeinfo, buffer);
		retval = str_new (buffer);
	}

	return retval;

}

// day/month/year time representation
void timer_date_to_string_actual (
	const struct tm *timeinfo, char *buffer
) {

	(void) strftime (buffer, CERVER_TIMER_BUFFER_SIZE, "%d/%m/%y", timeinfo);

}

// returns a string with day/month/year
String *timer_date_to_string (
	const struct tm *timeinfo
) {

	char buffer[CERVER_TIMER_BUFFER_SIZE] = { 0 };

	String *retval = NULL;

	if (timeinfo) {
		timer_date_to_string_actual (timeinfo, buffer);
		
		retval = str_new (buffer);
	}

	return retval;

}

// day/month/year - 24h time representation
void timer_date_and_time_to_string_actual (
	const struct tm *timeinfo, char *buffer
) {

	(void) strftime (buffer, CERVER_TIMER_BUFFER_SIZE, "%d/%m/%y - %T", timeinfo);

}

// returns a string with day/month/year - 24h time
String *timer_date_and_time_to_string (
	const struct tm *timeinfo
) {

	char buffer[CERVER_TIMER_BUFFER_SIZE] = { 0 };

	String *retval = NULL;

	if (timeinfo) {
		timer_date_and_time_to_string_actual (timeinfo, buffer);
		
		retval = str_new (buffer);
	}

	return retval;

}

// returns a string representing the time with custom format
String *timer_time_to_string_custom (
	const struct tm *timeinfo, const char *format
) {

	char buffer[CERVER_TIMER_BUFFER_SIZE] = { 0 };

	String *retval = NULL;

	if (timeinfo) {
		(void) strftime (buffer, CERVER_TIMER_BUFFER_SIZE, format, timeinfo);
		retval = str_new (buffer);
	}

	return retval;

}

#pragma GCC diagnostic pop