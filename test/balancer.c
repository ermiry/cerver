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
	test_check_bool_eq (service->reconnect_thread_running, false, NULL);
	test_check_unsigned_eq (service->reconnect_thread_id, 0, NULL);

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
	test_check_null_ptr (balancer->select_service);
	test_check_null_ptr (balancer->select_service_args);
	test_check_ptr (balancer->stats);
	test_check_ptr (balancer->mutex);

	return balancer;

}

static int test_balancer_select_service (
	const PacketHeader *header, const char *body, const size_t body_size, void *args
) {

	(void) body;
	(void) body_size;
	(void) args;

	return (int) header->request_type;

}

// #299: a custom balancer takes the method that selects the service of each packet
static void test_balancer_create_custom (void) {

	const BalancerType balancer_type = BALANCER_TYPE_CUSTOM;

	Balancer *balancer = test_balancer_create (balancer_type);

	test_check_int_eq (balancer_get_type (balancer), balancer_type, NULL);
	test_check_str_eq (balancer_type_to_string (balancer_type), "Custom", NULL);

	int args = 0;
	balancer_set_select_service (balancer, test_balancer_select_service, &args);
	test_check_ptr_eq (balancer->select_service, test_balancer_select_service);
	test_check_ptr_eq (balancer->select_service_args, &args);

	balancer_set_select_service (NULL, test_balancer_select_service, &args);

	balancer_delete (balancer);

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

#pragma region routes

#define ROUTES_SOCK_FD					5
#define ROUTES_OTHER_SOCK_FD			6
#define ROUTES_SLOT						0x12345u
#define ROUTES_GENERATION				0x0abc

// the id has the generation in its high bits and the slot in its low bits
static void test_balancer_route_id (void) {

	const u32 route_id = balancer_route_id (ROUTES_SLOT, ROUTES_GENERATION);

	test_check_unsigned_eq (balancer_route_id_slot (route_id), ROUTES_SLOT, NULL);
	test_check_unsigned_eq (balancer_route_id_generation (route_id), ROUTES_GENERATION, NULL);

	// the generation goes around without 0, so a route id is never 0
	test_check_unsigned_eq (
		balancer_route_next_generation (BALANCER_ROUTE_GENERATION_MASK), BALANCER_ROUTE_FIRST_GENERATION, NULL
	);

	test_check_unsigned_eq (balancer_route_next_generation (ROUTES_GENERATION), ROUTES_GENERATION + 1, NULL);
	test_check (balancer_route_id (0, BALANCER_ROUTE_FIRST_GENERATION) != 0, NULL);

}

// a response for a connection that ended is not routed to the connection that has its slot now
static void test_balancer_route_generation (void) {

	Balancer *balancer = test_balancer_create (BALANCER_TYPE_ROUND_ROBIN);

	Socket *first = socket_create (ROUTES_SOCK_FD);
	Socket *second = socket_create (ROUTES_OTHER_SOCK_FD);
	test_check_ptr (first);
	test_check_ptr (second);

	const u32 first_route = balancer_connection_start (balancer, first);
	test_check (first_route != 0, NULL);

	Socket *routed = balancer_route_get (balancer, first_route);
	test_check_ptr_eq (routed, first);
	socket_delete (routed);

	// the slot is released and given to the next connection, with another generation
	balancer_connection_end (balancer, first_route);
	test_check_null_ptr (balancer_route_get (balancer, first_route));

	const u32 second_route = balancer_connection_start (balancer, second);
	test_check_unsigned_eq (balancer_route_id_slot (second_route), balancer_route_id_slot (first_route), NULL);
	test_check (second_route != first_route, "the new connection has the route of the old one");

	test_check_null_ptr (balancer_route_get (balancer, first_route));

	routed = balancer_route_get (balancer, second_route);
	test_check_ptr_eq (routed, second);
	socket_delete (routed);

	// a route of an old generation does not end the connection that has its slot now
	balancer_connection_end (balancer, first_route);
	routed = balancer_route_get (balancer, second_route);
	test_check_ptr_eq (routed, second);
	socket_delete (routed);

	// a slot that was never used and a route id of 0 have no route
	test_check_null_ptr (balancer_route_get (balancer, balancer_route_id (ROUTES_SLOT, BALANCER_ROUTE_FIRST_GENERATION)));
	test_check_null_ptr (balancer_route_get (balancer, 0));

	balancer_connection_end (balancer, second_route);

	// the sockets were never opened, their sock fd is not closed by the test
	first->sock_fd = -1;
	second->sock_fd = -1;
	socket_delete (first);
	socket_delete (second);

	balancer_delete (balancer);

}

// the slots grow up to BALANCER_ROUTES_MAX, one more connection is not routed
static void test_balancer_route_limit (void) {

	Balancer *balancer = test_balancer_create (BALANCER_TYPE_ROUND_ROBIN);

	Socket *socket = socket_create (ROUTES_SOCK_FD);
	test_check_ptr (socket);

	u32 *routes = (u32 *) calloc (BALANCER_ROUTES_MAX, sizeof (u32));
	test_check_ptr (routes);

	for (u32 i = 0; i < BALANCER_ROUTES_MAX; i++) {
		routes[i] = balancer_connection_start (balancer, socket);
		test_check (routes[i] != 0, "a connection under the limit was not routed");
	}

	test_check_unsigned_eq (balancer_connection_start (balancer, socket), 0, NULL);

	// a released slot is given to the next connection
	balancer_connection_end (balancer, routes[0]);
	const u32 reused = balancer_connection_start (balancer, socket);
	test_check_unsigned_eq (balancer_route_id_slot (reused), balancer_route_id_slot (routes[0]), NULL);
	routes[0] = reused;

	for (u32 i = 0; i < BALANCER_ROUTES_MAX; i++) balancer_connection_end (balancer, routes[i]);

	free (routes);

	socket->sock_fd = -1;
	socket_delete (socket);

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
	test_balancer_create_custom ();

	// routes
	test_balancer_route_id ();
	test_balancer_route_generation ();
	test_balancer_route_limit ();

	(void) printf ("\nDone with BALANCER tests!\n\n");

	return 0;

}
