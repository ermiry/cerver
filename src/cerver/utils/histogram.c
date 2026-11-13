#include <stdint.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/utils/histogram.h"
#include "cerver/utils/log.h"

// sets the histogram without values
void histogram_init (Histogram *histogram) {

	if (histogram) {
		(void) memset (histogram, 0, sizeof (Histogram));
		histogram->min = HISTOGRAM_EMPTY_MIN;
	}

}

// returns the bucket of a value
unsigned int histogram_bucket (const u64 value) {

	unsigned int bucket = 0;
	while ((bucket <= HISTOGRAM_MAX_EXPONENT) && (value > ((u64) 1 << bucket))) {
		bucket += 1;
	}

	return bucket;

}

static void histogram_set_min (Histogram *histogram, const u64 value) {

	u64 current = __atomic_load_n (&histogram->min, __ATOMIC_RELAXED);
	while (
		(value < current)
		&& !__atomic_compare_exchange_n (
			&histogram->min, &current, value, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
		)
	);

}

static void histogram_set_max (Histogram *histogram, const u64 value) {

	u64 current = __atomic_load_n (&histogram->max, __ATOMIC_RELAXED);
	while (
		(value > current)
		&& !__atomic_compare_exchange_n (
			&histogram->max, &current, value, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
		)
	);

}

// adds a value to the histogram
void histogram_add (Histogram *histogram, const u64 value) {

	if (histogram) {
		(void) __atomic_add_fetch (&histogram->count, 1, __ATOMIC_RELAXED);
		(void) __atomic_add_fetch (&histogram->total, value, __ATOMIC_RELAXED);
		(void) __atomic_add_fetch (&histogram->buckets[histogram_bucket (value)], 1, __ATOMIC_RELAXED);

		histogram_set_min (histogram, value);
		histogram_set_max (histogram, value);
	}

}

// copies the values of the histogram with atomic reads
void histogram_copy (
	Histogram *destination, const Histogram *source
) {

	if (destination && source) {
		destination->count = __atomic_load_n (&source->count, __ATOMIC_RELAXED);
		destination->total = __atomic_load_n (&source->total, __ATOMIC_RELAXED);
		destination->min = __atomic_load_n (&source->min, __ATOMIC_RELAXED);
		destination->max = __atomic_load_n (&source->max, __ATOMIC_RELAXED);

		for (unsigned int bucket = 0; bucket < HISTOGRAM_BUCKETS; bucket++) {
			destination->buckets[bucket] = __atomic_load_n (&source->buckets[bucket], __ATOMIC_RELAXED);
		}
	}

}

// returns the mean of the values, 0 without values
double histogram_mean (const Histogram *histogram) {

	double mean = 0;

	if (histogram && histogram->count) {
		mean = (double) histogram->total / (double) histogram->count;
	}

	return mean;

}

// returns the min of the values, 0 without values
u64 histogram_min (const Histogram *histogram) {

	return (histogram && histogram->count) ? histogram->min : 0;

}

// prints the histogram with the cerver log, with the unit of its values
void histogram_print (
	const char *name, const char *unit, const Histogram *histogram
) {

	if (name && unit && histogram) {
		cerver_log_msg (
			"%s: %lu values - total %lu %s - min %lu - max %lu - mean %.2f",
			name, histogram->count, histogram->total, unit,
			histogram_min (histogram), histogram->max, histogram_mean (histogram)
		);

		for (unsigned int bucket = 0; bucket <= HISTOGRAM_MAX_EXPONENT; bucket++) {
			if (histogram->buckets[bucket]) {
				cerver_log_msg (
					"\t<= %lu %s: %lu",
					((u64) 1 << bucket), unit, histogram->buckets[bucket]
				);
			}
		}

		if (histogram->buckets[HISTOGRAM_BUCKETS - 1]) {
			cerver_log_msg (
				"\t> %lu %s: %lu",
				((u64) 1 << HISTOGRAM_MAX_EXPONENT), unit,
				histogram->buckets[HISTOGRAM_BUCKETS - 1]
			);
		}
	}

}
