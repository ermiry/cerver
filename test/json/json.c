#include <stdlib.h>
#include <stdio.h>

#include "json.h"

int main (int argc, char **argv) {

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

	printf ("\nDone with JSON tests!\n\n");

	return 0;

}