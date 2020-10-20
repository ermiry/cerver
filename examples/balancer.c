#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/balancer.h>
#include <cerver/cerver.h>
#include <cerver/events.h>

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

static void on_cever_started (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s has started!\n",
			event_data->cerver->info->name->str
		);
	}

}

static void on_cever_teardown (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s is going to be destroyed!\n",
			event_data->cerver->info->name->str
		);
	}

}

static void on_client_connected (void *event_data_ptr) {

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

}

static void on_client_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;
		
		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CLIENT,
			"A client closed a connection to cerver %s!\n",
			event_data->cerver->info->name->str
		);
	}

}

#pragma endregion

#pragma region start

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	cerver_init ();

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Load Balancer Example");
	printf ("\n");
	cerver_log_debug ("Simple Round Robin Load Balancer");
	printf ("\n");

	load_balancer = balancer_create ("test-balancer", BALANCER_TYPE_ROUND_ROBIN, 7000, 10, 2);
	if (load_balancer) {
		/*** register services ***/
		if (balancer_service_register (load_balancer, "127.0.0.1", 7001)) {
			cerver_log_error ("Failed to register FIRST service!");
		}

		if (balancer_service_register (load_balancer, "127.0.0.1", 7002)) {
			cerver_log_error ("Failed to register SECOND service!");
		}

		/*** register to events ***/
		cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_STARTED,
			on_cever_started, NULL, NULL,
			false, false
		);

		cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_TEARDOWN,
			on_cever_teardown, NULL, NULL,
			false, false
		);

		cerver_event_register (
			load_balancer->cerver,
			CERVER_EVENT_CLIENT_CONNECTED,
			on_client_connected, NULL, NULL,
			false, false
		);

		cerver_event_register (
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

	cerver_end ();

	return 0;

}

#pragma endregion