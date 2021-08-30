#include <stdlib.h>
#include <stdio.h>

#include "types.h"

int main (int argc, char **argv) {

	(void) printf ("Testing TYPES...\n");

	types_tests_string ();

	(void) printf ("\nDone with TYPES tests!\n\n");

	return 0;

}
