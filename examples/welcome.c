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
		cerver_stats_print (my_cerver, true, true);
		cerver_teardown (my_cerver);
	}

	cerver_end ();

	exit (0);

}

int main (void) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Welcome Example");
	cerver_log_line_break ();

	my_cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		"my-cerver",
		7000,
		PROTOCOL_TCP,
		false,
		2
	);

	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome to cerver!");

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (my_cerver, 2000);

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_app_handlers (my_cerver, NULL, NULL);

		if (cerver_start (my_cerver)) {
			cerver_log_error ("Failed to start cerver!");

			cerver_delete (my_cerver);
		}
	}

	else {
        cerver_log_error ("Failed to create cerver!");
		
		cerver_delete (my_cerver);
	}

	cerver_end ();

	return 0;

}