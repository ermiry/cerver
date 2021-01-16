#include <stdlib.h>
#include <stdio.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include "../test.h"

int main (int argc, const char **argv) {

	(void) printf ("Testing CLIENT packets...\n");

	Client *client = client_create ();

	test_check_ptr (client);

	client_set_name (client, "test-client");

	// Handler *app_handler = handler_create (client_app_handler);
	// handler_set_direct_handle (app_handler, true);
	// client_set_app_handlers (client, app_handler, NULL);

	Connection *connection = client_connection_create (
		client, "127.0.0.1", 7000, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_max_sleep (connection, 30);

	test_check_int_eq (
		client_connect_and_start (client, connection), 0,
		"Failed to connect to cerver!"
	);

	// send 10 requests message every second for 5 seconds
	for (unsigned int sec = 0; sec < 5; sec++) {
		for (unsigned int i = 0; i < 10; i++) {
			test_check_unsigned_eq (
				packet_send_ping (NULL, client, connection, NULL), 0, NULL
			);
		}

		(void) sleep (1);
	}

	client_connection_end (client, connection);

	client_teardown (client);

	(void) printf ("Done!\n\n");

	return 0;

}