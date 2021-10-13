#ifndef _CERVER_TIMER_H_
#define _CERVER_TIMER_H_

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"

#define CERVER_TIMER_BUFFER_SIZE		128

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timespec TimeSpec;

CERVER_EXPORT void timespec_delete (void *timespec_ptr);

CERVER_EXPORT TimeSpec *timer_get_timespec (void);

CERVER_EXPORT double timer_elapsed_time (TimeSpec *start, TimeSpec *end);

CERVER_EXPORT void timer_sleep_for_seconds (double seconds);

CERVER_EXPORT double timer_get_current_time (void);

CERVER_EXPORT struct tm *timer_get_gmt_time (void);

CERVER_EXPORT struct tm *timer_get_local_time (void);

// 24h time representation
CERVER_EXPORT void timer_time_to_string_actual (
	const struct tm *timeinfo, char *buffer
);

// returns a string representing the 24h time
CERVER_EXPORT String *timer_time_to_string (
	const struct tm *timeinfo
);

// day/month/year time representation
CERVER_EXPORT void timer_date_to_string_actual (
	const struct tm *timeinfo, char *buffer
);

// returns a string with day/month/year
CERVER_EXPORT String *timer_date_to_string (
	const struct tm *timeinfo
);

// day/month/year - 24h time representation
CERVER_EXPORT void timer_date_and_time_to_string_actual (
	const struct tm *timeinfo, char *buffer
);

// returns a string with day/month/year - 24h time
CERVER_EXPORT String *timer_date_and_time_to_string (
	const struct tm *timeinfo
);

// returns a string representing the time with custom format
CERVER_EXPORT String *timer_time_to_string_custom (
	const struct tm *timeinfo, const char *format
);

#ifdef __cplusplus
}
#endif

#endif