#include <stdlib.h>
#include <stdio.h>

#include "jwt.h"

int main (int argc, char **argv) {

	(void) printf ("Testing JWT...\n");

	jwt_tests_dump ();

	jwt_tests_encode ();

	jwt_tests_header ();

	jwt_tests_grant ();

	jwt_tests_rsa ();

	(void) printf ("\nDone with JWT tests!\n\n");

	return 0;

}