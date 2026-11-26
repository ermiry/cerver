#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "client.h"
#include "clock.h"
#include "core.h"
#include "sweep.h"

int main (void) {

	test_output_init ();

	// a peer that ends its connection does not end the process
	(void) signal (SIGPIPE, SIG_IGN);

	(void) printf ("Testing FAULTS...\n");

	faults_tests_core ();

	faults_tests_client ();

	faults_tests_clock ();

	faults_tests_sweep ();

	(void) printf ("\nDone with FAULTS tests!\n\n");

	return 0;

}
