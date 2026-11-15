/**
 * @file cerver/system/timer.h
 * @brief The times of cerver: the monotonic timer, the sleeps, the current time and its strings.
 */

#ifndef _CERVER_TIMER_H_
#define _CERVER_TIMER_H_

#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup system_timer Timer
 * @ingroup system
 * @brief Measuring, sleeping and formatting times.
 * @{
 */

/** @brief The size of the buffers of the *_actual () methods, with the NUL. */
#define CERVER_TIMER_BUFFER_SIZE		128

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A struct timespec. */
typedef struct timespec TimeSpec;

/**
 * @brief Releases a TimeSpec of timer_get_timespec ().
 *
 * @param[in] timespec_ptr the TimeSpec, it can be NULL
 */
CERVER_EXPORT void timespec_delete (void *timespec_ptr);

/**
 * @brief Returns the current time of CLOCK_MONOTONIC_RAW, to measure intervals.
 *
 * @return a new TimeSpec, released with timespec_delete (); NULL if it could not be allocated
 */
CERVER_EXPORT TimeSpec *timer_get_timespec (void);

/**
 * @brief Returns the seconds between two TimeSpec.
 *
 * @param[in] start the first time
 * @param[in] end the second time
 * @return end - start in seconds, 0 for a NULL argument
 */
CERVER_EXPORT double timer_elapsed_time (TimeSpec *start, TimeSpec *end);

/**
 * @brief Sleeps some seconds, continuing after a signal interrupts the sleep.
 *
 * @param[in] seconds the seconds, with their fraction
 */
CERVER_EXPORT void timer_sleep_for_seconds (double seconds);

/**
 * @brief Returns the current time of the day in seconds, with microseconds.
 *
 * @return the seconds since the epoch, 0 on error
 */
CERVER_EXPORT double timer_get_current_time (void);

/**
 * @brief Returns the current time in UTC.
 *
 * @return the time of gmtime (), in its static buffer
 * @threadsafety Not thread safe: the buffer of gmtime () is shared.
 * @deprecated Use timer_get_gmt_time_r (), which writes in a struct of the caller.
 */
DEPRECATED (CERVER_EXPORT struct tm *timer_get_gmt_time (void));

/**
 * @brief Returns the current local time.
 *
 * @return the time of localtime (), in its static buffer
 * @threadsafety Not thread safe: the buffer of localtime () is shared.
 * @deprecated Use timer_get_local_time_r (), which writes in a struct of the caller.
 */
DEPRECATED (CERVER_EXPORT struct tm *timer_get_local_time (void));

/**
 * @brief Writes the current time in UTC in a struct of the caller.
 *
 * @param[out] result the struct
 * @return result, NULL for a NULL result or on error
 * @threadsafety Safe from any thread, with gmtime_r ().
 * @since 1.9b-2
 */
CERVER_EXPORT struct tm *timer_get_gmt_time_r (struct tm *result);

/**
 * @brief Writes the current local time in a struct of the caller.
 *
 * @param[out] result the struct
 * @return result, NULL for a NULL result or on error
 * @threadsafety Safe from any thread, with localtime_r ().
 * @since 1.9b-2
 */
CERVER_EXPORT struct tm *timer_get_local_time_r (struct tm *result);

/**
 * @brief Writes the 24 h time, as "hh:mm:ss".
 *
 * @param[in] timeinfo the time, it must not be NULL
 * @param[out] buffer a buffer of #CERVER_TIMER_BUFFER_SIZE bytes
 */
CERVER_EXPORT void timer_time_to_string_actual (
	const struct tm *timeinfo, char *buffer
);

/**
 * @brief Returns the 24 h time, as "hh:mm:ss".
 *
 * @param[in] timeinfo the time
 * @return the new String, NULL for a NULL time
 */
CERVER_EXPORT String *timer_time_to_string (
	const struct tm *timeinfo
);

/**
 * @brief Writes the date, as "dd/mm/yy".
 *
 * @param[in] timeinfo the time, it must not be NULL
 * @param[out] buffer a buffer of #CERVER_TIMER_BUFFER_SIZE bytes
 */
CERVER_EXPORT void timer_date_to_string_actual (
	const struct tm *timeinfo, char *buffer
);

/**
 * @brief Returns the date, as "dd/mm/yy".
 *
 * @param[in] timeinfo the time
 * @return the new String, NULL for a NULL time
 */
CERVER_EXPORT String *timer_date_to_string (
	const struct tm *timeinfo
);

/**
 * @brief Writes the date and the 24 h time, as "dd/mm/yy - hh:mm:ss".
 *
 * @param[in] timeinfo the time, it must not be NULL
 * @param[out] buffer a buffer of #CERVER_TIMER_BUFFER_SIZE bytes
 */
CERVER_EXPORT void timer_date_and_time_to_string_actual (
	const struct tm *timeinfo, char *buffer
);

/**
 * @brief Returns the date and the 24 h time, as "dd/mm/yy - hh:mm:ss".
 *
 * @param[in] timeinfo the time
 * @return the new String, NULL for a NULL time
 */
CERVER_EXPORT String *timer_date_and_time_to_string (
	const struct tm *timeinfo
);

/**
 * @brief Returns the time with a format of strftime (), of at most CERVER_TIMER_BUFFER_SIZE - 1 characters.
 *
 * @param[in] timeinfo the time
 * @param[in] format the format of strftime (), it must not be NULL
 * @return the new String, NULL for a NULL time
 */
CERVER_EXPORT String *timer_time_to_string_custom (
	const struct tm *timeinfo, const char *format
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
