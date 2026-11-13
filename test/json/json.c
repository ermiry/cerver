#include <stdlib.h>
#include <stdio.h>

#include "json.h"

#include "../test.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing JSON...\n");

	// before any other test sets the seed with its first object
	json_tests_seed ();

	json_tests_array ();

	json_tests_chaos ();

	json_tests_copy ();

	json_tests_dump_cb ();

	json_tests_dump ();

	json_tests_equal ();

	json_tests_load_cb ();

	json_tests_load ();

	json_tests_loadb ();

	json_tests_memory ();

	json_tests_numbers ();

	json_tests_objects ();

	json_tests_pack ();

	json_tests_simple ();

	json_tests_sprintf ();

	json_tests_unpack ();

	json_tests_safety ();

	(void) printf ("\nDone with JSON tests!\n\n");

	return 0;

}
