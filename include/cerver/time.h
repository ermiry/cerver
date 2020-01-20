#ifndef _CERVER_TIME_H_
#define _CERVER_TIME_H_

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

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