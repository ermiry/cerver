#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/packets.h>

#include "app.h"

// send back a test message
static void app_handler_test (const Packet *packet) {

	PacketHeader header = {
		.packet_type = PACKET_TYPE_APP,
		.packet_size = sizeof (PacketHeader),

		.handler_id = 0,

		.request_type = APP_REQUEST_TEST,

		.sock_fd = 0,
	};

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

		.header = NULL,
		.version = NULL,
		.packet_size = sizeof (PacketHeader),
		.packet = &header,
		.packet_ref = false
	};

	size_t sent = 0;
	(void) packet_send (&response, 0, &sent, false);

}

// echo the same message back
static void app_handler_message (const Packet *packet) {
	
	AppMessage *app_message = (AppMessage *) (char *) packet->data;

	Packet *response = packet_new ();

	// generate the packet
	size_t packet_len = sizeof (PacketHeader) + sizeof (AppMessage);
	response->packet = malloc (packet_len);
	response->packet_size = packet_len;

	char *end = (char *) response->packet;
	PacketHeader *header = (PacketHeader *) end;
	header->packet_type = PACKET_TYPE_APP;
	header->packet_size = packet_len;

	header->request_type = APP_REQUEST_MESSAGE;

	end += sizeof (PacketHeader);

	(void) memcpy (end, app_message, sizeof (AppMessage));

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

		switch (packet->header->request_type) {
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