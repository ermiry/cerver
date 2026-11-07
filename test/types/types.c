#include <stdlib.h>
#include <stdio.h>

#include "types.h"

#include "../test.h"

int main (int argc, char **argv) {

	test_output_init ();

	(void) printf ("Testing TYPES...\n");

	types_tests_string ();

	(void) printf ("\nDone with TYPES tests!\n\n");

	return 0;

}
