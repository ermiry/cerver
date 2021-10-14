#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/timer.h>
#include <cerver/version.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include <app/app.h>
#include <app/handler.h>

static Cerver *my_cerver = NULL;

#pragma region end

static void end (int dummy) {

	if (my_cerver) {
		cerver_stats_print (my_cerver, true, true);
		cerver_teardown (my_cerver);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region events

static void *on_cever_started (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s has started!\n",
			event_data->cerver->info->name
		);
	}

	return NULL;

}

static void *on_cever_teardown (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s is going to be destroyed!\n",
			event_data->cerver->info->name
		);
	}

	return NULL;

}

static void *on_client_connected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CLIENT,
			"Client %ld connected with sock fd %d to cerver %s!\n",
			event_data->client->id,
			event_data->connection->socket->sock_fd,
			event_data->cerver->info->name
		);
	}

	return NULL;

}

static void *on_client_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CLIENT,
			"A client closed a connection to cerver %s!\n",
			event_data->cerver->info->name
		);
	}

	return NULL;

}

#pragma endregion

#pragma region main

static u16 get_port (int argc, char **argv) {

	u16 port = 7000;
	if (argc > 1) {
		int j = 0;
		const char *curr_arg = NULL;
		for (int i = 1; i < argc; i++) {
			curr_arg = argv[i];

			// port
			if (!strcmp (curr_arg, "-p")) {
				j = i + 1;
				if (j <= argc) {
					port = (u16) atoi (argv[j]);
					i++;
				}
			}

			else {
				cerver_log_warning (
					"Unknown argument: %s", curr_arg
				);
			}
		}
	}

	return port;

}

int main (int argc, char **argv) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Simple Test Message Example");
	cerver_log_line_break ();
	cerver_log_debug ("Single app handler with direct handle option enabled");
	cerver_log_line_break ();

	my_cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		"my-cerver",
		get_port (argc, argv),
		PROTOCOL_TCP,
		false,
		2
	);
	
	if (my_cerver) {
		cerver_set_welcome_msg (
			my_cerver, "Welcome - Simple Test Message Example"
		);

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_thpool_n_threads (my_cerver, 4);

		// cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		// cerver_set_poll_time_out (my_cerver, 2000);

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (my_cerver, true);

		Handler *app_packet_handler = handler_create (app_handler);
		handler_set_direct_handle (app_packet_handler, true);
		cerver_set_app_handlers (my_cerver, app_packet_handler, NULL);

		(void) cerver_event_register (
			my_cerver,
			CERVER_EVENT_STARTED,
			on_cever_started, NULL, NULL,
			false, false
		);

		(void) cerver_event_register (
			my_cerver,
			CERVER_EVENT_TEARDOWN,
			on_cever_teardown, NULL, NULL,
			false, false
		);

		(void) cerver_event_register (
			my_cerver,
			CERVER_EVENT_CLIENT_CONNECTED,
			on_client_connected, NULL, NULL,
			false, false
		);

		(void) cerver_event_register (
			my_cerver,
			CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
			on_client_close_connection, NULL, NULL,
			false, false
		);

		if (cerver_start (my_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				my_cerver->info->name
			);

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

#pragma endregion