#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/timer.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#define APP_MESSAGE_LEN			512

static Cerver *my_cerver = NULL;

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

		Packet *test_packet = packet_new ();
		if (test_packet) {
			packet_set_header_values (test_packet, PACKET_TYPE_APP, sizeof (PacketHeader), 0, TEST_MSG, packet->header->sock_fd);
			(void) packet_generate (test_packet);
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

static unsigned int send_response (Packet *packet, Client *client, Connection *connection) {

	unsigned int retval = 1;

	packet_set_network_values (packet, my_cerver, client, connection, NULL);
	size_t sent = 0;
	if (packet_send (packet, 0, &sent, false)) {
		cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Failed to send packet!");
	}

	else {
		printf ("Sent to client: %ld\n", sent);
		retval = 0;
	}

	return retval;

}

static void handle_app_message (Packet *packet) {

	if (packet) {
		char *final = (char *) packet->data;

		AppData *app_data = (AppData *) final;
		app_data_print (app_data);
		printf ("\n");

		// send the packet back to the client
		Packet *response = packet_new ();
		if (response) {
			size_t packet_len = sizeof (PacketHeader) + sizeof (AppData);
			response->packet = malloc (packet_len);
			response->packet_size = packet_len;

			char *end = (char *) response->packet;
			PacketHeader *header = (PacketHeader *) end;
			header->packet_type = PACKET_TYPE_APP;
			header->packet_size = packet_len;

			header->request_type = APP_MSG;

			header->sock_fd = packet->header->sock_fd;

			end += sizeof (PacketHeader);

			memcpy (end, final, sizeof (AppData));

			send_response (response, packet->client, packet->connection);

			packet_delete (response);
		}
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

		switch (packet->header->request_type) {
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

static void on_cever_started (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s has started!\n",
			event_data->cerver->info->name->str
		);

		printf ("Test Message: %s\n\n", ((String *) event_data->action_args)->str);
	}

}

static void on_cever_teardown (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Cerver %s is going to be destroyed!\n",
			event_data->cerver->info->name->str
		);
	}

}

static void on_client_connected (void *event_data_ptr) {

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

}

static void on_client_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CLIENT,
			"A client closed a connection to cerver %s!\n",
			event_data->cerver->info->name->str
		);
	}

}

#pragma endregion

#pragma region main

static u16 get_port (int argc, char **argv) {

	u16 port = 7000;
	if (argc > 1) {
		int j = 0;
		const char *curr_arg = NULL;
		for (int i = 1; i < argc; i++) {
			curr_arg = argv[i];

			// port
			if (!strcmp (curr_arg, "-p")) {
				j = i + 1;
				if (j <= argc) {
					port = (u16) atoi (argv[j]);
					i++;
				}
			}

			else {
				cerver_log_warning (
					"Unknown argument: %s", curr_arg
				);
			}
		}
	}

	return port;

}

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	cerver_init ();

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Simple Test Message Example");
	printf ("\n");
	cerver_log_debug ("Single app handler with direct handle option enabled");
	printf ("\n");

	my_cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		"my-cerver",
		get_port (argc, argv),
		PROTOCOL_TCP,
		false,
		2
	);
	
	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome - Simple Test Message Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_thpool_n_threads (my_cerver, 4);

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (my_cerver, 2000);

		Handler *app_handler = handler_create (handler);
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (my_cerver, app_handler, NULL);

		String *test = str_new ("This is a test!");
		cerver_event_register (
			my_cerver,
			CERVER_EVENT_STARTED,
			on_cever_started, test, str_delete,
			false, false
		);

		cerver_event_register (
			my_cerver,
			CERVER_EVENT_TEARDOWN,
			on_cever_teardown, NULL, NULL,
			false, false
		);

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