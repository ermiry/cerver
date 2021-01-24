#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/balancer.h>
#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/network.h>

#include <app/handler.h>

#include "cerver.h"

#include "../test.h"

static const char *balancer_name = "test-balancer";
static unsigned int n_services = 2;

static Balancer *load_balancer = NULL;

static void end (int dummy) {
	
	balancer_teardown (load_balancer);

	cerver_end ();

	exit (0);

}

int main (int argc, char **argv) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	/*** create ***/
	load_balancer = balancer_create (
		balancer_name,
		BALANCER_TYPE_ROUND_ROBIN,
		7000,
		CERVER_DEFAULT_CONNECTION_QUEUE,
		n_services
	);

	test_check_ptr (load_balancer);
	test_check_int_eq (load_balancer->type, BALANCER_TYPE_ROUND_ROBIN, NULL);
	test_check_ptr (load_balancer->cerver);
	test_check_str_eq (load_balancer->name->str, balancer_name, NULL);
	test_check_str_len (load_balancer->name->str, strlen (balancer_name), NULL);

	/*** configuration ***/
	cerver_set_reusable_address_flags (load_balancer->cerver, true);
	test_check_bool_eq (load_balancer->cerver->reusable, true, NULL);

	cerver_set_handler_type (load_balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
	test_check_int_eq (load_balancer->cerver->handler_type, CERVER_HANDLER_TYPE_THREADS, NULL);
	
	cerver_set_handle_detachable_threads (load_balancer->cerver, true);
	test_check_bool_eq (load_balancer->cerver->handle_detachable_threads, true, NULL);

	/*** services ***/
	u8 service_result = 0;

	char *service_ip = network_hostname_to_ip ("service-1");
	test_check_ptr (service_ip);
	(void) printf ("Service 0 ip: %s\n", service_ip);
	service_result = balancer_service_register (load_balancer, service_ip, 7001);
	test_check_unsigned_eq (service_result, 0, "Failed to register FIRST service!");
	free (service_ip);

	service_ip = network_hostname_to_ip ("service-2");
	test_check_ptr (service_ip);
	(void) printf ("Service 1 ip: %s\n", service_ip);
	service_result = balancer_service_register (load_balancer, service_ip, 7002);
	test_check_unsigned_eq (service_result, 0, "Failed to register SECOND service!");
	free (service_ip);

	/*** events ***/
	u8 event_result = 0;

	event_result = cerver_event_register (
		load_balancer->cerver,
		CERVER_EVENT_CLIENT_CONNECTED,
		on_client_connected, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		load_balancer->cerver,
		CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
		on_client_close_connection, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	/*** start ***/
	test_check_unsigned_eq (
		balancer_start (load_balancer), 0, "Failed to start balancer!"
	);

	cerver_end ();

	return 0;

}