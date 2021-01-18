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
				cerver_log_debug ("Got a PACKET_TYPE_APP test request!");
				handle_test_request (packet, PACKET_TYPE_APP); 
				break;

			default: 
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown PACKET_TYPE_APP request.");
				break;
		}
	}

}

static void my_app_error_handler_direct (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;

		switch (packet->header->request_type) {
			case TEST_MSG: 
				cerver_log_debug ("Got a PACKET_TYPE_APP_ERROR test request!");
				handle_test_request (packet, PACKET_TYPE_APP_ERROR); 
				break;

			default: 
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown PACKET_TYPE_APP_ERROR request.");
				break;
		}
	}

}

static void my_custom_handler_direct (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		
		switch (packet->header->request_type) {
			case TEST_MSG: 
				cerver_log_debug ("Got a PACKET_TYPE_CUSTOM test request!");
				handle_test_request (packet, PACKET_TYPE_CUSTOM); 
				break;

			default: 
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown PACKET_TYPE_CUSTOM request.");
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

#pragma region main

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

	cerver_log_debug ("Advanced Cerver Example");
	cerver_log_line_break ();
	cerver_log_debug ("Cerver example with multiple featues enabled");
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
		cerver_set_welcome_msg (my_cerver, "Welcome - Advanced Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_thpool_n_threads (my_cerver, 4);

		cerver_set_reusable_address_flags (my_cerver, true);

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (my_cerver, 2000);

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
			cerver_log_error (
				"Failed to start %s!",
				my_cerver->info->name->str
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