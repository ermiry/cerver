#ifndef _CERVER_UTILS_HISTOGRAM_H_
#define _CERVER_UTILS_HISTOGRAM_H_

#include <stdint.h>

#include "cerver/config.h"

#include "cerver/types/types.h"

// buckets[i] counts the values up to 2^i,
// the last bucket counts the ones bigger than 2^HISTOGRAM_MAX_EXPONENT
#define HISTOGRAM_MAX_EXPONENT			20
#define HISTOGRAM_BUCKETS				(HISTOGRAM_MAX_EXPONENT + 2)

// the min of a histogram without values
#define HISTOGRAM_EMPTY_MIN				UINT64_MAX

#define HISTOGRAM_INITIALIZER			{ 0, 0, HISTOGRAM_EMPTY_MIN, 0, { 0 } }

#ifdef __cplusplus
extern "C" {
#endif

// the count, total, min, max and the distribution of a value
// (like microseconds or bytes), every field is updated with atomics,
// so a histogram can be added from many threads without a lock
typedef struct Histogram {

	u64 count;
	u64 total;
	u64 min;
	u64 max;

	u64 buckets[HISTOGRAM_BUCKETS];

} Histogram;

// sets the histogram without values
CERVER_PUBLIC void histogram_init (Histogram *histogram);

// returns the bucket of a value
CERVER_PUBLIC unsigned int histogram_bucket (const u64 value);

// adds a value to the histogram
CERVER_PUBLIC void histogram_add (Histogram *histogram, const u64 value);

// copies the values of the histogram with atomic reads
CERVER_PUBLIC void histogram_copy (
	Histogram *destination, const Histogram *source
);

// returns the mean of the values, 0 without values
CERVER_PUBLIC double histogram_mean (const Histogram *histogram);

// returns the min of the values, 0 without values
CERVER_PUBLIC u64 histogram_min (const Histogram *histogram);

// prints the histogram with the cerver log, with the unit of its values
CERVER_PUBLIC void histogram_print (
	const char *name, const char *unit, const Histogram *histogram
);

#ifdef __cplusplus
}
#endif

#endif
