#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "types.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing TYPES...\n");

	types_tests_string ();

	(void) printf ("\nDone with TYPES tests!\n\n");

	return 0;

}
