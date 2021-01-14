#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/cerver.h>

#include "test.h"

static const char *cerver_name = "test-cerver";

static Cerver *test_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		cerver_name,
		7000,
		PROTOCOL_TCP,
		false,
		10
	);

	test_check_ptr (cerver);
	test_check_int_eq (cerver->type, CERVER_TYPE_CUSTOM, NULL);
	test_check_ptr (cerver->info);
	test_check_ptr (cerver->info->name);
	test_check_str_eq (cerver->info->name->str, cerver_name, NULL);
	test_check_str_len (cerver->info->name->str, strlen (cerver_name), NULL);
	test_check_int_eq (cerver->port, 7000, NULL);
	test_check_int_eq (cerver->protocol, PROTOCOL_TCP, NULL);
	test_check_bool_eq (cerver->use_ipv6, false, NULL);
	test_check_int_eq (cerver->connection_queue, 10, NULL);

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

int main (int argc, char **argv) {

	srand ((unsigned) time (NULL));

	(void) printf ("Testing CERVER...\n");

	test_cerver_base_configuration ();

	(void) printf ("\nDone with CERVER tests!\n\n");

	return 0;

}