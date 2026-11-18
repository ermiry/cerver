/**
 * @file cerver/http/responses/date.h
 * @brief The HTTP dates (IMF-fixdate) of the Date and the Last-Modified headers.
 */

#ifndef _CERVER_HTTP_DATE_H_
#define _CERVER_HTTP_DATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "cerver/config.h"

/**
 * @defgroup http_responses_date Date
 * @ingroup http_responses
 * @brief Formatting and parsing the IMF-fixdate of RFC 9110 section 5.6.7.
 * @{
 */

/** @brief The length of an IMF-fixdate, like "Sun, 06 Nov 1994 08:49:37 GMT". */
#define HTTP_DATE_LENGTH				29

/** @brief The size of a date, with its terminator. */
#define HTTP_DATE_SIZE					(HTTP_DATE_LENGTH + 1)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes a time as an IMF-fixdate, in UTC.
 *
 * The names of the days and the months never depend on the locale.
 *
 * @param[in] seconds the time
 * @param[out] date where the date is written, at least #HTTP_DATE_SIZE bytes
 * @return the length of the date (#HTTP_DATE_LENGTH), 0 on error
 */
CERVER_PUBLIC size_t cerver_http_date_format (
	const time_t seconds, char *date
);

/**
 * @brief Returns the current time as an IMF-fixdate.
 *
 * It is formatted once per second by each thread.
 *
 * @return the date, that belongs to the thread and is valid until its next call
 * @threadsafety Safe: each thread has its own date.
 */
CERVER_PUBLIC const char *cerver_http_date_current (void);

/**
 * @brief Parses an IMF-fixdate, the only format that cerver accepts in the conditional requests
 * (If-Modified-Since).
 *
 * @param[in] value the date
 * @param[in] length the length of the date, it must be #HTTP_DATE_LENGTH
 * @param[out] seconds the parsed time
 * @return true on success; false for a NULL argument or a date that is not an IMF-fixdate
 */
CERVER_PUBLIC bool cerver_http_date_parse (
	const char *value, const size_t length, time_t *seconds
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
