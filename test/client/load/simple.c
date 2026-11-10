#include <stdlib.h>
#include <stdio.h>

#include <signal.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include <app/app.h>

#include "../../test.h"

#define REQUESTS			4096
// #define REQUESTS			32768 * 2

static const char *MESSAGE = { 
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
	"sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
	"Ut enim ad minim veniam, quis nostrud exercitation ullamco "
	"laboris nisi ut aliquip ex ea commodo consequat."
};

static unsigned int MESSAGE_LEN = 0;

static const char *client_name = { "test-client" };

static Client *client = NULL;
static Connection *connection = NULL;

static unsigned int responses = 0;

static void app_handler (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		switch (packet->header.request_type) {
			case APP_REQUEST_NONE: break;

			case APP_REQUEST_TEST: break;

			case APP_REQUEST_MESSAGE: {
				test_counter_add (&responses);

				AppMessage *app_message = (AppMessage *) (char *) packet->data;

				// if (MESSAGE_LEN == app_message->len) {
				// 	printf ("[%lu] Length matches!\n", app_message->id);
				// }

				if (strcmp (MESSAGE, app_message->message)) {
					cerver_log_error (
						"Message [%lu] mismatch!",
						app_message->id
					);

					(void) printf ("|%s|\n", app_message->message);
				}
			} break;

			case APP_REQUEST_MULTI: break;

			default: break;
		}
	}

}

static void send_app_message (
	const size_t id, const char *msg
) {

	Packet *message = packet_new ();
	
	test_check_ptr (message);

	// generate the packet
	size_t packet_len = PACKET_HEADER_SIZE + sizeof (AppMessage);
	message->packet = calloc (1, packet_len);
	message->packet_size = packet_len;

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));
	header.packet_type = PACKET_TYPE_APP;
	header.packet_size = packet_len;

	header.request_type = APP_REQUEST_MESSAGE;

	header.version = packet_version_local ();

	char *end = (char *) message->packet;
	packet_header_encode (&header, (u8 *) end);

	end += PACKET_HEADER_SIZE;

	app_message_create_internal (
		(AppMessage *) end, id, msg
	);

	// send the packet
	packet_set_network_values (
		message, NULL, client, connection, NULL
	);

	size_t sent = 0;
	test_check_unsigned_eq (
		packet_send (
			message, 0, &sent, false
		), 0, NULL
	);

	test_check_unsigned_ne (sent, 0);

	// done
	packet_delete (message);

}

int main (int argc, const char **argv) {

	(void) signal (SIGPIPE, SIG_IGN);

	MESSAGE_LEN = strlen (MESSAGE);

	test_output_init ();

	(void) printf ("Testing CLIENT load...\n");

	client = client_create ();

	test_check_ptr (client);

	client_set_name (client, client_name);
	test_check_str_eq (client->name, client_name, NULL);
	test_check_str_len (client->name, strlen (client_name), NULL);

	Handler *app_packet_handler = handler_create (app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	client_set_app_handlers (client, app_packet_handler, NULL);

	connection = client_connection_create (
		client, test_address (), 7000, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_max_sleep (connection, 30);

	test_check_int_eq (
		client_connect_and_start (client, connection), 0,
		"Failed to connect to cerver!"
	);

	/*** send **/
	unsigned int n_sent_packets = 0;

	register size_t i = 0;
	for (; i < REQUESTS; i++) {
		// (void) printf ("Sending: %lu...\n", i);

		// test_check_unsigned_eq (
		// 	packet_send_ping (NULL, client, connection, NULL), 0, NULL
		// );
		
		send_app_message (i, MESSAGE);

		n_sent_packets += 1;

		usleep (1000);
	}

	// wait for every response to arrive
	(void) printf ("Waiting...\n");
	const unsigned int received = test_wait_counter (&responses, n_sent_packets, TEST_WAIT_SECONDS);

	/*** check ***/
	(void) printf (
		"\n\n%u responses / %u requests received!\n\n",
		received, n_sent_packets
	);

	test_check_unsigned_eq (
		n_sent_packets,
		received,
		NULL
	);

	/*** end **/
	client_connection_end (client, connection);
	client_teardown (client);

	(void) printf ("Done!\n\n");

	return 0;

}
