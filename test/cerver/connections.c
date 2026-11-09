#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include "../test.h"

// a cerver that answers every request of the connections client,
// test/performance.sh counts its allocations per connection

#define CONNECTIONS_NAME					"connections-cerver"
#define CONNECTIONS_PORT					7400
#define CONNECTIONS_THREADS					4
#define CONNECTIONS_POLL_TIMEOUT			200
#define CONNECTIONS_REQUEST_ECHO			1

#define CONNECTIONS_HANDLER_ARGUMENT		1
#define CONNECTIONS_HANDLER_THREADS			"threads"

static Cerver *cerver = NULL;

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

static void connections_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == CONNECTIONS_REQUEST_ECHO) {
		Packet response;
		packet_init_request (&response, PACKET_TYPE_APP, CONNECTIONS_REQUEST_ECHO);
		packet_set_network_values (
			&response, packet->cerver, packet->client, packet->connection, NULL
		);

		(void) packet_send (&response, 0, NULL, false);
	}

}

static CerverHandlerType connections_handler_type (int argc, char **argv) {

	return (
		(argc > CONNECTIONS_HANDLER_ARGUMENT)
		&& !strcmp (argv[CONNECTIONS_HANDLER_ARGUMENT], CONNECTIONS_HANDLER_THREADS)
	) ? CERVER_HANDLER_TYPE_THREADS : CERVER_HANDLER_TYPE_POLL;

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

	Handler *app_handler = handler_create (connections_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (cerver, app_handler, NULL);

	test_check_unsigned_eq (cerver_start (cerver), 0, "Failed to start cerver!");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	cerver_end ();

	return 0;

}
