#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include <app/app.h>

#include "../test.h"

#define HANDLER_CLIENT_PACKETS			100
#define HANDLER_CLIENT_HANDLER_ID		10

static const char *MESSAGE = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
	"sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
	"Ut enim ad minim veniam, quis nostrud exercitation ullamco "
	"laboris nisi ut aliquip ex ea commodo consequat."
};

static Client *client = NULL;
static Connection *connection = NULL;

static unsigned int messages = 0;

static void app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet && (packet->header.request_type == APP_REQUEST_MESSAGE)) {
		test_counter_add (&messages);
	}

}

static u8 single_app_message (const size_t id, const char *msg) {

	u8 retval = 1;

	AppMessage *app_message = app_message_create (id, msg);

	Packet *message = packet_create (
		PACKET_TYPE_APP, APP_REQUEST_MESSAGE,
		app_message, sizeof (AppMessage)
	);

	if (message) {
		message->header.handler_id = HANDLER_CLIENT_HANDLER_ID;

		(void) packet_generate (message);

		packet_set_network_values (
			message, NULL, client, connection, NULL
		);

		size_t sent = 0;
		retval = packet_send (message, 0, &sent, false);

		packet_delete (message);
	}

	app_message_delete (app_message);

	return retval;

}

static unsigned int check_responses (
	const char *kind, const uint64_t received, const uint64_t sent
) {

	unsigned int errors = 0;

	if (received != sent) {
		cerver_log_error (
			"%s responses %lu don't match sent packets %lu!",
			kind, received, sent
		);

		errors = 1;
	}

	else {
		cerver_log_success ("Got %lu / %lu %s responses!", received, sent, kind);
	}

	return errors;

}

static unsigned int send_pings (void) {

	uint64_t n_sent_packets = 0;
	for (unsigned int i = 0; i < HANDLER_CLIENT_PACKETS; i++) {
		n_sent_packets += !packet_send_ping (
			NULL,
			client, connection,
			NULL
		);
	}

	const uint64_t received = test_wait_counter_64 (
		&client->stats->received_packets->n_test_packets,
		n_sent_packets, TEST_WAIT_SECONDS
	);

	return check_responses ("ping", received, n_sent_packets);

}

static unsigned int send_messages (void) {

	unsigned int n_sent_packets = 0;
	for (unsigned int i = 0; i < HANDLER_CLIENT_PACKETS; i++) {
		n_sent_packets += !single_app_message (i, MESSAGE);
	}

	const unsigned int received = test_wait_counter (
		&messages, n_sent_packets, TEST_WAIT_SECONDS
	);

	return check_responses ("message", received, n_sent_packets);

}

static unsigned int send_all (const char *type) {

	unsigned int errors = 0;

	if (!type || !strcmp ("ping", type)) {
		errors |= send_pings ();
	}

	if (!type || !strcmp ("app", type)) {
		errors |= send_messages ();
	}

	return errors;

}

int main (int argc, const char **argv) {

	unsigned int errors = 1;

	cerver_log_init ();

	const char *type = (argc > 1) ? argv[1] : NULL;

	client = client_create ();

	client_set_name (client, "test-client");

	Handler *app_packet_handler = handler_create (app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	client_set_app_handlers (client, app_packet_handler, NULL);

	connection = client_connection_create (
		client, "127.0.0.1", 7000, PROTOCOL_TCP, false
	);

	connection_set_max_sleep (connection, 30);

	if (!client_connect_and_start (client, connection)) {
		errors = send_all (type);
	}

	else {
		cerver_log_error ("Failed to connect to cerver!");
	}

	client_connection_end (client, connection);
	client_teardown (client);

	if (!errors) cerver_log_success ("Done!\n\n");

	cerver_log_end ();

	return (int) errors;

}
