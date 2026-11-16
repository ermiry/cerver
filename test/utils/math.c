#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/utils/utils.h>

#include "../test.h"

static void test_clamp_int (void) {

	const int min = 0;
	const int max = 10;

	const int in_range = 5;
	test_check_int_eq (clamp_int (in_range, min, max), in_range, NULL);

	const int bigger = 18;
	test_check_int_eq (clamp_int (bigger, min, max), max, NULL);

	const int smaller = -4;
	test_check_int_eq (clamp_int (smaller, min, max), min, NULL);

}

static void test_abs_int (void) {

	const int pos = 12;
	test_check_int_eq (abs_int (pos), pos, NULL);

	const int negative = -7;
	test_check_int_eq (abs_int (negative), negative * -1, NULL);

}

static void test_float_compare (void) {

	const float one = 10.999;
	const float two = 12.92;
	const float three = 10.899;

	test_check_false (float_compare (one, two));
	test_check_false (float_compare (one, three));

}

static void test_random_set_seed (void) {

	random_set_seed (time (NULL));

}

static void test_random_int_in_range (void) {

	const int min = 0;
	const int max = 100;

	// the range is inclusive on both ends
	for (unsigned int i = 0; i < 1000; i++) {
		const int result = random_int_in_range (min, max);

		test_check_true ((result >= min));
		test_check_true ((result <= max));
	}

	// a range of a single value returns it
	test_check_int_eq (random_int_in_range (5, 5), 5, NULL);

	// the limits in the other order are the same inclusive range
	bool lowest = false;
	bool highest = false;
	for (unsigned int i = 0; i < 1000; i++) {
		const int result = random_int_in_range (3, 1);

		test_check_true ((result >= 1));
		test_check_true ((result <= 3));

		if (result == 1) lowest = true;
		if (result == 3) highest = true;
	}

	test_check (lowest && highest, "the limits in the other order were not inclusive");

}

#define MATH_TEST_INT_MIN_DIGITS		"-2147483648"
#define MATH_TEST_INT_DIGITS_SIZE		12
#define MATH_TEST_LIMITS_ROUNDS			64

// INT_MIN has a result in abs_int () and its digits in itoa (), and the whole int range is a range
// (they overflowed: undefined behavior that UBSan reports)
static void test_math_limits (void) {

	test_check_int_eq (abs_int (INT_MIN), INT_MAX, "abs_int () of INT_MIN is not INT_MAX");
	test_check_int_eq (abs_int (INT_MIN + 1), INT_MAX, NULL);

	char digits[MATH_TEST_INT_DIGITS_SIZE] = { 0 };
	test_check_str_eq (itoa (INT_MIN, digits), MATH_TEST_INT_MIN_DIGITS, "itoa () of INT_MIN");

	for (unsigned int i = 0; i < MATH_TEST_LIMITS_ROUNDS; i++) {
		(void) random_int_in_range (INT_MIN, INT_MAX);
		(void) random_int_in_range (INT_MAX - 1, INT_MAX);
	}

	test_check_int_eq (random_int_in_range (INT_MAX, INT_MAX), INT_MAX, NULL);

}

void utils_tests_math (void) {

	(void) printf ("Testing UTILS math...\n");

	test_clamp_int ();
	test_abs_int ();
	test_float_compare ();
	test_random_set_seed ();
	test_random_int_in_range ();
	test_math_limits ();

	(void) printf ("Done!\n");

}
