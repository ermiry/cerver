#include <stdlib.h>
#include <stdio.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include <app/app.h>

#include "../test.h"

#define REQUESTS			64

static unsigned int responses = 0;

static void app_handler (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		switch (packet->header->request_type) {
			case APP_REQUEST_NONE: break;

			case APP_REQUEST_TEST:
				responses += 1;
				break;

			case APP_REQUEST_MESSAGE: break;

			case APP_REQUEST_MULTI: break;

			default: break;
		}
	}

}

int main (int argc, const char **argv) {

	(void) printf ("Testing CLIENT threads...\n");

	Client *client = client_create ();

	test_check_ptr (client);

	client_set_name (client, "test-client");
	test_check_ptr (client->name->str);
	test_check_str_eq (client->name->str, "test-client", NULL);
	test_check_str_len (client->name->str, strlen ("test-client"), NULL);

	/*** handler ***/
	Handler *app_packet_handler = handler_create (app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	client_set_app_handlers (client, app_packet_handler, NULL);

	/*** connection ***/
	Connection *connection = client_connection_create (
		client, "127.0.0.1", 7000, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_max_sleep (connection, 30);

	/*** start ***/
	test_check_int_eq (
		client_connect_and_start (client, connection), 0,
		"Failed to connect to cerver!"
	);

	/*** send ***/
	// send a bunch of requests to the cerver
	Packet *request = NULL;
	for (unsigned int i = 0; i < REQUESTS; i++) {
		request = packet_new ();
		if (request) {
			(void) packet_create_request (
				request,
				PACKET_TYPE_APP, APP_REQUEST_TEST
			);

			packet_set_network_values (
				request,
				NULL, client, connection, NULL
			);

			test_check_unsigned_eq (
				packet_send (request, 0, NULL, false),
				0, NULL
			);

			packet_delete (request);
		}
	}

	// wait for any response to arrive
	(void) sleep (2);

	/*** check ***/
	// check that we have received all the responses
	test_check_unsigned_eq (responses, REQUESTS, NULL);

	/*** end ***/
	client_connection_end (client, connection);
	client_teardown (client);

	(void) printf ("Done!\n\n");

	return 0;

}