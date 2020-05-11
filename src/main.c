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

typedef enum AppRequest {

	TEST_MSG		= 0

} AppRequest;

static Cerver *my_cerver = NULL;

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver);
		cerver_teardown (my_cerver);
	} 

	exit (0);

}

static void handle_test_request (Packet *packet, unsigned int handler_id) {

	if (packet) {
		char *status = c_string_create ("Got a test message in handler <%d>! Sending another one back...", handler_id);
		if (status) {
			cerver_log_debug (status);
			free (status);
		}
		
		Packet *test_packet = packet_generate_request (APP_PACKET, TEST_MSG, NULL, 0);
		if (test_packet) {
			packet_set_network_values (test_packet, packet->cerver, packet->client, packet->connection, NULL);
			size_t sent = 0;
			if (packet_send (test_packet, 0, &sent, false)) 
				cerver_log_error ("Failed to send test packet to main cerver");

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
		if (packet->data_size >= sizeof (RequestData)) {
			RequestData *req = (RequestData *) (packet->data);

			switch (req->type) {
				case TEST_MSG: handle_test_request (packet, 7); break;

				default: 
					cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
					break;
			}
		}
	}

}

static void handler_cero (void *data) {

	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					default: 
						cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
						break;
				}
			}
		}
	}

}

static void handler_one (void *data) {
	
	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					default: 
						cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
						break;
				}
			}
		}
	}

}

static void handler_two (void *data) {
	
	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					default: 
						cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
						break;
				}
			}
		}
	}

}

static void handler_three (void *data) {
	
	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					default: 
						cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
						break;
				}
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

	cerver_log_debug ("Multiple handlers example");
	printf ("\n");

	my_cerver = cerver_create (CUSTOM_CERVER, "my-cerver", 8007, PROTOCOL_TCP, false, 2, 2000);
	if (my_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 16384);
		cerver_set_thpool_n_threads (my_cerver, 4);
		// cerver_set_app_handlers (my_cerver, handler, NULL);

		cerver_set_multiple_handlers (my_cerver, 4);

		// create custom app handlers
		Handler *handler_0 = handler_create (0, handler_cero);
		Handler *handler_1 = handler_create (1, handler_one);
		Handler *handler_2 = handler_create (2, handler_two);
		Handler *handler_3 = handler_create (3, handler_three);

		// register the handlers to be used by the cerver
		cerver_handlers_add (my_cerver, handler_0);
		cerver_handlers_add (my_cerver, handler_1);
		cerver_handlers_add (my_cerver, handler_2);
		cerver_handlers_add (my_cerver, handler_3);

		if (!cerver_start (my_cerver)) {
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
				"Failed to start magic cerver!");
		}
	}

	else {
		cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
			"Failed to create cerver!");
	}

	return 0;

}