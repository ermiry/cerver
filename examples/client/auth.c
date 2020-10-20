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

#pragma region auth

typedef struct Credentials {

	char username[64];
	char password[64];

} Credentials;

Credentials *credentials_new (const char *username, const char *password) {

	Credentials *credentials = (Credentials *) malloc (sizeof (Credentials));
	if (credentials) {
		strncpy (credentials->username, username, 64);
		strncpy (credentials->password, password, 64);
	}

	return credentials;

}

void credentials_delete (void *credentials_ptr) { if (credentials_ptr) free (credentials_ptr); }

#pragma endregion

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

	if (client_cerver) {
		cerver_stats_print (client_cerver, true, true);
		cerver_teardown (client_cerver);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region client

static void client_app_handler (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		switch (packet->header->request_type) {
			case TEST_MSG: cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Got a test message from cerver!"); break;

			default:
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_NONE, "Got an unknown app request.");
				break;
		}
	}

}

// create a new client connection & set auth values
// connects a client to the cerver & starts the new connection if successful
// returns 0 on success, 1 on error
static u8 cerver_client_connect (Client *client, Connection **connection) {

	u8 retval = 1;

	if (client) {
		*connection = client_connection_create (client, "127.0.0.1", 7000, PROTOCOL_TCP, false);
		if (*connection) {
			connection_set_name (*connection, "main");
			connection_set_max_sleep (*connection, 30);

			// auth configuration
			Credentials *credentials = credentials_new ("ermiry", "hola12");
			connection_set_auth_data (
				*connection,
				credentials, sizeof (Credentials),
				credentials_delete,
				false
			);

			if (!client_connect_to_cerver (client, *connection)) {
				cerver_log (LOG_TYPE_SUCCESS, LOG_TYPE_NONE, "Connected to cerver!");

				client_connection_start (client, *connection);

				retval = 0;
			}

			else {
				cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Failed to connect to cerver!");
			}
		}
	}

	return retval;

}

static void client_event_connection_close (void *client_event_data_ptr) {

	if (client_event_data_ptr) {
		ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

		if (client_event_data->connection) {
			cerver_log_warning (
				"client_event_connection_close () - connection <%s> has been closed!",
				client_event_data->connection->name->str
			);
		}

		client_event_data_delete (client_event_data);
	}

}

static void client_event_auth_sent (void *client_event_data_ptr) {

	if (client_event_data_ptr) {
		ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

		if (client_event_data->connection) {
			cerver_log_debug (
				"client_event_auth_sent () - sent connection <%s> auth data!",
				client_event_data->connection->name->str
			);
		}

		client_event_data_delete (client_event_data);
	}

}

static void client_error_failed_auth (void *client_error_data_ptr) {

	if (client_error_data_ptr) {
		ClientErrorData *client_error_data = (ClientErrorData *) client_error_data_ptr;

		if (client_error_data->connection) {
			cerver_log_error (
				"client_error_failed_auth () - connection <%s> failed to authenticate!",
				client_error_data->connection->name->str
			);
		}

		client_error_data_delete (client_error_data);
	}

}

static void client_event_success_auth (void *client_event_data_ptr) {

	if (client_event_data_ptr) {
		ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

		if (client_event_data->connection) {
			cerver_log_success (
				"client_event_success_auth () - connection <%s> has been authenticated!",
				client_event_data->connection->name->str
			);
		}

		client_event_data_delete (client_event_data);
	}

}

static int client_test_app_msg_send (Client *client, Connection *connection) {

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

#pragma endregion

#pragma region handler

static void handle_test_request (Packet *packet) {

	if (packet) {
		// cerver_log_debug ("Got a test message from client. Sending another one back...");
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Got a test message from client. Sending another one back...");

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

static void handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;

		switch (packet->header->request_type) {
			case TEST_MSG: handle_test_request (packet); break;

			default:
				cerver_log (stderr, LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown app request.");
				break;
		}
	}

}

#pragma endregion

#pragma region events

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

static void *cerver_client_connect_and_start (void *args) {

	// wait 1 second and then try to connect
	sleep (1);

	Client *client = client_create ();
	if (client) {
		client_set_name (client, "start-client");

		Handler *app_handler = handler_create (client_app_handler);
		handler_set_direct_handle (app_handler, true);
		client_set_app_handlers (client, app_handler, NULL);

		(void) client_event_register (
			client,
			CLIENT_EVENT_CONNECTION_CLOSE,
			client_event_connection_close, NULL, NULL,
			false, false
		);

		(void) client_event_register (
			client,
			CLIENT_EVENT_AUTH_SENT,
			client_event_auth_sent, NULL, NULL,
			true, false
		);

		(void) client_error_register (
			client,
			CLIENT_ERROR_FAILED_AUTH,
			client_error_failed_auth, NULL, NULL,
			false, false
		);

		(void) client_event_register (
			client,
			CLIENT_EVENT_SUCCESS_AUTH,
			client_event_success_auth, NULL, NULL,
			false, false
		);

		Connection *connection = NULL;
		if (!cerver_client_connect (client, &connection)) {
			// wait 2 seconds and start sending packst
			sleep (2);

			// send 1 request message every second
			for (unsigned int i = 0; i < 5; i++) {
				client_test_app_msg_send (client, connection);

				sleep (1);
			}

			client_connection_end (client, connection);
		}

		if (!client_teardown (client)) {
			cerver_log_success ("client_teardown ()");
		}

		else {
			cerver_log_error ("client_teardown () has failed!");
		}
	}

	return NULL;

}

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	cerver_init ();

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Cerver Client Auth Example");
	printf ("\n");
	cerver_log_debug ("Cerver creates a new client that will authenticate with a cerver & then, perform requests");
	printf ("\n");

	client_cerver = cerver_create (CERVER_TYPE_CUSTOM, "client-cerver", 7001, PROTOCOL_TCP, false, 2, 2000);
	if (client_cerver) {
		cerver_set_welcome_msg (client_cerver, "Welcome - Cerver Client Auth Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (client_cerver, 4096);
		cerver_set_thpool_n_threads (client_cerver, 4);

		Handler *app_handler = handler_create (handler);
		// 27/05/2020 - needed for this example!
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (client_cerver, app_handler, NULL);

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

		pthread_t client_thread = 0;
		thread_create_detachable (&client_thread, cerver_client_connect_and_start, NULL);

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

		cerver_delete (client_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion