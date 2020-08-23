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
	String *message;

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

		str_delete (app_data->message);

		free (app_data);
	}

}

AppData *app_data_create (int id, const char *msg) {

	AppData *app_data = app_data_new ();
	if (app_data) {
		app_data->id = id;
		app_data->message = str_new (msg);
	}

	return app_data;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// method that will be used to create unique handler data
void *app_data_copy (void *app_data_args_ptr) {

	AppData *handler_data = NULL;

	if (app_data_args_ptr) {
		AppData *app_data = (AppData *) app_data_args_ptr;

		handler_data = app_data_new ();
		if (handler_data) {
			handler_data->id = app_data->id;
			handler_data->message = str_new (app_data->message->str);
		}
	}

	return handler_data;

}

#pragma GCC diagnostic pop

static Cerver *my_cerver = NULL;

AppData *app_data = NULL;

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver, true, true);
		cerver_teardown (my_cerver);

		app_data_delete (app_data);
	} 

	exit (0);

}

static void handle_test_request (Packet *packet) {

	if (packet) {
		cerver_log_debug ("Got a TEST request from client! Sending another one back...");

		Packet *test_packet = packet_generate_request (APP_PACKET, TEST_MSG, NULL, 0);
		if (test_packet) {
			packet_set_network_values (test_packet, packet->cerver, packet->client, packet->connection, NULL);
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

static void handle_msg_request (Packet *packet, String *msg) {

	if (packet && msg) {
		cerver_log_debug ("Got an APP DATA request!");

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

static void app_packet_handler (void *data) {

	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		AppData *app_data = (AppData *) handler_data->data;
		Packet *packet = handler_data->packet;
		if (packet) {
			switch (packet->header->request_type) {
				case TEST_MSG: handle_test_request (packet); break;

				case GET_MSG: handle_msg_request(packet, app_data->message); break;

				default: 
					cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
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

	cerver_log_debug ("Requests Example");
	printf ("\n");

	my_cerver = cerver_create (CERVER_TYPE_CUSTOM, "my-cerver", 7000, PROTOCOL_TCP, false, 2, 2000);
	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome - Requests Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		// cerver_set_thpool_n_threads (my_cerver, 4);

		app_data = app_data_create (0, "Hello there!");

		Handler *app_handler = handler_create (app_packet_handler);
		// handler_set_direct_handle (app_handler, true);
		// handler_set_data_create (app_handler, app_data_copy, app_data);
		handler_set_data (app_handler, app_data);
		handler_set_data_delete (app_handler, app_data_delete);

		cerver_set_app_handlers (my_cerver, app_handler, NULL);
		
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