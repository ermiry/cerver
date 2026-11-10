#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/client.h>
#include <cerver/packets.h>
#include <cerver/serializer.h>

#include <app/report.h>

#include "../test.h"

// the older peer: it sends reports without the field that the cerver added,
// and reads the answers that have it, ignoring it

#define SCHEMAS_REPORTS				64
#define SCHEMAS_PORT				7000
#define SCHEMAS_MAX_SLEEP			30

static const char *client_name = { "schemas-client" };
static const char *report_name = { "report" };

static unsigned int answers = 0;
static u32 expected_id = 0;

static void schemas_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	ReportOld report;
	if (
		(packet->header.request_type == REPORT_REQUEST)
		&& (packet_get_object (packet, &report_old_schema, NULL, &report) == SERIALIZER_RESULT_NONE)
		&& (report.id == expected_id)
		&& !strcmp (report.name, report_name)
	) {
		answers += 1;
	}

}

static void schemas_send (Client *client, Connection *connection, const u32 id) {

	ReportOld report;
	serializer_object_init (&report_old_schema, &report);
	report.id = id;
	(void) strncpy (report.name, report_name, REPORT_NAME_SIZE - 1);

	Packet *request = packet_create_serialized (
		PACKET_TYPE_APP, REPORT_REQUEST, &report_old_schema, &report
	);

	test_check_ptr (request);

	expected_id = id;
	test_check_unsigned_eq (client_request_to_cerver (client, connection, request), 0, NULL);

	packet_delete (request);

}

int main (int argc, const char **argv) {

	(void) argc;
	(void) argv;

	(void) printf ("Testing CLIENT schemas...\n");

	Client *client = client_create ();
	test_check_ptr (client);

	client_set_name (client, client_name);

	Handler *handler = handler_create (schemas_handler);
	handler_set_direct_handle (handler, true);
	client_set_app_handlers (client, handler, NULL);

	Connection *connection = client_connection_create (
		client, test_address (), SCHEMAS_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_max_sleep (connection, SCHEMAS_MAX_SLEEP);

	test_check_int_eq (client_connect_to_cerver (client, connection), 0, "Failed to connect to cerver!");

	for (u32 id = 1; id <= SCHEMAS_REPORTS; id++) schemas_send (client, connection, id);

	test_check_unsigned_eq (answers, SCHEMAS_REPORTS, NULL);

	(void) client_connection_end (client, connection);
	(void) client_teardown (client);

	(void) printf ("Done!\n\n");

	return 0;

}
