#include <stdlib.h>
#include <stdio.h>

#include "http.h"

int main (int argc, char **argv) {

	(void) printf ("Testing HTTP...\n");

	http_tests_jwt ();

	(void) printf ("\nDone with HTTP tests!\n\n");

	return 0;

}