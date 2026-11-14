#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/auth/sessions.h>
#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/client/events.h>
#include <cerver/connection/connection.h>
#include <cerver/connection/data.h>
#include <cerver/packets/packets.h>

#include "../test.h"
#include <app/app.h>
#include <app/auth.h>

// the client of test/cerver/udp.c: it authenticates when it connects,
// gets the cerver info, its session id and its datagram token,
// and its requests and messages are answered with the token

#define UDP_CLIENT_PORT				7000
#define UDP_CLIENT_REQUESTS			64
#define UDP_CLIENT_MESSAGES			16
#define UDP_CLIENT_MAX_SLEEP		30

#define UDP_CLIENT_USERNAME			"ermiry"
#define UDP_CLIENT_PASSWORD			"049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b"

static const char *udp_client_message = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
	"sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
	"Ut enim ad minim veniam, quis nostrud exercitation ullamco "
	"laboris nisi ut aliquip ex ea commodo consequat."
};

static unsigned int responses = 0;
static unsigned int messages = 0;
static unsigned int success_auths = 0;

static void udp_client_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	switch (packet->header.request_type) {
		case APP_REQUEST_TEST:
			test_counter_add (&responses);
			break;

		case APP_REQUEST_MESSAGE: {
			test_check_unsigned_eq (packet->data_size, sizeof (AppMessage), NULL);

			const AppMessage *message = (const AppMessage *) packet->data;
			test_check_str_eq (message->message, udp_client_message, NULL);

			test_counter_add (&messages);
		} break;

		default: break;
	}

}

static void *udp_client_success_auth (void *client_event_data_ptr) {

	client_event_data_delete ((ClientEventData *) client_event_data_ptr);

	test_counter_add (&success_auths);

	return NULL;

}

static void udp_client_send_requests (Client *client, Connection *connection) {

	for (unsigned int i = 0; i < UDP_CLIENT_REQUESTS; i++) {
		Packet *request = packet_create_request (PACKET_TYPE_APP, APP_REQUEST_TEST);
		test_check_ptr (request);

		packet_set_network_values (request, NULL, client, connection, NULL);
		test_check_unsigned_eq (packet_send (request, 0, NULL, false), 0, NULL);

		packet_delete (request);
	}

}

static void udp_client_send_messages (Client *client, Connection *connection) {

	AppMessage message;
	for (unsigned int i = 0; i < UDP_CLIENT_MESSAGES; i++) {
		app_message_create_internal (&message, i, udp_client_message);

		Packet *packet = packet_generate_request (
			PACKET_TYPE_APP, APP_REQUEST_MESSAGE, &message, sizeof (AppMessage)
		);

		test_check_ptr (packet);

		packet_set_network_values (packet, NULL, client, connection, NULL);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

		packet_delete (packet);
	}

}

int main (int argc, const char **argv) {

	(void) argc;
	(void) argv;

	(void) printf ("Testing CLIENT UDP...\n");

	Client *client = client_create ();
	test_check_ptr (client);

	Handler *app_packet_handler = handler_create (udp_client_app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	client_set_app_handlers (client, app_packet_handler, NULL);

	test_check_unsigned_eq (
		client_event_register (
			client, CLIENT_EVENT_SUCCESS_AUTH,
			udp_client_success_auth, NULL, NULL,
			false, false
		), 0, NULL
	);

	Connection *connection = client_connection_create (
		client, test_address (), UDP_CLIENT_PORT, PROTOCOL_UDP, false
	);

	test_check_ptr (connection);
	connection_set_max_sleep (connection, UDP_CLIENT_MAX_SLEEP);

	connection_set_auth_data (
		connection,
		credentials_new (UDP_CLIENT_USERNAME, UDP_CLIENT_PASSWORD), sizeof (Credentials),
		credentials_delete,
		false
	);

	test_check_int_eq (
		client_connect_and_start (client, connection), 0,
		"Failed to connect to cerver!"
	);

	test_check_unsigned_eq (
		test_wait_counter (&success_auths, 1, TEST_WAIT_SECONDS),
		1, "Failed to authenticate!"
	);

	test_check (connection_has_datagram_token (connection), "the connection does not have its token");

	char session_id[TOKEN_SIZE] = { 0 };
	test_check (client_get_session_id (client, session_id, TOKEN_SIZE) > 0, "the client does not have its session id");

	udp_client_send_requests (client, connection);
	udp_client_send_messages (client, connection);

	test_check_unsigned_eq (
		test_wait_counter (&responses, UDP_CLIENT_REQUESTS, TEST_WAIT_SECONDS),
		UDP_CLIENT_REQUESTS, NULL
	);

	test_check_unsigned_eq (
		test_wait_counter (&messages, UDP_CLIENT_MESSAGES, TEST_WAIT_SECONDS),
		UDP_CLIENT_MESSAGES, NULL
	);

	test_check_ptr (__atomic_load_n (&connection->cerver_report, __ATOMIC_ACQUIRE));

	// the close connection packet removes the peer of the client
	(void) client_connection_end (client, connection);
	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	(void) printf ("Done!\n\n");

	return 0;

}
