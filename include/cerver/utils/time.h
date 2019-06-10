#ifndef _CERVER_TIME_H_
#define _CERVER_TIME_H_

#ifndef __USE_POSIX199309
    #define __USE_POSIX199309
#endif

#include <time.h>
#include <errno.h>

typedef struct timespec TimeSpec;

extern TimeSpec getTimeSpec (void);

extern double timeElapsed (TimeSpec *start, TimeSpec *end);

extern void sleepFor (double seconds);

#endif