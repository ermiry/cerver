#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/String.h"

#include "cerver/time.h"

static TimeSpec *timespec_new (void) {

    TimeSpec *t = (TimeSpec *) malloc (sizeof (TimeSpec));
    if (t) {
        t->tv_nsec = 0;
        t->tv_sec = 0;
    }

    return t;

}

void timespec_delete (void *timespec_ptr) { if (timespec_ptr) free (timespec_ptr); }

TimeSpec *timer_get_timespec (void) {

    TimeSpec *t = timespec_new ();
    if (t) {
        clock_gettime (4, t);
    }

    return t;

}

double timer_elapsed_time (TimeSpec *start, TimeSpec *end) {

    return (start && end) ? (double) end->tv_sec + (double) end->tv_nsec / 1000000000
        - (double) start->tv_sec - (double) start->tv_nsec / 1000000000 : 0;

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

struct tm *timer_get_gmt_time (void) {

    time_t rawtime = 0;
    time (&rawtime);
    return gmtime (&rawtime);

}

struct tm *timer_get_local_time (void) {

    time_t rawtime = 0;
    time (&rawtime);
    return localtime (&rawtime);

}

// returns a string representing the 24h time 
String *timer_time_to_string (struct tm *timeinfo) {

    if (timeinfo) {
        char buffer[128] = { 0 };
        strftime (buffer, 128, "%T", timeinfo);
        return str_new (buffer);
    }

    return NULL;

}

// returns a string with day/month/year
String *timer_date_to_string (struct tm *timeinfo) {

    if (timeinfo) {
        char buffer[128] = { 0 };
        strftime (buffer, 128, "%d/%m/%y", timeinfo);
        return str_new (buffer);
    }

    return NULL;

}

// returns a string with day/month/year - 24h time
String *timer_date_and_time_to_string (struct tm *timeinfo) {

    if (timeinfo) {
        char buffer[128] = { 0 };
        strftime (buffer, 128, "%d/%m/%y - %T", timeinfo);
        return str_new (buffer);
    }

    return NULL;

}

// returns a string representing the time with custom format
String *timer_time_to_string_custom (struct tm *timeinfo, const char *format) {

    if (timeinfo) {
        char buffer[128] = { 0 };
        strftime (buffer, 128, format, timeinfo);
        return str_new (buffer);
    }
    
    return NULL;

}