#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/types/string.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/timer.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

static Cerver *my_cerver = NULL;

#pragma region app

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#define APP_MESSAGE_LEN			512

typedef enum AppRequest {

	TEST_MSG		= 0,
	APP_MSG			= 1,
	MULTI_MSG		= 2,

} AppRequest;

typedef struct AppData {

	time_t timestamp;
	size_t message_len;
	char message[APP_MESSAGE_LEN];

} AppData;

static AppData *app_data_new (void) {

	AppData *app_data = (AppData *) malloc (sizeof (AppData));
	if (app_data) {
		memset (app_data, 0, sizeof (AppData));
	}

	return app_data;

}

static void app_data_delete (void *app_data_ptr) {

	if (app_data_ptr) free (app_data_ptr);

}

static AppData *app_data_create (const char *message) {

	AppData *app_data = app_data_new ();
	if (app_data) {
		time (&app_data->timestamp);

		if (message) {
			app_data->message_len = strlen (message);
			strncpy (app_data->message, message, APP_MESSAGE_LEN);
		}
	}

	return app_data;

}

static void app_data_print (AppData *app_data) {

	if (app_data) {
		String *date = timer_time_to_string (gmtime (&app_data->timestamp));
		if (date) {
			printf ("Timestamp: %s\n", date->str);
			str_delete (date);
		}

		printf ("Message (%ld): %s\n", app_data->message_len, app_data->message);
	}

}

#pragma GCC diagnostic pop

#pragma endregion

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

	if (my_cerver) {
		cerver_stats_print (my_cerver, true, true);
		cerver_teardown (my_cerver);
	}

	cerver_end ();

	exit (0);

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

static void handle_app_message (Packet *packet) {

	if (packet) {
		char *end = (char *) packet->data;

		AppData *app_data = (AppData *) end;
		app_data_print (app_data);
		printf ("\n");
	}

}

static void handle_multi_message (Packet *packet) {

	if (packet) {
		cerver_log_debug ("MULTI message!");

		char *end = (char *) packet->data;

		AppData *app_data = NULL;
		for (u32 i = 0; i < 5; i++) {
			app_data = (AppData *) end;
			printf ("Message (%ld): %s\n", app_data->message_len, app_data->message);
			printf ("\n");

			end += sizeof (AppData);
		}
	}

}

static void handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;

		switch (packet->header.request_type) {
			case TEST_MSG: handle_test_request (packet); break;

			case APP_MSG: handle_app_message (packet); break;

			case MULTI_MSG: handle_multi_message (packet); break;

			default:
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown app request.");
				break;
		}
	}

}

#pragma endregion

#pragma region events

static void *on_client_connected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf (
			"\nClient %ld connected with sock fd %d to cerver %s!\n\n",
			event_data->client->id,
			event_data->connection->socket->sock_fd,
			event_data->cerver->info->name
		);
	}

	return NULL;

}

static void *on_client_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf (
			"\nA client closed a connection to cerver %s!\n\n",
			event_data->cerver->info->name
		);
	}

	return NULL;

}

#pragma endregion

#pragma region main

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

	cerver_log_debug ("Packets Example");
	cerver_log_line_break ();
	cerver_log_debug ("We should always receive the same message no matter the method the client is using to send it");
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
		cerver_set_welcome_msg (my_cerver, "Welcome - Packets Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_thpool_n_threads (my_cerver, 4);

		cerver_set_reusable_address_flags (my_cerver, true);

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (my_cerver, 2000);

		Handler *app_handler = handler_create (handler);
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (my_cerver, app_handler, NULL);

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

		if (cerver_start (my_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				my_cerver->info->name
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