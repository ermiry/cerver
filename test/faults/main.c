#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "websockets.h"

int main (void) {

	test_output_init ();

	// a peer that ends its connection does not end the process
	(void) signal (SIGPIPE, SIG_IGN);

	(void) printf ("Testing FAULTS...\n");

	faults_tests_websockets ();

	(void) printf ("\nDone with FAULTS tests!\n\n");

	return 0;

}
