#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/events.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

typedef enum AppRequest {

	TEST_MSG		= 0

} AppRequest;

static Cerver *my_cerver = NULL;

#pragma region handlers

static void handle_test_request (Packet *packet, PacketType packet_type) {

	if (packet) {
		cerver_log_debug ("Got a test message from client. Sending another one back...");
		
		Packet *test_packet = packet_generate_request (packet_type, TEST_MSG, NULL, 0);
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

static void my_app_handler_direct (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;

		switch (packet->header->request_type) {
			case TEST_MSG: 
				cerver_log_debug ("Got a APP_PACKET test request!");
				handle_test_request (packet, APP_PACKET); 
				break;

			default: 
				cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown APP_PACKET request.");
				break;
		}
	}

}

static void my_app_error_handler_direct (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;

		switch (packet->header->request_type) {
			case TEST_MSG: 
				cerver_log_debug ("Got a APP_ERROR_PACKET test request!");
				handle_test_request (packet, APP_ERROR_PACKET); 
				break;

			default: 
				cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown APP_ERROR_PACKET request.");
				break;
		}
	}

}

static void my_custom_handler_direct (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		
		switch (packet->header->request_type) {
			case TEST_MSG: 
				cerver_log_debug ("Got a CUSTOM_PACKET test request!");
				handle_test_request (packet, CUSTOM_PACKET); 
				break;

			default: 
				cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown CUSTOM_PACKET request.");
				break;
		}
	}

}

#pragma endregion

#pragma region update

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

void update_thread (void *data) {

	CerverUpdate *cerver_update = (CerverUpdate *) data;

	printf ("%s\n", ((String *) cerver_update->args)->str);

}

void update_interval_thread (void *data) {
	
	CerverUpdate *cerver_update = (CerverUpdate *) data;

	printf ("%s\n", ((String *) cerver_update->args)->str);

}

#pragma GCC diagnostic pop

#pragma endregion

#pragma region events

static void on_client_connected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		char *status = c_string_create (
			"Client %ld connected with sock fd %d to cerver %s!\n",
			event_data->client->id,
			event_data->connection->socket->sock_fd, 
			event_data->cerver->info->name->str
		);

		if (status) {
			printf ("\n");
			cerver_log_msg (stdout, LOG_TYPE_EVENT, LOG_TYPE_CLIENT, status);
			free (status);
		}
	}

}

static void on_client_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		char *status = c_string_create (
			"A client closed a connection to cerver %s!\n",
			event_data->cerver->info->name->str
		);

		if (status) {
			printf ("\n");
			cerver_log_msg (stdout, LOG_TYPE_EVENT, LOG_TYPE_CLIENT, status);
			free (status);
		}
	}

}

#pragma endregion

#pragma region main

static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver, true, true);
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

	cerver_log_debug ("Advanced Cerver Example");
	printf ("\n");
	cerver_log_debug ("Cerver example with multiple featues enabled");
	printf ("\n");

	my_cerver = cerver_create (CERVER_TYPE_CUSTOM, "my-cerver", 7000, PROTOCOL_TCP, false, 2, 2000);
	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome - Advanced Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_thpool_n_threads (my_cerver, 4);

		Handler *app_handler = handler_create (my_app_handler_direct);
		handler_set_direct_handle (app_handler, true);

		Handler *app_error_handler = handler_create (my_app_error_handler_direct);
		handler_set_direct_handle (app_error_handler, true);

		Handler *app_custom_handler = handler_create (my_custom_handler_direct);
		handler_set_direct_handle (app_custom_handler, true);

		cerver_set_app_handlers (my_cerver, app_handler, app_error_handler);
		cerver_set_custom_handler (my_cerver, app_custom_handler);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_CLIENT_CONNECTED,
			on_client_connected, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
			on_client_close_connection, NULL, NULL,
			false, false
		);

		unsigned int fps = 5;
		String *update = str_create ("Update fps: %d", fps);
		cerver_set_update (my_cerver, update_thread, update, str_delete, fps);

		unsigned int interval_value = 1;
		String *interval = str_create ("Update interval: %d", interval_value);
		cerver_set_update_interval (my_cerver, update_interval_thread, interval, str_delete, interval_value);

		if (cerver_start (my_cerver)) {
			char *s = c_string_create ("Failed to start %s!",
				my_cerver->info->name->str);
			if (s) {
				cerver_log_error (s);
				free (s);
			}

			cerver_delete (my_cerver);
		}
	}

	else {
        cerver_log_error ("Failed to create cerver!");

		cerver_delete (my_cerver);
	}

	return 0;

}

#pragma endregion