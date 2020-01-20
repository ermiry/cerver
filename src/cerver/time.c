#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

struct tm *timer_get_gmt_time (void) {

    time_t rawtime;
    time (&rawtime);
    return gmtime (&rawtime);

}

struct tm *timer_get_local_time (void) {

    time_t rawtime;
    time (&rawtime);
    return localtime (&rawtime);

}

// returns a string representing the 24h time 
estring *timer_time_to_string (struct tm *timeinfo) {

    if (timeinfo) {
        char buffer[80];
        strftime (buffer, sizeof (buffer), "%T", timeinfo);
        return estring_new (buffer);
    }

    return NULL;

}

// returns a string with day/month/year
estring *timer_date_to_string (struct tm *timeinfo) {

    if (timeinfo) {
        char buffer[80];
        strftime (buffer, sizeof (buffer), "%d/%m/%y", timeinfo);
        return estring_new (buffer);
    }

    return NULL;

}

// returns a string with day/month/year - 24h time
estring *timer_date_and_time_to_string (struct tm *timeinfo) {

    if (timeinfo) {
        char buffer[80];
        strftime (buffer, sizeof (buffer), "%d/%m/%y - %T", timeinfo);
        return estring_new (buffer);
    }

    return NULL;

}

// returns a string representing the time with custom format
estring *timer_time_to_string_custom (struct tm *timeinfo, const char *format) {

    if (timeinfo) {
        char buffer[80];
        strftime (buffer, sizeof (buffer), format, timeinfo);
        return estring_new (buffer);
    }
    
    return NULL;

}