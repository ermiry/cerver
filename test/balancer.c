#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/balancer.h>

#include "test.h"

static const char *balancer_name = "test-balancer";
static const unsigned int n_services = 2;

static Balancer *test_balancer_create (void) {

	Balancer *balancer = balancer_create (
		balancer_name,
		BALANCER_TYPE_ROUND_ROBIN,
		7000,
		CERVER_DEFAULT_CONNECTION_QUEUE,
		n_services
	);

	test_check_ptr (balancer);
	test_check_int_eq (balancer->type, BALANCER_TYPE_ROUND_ROBIN, NULL);
	test_check_ptr (balancer->cerver);
	test_check_str_eq (balancer->name->str, balancer_name, NULL);
	test_check_str_len (balancer->name->str, strlen (balancer_name), NULL);

	return balancer;

}

static void test_balancer_base_configuration (void) {

	Balancer *balancer = test_balancer_create ();

	cerver_set_reusable_address_flags (balancer->cerver, true);
	test_check_bool_eq (balancer->cerver->reusable, true, NULL);

	cerver_set_handler_type (balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
	test_check_int_eq (balancer->cerver->handler_type, CERVER_HANDLER_TYPE_THREADS, NULL);
	
	cerver_set_handle_detachable_threads (balancer->cerver, true);
	test_check_bool_eq (balancer->cerver->handle_detachable_threads, true, NULL);

	u8 service_result = 0;
	service_result = balancer_service_register (balancer, "127.0.0.1", 7001);
	test_check_unsigned_eq (service_result, 0, "Failed to register FIRST service!");

	service_result = balancer_service_register (balancer, "127.0.0.1", 7002);
	test_check_unsigned_eq (service_result, 0, "Failed to register SECOND service!");

	balancer_delete (balancer);

}

int main (int argc, char **argv) {

	srand ((unsigned) time (NULL));

	(void) printf ("Testing BALANCER...\n");

	test_balancer_base_configuration ();

	(void) printf ("\nDone with BALANCER tests!\n\n");

	return 0;

}