#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>
#include <unistd.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include "upstream.h"

#include "../test.h"

// a service behind the balancer of the process pairs:
// it echoes bodies of any size, tells its id, answers slowly, or stops when it is asked to

#define UPSTREAM_NAME					"upstream-cerver"
#define UPSTREAM_THREADS				4
#define UPSTREAM_NUMBER_BASE			10

#define UPSTREAM_PORT_ARGUMENT			"-p"
#define UPSTREAM_ID_ARGUMENT			"-i"

static Cerver *cerver = NULL;
static UpstreamIdentity identity = { 0, 0 };

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

// the response is sent with the route id of the request, so the balancer routes it back
static void upstream_answer (
	const Packet *packet, const void *body, const size_t body_size
) {

	Packet *response = packet_generate_request (
		PACKET_TYPE_APP, packet->header.request_type, body, body_size
	);

	test_check_ptr (response);

	packet_set_route_id (response, packet->header.route_id);

	packet_set_network_values (
		response, packet->cerver, packet->client, packet->connection, NULL
	);

	(void) packet_send (response, 0, NULL, false);

	packet_delete (response);

}

static void upstream_slow (const Packet *packet) {

	const struct timespec delay = { 0, UPSTREAM_SLOW_NANOSECONDS };
	(void) nanosleep (&delay, NULL);

	upstream_answer (packet, packet->data, packet->data_size);

}

static void upstream_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	switch (packet->header.request_type) {
		case UPSTREAM_REQUEST_ECHO:
			upstream_answer (packet, packet->data, packet->data_size);
			break;

		case UPSTREAM_REQUEST_IDENTITY:
			upstream_answer (packet, &identity, sizeof (UpstreamIdentity));
			break;

		case UPSTREAM_REQUEST_EXIT:
			(void) cerver_shutdown (packet->cerver);
			break;

		case UPSTREAM_REQUEST_SLOW:
			upstream_slow (packet);
			break;

		default: break;
	}

}

static unsigned long upstream_argument (
	int argc, char **argv, const char *name, const unsigned long fallback
) {

	unsigned long value = fallback;
	for (int i = 1; (i + 1) < argc; i++) {
		if (!strcmp (argv[i], name)) {
			value = strtoul (argv[i + 1], NULL, UPSTREAM_NUMBER_BASE);
		}
	}

	return value;

}

int main (int argc, char **argv) {

	test_output_init ();

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	identity.id = (uint32_t) upstream_argument (argc, argv, UPSTREAM_ID_ARGUMENT, 0);
	identity.pid = (uint32_t) getpid ();
	const u16 port = (u16) upstream_argument (
		argc, argv, UPSTREAM_PORT_ARGUMENT, UPSTREAM_FIRST_SERVICE_PORT
	);

	cerver = cerver_create (
		CERVER_TYPE_CUSTOM, UPSTREAM_NAME,
		port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, UPSTREAM_THREADS);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_max_received_packet_size (cerver, UPSTREAM_MAX_PACKET_SIZE);

	Handler *app_handler = handler_create (upstream_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (cerver, app_handler, NULL);

	test_check_unsigned_eq (cerver_start (cerver), 0, "Failed to start the upstream cerver!");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	cerver_end ();

	return 0;

}
