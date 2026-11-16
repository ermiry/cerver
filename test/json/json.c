#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "json.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing JSON...\n");

	json_tests_document ();

	json_tests_values ();

	json_tests_objects ();

	json_tests_strings ();

	json_tests_numbers ();

	json_tests_pointer ();

	json_tests_writer ();

	json_tests_events ();

	(void) printf ("\nDone with JSON tests!\n\n");

	return 0;

}
