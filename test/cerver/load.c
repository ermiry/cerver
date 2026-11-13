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

#include <app/app.h>
#include <app/handler.h>

#include "cerver.h"
#include "upstream.h"

#include "../test.h"

// a service that went down is tried again every second, so the pairs that stop one see it back quickly
#define LOAD_RECONNECT_SECONDS		1

#define LOAD_NO_SERVICE				-1

static const char *balancer_name = "test-balancer";
static unsigned int n_services = 2;

static Balancer *load_balancer = NULL;

// the balancer is released in start () when balancer_start () returns
static void end (int dummy) {

	(void) dummy;

	if (load_balancer) (void) cerver_shutdown (load_balancer->cerver);

}

static BalancerType get_balancer_type (int argc, char **argv) {

	BalancerType type = BALANCER_TYPE_NONE;
	if (argc > 1) {
		int j = 0;
		const char *curr_arg = NULL;
		for (int i = 1; i < argc; i++) {
			curr_arg = argv[i];

			// port
			if (!strcmp (curr_arg, "--type")) {
				j = i + 1;
				if (j <= argc) {
					if (!strcmp ("round_robin", argv[j]))
						type = BALANCER_TYPE_ROUND_ROBIN;

					else if (!strcmp ("handler_id", argv[j]))
						type = BALANCER_TYPE_HANDLER_ID;

					else if (!strcmp ("custom", argv[j]))
						type = BALANCER_TYPE_CUSTOM;

					i++;
				}
			}

			else {
				cerver_log_warning (
					"Unknown argument: %s", curr_arg
				);
			}
		}
	}

	return type;

}

// the custom balancer selects the service with the id of the message in the body,
// the packets that are not messages do not have a service
static int load_select_service (
	const PacketHeader *header, const char *body, const size_t body_size, void *args
) {

	const unsigned int services = *((const unsigned int *) args);

	int selected = LOAD_NO_SERVICE;
	if ((header->request_type == APP_REQUEST_MESSAGE) && body && (body_size >= sizeof (AppMessage))) {
		const AppMessage *message = (const AppMessage *) body;
		selected = (int) (message->id % services);
	}

	return selected;

}

static void start (const BalancerType type) {

	/*** create ***/
	load_balancer = balancer_create (
		balancer_name,
		type,
		7000,
		CERVER_DEFAULT_CONNECTION_QUEUE,
		n_services
	);

	test_check_ptr (load_balancer);
	test_check_int_eq (load_balancer->type, type, NULL);
	test_check_ptr (load_balancer->cerver);
	test_check_str_eq (load_balancer->name->str, balancer_name, NULL);
	test_check_str_len (load_balancer->name->str, strlen (balancer_name), NULL);

	/*** configuration ***/
	test_check_unsigned_eq (cerver_set_bind_address (load_balancer->cerver, test_address ()), 0, NULL);

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

	for (unsigned int i = 0; i < n_services; i++) {
		balancer_service_set_reconnect_wait_time (load_balancer->services[i], LOAD_RECONNECT_SECONDS);
	}

	balancer_set_select_service (load_balancer, load_select_service, &n_services);

	// the big packets of the balancer/large pair go through the balancer both ways
	cerver_set_max_received_packet_size (load_balancer->cerver, UPSTREAM_MAX_PACKET_SIZE);
	client_set_max_received_packet_size (load_balancer->client, UPSTREAM_MAX_PACKET_SIZE);

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

	test_check_unsigned_eq (balancer_teardown (load_balancer), 0, NULL);
	load_balancer = NULL;

}

int main (int argc, char **argv) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	if (argc > 1) {
		start (get_balancer_type (argc, argv));
	}

	cerver_end ();

	return 0;

}
