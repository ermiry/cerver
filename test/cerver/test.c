#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/cerver.h>

#include "../test.h"

static const char *cerver_name = "test-cerver";

static Cerver *test_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		cerver_name,
		CERVER_DEFAULT_PORT,
		PROTOCOL_TCP,
		false,
		CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_int_eq (cerver->type, CERVER_TYPE_CUSTOM, NULL);
	test_check_ptr (cerver->info);
	test_check_str_eq (cerver->info->name, cerver_name, NULL);
	test_check_str_len (cerver->info->name, strlen (cerver_name), NULL);
	test_check_int_eq (cerver->port, CERVER_DEFAULT_PORT, NULL);
	test_check_int_eq (cerver->protocol, PROTOCOL_TCP, NULL);
	test_check_bool_eq (cerver->use_ipv6, false, NULL);
	test_check_int_eq (cerver->connection_queue, CERVER_DEFAULT_CONNECTION_QUEUE, NULL);

	return cerver;

}

static void test_cerver_base_configuration (void) {

	Cerver *cerver = test_cerver_create ();

	cerver_set_receive_buffer_size (cerver, 4096);
	test_check_int_eq (cerver->receive_buffer_size, 4096, NULL);

	cerver_set_thpool_n_threads (cerver, 4);
	test_check_int_eq (cerver->n_thpool_threads, 4, NULL);

	cerver_set_reusable_address_flags (cerver, true);
	test_check_bool_eq (cerver->reusable, true, NULL);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	test_check_int_eq (cerver->handler_type, CERVER_HANDLER_TYPE_POLL, NULL);

	cerver_set_poll_time_out (cerver, 2000);
	test_check_int_eq (cerver->poll_timeout, 2000, NULL);

	cerver_delete (cerver);

}

#define CERVER_INFO_FILL				'a'
#define CERVER_INFO_PORT				7400

// the info of a cerver comes from the network as a summary that its schema validates,
// the longest texts fit in the biggest encoded summary
static void test_cerver_deserialize (void) {

	CerverSummary summary;
	serializer_object_init (&cerver_summary_schema, &summary);

	summary.type = CERVER_TYPE_CUSTOM;
	(void) memset (summary.name, CERVER_INFO_FILL, CERVER_SUMMARY_NAME_SIZE - 1);
	(void) memset (summary.welcome, CERVER_INFO_FILL, CERVER_SUMMARY_WELCOME_SIZE - 1);
	summary.protocol = PROTOCOL_TCP;
	summary.port = CERVER_INFO_PORT;
	summary.use_ipv6 = true;
	summary.auth_required = true;

	u8 buffer[CERVER_SUMMARY_MAX_ENCODED_SIZE];
	size_t written = 0;
	test_check_unsigned_eq (
		serializer_encode (&cerver_summary_schema, &summary, buffer, CERVER_SUMMARY_MAX_ENCODED_SIZE, &written),
		SERIALIZER_RESULT_NONE, NULL
	);

	CerverSummary decoded;
	test_check_unsigned_eq (
		serializer_decode (&cerver_summary_schema, buffer, written, NULL, &decoded),
		SERIALIZER_RESULT_NONE, NULL
	);

	CerverReport *report = cerver_deserialize (&decoded);
	test_check_ptr (report);

	test_check_unsigned_eq (report->name->len, CERVER_SUMMARY_NAME_SIZE - 1, NULL);
	test_check_unsigned_eq (report->welcome->len, CERVER_SUMMARY_WELCOME_SIZE - 1, NULL);
	test_check_true (report->use_ipv6);
	test_check_true (report->auth_required);
	test_check_false (report->uses_sessions);
	test_check_unsigned_eq (report->port, CERVER_INFO_PORT, NULL);

	cerver_report_delete (report);

	// an empty welcome is not created
	decoded.welcome[0] = '\0';
	report = cerver_deserialize (&decoded);
	test_check_ptr (report);
	test_check_null_ptr (report->welcome);
	cerver_report_delete (report);

}

int main (int argc, char **argv) {

	test_output_init ();

	srand ((unsigned) time (NULL));

	(void) printf ("Testing CERVER...\n");

	test_cerver_base_configuration ();
	test_cerver_deserialize ();

	(void) printf ("\nDone with CERVER tests!\n\n");

	return 0;

}
