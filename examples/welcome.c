#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>

#include <cerver/cerver.h>
#include <cerver/utils/log.h>

static Cerver *my_cerver = NULL;

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver);
		cerver_teardown (my_cerver);
	} 

	exit (0);

}

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Welcome Example");
	printf ("\n");

	my_cerver = cerver_create (CUSTOM_CERVER, "my-cerver", 7000, PROTOCOL_TCP, false, 2, 2000);
	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome to cerver!");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_app_handlers (my_cerver, NULL, NULL);

		if (!cerver_start (my_cerver)) {
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
				"Failed to start cerver!");
		}
	}

	else {
        cerver_log_error ("Failed to create cerver!");

        // DONT call - cerver_teardown () is called automatically if cerver_create () fails
		// cerver_delete (client_cerver);
	}

	return 0;

}