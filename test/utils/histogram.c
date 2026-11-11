#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#include <cerver/utils/histogram.h>

#include "../test.h"

#define HISTOGRAM_TEST_SMALL			3
#define HISTOGRAM_TEST_MEDIUM			100
#define HISTOGRAM_TEST_LARGE			5000

#define HISTOGRAM_TEST_THREADS			4
#define HISTOGRAM_TEST_THREAD_VALUES	4096

// each value is in the bucket of the smallest power of 2 that is not smaller than it
static void utils_tests_histogram_buckets (void) {

	test_check_unsigned_eq (histogram_bucket (0), 0, NULL);
	test_check_unsigned_eq (histogram_bucket (1), 0, NULL);
	test_check_unsigned_eq (histogram_bucket (2), 1, NULL);
	test_check_unsigned_eq (histogram_bucket (3), 2, NULL);
	test_check_unsigned_eq (histogram_bucket (4), 2, NULL);
	test_check_unsigned_eq (histogram_bucket (5), 3, NULL);

	const u64 last = (u64) 1 << HISTOGRAM_MAX_EXPONENT;
	test_check_unsigned_eq (histogram_bucket (last), HISTOGRAM_MAX_EXPONENT, NULL);
	test_check_unsigned_eq (histogram_bucket (last + 1), HISTOGRAM_BUCKETS - 1, NULL);
	test_check_unsigned_eq (histogram_bucket (UINT64_MAX), HISTOGRAM_BUCKETS - 1, NULL);

}

static void utils_tests_histogram_values (void) {

	Histogram histogram = HISTOGRAM_INITIALIZER;

	// without values
	test_check_unsigned_eq (histogram.count, 0, NULL);
	test_check_unsigned_eq (histogram_min (&histogram), 0, NULL);
	test_check (test_double_equal (histogram_mean (&histogram), 0), NULL);
	test_check_unsigned_eq (histogram_min (NULL), 0, NULL);
	test_check (test_double_equal (histogram_mean (NULL), 0), NULL);

	histogram_add (&histogram, HISTOGRAM_TEST_MEDIUM);
	histogram_add (&histogram, HISTOGRAM_TEST_SMALL);
	histogram_add (&histogram, HISTOGRAM_TEST_LARGE);
	histogram_add (NULL, HISTOGRAM_TEST_LARGE);

	test_check_unsigned_eq (histogram.count, 3, NULL);
	test_check_unsigned_eq (histogram.total, HISTOGRAM_TEST_SMALL + HISTOGRAM_TEST_MEDIUM + HISTOGRAM_TEST_LARGE, NULL);
	test_check_unsigned_eq (histogram_min (&histogram), HISTOGRAM_TEST_SMALL, NULL);
	test_check_unsigned_eq (histogram.max, HISTOGRAM_TEST_LARGE, NULL);
	test_check (
		test_double_equal (
			histogram_mean (&histogram),
			(double) (HISTOGRAM_TEST_SMALL + HISTOGRAM_TEST_MEDIUM + HISTOGRAM_TEST_LARGE) / 3
		),
		"the mean is not the total over the count"
	);

	test_check_unsigned_eq (histogram.buckets[histogram_bucket (HISTOGRAM_TEST_SMALL)], 1, NULL);
	test_check_unsigned_eq (histogram.buckets[histogram_bucket (HISTOGRAM_TEST_MEDIUM)], 1, NULL);
	test_check_unsigned_eq (histogram.buckets[histogram_bucket (HISTOGRAM_TEST_LARGE)], 1, NULL);

	Histogram copy;
	histogram_copy (&copy, &histogram);
	test_check (!memcmp (&copy, &histogram, sizeof (Histogram)), "the copy is different");

	histogram_print ("Test", "us", &histogram);

	// back without values
	histogram_init (&histogram);
	test_check_unsigned_eq (histogram.count, 0, NULL);
	test_check_unsigned_eq (histogram.min, HISTOGRAM_EMPTY_MIN, NULL);
	test_check_unsigned_eq (histogram.max, 0, NULL);

}

static void *histogram_test_thread (void *histogram_ptr) {

	Histogram *histogram = (Histogram *) histogram_ptr;

	for (u64 value = 1; value <= HISTOGRAM_TEST_THREAD_VALUES; value++) {
		histogram_add (histogram, value);
	}

	return NULL;

}

// the values added from many threads at the same time are not lost
static void utils_tests_histogram_threads (void) {

	Histogram histogram = HISTOGRAM_INITIALIZER;

	pthread_t threads[HISTOGRAM_TEST_THREADS];
	for (unsigned int index = 0; index < HISTOGRAM_TEST_THREADS; index++) {
		test_check_int_eq (pthread_create (&threads[index], NULL, histogram_test_thread, &histogram), 0, NULL);
	}

	for (unsigned int index = 0; index < HISTOGRAM_TEST_THREADS; index++) {
		test_check_int_eq (pthread_join (threads[index], NULL), 0, NULL);
	}

	const u64 values = (u64) HISTOGRAM_TEST_THREADS * HISTOGRAM_TEST_THREAD_VALUES;
	const u64 thread_total = ((u64) HISTOGRAM_TEST_THREAD_VALUES * (HISTOGRAM_TEST_THREAD_VALUES + 1)) / 2;

	test_check_unsigned_eq (histogram.count, values, NULL);
	test_check_unsigned_eq (histogram.total, thread_total * HISTOGRAM_TEST_THREADS, NULL);
	test_check_unsigned_eq (histogram_min (&histogram), 1, NULL);
	test_check_unsigned_eq (histogram.max, HISTOGRAM_TEST_THREAD_VALUES, NULL);

	u64 bucketed = 0;
	for (unsigned int bucket = 0; bucket < HISTOGRAM_BUCKETS; bucket++) {
		bucketed += histogram.buckets[bucket];
	}

	test_check_unsigned_eq (bucketed, values, NULL);

}

void utils_tests_histogram (void) {

	(void) printf ("Testing UTILS histogram...\n");

	utils_tests_histogram_buckets ();
	utils_tests_histogram_values ();
	utils_tests_histogram_threads ();

	(void) printf ("Done!\n");

}
