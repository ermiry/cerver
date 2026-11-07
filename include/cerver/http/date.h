#ifndef _CERVER_HTTP_DATE_H_
#define _CERVER_HTTP_DATE_H_

#include <stdbool.h>
#include <stddef.h>

#include <time.h>

#include "cerver/config.h"

// an IMF-fixdate (RFC 9110 section 5.6.7), "Sun, 06 Nov 1994 08:49:37 GMT"
#define HTTP_DATE_LENGTH				29

// the length of a date and its '\0'
#define HTTP_DATE_SIZE					(HTTP_DATE_LENGTH + 1)

#ifdef __cplusplus
extern "C" {
#endif

// writes the time as an IMF-fixdate in date,
// that must have at least HTTP_DATE_SIZE bytes,
// the names of the days and months never depend on the locale
// returns the length of the date, 0 on error
CERVER_PUBLIC size_t http_date_format (
	const time_t seconds, char *date
);

// the current time as an IMF-fixdate,
// it is formatted once per second by each thread
// the returned date belongs to the thread and it is valid until its next call
CERVER_PUBLIC const char *http_date_current (void);

// parses an IMF-fixdate, the only format that cerver accepts
// in the conditional requests (If-Modified-Since)
// returns true on success, and sets the parsed time
CERVER_PUBLIC bool http_date_parse (
	const char *value, const size_t length, time_t *seconds
);

#ifdef __cplusplus
}
#endif

#endif
