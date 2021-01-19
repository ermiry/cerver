#include <stdlib.h>
#include <stdio.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include <cerver/utils/log.h>

int main (int argc, const char **argv) {

	cerver_log_init ();

	Client *client = client_create ();
	
	client_set_name (client, "test-client");
	Connection *connection = client_connection_create (
		client, "127.0.0.1", 7000, PROTOCOL_TCP, false
	);

	connection_set_max_sleep (connection, 30);

	if (!client_connect_and_start (client, connection)) {
		unsigned int n_sent_packets = 0;

		/*** send **/
		for (unsigned int i = 0; i < 10; i++) {
			if (!packet_send_ping (NULL, client, connection, NULL)) {
				n_sent_packets += 1;	
			}

			else {
				cerver_log_error ("Failed to send packet!");
			}
		}

		// wait for remaining packets
		(void) sleep (2);

		// check that we received matching responses
		if (
			client->stats->received_packets->n_test_packets
			!= n_sent_packets
		) {
			(void) printf ("\n\n");
			cerver_log_error (
				"Responses %lu don't match n_sent_packets %lu!",
				client->stats->received_packets->n_test_packets,
				n_sent_packets
			);
			(void) printf ("\n\n");
		}
	}

	else {
		cerver_log_error ("Failed to connect to cerver!");
	}

	client_connection_end (client, connection);
	client_teardown (client);

	cerver_log_end ();

	(void) printf ("Done!\n\n");

	return 0;

}