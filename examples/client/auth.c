#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/events.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

typedef enum AppRequest {

	TEST_MSG		= 0

} AppRequest;

static Cerver *client_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (client_cerver) {
		cerver_stats_print (client_cerver);
		cerver_teardown (client_cerver);
	} 

	exit (0);

}

#pragma endregion

#pragma region handler

static void handle_test_request (Packet *packet) {

	if (packet) {
		// cerver_log_debug ("Got a test message from client. Sending another one back...");
		cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Got a test message from client. Sending another one back...");
		
		Packet *test_packet = packet_generate_request (APP_PACKET, TEST_MSG, NULL, 0);
		if (test_packet) {
			packet_set_network_values (test_packet, NULL, NULL, packet->connection, NULL);
			size_t sent = 0;
			if (packet_send (test_packet, 0, &sent, false)) 
				cerver_log_error ("Failed to send test packet to client!");

			else {
				// printf ("Response packet sent: %ld\n", sent);
			}
			
			packet_delete (test_packet);
		}
	}

}

static void handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		
		switch (packet->header->request_type) {
			case TEST_MSG: handle_test_request (packet); break;

			default: 
				cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
				break;
		}
	}

}

#pragma endregion

#pragma region events

static void on_cever_started (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\nCerver %s has started!\n", event_data->cerver->info->name->str);
		printf ("Test Message: %s\n\n", ((estring *) event_data->action_args)->str);
	}

}

static void on_cever_teardown (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\nCerver %s is going to be destroyed!\n\n", event_data->cerver->info->name->str);
	}

}

static void on_client_connected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf (
			"\nClient %ld connected with sock fd %d to cerver %s!\n\n",
			event_data->client->id,
			event_data->connection->socket->sock_fd, 
			event_data->cerver->info->name->str
		);
	}

}

static void on_client_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf (
			"\nA client closed a connection to cerver %s!\n\n",
			event_data->cerver->info->name->str
		);
	}

}

#pragma endregion

#pragma region main

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Cerver Client Auth Example");
	printf ("\n");
	cerver_log_debug ("Cerver creates a new client that will authenticate with a cerver & the perform requests");
	printf ("\n");

	client_cerver = cerver_create (CUSTOM_CERVER, "client-cerver", 7001, PROTOCOL_TCP, false, 2, 2000);
	if (client_cerver) {
		cerver_set_welcome_msg (client_cerver, "Welcome - Cerver Client Auth Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (client_cerver, 4096);
		cerver_set_thpool_n_threads (client_cerver, 4);

		Handler *app_handler = handler_create (handler);
		// 27/05/2020 - needed for this example!
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (client_cerver, app_handler, NULL);

		estring *test = estring_new ("This is a test!");
		cerver_event_register (
			client_cerver, 
			CERVER_EVENT_STARTED,
			on_cever_started, test, estring_delete,
			false, false
		);

		cerver_event_register (
			client_cerver, 
			CERVER_EVENT_TEARDOWN,
			on_cever_teardown, NULL, NULL,
			false, false
		);

		cerver_event_register (
			client_cerver, 
			CERVER_EVENT_CLIENT_CONNECTED,
			on_client_connected, NULL, NULL,
			false, false
		);

		cerver_event_register (
			client_cerver, 
			CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
			on_client_close_connection, NULL, NULL,
			false, false
		);

		if (cerver_start (client_cerver)) {
			char *s = c_string_create ("Failed to start %s!",
				client_cerver->info->name->str);
			if (s) {
				cerver_log_error (s);
				free (s);
			}

			cerver_delete (client_cerver);
		}
	}

	else {
        cerver_log_error ("Failed to create cerver!");

        // DONT call - cerver_teardown () is called automatically if cerver_create () fails
		// cerver_delete (client_cerver);
	}

	return 0;

}

#pragma endregion