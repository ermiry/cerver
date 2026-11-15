#include <stdio.h>
#include <stdlib.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/packets/packets.h>

#include "../test.h"

#define PINGS				32

static const char *client_name = { "test-client" };

int main (void) {

	(void) printf ("Testing CLIENT ping...\n");

	Client *client = client_create ();

	test_check_ptr (client);

	client_set_name (client, client_name);
	test_check_str_eq (client->name, client_name, NULL);
	test_check_str_len (client->name, strlen (client_name), NULL);

	Connection *connection = client_connection_create (
		client, test_address (), 7000, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_max_sleep (connection, 30);

	test_check_int_eq (
		client_connect_and_start (client, connection), 0,
		"Failed to connect to cerver!"
	);

	/*** send **/
	uint64_t n_sent_packets = 0;

	for (unsigned int i = 0; i < PINGS; i++) {
		test_check_unsigned_eq (
			packet_send_ping (NULL, client, connection, NULL), 0, NULL
		);

		n_sent_packets += 1;
	}

	/*** check ***/
	// wait until every response has arrived
	test_check_unsigned_eq (
		test_wait_counter_64 (
			&client->stats->received_packets->n_test_packets,
			n_sent_packets, TEST_WAIT_SECONDS
		),
		n_sent_packets,
		NULL
	);

	/*** end **/
	client_connection_end (client, connection);
	client_teardown (client);

	(void) printf ("Done!\n\n");

	return 0;

}
