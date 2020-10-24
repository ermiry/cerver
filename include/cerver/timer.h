#ifndef _CERVER_TIME_H_
#define _CERVER_TIME_H_

#ifndef __USE_POSIX199309
	#define __USE_POSIX199309
#endif

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"

typedef struct timespec TimeSpec;

CERVER_EXPORT void timespec_delete (void *timespec_ptr);

CERVER_EXPORT TimeSpec *timer_get_timespec (void);

CERVER_EXPORT double timer_elapsed_time (TimeSpec *start, TimeSpec *end);

CERVER_EXPORT void timer_sleep_for_seconds (double seconds);

CERVER_EXPORT struct tm *timer_get_gmt_time (void);

CERVER_EXPORT struct tm *timer_get_local_time (void);

// returns a string representing the 24h time 
CERVER_EXPORT String *timer_time_to_string (struct tm *timeinfo);

// returns a string with day/month/year
CERVER_EXPORT String *timer_date_to_string (struct tm *timeinfo);

// returns a string with day/month/year - 24h time
CERVER_EXPORT String *timer_date_and_time_to_string (struct tm *timeinfo);

// returns a string representing the time with custom format
CERVER_EXPORT String *timer_time_to_string_custom (struct tm *timeinfo, const char *format);

#endif