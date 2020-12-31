#include <stdlib.h>
#include <stdio.h>

#include "utils.h"

int main (int argc, char **argv) {

	(void) printf ("Testing UTILS...\n");

	utils_tests_base64 ();

	utils_tests_c_strings ();

	(void) printf ("\nDone with UTILS tests!\n\n");

	return 0;

}