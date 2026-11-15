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
#include <cerver/packets/serializer.h>

#include "../test.h"
#include <app/report.h>

// the newer peer: it reads the reports of an older client, whose missing field takes its default,
// and answers with the new field, that the older client ignores

#define SCHEMAS_THREADS				4
#define SCHEMAS_POLL_TIMEOUT		1000

static const char *cerver_name = "schemas-cerver";

static Cerver *cerver = NULL;

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

// a report without the default of the new field is not answered, so the client fails
static void schemas_answer (const Packet *packet) {

	ReportNew report;
	if (
		(packet_get_object (packet, &report_new_schema, NULL, &report) == SERIALIZER_RESULT_NONE)
		&& (report.extra == REPORT_EXTRA_DEFAULT)
	) {
		report.extra = report.id;

		Packet *response = packet_create_serialized (
			PACKET_TYPE_APP, REPORT_REQUEST, &report_new_schema, &report
		);

		if (response) {
			packet_set_network_values (
				response, packet->cerver, packet->client, packet->connection, NULL
			);

			(void) packet_send (response, 0, NULL, false);

			packet_delete (response);
		}
	}

}

static void schemas_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == REPORT_REQUEST) schemas_answer (packet);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);

	cerver = cerver_create (
		CERVER_TYPE_CUSTOM, cerver_name,
		CERVER_DEFAULT_PORT, PROTOCOL_TCP, false,
		CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);

	cerver_set_thpool_n_threads (cerver, SCHEMAS_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (cerver, SCHEMAS_POLL_TIMEOUT);

	Handler *handler = handler_create (schemas_handler);
	handler_set_direct_handle (handler, true);
	cerver_set_app_handlers (cerver, handler, NULL);

	test_check_unsigned_eq (cerver_start (cerver), 0, "Failed to start cerver!");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	return 0;

}
