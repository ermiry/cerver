#ifndef _CERVER_TIME_H_
#define _CERVER_TIME_H_

#ifndef __USE_POSIX199309
    #define __USE_POSIX199309
#endif

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

typedef struct timespec TimeSpec;

extern void timespec_delete (void *timespec_ptr);

extern TimeSpec *timer_get_timespec (void);

extern double timer_elapsed_time (TimeSpec *start, TimeSpec *end);

extern void timer_sleep_for_seconds (double seconds);

extern struct tm *timer_get_gmt_time (void);

extern struct tm *timer_get_local_time (void);

// returns a string representing the 24h time 
extern estring *timer_time_to_string (struct tm *timeinfo);

// returns a string with day/month/year
extern estring *timer_date_to_string (struct tm *timeinfo);

// returns a string with day/month/year - 24h time
extern estring *timer_date_and_time_to_string (struct tm *timeinfo);

// returns a string representing the time with custom format
extern estring *timer_time_to_string_custom (struct tm *timeinfo, const char *format);

#endif