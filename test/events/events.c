#include <stdio.h>
#include <stdlib.h>

#include "../test.h"
#include "events.h"

int main (void) {

	test_output_init ();

	(void) printf ("Testing EVENTS...\n");

	events_tests_poller ();

	events_tests_wheel ();

	(void) printf ("\nDone with EVENTS tests!\n\n");

	return 0;

}
