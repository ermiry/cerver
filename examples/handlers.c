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

	TEST_MSG		= 0,

	GET_MSG			= 1

} AppRequest;

// data to be allocated directly in handler thread
// also used as the data args to create a new one (a copy)
typedef struct AppData {

	int id;
	estring *message;

} AppData;

// message to be sent to the client
typedef struct AppMessage {

	unsigned int len;
	char message[128];

} AppMessage;

AppData *app_data_new (void) {

	AppData *app_data = (AppData *) malloc (sizeof (AppData));
	if (app_data) {
		app_data->id = -1;
		app_data->message = NULL;
	}

	return app_data;

}

void app_data_delete (void *app_data_ptr) {

	if (app_data_ptr) {
		AppData *app_data = (AppData *) app_data_ptr;

		estring_delete (app_data_ptr);

		free (app_data);
	}

}

AppData *app_data_create (int id, const char *msg) {

	AppData *app_data = app_data_new ();
	if (app_data) {
		app_data->id = id;
		app_data->message = estring_new (msg);
	}

	return app_data;

}

// method that will be used to create unique handler data
void *app_data_copy (void *app_data_args_ptr) {

	AppData *handler_data = NULL;

	if (app_data_args_ptr) {
		AppData *app_data = (AppData *) app_data_args_ptr;

		handler_data = app_data_new ();
		if (handler_data) {
			handler_data->id = app_data->id;
			handler_data->message =estring_new (app_data->message->str);
		}
	}

	return handler_data;

}

static Cerver *my_cerver = NULL;

AppData *app_data_0 = NULL;
AppData *app_data_1 = NULL;
AppData *app_data_2 = NULL;
AppData *app_data_3 = NULL;

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver);
		cerver_teardown (my_cerver);

		app_data_delete (app_data_0);
		app_data_delete (app_data_1);
		app_data_delete (app_data_2);
		app_data_delete (app_data_3);
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
				cerver_log_error ("Failed to send test packet to client");

			else {
				// printf ("Response packet sent: %ld\n", sent);
			}
			
			packet_delete (test_packet);
		}
	}

}

static void handle_msg_request (Packet *packet, unsigned int handler_id, estring *msg) {

	if (packet && msg) {
		char *status = c_string_create ("Got a request for handler's <%d> message!", handler_id);
		if (status) {
			cerver_log_debug (status);
			free (status);
		}

		printf ("%s - %d\n", msg->str, msg->len);

		AppMessage *app_message = (AppMessage *) malloc (sizeof (AppMessage));
		memset (app_message, 0, sizeof (AppMessage));
		strncpy (app_message->message, msg->str, 128);
		app_message->len = msg->len;
		
		Packet *msg_packet = packet_generate_request (APP_PACKET, GET_MSG, app_message, sizeof (AppMessage));
		if (msg_packet) {
			packet_set_network_values (msg_packet, packet->cerver, packet->client, packet->connection, NULL);
			size_t sent = 0;
			if (packet_send (msg_packet, 0, &sent, false)) 
				cerver_log_error ("Failed to send handler's message to client");

			else {
				printf ("Response packet sent: %ld\n", sent);
			}
			
			packet_delete (msg_packet);
		}

		free (app_message);
	}

}

static void handler_cero (void *data) {

	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		AppData *app_data = handler_data->data;
		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					case GET_MSG: handle_msg_request(packet, handler_data->handler_id, app_data->message); break;

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

		AppData *app_data = handler_data->data;
		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					case GET_MSG: handle_msg_request(packet, handler_data->handler_id, app_data->message); break;

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

		AppData *app_data = handler_data->data;
		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					case GET_MSG: handle_msg_request(packet, handler_data->handler_id, app_data->message); break;

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

		AppData *app_data = handler_data->data;
		Packet *packet = handler_data->packet;
		if (packet) {
			if (packet->data_size >= sizeof (RequestData)) {
				RequestData *req = (RequestData *) (packet->data);

				switch (req->type) {
					case TEST_MSG: handle_test_request (packet, handler_data->handler_id); break;

					case GET_MSG: handle_msg_request(packet, handler_data->handler_id, app_data->message); break;

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

	cerver_log_debug ("Multiple Handlers Example");
	printf ("\n");

	my_cerver = cerver_create (CUSTOM_CERVER, "my-cerver", 8007, PROTOCOL_TCP, false, 2, 2000);
	if (my_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 16384);
		cerver_set_thpool_n_threads (my_cerver, 4);
		// cerver_set_app_handlers (my_cerver, handler, NULL);

		cerver_set_multiple_handlers (my_cerver, 4);

		// create my app data
		AppData *app_data_0 = app_data_create (0, "Hello there!");
		AppData *app_data_1 = app_data_create (1, "See you later!");
		AppData *app_data_2 = app_data_create (2, "Well done!");
		AppData *app_data_3 = app_data_create (3, "Good morning!");

		// create custom app handlers
		Handler *handler_0 = handler_create (0, handler_cero);
		Handler *handler_1 = handler_create (1, handler_one);
		Handler *handler_2 = handler_create (2, handler_two);
		Handler *handler_3 = handler_create (3, handler_three);

		// register the handlers to be used by the cerver
		cerver_handlers_add (my_cerver, handler_0);
		handler_set_data_create (handler_0, app_data_copy, app_data_0);
		handler_set_data_delete (handler_0, app_data_delete);
		cerver_handlers_add (my_cerver, handler_1);
		handler_set_data_create (handler_1, app_data_copy, app_data_1);
		handler_set_data_delete (handler_1, app_data_delete);
		cerver_handlers_add (my_cerver, handler_2);
		handler_set_data_create (handler_2, app_data_copy, app_data_2);
		handler_set_data_delete (handler_2, app_data_delete);
		cerver_handlers_add (my_cerver, handler_3);
		handler_set_data_create (handler_3, app_data_copy, app_data_3);
		handler_set_data_delete (handler_3, app_data_delete);

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