#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

static Cerver *my_cerver = NULL;

typedef enum AppRequest {

	TEST_MSG		= 0,

	GET_MSG			= 1

} AppRequest;

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver);
		cerver_teardown (my_cerver);
	} 

	exit (0);

}

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

static void my_app_handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		if (packet->data_size >= sizeof (RequestData)) {
			RequestData *req = (RequestData *) (packet->data);

			switch (req->type) {
				case TEST_MSG: 
					cerver_log_debug ("Got a APP_PACKET test request!");
					handle_test_request (packet, APP_PACKET); 
					break;

				default: 
					cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown APP_PACKET request.");
					break;
			}
		}
	}

}

static void my_app_error_handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		if (packet->data_size >= sizeof (RequestData)) {
			RequestData *req = (RequestData *) (packet->data);

			switch (req->type) {
				case TEST_MSG: 
					cerver_log_debug ("Got a APP_ERROR_PACKET test request!");
					handle_test_request (packet, APP_ERROR_PACKET); 
					break;

				default: 
					cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown APP_ERROR_PACKET request.");
					break;
			}
		}
	}

}

static void my_custom_handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		if (packet->data_size >= sizeof (RequestData)) {
			RequestData *req = (RequestData *) (packet->data);

			switch (req->type) {
				case TEST_MSG: 
					cerver_log_debug ("Got a CUSTOM_PACKET test request!");
					handle_test_request (packet, CUSTOM_PACKET); 
					break;

				default: 
					cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown CUSTOM_PACKET request.");
					break;
			}
		}
	}

}

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("App & Custom Handlers Example");
	cerver_log_debug ("Testing correct handling of APP_PACKET, APP_ERROR_PACKET & CUSTOM_PACKET packet types");
	printf ("\n");

	my_cerver = cerver_create (CUSTOM_CERVER, "my-cerver", 8007, PROTOCOL_TCP, false, 2, 2000);
	if (my_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);

		Handler *app_handler = handler_create (my_app_handler);
		handler_set_direct_handle (app_handler, true);

		Handler *app_error_handler = handler_create (my_app_error_handler);
		handler_set_direct_handle (app_error_handler, true);

		Handler *app_custom_handler = handler_create (my_custom_handler);
		handler_set_direct_handle (app_custom_handler, true);

		cerver_set_app_handlers (my_cerver, app_handler, app_error_handler);
		cerver_set_custom_handler (my_cerver, app_custom_handler);

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

        // DONT call - cerver_teardown () is called automatically if cerver_create () fails
		// cerver_delete (client_cerver);
	}

	return 0;

}