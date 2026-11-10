#ifndef _CERVER_TESTS_H_
#define _CERVER_TESTS_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

// setvbuf () allocates the buffer with its default size
#define TEST_OUTPUT_DEFAULT_BUFFER_SIZE		0

// stdout is buffered by lines even in a pipe, so a test that is killed
// (alarm, deadline or crash) still shows how far it went
static inline void test_output_init (void) {

	(void) setvbuf (stdout, NULL, _IOLBF, TEST_OUTPUT_DEFAULT_BUFFER_SIZE);

}

#define where (void) fprintf (stderr, "%s:%d -> ", __FILE__, __LINE__)

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
		if (!ptr) {													\
			where;													\
			(void) fprintf (stderr, "Pointer is NULL!");			\
			exit (1);												\
		}															\
	})

#define																\
	test_check_null_ptr(ptr)										\
	({																\
		if (ptr) {													\
			where;													\
			(void) fprintf (stderr, "Pointer is NOT NULL!");		\
			exit (1);												\
		}															\
	})

#define																\
	test_check_true(value)											\
	({																\
		if (!value) {												\
			where;													\
			(void) fprintf (stderr, "Value is NOT true!");			\
			exit (1);												\
		}															\
	})

#define																\
	test_check_false(value)											\
	({																\
		if (value) {												\
			where;													\
			(void) fprintf (stderr, "Value is NOT false!");			\
			exit (1);												\
		}															\
	})

#define																\
	test_check_bool_eq(value, expected, msg)						\
	({																\
		if (value != expected) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"BOOL %d does not match %d\n",						\
				value, expected										\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_unsigned_eq(value, expected, msg)					\
	({																\
		if (value != expected) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"UNSIGNED %d does not match %d\n",					\
				value, expected										\
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
		if (value != expected) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"INTEGER %d does not match %d\n",					\
				value, expected										\
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
		if (value != expected) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"INTEGER %ld does not match %ld\n",					\
				value, expected										\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_str_eq(value, expected, msg)							\
	({																\
		if (strcmp ((char *) value, (char *) expected)) {			\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"STRING %s does not match %s\n",					\
				(char *) value, (char *) expected					\
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
				"STRING %s LEN does not match %d\n",				\
				(char *) string, expected							\
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
