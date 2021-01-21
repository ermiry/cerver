#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/handler.h>

#include <cerver/utils/log.h>

#include <app/handler.h>

static const char *cerver_name = "test-cerver";
static const char *welcome_message = "Hello there!";

static Cerver *cerver = NULL;

static void end (int dummy) {
	
	cerver_stats_print (cerver, true, true);

	cerver_teardown (cerver);

	cerver_end ();

	exit (0);

}

int main (int argc, char **argv) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	cerver_init ();

	/*** create ***/
	cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		cerver_name,
		CERVER_DEFAULT_PORT,
		PROTOCOL_TCP,
		false,
		CERVER_DEFAULT_CONNECTION_QUEUE
	);

	/*** configuration ***/
	cerver_set_welcome_msg (cerver, welcome_message);
	cerver_set_receive_buffer_size (cerver, 4096);
	cerver_set_thpool_n_threads (cerver, 4);
	cerver_set_reusable_address_flags (cerver, true);

	// cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	// cerver_set_poll_time_out (cerver, 1000);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);

	cerver_set_handle_recieved_buffer (cerver, cerver_receive_handle_buffer_new);

	/*** handlers ***/
	Handler *app_packet_handler = handler_create (app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	cerver_set_app_handlers (cerver, app_packet_handler, NULL);

	/*** start ***/
	if (cerver_start (cerver)) {
		cerver_log_error ("Failed to start cerver!");
	}

	cerver_end ();

	return 0;

}