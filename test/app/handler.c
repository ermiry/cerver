#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/packets.h>

#include "app.h"

static void app_handler_test (const Packet *packet) {

}

static void app_handler_message (const Packet *packet) {
	
}

static void app_handler_multi (const Packet *packet) {
	
}

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