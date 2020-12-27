#include <stdlib.h>
#include <stdio.h>

#include "jwt.h"

int main (int argc, char **argv) {

	(void) printf ("Testing JWT...\n");

	jwt_tests_dump ();

	(void) printf ("\nDone with JWT tests!\n\n");

	return 0;

}