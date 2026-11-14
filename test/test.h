#ifndef _CERVER_TESTS_H_
#define _CERVER_TESTS_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// setvbuf () allocates the buffer with its default size
#define TEST_OUTPUT_DEFAULT_BUFFER_SIZE		0

// stdout is buffered by lines even in a pipe, so a test that is killed
// (alarm, deadline or crash) still shows how far it went
static inline void test_output_init (void) {

	(void) setvbuf (stdout, NULL, _IOLBF, TEST_OUTPUT_DEFAULT_BUFFER_SIZE);

}

#define where (void) fprintf (stderr, "%s:%d -> ", __FILE__, __LINE__)

// runs a test after it prints its name, so a failure shows which test it was
#define						\
	test_run(test)			\
	({						\
		(void) printf (		\
			"\t%s\n", #test	\
		);					\
		test ();			\
	})

#define											\
	fail(msg) 									\
	({ 											\
		where; 									\
		(void) fprintf (stderr, "%s\n", msg); 	\
		exit (1); 								\
	})

#define																\
	test_check(result, msg)											\
	({																\
		if ((result) == false) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"Check has failed!\n"								\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define test_check_ptr_eq(X, Y) test_check (X == Y, NULL)

#define test_check_ptr_ne(X, Y) test_check (X != Y, NULL)

#define																\
	test_check_ptr(ptr)												\
	({																\
		if (!(ptr)) {												\
			where;													\
			(void) fprintf (stderr, "Pointer is NULL!");			\
			exit (1);												\
		}															\
	})

#define																\
	test_check_null_ptr(ptr)										\
	({																\
		if ((ptr)) {												\
			where;													\
			(void) fprintf (stderr, "Pointer is NOT NULL!");		\
			exit (1);												\
		}															\
	})

#define																\
	test_check_true(value)											\
	({																\
		if (!(value)) {												\
			where;													\
			(void) fprintf (stderr, "Value is NOT true!");			\
			exit (1);												\
		}															\
	})

#define																\
	test_check_false(value)											\
	({																\
		if ((value)) {												\
			where;													\
			(void) fprintf (stderr, "Value is NOT false!");			\
			exit (1);												\
		}															\
	})

#define																\
	test_check_bool_eq(value, expected, msg)						\
	({																\
		const __typeof__ (value) test_value = (value);				\
		if (test_value != (expected)) {								\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"BOOL %d does not match %d\n",						\
				(int) (test_value), (int) (expected)				\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

// the value is evaluated once, so a check of a call with side effects runs it once
#define																\
	test_check_unsigned_eq(value, expected, msg)					\
	({																\
		const __typeof__ (value) test_value = (value);				\
		if (test_value != (expected)) {								\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"UNSIGNED %llu does not match %llu\n",				\
				(unsigned long long) (test_value),					\
				(unsigned long long) (expected)						\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define test_check_unsigned_ne(X, Y) test_check (X != Y, NULL)

#define test_check_unsigned_gt(X, Y) test_check (X > Y, NULL)

#define																\
	test_check_int_eq(value, expected, msg)							\
	({																\
		const __typeof__ (value) test_value = (value);				\
		if (test_value != (expected)) {								\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"INTEGER %lld does not match %lld\n",				\
				(long long) (test_value), (long long) (expected)	\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define test_check_int_ne(X, Y) test_check (X != Y, NULL)

#define test_check_int_gt(X, Y) test_check (X > Y, NULL)

#define																\
	test_check_long_int_eq(value, expected, msg)					\
	({																\
		const __typeof__ (value) test_value = (value);				\
		if (test_value != (expected)) {								\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"INTEGER %lld does not match %lld\n",				\
				(long long) (test_value), (long long) (expected)	\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_str_eq(value, expected, msg)							\
	({																\
		const char *test_value = (const char *) (value);			\
		if (strcmp (test_value, (const char *) (expected))) {		\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"STRING %s does not match %s\n",					\
				test_value, (const char *) (expected)				\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_str_len(string, expected, msg)						\
	({																\
		if (strlen ((char *) string) != expected) {					\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"STRING %s LEN does not match %zu\n",				\
				(char *) string, (size_t) (expected)				\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_str_empty(string)									\
	({																\
		if (strlen ((char *) string)) {								\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"STRING %s is not empty\n",							\
				(char *) string										\
			);														\
																	\
			exit (1);												\
		}															\
	})

#define TEST_DOUBLE_TOLERANCE				0.000001

// compares two doubles within TEST_DOUBLE_TOLERANCE, without an exact comparison
static inline int test_double_equal (const double value, const double expected) {

	const double difference = value - expected;

	return (difference < TEST_DOUBLE_TOLERANCE) && (difference > -TEST_DOUBLE_TOLERANCE);

}

#pragma region time

// the timeouts of less than a few seconds that a test gives to cerver (a ping, a close, a connect)
// are multiplied by a scale when the binary runs many times slower: under valgrind, where the runner
// sets TEST_TIME_SCALE, and in a build with a sanitizer; without it what is tested is the speed of the machine
#define TEST_TIME_SCALE_VARIABLE			"TEST_TIME_SCALE"
#define TEST_TIME_SCALE_BASE				10

#if defined (__SANITIZE_THREAD__) || defined (__SANITIZE_ADDRESS__)
	#define TEST_TIME_SCALE_DEFAULT			4
#elif defined (__has_feature)
	#if __has_feature (thread_sanitizer) || __has_feature (address_sanitizer)
		#define TEST_TIME_SCALE_DEFAULT		4
	#endif
#endif

#ifndef TEST_TIME_SCALE_DEFAULT
	#define TEST_TIME_SCALE_DEFAULT			1
#endif

static inline unsigned int test_time_scale (void) {

	const char *scale = getenv (TEST_TIME_SCALE_VARIABLE);
	const unsigned long value = scale ? strtoul (scale, NULL, TEST_TIME_SCALE_BASE) : 0;

	return value ? (unsigned int) value : TEST_TIME_SCALE_DEFAULT;

}

// the milliseconds of a short timeout of a test, with the scale of the run
static inline unsigned int test_milliseconds (const unsigned int milliseconds) {

	return milliseconds * test_time_scale ();

}

#pragma endregion

#pragma region wait

#define TEST_WAIT_SECONDS					16
#define TEST_WAIT_POLL_NANOSECONDS			1000000L
#define TEST_WAIT_NANOSECONDS				1000000000L

static inline double test_wait_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / (double) TEST_WAIT_NANOSECONDS);

}

// waits until the counter, updated by other threads, reaches the expected value
// returns the last value that was read
static inline unsigned int test_wait_counter (
	const unsigned int *counter, const unsigned int expected,
	const unsigned int seconds
) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) seconds;

	unsigned int value = __atomic_load_n (counter, __ATOMIC_SEQ_CST);
	while ((value < expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		value = __atomic_load_n (counter, __ATOMIC_SEQ_CST);
	}

	return value;

}

// works like test_wait_counter () for 64 bits counters, like the stats
static inline uint64_t test_wait_counter_64 (
	const uint64_t *counter, const uint64_t expected,
	const unsigned int seconds
) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) seconds;

	uint64_t value = __atomic_load_n (counter, __ATOMIC_SEQ_CST);
	while ((value < expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		value = __atomic_load_n (counter, __ATOMIC_SEQ_CST);
	}

	return value;

}

// adds one to a counter that is read by other threads
static inline void test_counter_add (unsigned int *counter) {

	(void) __atomic_add_fetch (counter, 1, __ATOMIC_SEQ_CST);

}

#pragma endregion

#include "address.h"

#endif
