#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/utils/log.h>

#include "../test.h"

// opens and closes connections to the connections cerver one after the other,
// each one waits for the answer to its request before it is closed,
// with the packets mode a single connection sends every request, one after the other

#define CONNECTIONS_ADDRESS					"127.0.0.1"
#define CONNECTIONS_PORT					7400
#define CONNECTIONS_MAX_SLEEP				30
#define CONNECTIONS_REQUEST_ECHO			1

#define CONNECTIONS_COUNT_ARGUMENT			1
#define CONNECTIONS_MODE_ARGUMENT			2
#define CONNECTIONS_MODE_PACKETS			"packets"
#define CONNECTIONS_DEFAULT_COUNT			64
#define CONNECTIONS_NUMBER_BASE				10

static unsigned int echoes = 0;

static void connections_client_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == CONNECTIONS_REQUEST_ECHO) {
		test_counter_add (&echoes);
	}

}

static unsigned int connections_count (int argc, char **argv) {

	return (argc > CONNECTIONS_COUNT_ARGUMENT) ?
		(unsigned int) strtoul (argv[CONNECTIONS_COUNT_ARGUMENT], NULL, CONNECTIONS_NUMBER_BASE)
		: CONNECTIONS_DEFAULT_COUNT;

}

static bool connections_packets_mode (int argc, char **argv) {

	return (argc > CONNECTIONS_MODE_ARGUMENT) && !strcmp (argv[CONNECTIONS_MODE_ARGUMENT], CONNECTIONS_MODE_PACKETS);

}

// a client with a connection to the connections cerver
static Client *connections_connect (Connection **connection) {

	Client *client = client_create ();
	test_check_ptr (client);

	Handler *app_handler = handler_create (connections_client_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (client, app_handler, NULL);

	*connection = client_connection_create (
		client, CONNECTIONS_ADDRESS, CONNECTIONS_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (*connection);
	connection_set_max_sleep (*connection, CONNECTIONS_MAX_SLEEP);

	test_check_int_eq (
		client_connect_and_start (client, *connection), 0,
		"Failed to connect to the connections cerver!"
	);

	return client;

}

// sends a request and waits for its answer
static void connections_request (
	Client *client, Connection *connection, const unsigned int expected
) {

	Packet request;
	packet_init_request (&request, PACKET_TYPE_APP, CONNECTIONS_REQUEST_ECHO);
	packet_set_network_values (&request, NULL, client, connection, NULL);
	test_check_unsigned_eq (packet_send (&request, 0, NULL, false), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&echoes, expected, TEST_WAIT_SECONDS),
		expected, "the request was not answered"
	);

}

static void connections_close (Client *client, Connection *connection) {

	(void) client_connection_end (client, connection);
	test_check_unsigned_eq (client_teardown (client), 0, NULL);

}

// a connection that sends a request and waits for its answer
static void connections_once (const unsigned int expected) {

	Connection *connection = NULL;
	Client *client = connections_connect (&connection);

	connections_request (client, connection, expected);

	connections_close (client, connection);

}

// a single connection that sends every request
static void connections_packets (const unsigned int count) {

	Connection *connection = NULL;
	Client *client = connections_connect (&connection);

	for (unsigned int i = 1; i <= count; i++) {
		connections_request (client, connection, i);
	}

	connections_close (client, connection);

}

int main (int argc, char **argv) {

	cerver_log_init ();
	cerver_log_set_quiet (true);

	const unsigned int count = connections_count (argc, argv);

	if (connections_packets_mode (argc, argv)) {
		connections_packets (count);
		(void) printf ("Done with %u packets\n", count);
	}

	else {
		for (unsigned int i = 1; i <= count; i++) {
			connections_once (i);
		}

		(void) printf ("Done with %u connections\n", count);
	}

	cerver_log_end ();

	return 0;

}
