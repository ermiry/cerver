#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/balancer.h>
#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/version.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

static Balancer *load_balancer = NULL;

#pragma region end

static void end (int dummy) {

	if (load_balancer) {
		balancer_stats_print (load_balancer);
		balancer_teardown (load_balancer);
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
			event_data->cerver->info->name->str
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
			event_data->cerver->info->name->str
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
			event_data->cerver->info->name->str
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
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

#pragma endregion

#pragma region start

static BalancerType get_balancer_type (int argc, char **argv) {

	BalancerType type = BALANCER_TYPE_NONE;
	if (argc > 1) {
		int j = 0;
		const char *curr_arg = NULL;
		for (int i = 1; i < argc; i++) {
			curr_arg = argv[i];

			// port
			if (!strcmp (curr_arg, "--type")) {
				j = i + 1;
				if (j <= argc) {
					if (!strcmp ("round_robin", argv[j]))
						type = BALANCER_TYPE_ROUND_ROBIN;

					else if (!strcmp ("handler_id", argv[j]))
						type = BALANCER_TYPE_HANDLER_ID;

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

	return type;

}

static void start (const BalancerType type) {

	load_balancer = balancer_create (
		"test-balancer",
		type,
		7000,
		10,
		2
	);

	if (load_balancer) {
		/*** cerver configuration ***/
		cerver_set_handler_type (load_balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_handle_detachable_threads (load_balancer->cerver, true);

		cerver_set_reusable_address_flags (load_balancer->cerver, true);

		/*** register services ***/
		if (balancer_service_register (load_balancer, "127.0.0.1", 7001)) {
			cerver_log_error ("Failed to register FIRST service!");
		}

		if (balancer_service_register (load_balancer, "127.0.0.1", 7002)) {
			cerver_log_error ("Failed to register SECOND service!");
		}

		/*** register to events ***/
		(void) cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_STARTED,
			on_cever_started, NULL, NULL,
			false, false
		);

		(void) cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_TEARDOWN,
			on_cever_teardown, NULL, NULL,
			false, false
		);

		(void) cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_CLIENT_CONNECTED,
			on_client_connected, NULL, NULL,
			false, false
		);

		(void) cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
			on_client_close_connection, NULL, NULL,
			false, false
		);

		if (balancer_start (load_balancer)) {
			balancer_delete (load_balancer);
		}
	}

	else {
		cerver_log_error ("Failed to create load balancer!");
	}

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

	cerver_log_debug ("Load Balancer Example");
	cerver_log_line_break ();

	if (argc > 1) {
		BalancerType type = get_balancer_type (argc, argv);
		switch (type) {
			case BALANCER_TYPE_ROUND_ROBIN:
				cerver_log_debug ("Simple Round Robin Load Balancer");
				cerver_log_line_break ();
				break;

			case BALANCER_TYPE_HANDLER_ID:
				cerver_log_debug ("Simple Handler ID Load Balancer");
				cerver_log_line_break ();
				break;

			default: break;
		}

		start (type);
	}

	else {
		cerver_log_error ("Missing balancer type!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion