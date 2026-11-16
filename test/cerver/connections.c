#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/utils/log.h>

#include "../test.h"

// a cerver that answers every request of the connections client,
// test/allocations.sh counts its allocations per connection,
// and per packet with the handlers mode, where the app handler has its own thread and queue

#define CONNECTIONS_NAME					"connections-cerver"
#define CONNECTIONS_PORT					7400
#define CONNECTIONS_THREADS					4
#define CONNECTIONS_POLL_TIMEOUT			200
#define CONNECTIONS_REQUEST_ECHO			1

#define CONNECTIONS_HANDLER_ARGUMENT		1
#define CONNECTIONS_HANDLER_THREADS			"threads"
#define CONNECTIONS_HANDLER_QUEUED			"handlers"

static Cerver *cerver = NULL;

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

static void connections_answer (const Packet *packet) {

	if (packet->header.request_type == CONNECTIONS_REQUEST_ECHO) {
		Packet response;
		packet_init_request (&response, PACKET_TYPE_APP, CONNECTIONS_REQUEST_ECHO);
		packet_set_network_values (
			&response, packet->cerver, packet->client, packet->connection, NULL
		);

		(void) packet_send (&response, 0, NULL, false);
	}

}

// a direct handler gets the packet
static void connections_handler (void *packet_ptr) {

	connections_answer ((const Packet *) packet_ptr);

}

// a handler with its own thread and queue gets a HandlerData with the packet
static void connections_queued_handler (void *handler_data_ptr) {

	connections_answer (((const HandlerData *) handler_data_ptr)->packet);

}

static bool connections_mode_is (int argc, char **argv, const char *mode) {

	return (argc > CONNECTIONS_HANDLER_ARGUMENT) && !strcmp (argv[CONNECTIONS_HANDLER_ARGUMENT], mode);

}

static CerverHandlerType connections_handler_type (int argc, char **argv) {

	return connections_mode_is (argc, argv, CONNECTIONS_HANDLER_THREADS) ?
		CERVER_HANDLER_TYPE_THREADS : CERVER_HANDLER_TYPE_POLL;

}

// the handlers mode pushes each packet to the queue of the app handler thread,
// the other modes handle it in the thread that received it
static Handler *connections_app_handler (int argc, char **argv) {

	Handler *app_handler = NULL;
	if (connections_mode_is (argc, argv, CONNECTIONS_HANDLER_QUEUED)) {
		app_handler = handler_create (connections_queued_handler);
	}

	else {
		app_handler = handler_create (connections_handler);
		handler_set_direct_handle (app_handler, true);
	}

	return app_handler;

}

int main (int argc, char **argv) {

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);

	cerver_init ();

	cerver_log_set_quiet (true);

	cerver = cerver_create (
		CERVER_TYPE_CUSTOM, CONNECTIONS_NAME,
		CONNECTIONS_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, CONNECTIONS_THREADS);
	cerver_set_handler_type (cerver, connections_handler_type (argc, argv));
	cerver_set_poll_time_out (cerver, CONNECTIONS_POLL_TIMEOUT);

	cerver_set_app_handlers (cerver, connections_app_handler (argc, argv), NULL);

	test_check_unsigned_eq (cerver_start (cerver), 0, "Failed to start cerver!");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	cerver_end ();

	return 0;

}
