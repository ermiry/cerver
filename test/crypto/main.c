#include <stdlib.h>
#include <stdio.h>

#include "crypto.h"

#include "../test.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing CRYPTO...\n");

	crypto_tests_random ();

	crypto_tests_hash ();

	crypto_tests_memory ();

	(void) printf ("\nDone with CRYPTO tests!\n\n");

	return 0;

}
