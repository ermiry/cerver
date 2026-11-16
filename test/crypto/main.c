#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "crypto.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing CRYPTO...\n");

	crypto_tests_random ();

	(void) printf ("\nDone with CRYPTO tests!\n\n");

	return 0;

}
