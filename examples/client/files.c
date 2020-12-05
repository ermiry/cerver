#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/types/string.h>

#include <cerver/version.h>
#include <cerver/cerver.h>

#include <cerver/threads/thread.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

typedef enum AppRequest {

	TEST_MSG		= 0,

} AppRequest;

static Cerver *client_cerver = NULL;

static String *action = NULL;
static String *filename = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

	if (client_cerver) {
		cerver_stats_print (client_cerver, true, true);
		cerver_teardown (client_cerver);
	}

	exit (0);

}

#pragma endregion

#pragma region handler

static void cerver_handle_test_request (Packet *packet) {

	if (packet) {
		cerver_log_debug ("Got a test message from client. Sending another one back...");

		Packet *test_packet = packet_generate_request (PACKET_TYPE_APP, TEST_MSG, NULL, 0);
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

static void cerver_app_handler_direct (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;

		switch (packet->header->request_type) {
			case TEST_MSG: cerver_handle_test_request (packet); break;

			default:
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown app request.");
				break;
		}
	}

}

#pragma endregion

#pragma region client

static void client_app_handler (void *data) {

	if (data) {
		HandlerData *handler_data = (HandlerData *) data;

		// AppData *app_data = (AppData *) handler_data->data;
		Packet *packet = handler_data->packet;
		switch (packet->header->request_type) {
			case TEST_MSG: {
				cerver_log_debug ("Got a test message from cerver!");
			} break;

			default:
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown app request.");
				break;
		}
	}

}

static int test_app_msg_send (Client *client, Connection *connection) {

	int retval = 1;

	if ((client->running) && connection->active) {
		Packet *packet = packet_generate_request (PACKET_TYPE_APP, TEST_MSG, NULL, 0);
		if (packet) {
			packet_set_network_values (packet, NULL, client, connection, NULL);
			size_t sent = 0;
			if (packet_send (packet, 0, &sent, false)) {
				cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Failed to send test to cerver");
			}

			else {
				printf ("PACKET_TYPE_APP sent to cerver: %ld\n", sent);
				retval = 0;
			}

			packet_delete (packet);
		}
	}

	return retval;

}

static void request_file (Client *client, Connection *connection, const char *filename) {

	if (!client_file_get (client, connection, filename)) {
		cerver_log_success ("REQUESTED file to cerver!");
	}

	else {
		cerver_log_error ("Failed to REQUEST file to cerver!");
	}

}

static void send_file (Client *client, Connection *connection, const char *filename) {

	if (!client_file_send (client, connection, filename)) {
		cerver_log_success ("SENT file to cerver!");
	}

	else {
		cerver_log_error ("Failed to SEND file to cerver!");
	}

}

static void *cerver_client_connect_and_start (void *args) {

	Client *client = client_create ();
	if (client) {
		client_set_name (client, "start-client");

		Handler *app_handler = handler_create (client_app_handler);
		// handler_set_direct_handle (app_handler, true);
		client_set_app_handlers (client, app_handler, NULL);

		// add client's files configuration
		client_files_add_path (client, "./data");
		client_files_set_uploads_path (client, "./uploads");

		// wait 1 seconds before connecting to cerver
		sleep (1);
		Connection *connection = client_connection_create (client, "127.0.0.1", 7000, PROTOCOL_TCP, false);
		if (connection) {
			connection_set_max_sleep (connection, 30);

			if (!client_connect_and_start (client, connection)) {
				cerver_log (LOG_TYPE_SUCCESS, LOG_TYPE_NONE, "Connected to cerver!");
			}

			else {
				cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Failed to connect to cerver!");
			}
		}

		sleep (2);

		for (unsigned int i = 0; i < 10; i++) {
			test_app_msg_send (client, connection);
		}

		if (!strcmp ("get", action->str)) request_file (client, connection, filename->str);
		else if (!strcmp ("send", action->str)) send_file (client, connection, filename->str);
		else {
			printf ("\n");
			cerver_log_error ("Unknown action %s", action->str);
			printf ("\n");
		}

		for (unsigned int i = 0; i < 10; i++) {
			test_app_msg_send (client, connection);
		}

		sleep (5);

		client_connection_end (client, connection);

		if (!client_teardown (client)) {
			cerver_log_success ("client_teardown ()");
		}

		else {
			cerver_log_error ("client_teardown () has failed!");
		}
	}

	return NULL;

}

#pragma endregion

#pragma region start

static void start (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Cerver Client Files Example");
	printf ("\n");
	cerver_log_debug ("Cerver creates a new client that will use to make files requests to another cerver");
	printf ("\n");

	client_cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		"client-cerver",
		7001,
		PROTOCOL_TCP,
		false,
		2
	);

	if (client_cerver) {
		cerver_set_welcome_msg (client_cerver, "Welcome - Cerver Client Files Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (client_cerver, 4096);
		cerver_set_thpool_n_threads (client_cerver, 4);

		cerver_set_handler_type (client_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (client_cerver, 2000);

		Handler *app_handler = handler_create (cerver_app_handler_direct);
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (client_cerver, app_handler, NULL);

		pthread_t client_thread = 0;
		thread_create_detachable (&client_thread, cerver_client_connect_and_start, NULL);

		if (cerver_start (client_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				client_cerver->info->name->str
			);

			cerver_delete (client_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (client_cerver);
	}

}

int main (int argc, const char **argv) {

	if (argc >= 3) {
		action = str_new (argv[1]);
		filename = str_new (argv[2]);
		start ();
	}

	else {
		printf ("\nUsage: %s get/send [filename]\n\n", argv[0]);
	}

	return 0;

}

#pragma endregion