#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/balancer.h>
#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/version.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include <app/app.h>
#include <app/handler.h>

#define BALANCER_PORT				7000
#define BALANCER_CONNECTION_QUEUE	10
#define BALANCER_SERVICES			2

#define BALANCER_SERVICE_ADDRESS	"127.0.0.1"
#define BALANCER_FIRST_SERVICE		7001
#define BALANCER_SECOND_SERVICE		7002

static Balancer *load_balancer = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
// the signals only stop the balancer, it is released when balancer_start () returns
static void end (int dummy) {

	(void) dummy;

	if (load_balancer) (void) cerver_shutdown (load_balancer->cerver);

}

static void finish (void) {

	balancer_stats_print (load_balancer);
	(void) balancer_teardown (load_balancer);
	load_balancer = NULL;

}

#pragma endregion

#pragma region events

static void on_service_status_change (void *service_ptr) {

	if (service_ptr) {
		Service *service = (Service *) service_ptr;

		const char *status = balancer_service_get_status_string (service);

		cerver_log_debug (
			"Service %s changed status to %s",
			service->name, status
		);
	}

}

static void on_unavailable_services (void *packet_ptr) {

	Packet *packet = (Packet *) packet_ptr;

	packet_header_print (&packet->header);

	if (packet->data && (packet->data_size >= sizeof (AppMessage))) {
		app_message_print ((const AppMessage *) packet->data);
	}

	// send back test message to client
	send_test_message (packet);

}

static void *on_cever_started (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s has started!\n",
			event_data->cerver->info->name
		);

		cerver_event_data_delete (event_data);
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

		cerver_event_data_delete (event_data);
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

		cerver_event_data_delete (event_data);
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

		cerver_event_data_delete (event_data);
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

			// balancer type
			if (!strcmp (curr_arg, "--type")) {
				j = i + 1;
				if (j < argc) {
					if (!strcmp ("round_robin", argv[j]))
						type = BALANCER_TYPE_ROUND_ROBIN;

					else if (!strcmp ("handler_id", argv[j]))
						type = BALANCER_TYPE_HANDLER_ID;

					i++;
				}
			}

			else {
				cerver_log_warning ("Unknown argument: %s", curr_arg);
			}
		}
	}

	return type;

}

static void start_events (void) {

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

}

static void start_services (void) {

	if (balancer_service_register (load_balancer, BALANCER_SERVICE_ADDRESS, BALANCER_FIRST_SERVICE)) {
		cerver_log_error ("Failed to register FIRST service!");
	}

	if (balancer_service_register (load_balancer, BALANCER_SERVICE_ADDRESS, BALANCER_SECOND_SERVICE)) {
		cerver_log_error ("Failed to register SECOND service!");
	}

	for (int idx = 0; idx < load_balancer->n_services; idx++) {
		balancer_service_set_on_status_change (
			load_balancer->services[idx], on_service_status_change
		);
	}

}

static void start (const BalancerType type) {

	load_balancer = balancer_create (
		"test-balancer",
		type,
		BALANCER_PORT,
		BALANCER_CONNECTION_QUEUE,
		BALANCER_SERVICES
	);

	if (load_balancer) {
		/*** cerver configuration ***/
		cerver_set_handler_type (load_balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_handle_detachable_threads (load_balancer->cerver, true);

		cerver_set_reusable_address_flags (load_balancer->cerver, true);

		/*** balancer configuration ***/
		balancer_set_on_unavailable_services (load_balancer, on_unavailable_services);

		start_services ();

		start_events ();

		if (!balancer_start (load_balancer)) {
			finish ();
		}

		else {
			cerver_log_error ("Failed to start the load balancer!");

			(void) balancer_teardown (load_balancer);
			load_balancer = NULL;
		}
	}

	else {
		cerver_log_error ("Failed to create load balancer!");
	}

}

static void print_type (const BalancerType type) {

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

}

int main (int argc, char **argv) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Load Balancer Example");
	cerver_log_line_break ();

	if (argc > 1) {
		const BalancerType type = get_balancer_type (argc, argv);
		print_type (type);

		start (type);
	}

	else {
		cerver_log_error ("Missing balancer type!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion
