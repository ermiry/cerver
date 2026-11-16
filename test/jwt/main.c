#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "jwt.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing JWT...\n");

	jwt_tests_algorithms ();

	jwt_tests_keys ();

	jwt_tests_sign ();

	jwt_tests_verify ();

	jwt_tests_claims ();

	(void) printf ("\nDone with JWT tests!\n\n");

	return 0;

}
