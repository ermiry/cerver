#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include "app.h"

// sends back a test message to the client of the packet,
// with the sock fd of the packet so a balancer routes it back
void send_test_message (const Packet *packet) {

	Packet response = {
		.cerver = packet->cerver,
		.client = packet->client,
		.connection = packet->connection,
		.lobby = packet->lobby,

		.packet_type = PACKET_TYPE_APP,
		.req_type = APP_REQUEST_TEST,

		.data_size = 0,
		.data = NULL,
		.data_ptr = NULL,
		.data_end = NULL,
		.data_ref = false,

		.remaining_data = 0,

		.header = (PacketHeader) {
			.packet_type = PACKET_TYPE_APP,
			.packet_size = PACKET_HEADER_SIZE,

			.handler_id = 0,
			.flags = 0,

			.request_type = APP_REQUEST_TEST,

			.route_id = packet->header.route_id,

			.version = packet_version_local ()
		},

		.version = (PacketVersion) {
			.protocol_id = 0,
			.protocol_version = {
				.major = 0,
				.minor = 0
			}
		},

		// the header is encoded from the packet when it is sent
		.packet_size = PACKET_HEADER_SIZE,
		.packet = response.encoded_header,
		.packet_ref = false,

		.release = NULL,
		.release_args = NULL,

		.data_class = 0,
		.recycle_next = NULL
	};

	size_t sent = 0;
	if (packet_send (&response, 0, &sent, false)) {
		cerver_log_error ("Failed to send test message!");
	}

}

// send back a test message
static void app_handler_test (const Packet *packet) {

	send_test_message (packet);

}

// echo the same message back
static void app_handler_message (const Packet *packet) {

	if (!packet->data || (packet->data_size < sizeof (AppMessage))) {
		cerver_log_error (
			"Message packet of %lu bytes is too small!", packet->data_size
		);

		return;
	}

	const AppMessage *app_message = (const AppMessage *) packet->data;

	// print the client's message
	#ifdef EXAMPLE_APP_DEBUG
	(void) printf ("|%s|\n", app_message->message);
	#endif

	Packet *response = packet_create (
		PACKET_TYPE_APP, APP_REQUEST_MESSAGE,
		app_message, sizeof (AppMessage)
	);

	if (!response) return;

	// the response goes back through the route of the balancer
	response->header.route_id = packet->header.route_id;

	if (!packet_generate (response)) {
		// send the packet
		packet_set_network_values (
			response, packet->cerver, packet->client, packet->connection, NULL
		);

		size_t sent = 0;
		(void) packet_send (response, 0, &sent, false);
	}

	packet_delete (response);

}

void app_handler (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		switch (packet->header.request_type) {
			case APP_REQUEST_NONE: break;

			case APP_REQUEST_TEST:
				app_handler_test (packet);
				break;

			case APP_REQUEST_MESSAGE:
				app_handler_message (packet);
				break;

			// the multi message is not implemented in the example app
			case APP_REQUEST_MULTI: break;

			default: break;
		}
	}

}
