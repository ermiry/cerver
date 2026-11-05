#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/utils/log.h>

#include "collections.h"

int main (int argc, char **argv) {

	srand ((unsigned) time (NULL));

	(void) printf ("Testing COLLECTIONS...\n");

	cerver_log_init ();

	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			if (!strcmp (argv[i], "--quiet")) {
				cerver_log_set_quiet (true);
			}
		}
	}

	collections_tests_arena ();

	collections_tests_avl ();

	collections_tests_avl_balance ();

	(void) collections_tests_dlist ();

	collections_tests_dlist_invariants ();

	collections_tests_htab ();

	collections_tests_htab_hashing ();

	collections_tests_pool ();

	collections_tests_queue ();

	(void) printf ("\nDone with COLLECTIONS tests!\n\n");

	cerver_log_end ();

	return 0;

}