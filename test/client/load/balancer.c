#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/client.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include <cerver/utils/log.h>

#include "balancer.h"

#include "../../test.h"

#define BALANCER_PATTERN_MASK			0xFF
#define BALANCER_MAX_SLEEP				30

BalancerCounters balancer_counters = { { 0 }, { 0 }, { 0 }, { 0 }, 0, 0 };

static Client *balancer_owners[BALANCER_MAX_CLIENTS] = { NULL };

static char balancer_pattern_byte (const unsigned int owner, const size_t index) {

	return (char) ((index + owner) & BALANCER_PATTERN_MASK);

}

static void balancer_body_fill (char *body, const size_t size, const unsigned int owner, const uint32_t sequence) {

	BalancerBody *header = (BalancerBody *) body;
	header->owner = owner;
	header->sequence = sequence;

	for (size_t index = sizeof (BalancerBody); index < size; index++) {
		body[index] = balancer_pattern_byte (owner, index);
	}

}

static bool balancer_body_valid (const char *body, const size_t size, const unsigned int owner) {

	bool valid = true;
	for (size_t index = sizeof (BalancerBody); valid && (index < size); index++) {
		valid = (body[index] == balancer_pattern_byte (owner, index));
	}

	return valid;

}

// the owner of a response, BALANCER_MAX_CLIENTS if it is not a client of this process
static unsigned int balancer_owner_of (const Client *client) {

	unsigned int owner = BALANCER_MAX_CLIENTS;
	for (unsigned int index = 0; (owner == BALANCER_MAX_CLIENTS) && (index < BALANCER_MAX_CLIENTS); index++) {
		if (__atomic_load_n (&balancer_owners[index], __ATOMIC_ACQUIRE) == client) owner = index;
	}

	return owner;

}

static void balancer_handle_echo (const Packet *packet) {

	const unsigned int receiver = balancer_owner_of (packet->client);

	if (!packet->data || (packet->data_size < sizeof (BalancerBody)) || (receiver == BALANCER_MAX_CLIENTS)) {
		test_counter_add (&balancer_counters.bad_bodies);
	}

	else {
		const BalancerBody *body = (const BalancerBody *) packet->data;

		if (body->owner != receiver) {
			test_counter_add (&balancer_counters.wrong_owner);
		}

		else if (!balancer_body_valid ((const char *) packet->data, packet->data_size, receiver)) {
			test_counter_add (&balancer_counters.bad_bodies);
		}

		else {
			test_counter_add (&balancer_counters.echoes[receiver]);
		}
	}

}

static void balancer_handle_identity (const Packet *packet) {

	if (!packet->data || (packet->data_size != sizeof (UpstreamIdentity))) {
		test_counter_add (&balancer_counters.bad_bodies);
	}

	else {
		const UpstreamIdentity *identity = (const UpstreamIdentity *) packet->data;
		if (identity->id < UPSTREAM_SERVICES) {
			__atomic_store_n (&balancer_counters.pids[identity->id], identity->pid, __ATOMIC_SEQ_CST);
			test_counter_add (&balancer_counters.identities[identity->id]);
		}

		else {
			test_counter_add (&balancer_counters.bad_bodies);
		}
	}

}

static void balancer_client_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	switch (packet->header.request_type) {
		case UPSTREAM_REQUEST_ECHO:
		case UPSTREAM_REQUEST_SLOW:
			balancer_handle_echo (packet);
			break;

		case UPSTREAM_REQUEST_IDENTITY:
			balancer_handle_identity (packet);
			break;

		default: break;
	}

}

// the balancer answers with an error when it has no service for a request
static void *balancer_client_error (void *error_data_ptr) {

	ClientErrorData *error_data = (ClientErrorData *) error_data_ptr;

	const unsigned int owner = balancer_owner_of (error_data->client);
	if (owner < BALANCER_MAX_CLIENTS) test_counter_add (&balancer_counters.errors[owner]);

	client_error_data_delete (error_data);

	return NULL;

}

BalancerClient balancer_client_connect (const unsigned int owner) {

	test_check (owner < BALANCER_MAX_CLIENTS, "invalid balancer client owner");

	BalancerClient balancer_client = { owner, NULL, NULL, 0 };

	balancer_client.client = client_create ();
	test_check_ptr (balancer_client.client);

	client_set_max_received_packet_size (balancer_client.client, UPSTREAM_MAX_PACKET_SIZE);

	test_check_unsigned_eq (
		client_error_register (
			balancer_client.client, CLIENT_ERROR_PACKET_ERROR,
			balancer_client_error, NULL, NULL,
			false, false
		), 0, NULL
	);

	Handler *app_handler = handler_create (balancer_client_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (balancer_client.client, app_handler, NULL);

	balancer_client.connection = client_connection_create (
		balancer_client.client, test_address (), UPSTREAM_BALANCER_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (balancer_client.connection);
	connection_set_max_sleep (balancer_client.connection, BALANCER_MAX_SLEEP);

	// the owner is known before any response can arrive
	__atomic_store_n (&balancer_owners[owner], balancer_client.client, __ATOMIC_RELEASE);

	test_check_int_eq (
		client_connect_and_start (balancer_client.client, balancer_client.connection), 0,
		"Failed to connect to the balancer!"
	);

	return balancer_client;

}

static void balancer_client_send_packet (BalancerClient *client, Packet *packet) {

	test_check_ptr (packet);

	packet_set_network_values (packet, NULL, client->client, client->connection, NULL);
	test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, "Failed to send to the balancer!");

	packet_delete (packet);

	client->sent += 1;

}

void balancer_client_send (
	BalancerClient *client, const uint32_t request_type, const size_t body_size
) {

	const size_t size = (body_size < sizeof (BalancerBody)) ? sizeof (BalancerBody) : body_size;

	char *body = (char *) malloc (size);
	test_check_ptr (body);

	balancer_body_fill (body, size, client->owner, client->sent);

	balancer_client_send_packet (
		client, packet_generate_request (PACKET_TYPE_APP, request_type, body, size)
	);

	free (body);

}

void balancer_client_send_empty (
	BalancerClient *client, const uint32_t request_type
) {

	balancer_client_send_packet (
		client, packet_generate_request (PACKET_TYPE_APP, request_type, NULL, 0)
	);

}

void balancer_client_end (BalancerClient *client) {

	if (client_is_running (client->client)) {
		(void) client_connection_end (client->client, client->connection);
	}

	test_check_unsigned_eq (client_teardown (client->client), 0, NULL);

	__atomic_store_n (&balancer_owners[client->owner], NULL, __ATOMIC_RELEASE);

	client->client = NULL;
	client->connection = NULL;

}

bool balancer_client_stopped (const BalancerClient *client) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + BALANCER_WAIT_SECONDS;

	while (client_is_running (client->client) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	return !client_is_running (client->client);

}

unsigned int balancer_echoes (const unsigned int owner) {

	return __atomic_load_n (&balancer_counters.echoes[owner], __ATOMIC_SEQ_CST);

}

unsigned int balancer_errors (const unsigned int owner) {

	return __atomic_load_n (&balancer_counters.errors[owner], __ATOMIC_SEQ_CST);

}

unsigned int balancer_wait_echoes (const unsigned int owner, const unsigned int expected) {

	return test_wait_counter (&balancer_counters.echoes[owner], expected, BALANCER_WAIT_SECONDS);

}

unsigned int balancer_wait_errors (const unsigned int owner, const unsigned int expected) {

	return test_wait_counter (&balancer_counters.errors[owner], expected, BALANCER_WAIT_SECONDS);

}

uint32_t balancer_service_pid (const unsigned int service) {

	return __atomic_load_n (&balancer_counters.pids[service], __ATOMIC_SEQ_CST);

}

void balancer_check_bodies (void) {

	test_check_unsigned_eq (
		__atomic_load_n (&balancer_counters.wrong_owner, __ATOMIC_SEQ_CST), 0,
		"a response reached another client"
	);

	test_check_unsigned_eq (
		__atomic_load_n (&balancer_counters.bad_bodies, __ATOMIC_SEQ_CST), 0,
		"a response came back changed"
	);

}

void balancer_sleep (const long nanoseconds) {

	const struct timespec interval = { nanoseconds / TEST_WAIT_NANOSECONDS, nanoseconds % TEST_WAIT_NANOSECONDS };
	(void) nanosleep (&interval, NULL);

}

void balancer_init (void) {

	test_output_init ();

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_log_init ();

}

void balancer_end (void) {

	balancer_check_bodies ();

	cerver_log_end ();

}
