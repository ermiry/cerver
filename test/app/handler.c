#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include "app.h"

static const char *MESSAGE = { 
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
	"sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
	"Ut enim ad minim veniam, quis nostrud exercitation ullamco "
	"laboris nisi ut aliquip ex ea commodo consequat."
};

// send back a test message
static void app_handler_test (const Packet *packet) {

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

		.encoded_header = { 0 },

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
	(void) packet_send (&response, 0, &sent, false);

}

// echo the same message back
static void app_handler_message (const Packet *packet) {

	if (!packet->data || (packet->data_size < sizeof (AppMessage))) {
		cerver_log_error (
			"Message packet of %lu bytes is too small!", packet->data_size
		);

		return;
	}

	AppMessage *app_message = (AppMessage *) (char *) packet->data;

	if (strncmp (MESSAGE, app_message->message, APP_MESSAGE_LEN)) {
		cerver_log_error ("Message [%lu] mismatch!", app_message->id);
	}

	Packet *response = packet_create (
		PACKET_TYPE_APP, APP_REQUEST_MESSAGE,
		app_message, sizeof (AppMessage)
	);

	if (!response) return;

	// the response goes back through the route of the balancer
	response->header.route_id = packet->header.route_id;
	(void) packet_generate (response);

	// send the packet
	packet_set_network_values (
		response, packet->cerver, packet->client, packet->connection, NULL
	);

	size_t sent = 0;
	(void) packet_send (response, 0, &sent, false);

	// done
	packet_delete (response);

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void app_handler_multi (const Packet *packet) {
	
	// TODO:

}

#pragma GCC diagnostic pop

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

			case APP_REQUEST_MULTI:
				app_handler_multi (packet);
				break;

			default: break;
		}
	}

}
