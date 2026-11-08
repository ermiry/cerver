#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>

#include <cerver/balancer.h>
#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/errors.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include "test.h"

// a load balancer and its services in the same process, with cerver clients:
// - round robin uses every service, handler id uses the one in the header
// - a handler id without a service and an unknown packet type do not break the balancer
// - a header with a bad size closes only the connection that sent it
// - a big packet is routed and answered with the same bytes
// - a service that goes down is not used, and it is used again when it comes back
// - without services the client gets an error, or the unavailable services method handles it
// - the balancer starts with a service down, and connects it later
// - the response of a client that already disconnected is discarded
// - the teardown returns 0 and stops every thread

#define ROUTING_ADDRESS					"127.0.0.1"
#define ROUTING_PORT_BASE				7400
#define ROUTING_SERVICES				2

#define ROUTING_THREADS					4
#define ROUTING_POLL_TIMEOUT			200
#define ROUTING_JOIN_SECONDS			8
#define ROUTING_TEARDOWN_MAX_SECONDS	8
#define ROUTING_RECONNECT_SECONDS		1

#define ROUTING_PACKETS					64
#define ROUTING_BODY_SIZE				64
#define ROUTING_BIG_BODY_SIZE			(1 << 20)
#define ROUTING_MAX_PACKET_SIZE			(1 << 21)

#define ROUTING_REQUEST_ECHO			1
#define ROUTING_REQUEST_SLOW			2

#define ROUTING_SLOW_MICROSECONDS		250000

#define ROUTING_MISSING_HANDLER_ID		7
#define ROUTING_UNKNOWN_PACKET_TYPE		200
#define ROUTING_BAD_PACKET_SIZE			4

#define ROUTING_BODY_PATTERN			0xFF

typedef struct RoutingService {

	Cerver *cerver;
	u16 port;

	pthread_t thread;
	u8 start_result;

	unsigned int started;
	unsigned int packets;

} RoutingService;

typedef struct RoutingBalancer {

	Balancer *balancer;
	u16 port;

	pthread_t thread;
	u8 start_result;

	unsigned int started;

	RoutingService services[ROUTING_SERVICES];

} RoutingBalancer;

typedef struct RoutingClient {

	Client *client;
	Connection *connection;

} RoutingClient;

static u16 routing_port = ROUTING_PORT_BASE;

static unsigned int echoes = 0;
static unsigned int bad_echoes = 0;
static unsigned int packet_errors = 0;
static unsigned int unavailable_packets = 0;

static void routing_counters_reset (void) {

	echoes = 0;
	bad_echoes = 0;
	packet_errors = 0;
	unavailable_packets = 0;

}

static void routing_body_fill (char *body, const size_t size) {

	for (size_t i = 0; i < size; i++) body[i] = (char) (i & ROUTING_BODY_PATTERN);

}

static bool routing_body_check (const char *body, const size_t size) {

	bool valid = true;

	for (size_t i = 0; i < size; i++) {
		if (body[i] != (char) (i & ROUTING_BODY_PATTERN)) {
			valid = false;
			break;
		}
	}

	return valid;

}

#pragma region services

// the response is sent with the sock fd of the request, so the balancer routes it back
static void routing_service_echo (const Packet *packet) {

	Packet *response = packet_generate_request (
		PACKET_TYPE_APP, packet->header.request_type,
		packet->data, packet->data_size
	);

	test_check_ptr (response);

	((PacketHeader *) response->packet)->sock_fd = packet->header.sock_fd;

	packet_set_network_values (
		response, packet->cerver, packet->client, packet->connection, NULL
	);

	(void) packet_send (response, 0, NULL, false);

	packet_delete (response);

}

static void routing_service_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	RoutingService *service = (RoutingService *) packet->cerver->cerver_data;
	test_counter_add (&service->packets);

	switch (packet->header.request_type) {
		case ROUTING_REQUEST_ECHO:
			routing_service_echo (packet);
			break;

		case ROUTING_REQUEST_SLOW:
			(void) usleep (ROUTING_SLOW_MICROSECONDS);
			routing_service_echo (packet);
			break;

		default: break;
	}

}

static void *routing_started (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

static void *routing_service_thread (void *service_ptr) {

	RoutingService *service = (RoutingService *) service_ptr;

	service->start_result = cerver_start (service->cerver);

	return NULL;

}

static void routing_service_start (RoutingService *service) {

	service->started = 0;
	service->start_result = 0;

	service->cerver = cerver_create (
		CERVER_TYPE_CUSTOM, "routing-service",
		service->port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (service->cerver);

	// the handler finds its service with the cerver data, it is not deleted by the cerver
	service->cerver->cerver_data = service;

	cerver_set_reusable_address_flags (service->cerver, true);
	cerver_set_thpool_n_threads (service->cerver, ROUTING_THREADS);
	cerver_set_handler_type (service->cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_max_received_packet_size (service->cerver, ROUTING_MAX_PACKET_SIZE);

	Handler *app_handler = handler_create (routing_service_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (service->cerver, app_handler, NULL);

	(void) cerver_event_register (
		service->cerver, CERVER_EVENT_STARTED,
		routing_started, &service->started, NULL,
		false, false
	);

	test_check_int_eq (pthread_create (&service->thread, NULL, routing_service_thread, service), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&service->started, 1, TEST_WAIT_SECONDS),
		1, "the service did not start"
	);

}

static void routing_join (pthread_t thread) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += ROUTING_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (thread, NULL, &deadline), 0, "the start did not return");

}

static void routing_service_stop (RoutingService *service) {

	test_check_unsigned_eq (cerver_shutdown (service->cerver), 0, NULL);

	routing_join (service->thread);
	test_check_unsigned_eq (service->start_result, 0, NULL);

	service->cerver->cerver_data = NULL;
	test_check_unsigned_eq (cerver_teardown (service->cerver), 0, NULL);
	service->cerver = NULL;

}

#pragma endregion

#pragma region balancer

static void routing_unavailable (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	test_check_ptr (packet->cerver);
	test_check_ptr (packet->connection);
	test_check_int_eq (packet->packet_type, PACKET_TYPE_APP, NULL);

	test_counter_add (&unavailable_packets);

}

static void *routing_balancer_thread (void *routing_ptr) {

	RoutingBalancer *routing = (RoutingBalancer *) routing_ptr;

	routing->start_result = balancer_start (routing->balancer);

	return NULL;

}

static void routing_balancer_create (
	RoutingBalancer *routing, const BalancerType type
) {

	routing->port = routing_port++;
	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		routing->services[i].port = routing_port++;
		routing->services[i].packets = 0;
		routing->services[i].cerver = NULL;
	}

	routing->balancer = balancer_create (
		"routing-balancer", type,
		routing->port, CERVER_DEFAULT_CONNECTION_QUEUE,
		ROUTING_SERVICES
	);

	test_check_ptr (routing->balancer);

	cerver_set_reusable_address_flags (routing->balancer->cerver, true);
	cerver_set_max_received_packet_size (routing->balancer->cerver, ROUTING_MAX_PACKET_SIZE);
	client_set_max_received_packet_size (routing->balancer->client, ROUTING_MAX_PACKET_SIZE);

	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		test_check_unsigned_eq (
			balancer_service_register (routing->balancer, ROUTING_ADDRESS, routing->services[i].port),
			0, NULL
		);

		balancer_service_set_reconnect_wait_time (routing->balancer->services[i], ROUTING_RECONNECT_SECONDS);
	}

	(void) cerver_event_register (
		routing->balancer->cerver, CERVER_EVENT_STARTED,
		routing_started, &routing->started, NULL,
		false, false
	);

}

static void routing_balancer_start (RoutingBalancer *routing) {

	routing->started = 0;
	routing->start_result = 0;

	test_check_int_eq (pthread_create (&routing->thread, NULL, routing_balancer_thread, routing), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&routing->started, 1, TEST_WAIT_SECONDS),
		1, "the balancer did not start"
	);

}

// starts the services and then the balancer
static void routing_start (RoutingBalancer *routing, const BalancerType type) {

	routing_balancer_create (routing, type);

	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		routing_service_start (&routing->services[i]);
	}

	routing_balancer_start (routing);

}

static void routing_stop (RoutingBalancer *routing) {

	const double start = test_wait_now ();

	test_check_unsigned_eq (cerver_shutdown (routing->balancer->cerver), 0, NULL);
	routing_join (routing->thread);
	test_check_unsigned_eq (routing->start_result, 0, NULL);

	test_check_unsigned_eq (balancer_teardown (routing->balancer), 0, NULL);
	routing->balancer = NULL;

	test_check ((test_wait_now () - start) < ROUTING_TEARDOWN_MAX_SECONDS, "the balancer took too long to stop");

	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		if (routing->services[i].cerver) routing_service_stop (&routing->services[i]);
	}

}

static bool routing_service_working (const RoutingBalancer *routing, const unsigned int index) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	const Service *service = routing->balancer->services[index];
	while (
		(balancer_service_get_status (service) != SERVICE_STATUS_WORKING)
		&& (test_wait_now () < deadline)
	) {
		(void) nanosleep (&interval, NULL);
	}

	return balancer_service_get_status (service) == SERVICE_STATUS_WORKING;

}

static bool routing_service_down (const RoutingBalancer *routing, const unsigned int index) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	const Service *service = routing->balancer->services[index];
	while (
		(balancer_service_get_status (service) == SERVICE_STATUS_WORKING)
		&& (test_wait_now () < deadline)
	) {
		(void) nanosleep (&interval, NULL);
	}

	return balancer_service_get_status (service) != SERVICE_STATUS_WORKING;

}

#pragma endregion

#pragma region client

static void routing_client_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (routing_body_check ((const char *) packet->data, packet->data_size)) {
		test_counter_add (&echoes);
	}

	else {
		test_counter_add (&bad_echoes);
	}

}

static void *routing_client_packet_error (void *error_data_ptr) {

	client_error_data_delete ((ClientErrorData *) error_data_ptr);

	test_counter_add (&packet_errors);

	return NULL;

}

static RoutingClient routing_client_connect (const u16 port) {

	RoutingClient routing = { NULL, NULL };

	routing.client = client_create ();
	test_check_ptr (routing.client);

	client_set_max_received_packet_size (routing.client, ROUTING_MAX_PACKET_SIZE);

	(void) client_error_register (
		routing.client, CLIENT_ERROR_PACKET_ERROR,
		routing_client_packet_error, NULL, NULL,
		false, false
	);

	Handler *app_handler = handler_create (routing_client_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (routing.client, app_handler, NULL);

	routing.connection = client_connection_create (
		routing.client, ROUTING_ADDRESS, port, PROTOCOL_TCP, false
	);

	test_check_ptr (routing.connection);

	test_check_int_eq (
		client_connect_and_start (routing.client, routing.connection), 0,
		"failed to connect to the balancer"
	);

	return routing;

}

static void routing_client_send (
	const RoutingClient *routing,
	const PacketType packet_type, const u32 request_type,
	const u8 handler_id, const size_t body_size
) {

	char *body = body_size ? (char *) malloc (body_size) : NULL;
	if (body) routing_body_fill (body, body_size);

	Packet *packet = packet_generate_request (packet_type, request_type, body, body_size);
	test_check_ptr (packet);

	((PacketHeader *) packet->packet)->handler_id = handler_id;

	packet_set_network_values (packet, NULL, routing->client, routing->connection, NULL);
	test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

	packet_delete (packet);
	free (body);

}

static void routing_client_send_many (
	const RoutingClient *routing, const u8 handler_id, const unsigned int count
) {

	for (unsigned int i = 0; i < count; i++) {
		routing_client_send (routing, PACKET_TYPE_APP, ROUTING_REQUEST_ECHO, handler_id, ROUTING_BODY_SIZE);
	}

}

static void routing_client_end (RoutingClient *routing) {

	if (client_is_running (routing->client)) {
		(void) client_connection_end (routing->client, routing->connection);
	}

	test_check_unsigned_eq (client_teardown (routing->client), 0, NULL);

}

static bool routing_client_stopped (const RoutingClient *routing) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	while (client_is_running (routing->client) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	return !client_is_running (routing->client);

}

static void routing_check_echoes (const unsigned int expected) {

	test_check_unsigned_eq (
		test_wait_counter (&echoes, expected, TEST_WAIT_SECONDS),
		expected, "the responses did not arrive"
	);

	test_check_unsigned_eq (bad_echoes, 0, NULL);

}

#pragma endregion

#pragma region tests

static void test_routing_round_robin (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient client = routing_client_connect (routing.port);

	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);

	// every service handled half of the packets
	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		test_check_unsigned_eq (routing.services[i].packets, ROUTING_PACKETS / ROUTING_SERVICES, NULL);
		test_check_unsigned_eq (
			CERVER_STATS_GET (routing.balancer->services[i]->stats->n_packets_routed),
			ROUTING_PACKETS / ROUTING_SERVICES, NULL
		);
	}

	test_check_unsigned_eq (CERVER_STATS_GET (routing.balancer->stats->n_packets_routed), ROUTING_PACKETS, NULL);
	test_check_unsigned_eq (CERVER_STATS_GET (routing.balancer->stats->n_packets_sent), ROUTING_PACKETS, NULL);

	routing_client_end (&client);
	routing_stop (&routing);

}

static void test_routing_handler_id (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_HANDLER_ID);

	RoutingClient client = routing_client_connect (routing.port);

	routing_client_send_many (&client, 1, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);

	test_check_unsigned_eq (routing.services[0].packets, 0, NULL);
	test_check_unsigned_eq (routing.services[1].packets, ROUTING_PACKETS, NULL);

	routing_client_end (&client);
	routing_stop (&routing);

}

// a handler id without a service is answered with an error
static void test_routing_missing_handler_id (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_HANDLER_ID);

	RoutingClient client = routing_client_connect (routing.port);

	routing_client_send_many (&client, ROUTING_MISSING_HANDLER_ID, ROUTING_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter (&packet_errors, ROUTING_PACKETS, TEST_WAIT_SECONDS),
		ROUTING_PACKETS, NULL
	);

	test_check_unsigned_eq (CERVER_STATS_GET (routing.balancer->stats->unhandled_packets), ROUTING_PACKETS, NULL);

	// the balancer still works
	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);

	routing_client_end (&client);
	routing_stop (&routing);

}

// a packet type out of the stats arrays is counted as a bad packet
static void test_routing_unknown_packet_type (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient client = routing_client_connect (routing.port);

	for (unsigned int i = 0; i < ROUTING_PACKETS; i++) {
		routing_client_send (&client, (PacketType) ROUTING_UNKNOWN_PACKET_TYPE, ROUTING_REQUEST_ECHO, 0, ROUTING_BODY_SIZE);
	}

	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);

	test_check_unsigned_eq (CERVER_STATS_GET (routing.balancer->stats->bad_n_packets_received), ROUTING_PACKETS, NULL);

	routing_client_end (&client);
	routing_stop (&routing);

}

// a header smaller than itself can not be skipped, so the connection is closed,
// and the other clients still work
static void test_routing_bad_packet_size (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient good = routing_client_connect (routing.port);
	RoutingClient bad = routing_client_connect (routing.port);

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));
	header.packet_type = PACKET_TYPE_APP;
	header.packet_size = ROUTING_BAD_PACKET_SIZE;
	header.version = packet_version_local ();

	test_check (
		send (bad.connection->socket->sock_fd, &header, sizeof (PacketHeader), 0) == (ssize_t) sizeof (PacketHeader),
		"failed to send the bad header"
	);

	test_check (routing_client_stopped (&bad), "the balancer did not close the bad connection");

	routing_client_send_many (&good, 0, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);

	routing_client_end (&bad);
	routing_client_end (&good);
	routing_stop (&routing);

}

static void test_routing_big_packet (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient client = routing_client_connect (routing.port);

	routing_client_send (&client, PACKET_TYPE_APP, ROUTING_REQUEST_ECHO, 0, ROUTING_BIG_BODY_SIZE);
	routing_check_echoes (1);

	// the stream continues after the big packet
	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (1 + ROUTING_PACKETS);

	routing_client_end (&client);
	routing_stop (&routing);

}

// a service that goes down is not used, and it is used again when it comes back
static void test_routing_service_reconnect (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient client = routing_client_connect (routing.port);

	routing_service_stop (&routing.services[0]);
	test_check (routing_service_down (&routing, 0), "the service is still working");

	routing.services[1].packets = 0;
	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);
	test_check_unsigned_eq (routing.services[1].packets, ROUTING_PACKETS, NULL);

	routing_service_start (&routing.services[0]);
	test_check (routing_service_working (&routing, 0), "the service did not reconnect");

	routing.services[0].packets = 0;
	routing.services[1].packets = 0;
	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (2 * ROUTING_PACKETS);

	test_check_unsigned_eq (routing.services[0].packets, ROUTING_PACKETS / ROUTING_SERVICES, NULL);
	test_check_unsigned_eq (routing.services[1].packets, ROUTING_PACKETS / ROUTING_SERVICES, NULL);

	routing_client_end (&client);
	routing_stop (&routing);

}

static void routing_services_stop (RoutingBalancer *routing) {

	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		routing_service_stop (&routing->services[i]);
		test_check (routing_service_down (routing, i), "the service is still working");
	}

}

// without services, the client gets an error
static void test_routing_unavailable (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient client = routing_client_connect (routing.port);

	routing_services_stop (&routing);

	routing_client_send_many (&client, 0, ROUTING_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter (&packet_errors, ROUTING_PACKETS, TEST_WAIT_SECONDS),
		ROUTING_PACKETS, NULL
	);

	routing_client_end (&client);
	routing_stop (&routing);

}

// without services, the unavailable services method handles the packets
static void test_routing_unavailable_method (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_balancer_create (&routing, BALANCER_TYPE_ROUND_ROBIN);
	balancer_set_on_unavailable_services (routing.balancer, routing_unavailable);

	for (unsigned int i = 0; i < ROUTING_SERVICES; i++) {
		routing_service_start (&routing.services[i]);
	}

	routing_balancer_start (&routing);

	RoutingClient client = routing_client_connect (routing.port);

	routing_services_stop (&routing);

	routing_client_send_many (&client, 0, ROUTING_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter (&unavailable_packets, ROUTING_PACKETS, TEST_WAIT_SECONDS),
		ROUTING_PACKETS, NULL
	);

	test_check_unsigned_eq (
		CERVER_STATS_GET (routing.balancer->stats->backup_handled_packets), ROUTING_PACKETS, NULL
	);

	routing_client_end (&client);
	routing_stop (&routing);

}

// the balancer starts with a service that is down, and connects it when it starts
static void test_routing_start_service_down (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_balancer_create (&routing, BALANCER_TYPE_ROUND_ROBIN);

	routing_service_start (&routing.services[1]);

	routing_balancer_start (&routing);

	test_check_int_eq (
		balancer_service_get_status (routing.balancer->services[0]),
		SERVICE_STATUS_UNAVAILABLE, NULL
	);

	RoutingClient client = routing_client_connect (routing.port);

	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (ROUTING_PACKETS);

	routing_service_start (&routing.services[0]);
	test_check (routing_service_working (&routing, 0), "the service was not connected");

	routing_client_send_many (&client, 0, ROUTING_PACKETS);
	routing_check_echoes (2 * ROUTING_PACKETS);

	test_check_unsigned_ne (routing.services[0].packets, 0);

	routing_client_end (&client);
	routing_stop (&routing);

}

// a balancer without any service does not start
static void test_routing_start_without_services (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_balancer_create (&routing, BALANCER_TYPE_ROUND_ROBIN);

	test_check_unsigned_ne (balancer_start (routing.balancer), 0);

	test_check_unsigned_eq (balancer_teardown (routing.balancer), 0, NULL);

}

// the client disconnects before the response arrives, so the response is discarded
static void test_routing_discarded_response (void) {

	routing_counters_reset ();

	RoutingBalancer routing;
	routing_start (&routing, BALANCER_TYPE_ROUND_ROBIN);

	RoutingClient client = routing_client_connect (routing.port);

	routing_client_send (&client, PACKET_TYPE_APP, ROUTING_REQUEST_SLOW, 0, ROUTING_BODY_SIZE);

	routing_client_end (&client);

	test_check_unsigned_eq (
		test_wait_counter_64 (&routing.balancer->stats->discarded_responses, 1, TEST_WAIT_SECONDS),
		1, "the response was not discarded"
	);

	routing_stop (&routing);

}

#pragma endregion

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) printf ("Testing BALANCER routing...\n");

	test_routing_round_robin ();
	test_routing_handler_id ();
	test_routing_missing_handler_id ();
	test_routing_unknown_packet_type ();
	test_routing_bad_packet_size ();
	test_routing_big_packet ();

	test_routing_service_reconnect ();
	test_routing_unavailable ();
	test_routing_unavailable_method ();

	test_routing_start_service_down ();
	test_routing_start_without_services ();

	test_routing_discarded_response ();

	(void) printf ("Done with BALANCER routing tests!\n\n");

	return 0;

}
