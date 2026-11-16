/**
 * @file cerver/utils/histogram.h
 * @brief A histogram of a value, like microseconds or bytes, that many threads add to without a lock.
 */

#ifndef _CERVER_UTILS_HISTOGRAM_H_
#define _CERVER_UTILS_HISTOGRAM_H_

#include <stdint.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup utils_histogram Histogram
 * @ingroup utils
 * @brief The count, the total, the min, the max and the distribution of a value.
 * @{
 */

/** @brief buckets[i] counts the values up to 2^i, the last bucket the ones bigger than 2^HISTOGRAM_MAX_EXPONENT. */
#define HISTOGRAM_MAX_EXPONENT			20

/** @brief The number of buckets: one for each exponent and the last one. */
#define HISTOGRAM_BUCKETS				(HISTOGRAM_MAX_EXPONENT + 2)

/** @brief The min of a histogram without values. */
#define HISTOGRAM_EMPTY_MIN				UINT64_MAX

/** @brief Initializes a Histogram without values, like histogram_init (). */
#define HISTOGRAM_INITIALIZER			{ 0, 0, HISTOGRAM_EMPTY_MIN, 0, { 0 } }

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The count, total, min, max and the distribution of a value.
 *
 * @threadsafety histogram_add () updates every field with relaxed atomics, so a histogram can be added from many
 * threads without a lock; histogram_copy () reads a copy of it.
 */
typedef struct Histogram {

	/** @brief The values that were added. */
	u64 count;
	/** @brief The sum of the values. */
	u64 total;
	/** @brief The smallest value, #HISTOGRAM_EMPTY_MIN without values (see histogram_min ()). */
	u64 min;
	/** @brief The biggest value. */
	u64 max;

	/** @brief The values of each bucket, see histogram_bucket (). */
	u64 buckets[HISTOGRAM_BUCKETS];

} Histogram;

/**
 * @brief Sets a histogram without values.
 *
 * @param[out] histogram the histogram, it can be NULL
 */
CERVER_PUBLIC void histogram_init (Histogram *histogram);

/**
 * @brief Returns the bucket of a value: the smallest i with value <= 2^i, or the last bucket.
 *
 * @param[in] value the value
 * @return the bucket, from 0 to HISTOGRAM_BUCKETS - 1
 */
CERVER_PUBLIC unsigned int histogram_bucket (const u64 value);

/**
 * @brief Adds a value to a histogram.
 *
 * @param[in,out] histogram the histogram, it can be NULL
 * @param[in] value the value
 * @threadsafety Safe from many threads at the same time.
 */
CERVER_PUBLIC void histogram_add (Histogram *histogram, const u64 value);

/**
 * @brief Copies the values of a histogram, reading each field with an atomic load.
 *
 * @param[out] destination the copy
 * @param[in] source the histogram
 * @note Each field is read on its own, so a copy made while values are added can mix them.
 */
CERVER_PUBLIC void histogram_copy (
	Histogram *destination, const Histogram *source
);

/**
 * @brief Returns the mean of the values.
 *
 * @param[in] histogram the histogram
 * @return the mean, 0 without values or for a NULL histogram
 */
CERVER_PUBLIC double histogram_mean (const Histogram *histogram);

/**
 * @brief Returns the min of the values.
 *
 * @param[in] histogram the histogram
 * @return the min, 0 without values or for a NULL histogram
 */
CERVER_PUBLIC u64 histogram_min (const Histogram *histogram);

/**
 * @brief Prints a histogram with the cerver log: its count, total, min, max and mean, and each bucket with values.
 *
 * @param[in] name the name of the histogram
 * @param[in] unit the unit of its values
 * @param[in] histogram the histogram; nothing is printed when an argument is NULL
 */
CERVER_PUBLIC void histogram_print (
	const char *name, const char *unit, const Histogram *histogram
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
