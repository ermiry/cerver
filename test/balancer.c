#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/balancer.h>

#include "test.h"

#pragma region services

static const char *none_string = "None";
static const char *connecting_string = "Connecting";
static const char *ready_string = "Ready";
static const char *working_string = "Working";
static const char *disconnecting_string = "Disconnecting";
static const char *disconnected_string = "Disconnected";
static const char *unavailable_string = "Unavailable";
static const char *ended_string = "Ended";

static const char *service_name = "test";

static void balancer_service_on_status_change (void *service_ptr) {

	Service *service = (Service *) service_ptr;

	(void) printf ("%s\n", balancer_service_get_status_string (service));

}

static void test_balancer_service_status_to_string (void) {

	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_NONE), none_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_CONNECTING), connecting_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_READY), ready_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_WORKING), working_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_DISCONNECTING), disconnecting_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_DISCONNECTED), disconnected_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_UNAVAILABLE), unavailable_string, NULL);
	test_check_str_eq (balancer_service_status_to_string (SERVICE_STATUS_ENDED), ended_string, NULL);

}

static void test_balancer_service_status_description (void) {

	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_NONE));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_CONNECTING));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_READY));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_WORKING));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_DISCONNECTING));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_DISCONNECTED));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_UNAVAILABLE));
	test_check_ptr (balancer_service_status_description (SERVICE_STATUS_ENDED));

}

static Service *test_balancer_service_create (void) {

	Service *service = balancer_service_create ();

	test_check_unsigned_eq (service->id, 0, NULL);
	test_check_str_empty (service->name);

	test_check_int_eq (service->status, SERVICE_STATUS_NONE, NULL);
	test_check_null_ptr (service->on_status_change);

	test_check_null_ptr (service->connection);
	test_check_unsigned_eq (service->reconnect_wait_time, SERVICE_DEFAULT_WAIT_TIME, NULL);
	test_check_unsigned_eq (service->reconnect_thread_id, 0, NULL);

	test_check_int_eq (service->forward_pipe_fds[0], 0, NULL);
	test_check_int_eq (service->forward_pipe_fds[1], 0, NULL);

	test_check_int_eq (service->receive_pipe_fds[0], 0, NULL);
	test_check_int_eq (service->receive_pipe_fds[1], 0, NULL);

	test_check_ptr (service->stats);

	return service;

}

static void test_balancer_service_configuration (void) {

	const unsigned int wait_time = 60;

	Service *service = test_balancer_service_create ();

	test_check_int_eq (balancer_service_get_status (service), SERVICE_STATUS_NONE, NULL);
	test_check_str_eq (balancer_service_get_status_string (service), none_string, NULL);

	balancer_service_set_on_status_change (service, balancer_service_on_status_change);
	test_check_ptr_eq (service->on_status_change, balancer_service_on_status_change);

	test_check_str_empty (balancer_service_get_name (service));
	balancer_service_set_name (service, service_name);
	test_check_str_eq (balancer_service_get_name (service), service_name, NULL);

	balancer_service_set_reconnect_wait_time (service, wait_time);
	test_check_unsigned_eq (service->reconnect_wait_time, wait_time, NULL);

	balancer_service_delete (service);

}

#pragma endregion

#pragma region balancer

static const char *balancer_name = "test-balancer";
static const unsigned int n_services = 2;

static void on_unavailable_services (void *packet_ptr) {

	Packet *packet = (Packet *) packet_ptr;

	packet_header_print (&packet->header);

}

static Balancer *test_balancer_create (const BalancerType type) {

	Balancer *balancer = balancer_create (
		balancer_name,
		type,
		7000,
		CERVER_DEFAULT_CONNECTION_QUEUE,
		n_services
	);

	test_check_ptr (balancer);
	test_check_str_eq (balancer->name->str, balancer_name, NULL);
	test_check_str_len (balancer->name->str, strlen (balancer_name), NULL);
	test_check_int_eq (balancer->type, type, NULL);
	test_check_ptr (balancer->cerver);
	test_check_ptr (balancer->client);
	test_check_int_eq (balancer->next_service, 0, NULL);
	test_check_int_eq (balancer->n_services, n_services, NULL);
	test_check_ptr (balancer->services);
	test_check_null_ptr (balancer->on_unavailable_services);
	test_check_ptr (balancer->stats);
	test_check_ptr (balancer->mutex);

	return balancer;

}

static void test_balancer_create_round_robin (void) {

	const BalancerType balancer_type = BALANCER_TYPE_ROUND_ROBIN;

	Balancer *balancer = test_balancer_create (balancer_type);

	test_check_int_eq (balancer_get_type (balancer), balancer_type, NULL);

	/*** cerver config ***/
	cerver_set_reusable_address_flags (balancer->cerver, true);
	test_check_bool_eq (balancer->cerver->reusable, true, NULL);

	cerver_set_handler_type (balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
	test_check_int_eq (balancer->cerver->handler_type, CERVER_HANDLER_TYPE_THREADS, NULL);

	cerver_set_handle_detachable_threads (balancer->cerver, true);
	test_check_bool_eq (balancer->cerver->handle_detachable_threads, true, NULL);

	/*** balancer config ***/
	balancer_set_on_unavailable_services (balancer, on_unavailable_services);
	test_check_ptr_eq (balancer->on_unavailable_services, on_unavailable_services);

	/*** services config ***/
	u8 service_result = 0;
	service_result = balancer_service_register (balancer, "127.0.0.1", 7001);
	test_check_unsigned_eq (service_result, 0, "Failed to register FIRST service!");

	service_result = balancer_service_register (balancer, "127.0.0.1", 7002);
	test_check_unsigned_eq (service_result, 0, "Failed to register SECOND service!");

	balancer_delete (balancer);

}

static void test_balancer_create_handler_id (void) {

	const BalancerType balancer_type = BALANCER_TYPE_HANDLER_ID;

	Balancer *balancer = test_balancer_create (balancer_type);

	test_check_int_eq (balancer_get_type (balancer), balancer_type, NULL);

	/*** cerver config ***/
	cerver_set_reusable_address_flags (balancer->cerver, true);
	test_check_bool_eq (balancer->cerver->reusable, true, NULL);

	cerver_set_handler_type (balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
	test_check_int_eq (balancer->cerver->handler_type, CERVER_HANDLER_TYPE_THREADS, NULL);

	cerver_set_handle_detachable_threads (balancer->cerver, true);
	test_check_bool_eq (balancer->cerver->handle_detachable_threads, true, NULL);

	/*** balancer config ***/
	balancer_set_on_unavailable_services (balancer, on_unavailable_services);
	test_check_ptr_eq (balancer->on_unavailable_services, on_unavailable_services);

	/*** services config ***/
	u8 service_result = 0;
	service_result = balancer_service_register (balancer, "127.0.0.1", 7001);
	test_check_unsigned_eq (service_result, 0, "Failed to register FIRST service!");

	service_result = balancer_service_register (balancer, "127.0.0.1", 7002);
	test_check_unsigned_eq (service_result, 0, "Failed to register SECOND service!");

	balancer_delete (balancer);

}

#pragma endregion

int main (int argc, char **argv) {

	srand ((unsigned) time (NULL));

	(void) printf ("Testing BALANCER...\n");

	// services
	test_balancer_service_status_to_string ();
	test_balancer_service_status_description ();
	test_balancer_service_configuration ();

	// balancer
	test_balancer_create_round_robin ();
	test_balancer_create_handler_id ();

	(void) printf ("\nDone with BALANCER tests!\n\n");

	return 0;

}
