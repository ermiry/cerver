#include <stdlib.h>
#include <stdio.h>

#include <cerver/client.h>
#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include <app/app.h>

// static const char *MESSAGE = {
// 	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
// 	"incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud "
// 	"exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat." 
// };

static const char *MESSAGE = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
	"incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud "
	"exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. "
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
	"incididunt ut labore et dolore magna aliqua."
};

static Client *client = NULL;
static Connection *connection = NULL;

static unsigned int responses = 0;

static void app_handler (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		switch (packet->header.request_type) {
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

static u8 single_app_message (const char *msg) {

	u8 retval = 1;

	Packet *message = packet_create (
		PACKET_TYPE_APP, APP_REQUEST_MESSAGE,
		msg, strlen (msg)
	);

	message->header.handler_id = 10;
	message->header.sock_fd = 1023;

	(void) packet_generate (message);

	// send the packet
	packet_set_network_values (
		message, NULL, client, connection, NULL
	);

	size_t sent = 0;
	retval = packet_send (message, 0, &sent, false);

	packet_delete (message);

	return retval;

}

static void send_pings (void) {

	unsigned int n_sent_packets = 0;
	register size_t i = 0;
	for (; i < 100; i++) {
		n_sent_packets += !packet_send_ping (
			NULL,
			client, connection,
			NULL
		);
	}

	// wait for remaining packets
	(void) sleep (4);

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

	else {
		(void) printf ("\n\n");
		cerver_log_success (
			"Got %lu / %lu responses!",
			client->stats->received_packets->n_test_packets,
			n_sent_packets
		);
		(void) printf ("\n\n");
	}

}

static void send_messages (void) {

	unsigned int n_sent_packets = 0;
	register size_t i = 0;
	for (; i < 100; i++) {
		n_sent_packets += !single_app_message (MESSAGE);
	}

	// wait for remaining packets
	(void) sleep (4);

	// check that we received matching responses
	if (
		client->stats->received_packets->n_app_packets
		!= n_sent_packets
	) {
		(void) printf ("\n\n");
		cerver_log_error (
			"Responses %lu don't match n_sent_packets %lu!",
			client->stats->received_packets->n_app_packets,
			n_sent_packets
		);
		(void) printf ("\n\n");
	}

	else {
		(void) printf ("\n\n");
		cerver_log_success (
			"Got %lu / %lu responses!",
			client->stats->received_packets->n_app_packets,
			n_sent_packets
		);
		(void) printf ("\n\n");
	}

}

int main (int argc, const char **argv) {

	cerver_log_init ();

	// const char *curr_arg = NULL;
	const char *type = NULL;
	if (argc > 1) {
		type = argv[1];
	}

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
		/*** send **/
		if (!type || !strcmp ("ping", type)) {
			send_pings ();
		}

		else if (!strcmp ("app", type)) {
			send_messages ();
		}

		else {
			cerver_log_error ("Wrong type!");
		}
	}

	else {
		cerver_log_error ("Failed to connect to cerver!");
	}

	client_connection_end (client, connection);
	client_teardown (client);

	cerver_log_line_break ();
	cerver_log_success ("Done!\n\n");

	cerver_log_end ();

	return 0;

}