#include <stdlib.h>
#include <stdio.h>

#include "utils.h"

#include "../test.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing UTILS...\n");

	utils_tests_base64 ();

	utils_tests_c_strings ();

	utils_tests_math ();

	utils_tests_sha256 ();

	utils_tests_timer ();

	utils_tests_serializer ();

	utils_tests_log ();

	utils_tests_histogram ();

	(void) printf ("\nDone with UTILS tests!\n\n");

	return 0;

}
