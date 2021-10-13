#include <stdlib.h>
#include <stdio.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include "../test.h"

#define PACKETS_TO_SEND			128

static const char *client_name = { "test-client" };

int main (int argc, const char **argv) {

	(void) printf ("Testing CLIENT queue...\n");

	Client *client = client_create ();

	test_check_ptr (client);

	client_set_name (client, client_name);
	test_check_str_eq (client->name, client_name, NULL);
	test_check_str_len (client->name, strlen (client_name), NULL);

	Connection *connection = client_connection_create (
		client, "127.0.0.1", 7000, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_max_sleep (connection, 30);
	connection_set_send_queue (connection, 0);

	test_check_int_eq (
		client_connect_and_start (client, connection), 0,
		"Failed to connect to cerver!"
	);

	/*** send **/
	unsigned int n_sent_packets = 0;

	Packet *ping = NULL;

	// send 10 requests message every second for 3 seconds
	for (unsigned int i = 0; i < PACKETS_TO_SEND; i++) {
		ping = packet_create_ping ();
		packet_set_network_values (
			ping,
			NULL, client, connection, NULL
		);

		// send the packet using the connections queue
		connection_send_packet (connection, ping);

		n_sent_packets += 1;
	}

	/*** check ***/
	(void) sleep (3);

	// check that we received matching responses
	test_check_unsigned_eq (
		client->stats->received_packets->n_test_packets,
		n_sent_packets,
		NULL
	);

	/*** end **/
	client_connection_end (client, connection);
	client_teardown (client);

	(void) printf ("Done!\n\n");

	return 0;

}