#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

	const int result = random_int_in_range (min, max);

	test_check_true ((result >= min));
	test_check_true ((result <= max));

}

void utils_tests_math (void) {

	(void) printf ("Testing UTILS math...\n");

	test_clamp_int ();
	test_abs_int ();
	test_float_compare ();
	test_random_set_seed ();
	test_random_int_in_range ();

	(void) printf ("Done!\n");

}
