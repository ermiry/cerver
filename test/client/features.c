#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <pthread.h>

#include <cerver/collections/dlist.h>

#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/packets.h>
#include <cerver/sessions.h>

#include <app/auth.h>

#include "../test.h"

// the client features that only exist in the 1.x, with a TCP cerver in the same process:
// - the connection states while it connects, works and ends
// - client_connect_to_cerver_async () handles the cerver info and is waited by client_requests_wait ()
// - the send queue sends every packet from its own thread, that is joined at the end
// - a connection with attempt reconnect connects again after the cerver restarts
// - the reconnection resolves the host again and finds the cerver in its other address
// - connection_reconnect () closes the socket and the connection connects again
// - the end of a connection that waits to reconnect does not wait the reconnect time
// - packet_init_ping () and packet_init_request () send a header with the local version
// - a client authenticates and gets a session token, and a second connection
//   authenticates with the token and is added to the same client of the cerver

#define FEATURES_ADDRESS				"127.0.0.1"
#define FEATURES_IPV6_ADDRESS			"::1"
#define FEATURES_LOCALHOST				"localhost"
#define FEATURES_PORT_BASE				7300
#define FEATURES_NAME					"features-cerver"

#define FEATURES_THREADS				4
#define FEATURES_POLL_TIMEOUT			200
#define FEATURES_JOIN_SECONDS			8

#define FEATURES_PACKETS				64
#define FEATURES_REQUEST_ECHO			1
#define FEATURES_REQUEST_LARGE			2

// more than the socket buffers, so the send finds a full socket
#define FEATURES_LARGE_PACKET_SIZE		(1 << 24)
#define FEATURES_MAX_PACKET_SIZE		(1 << 25)

#define FEATURES_RECONNECT_SECONDS		1
#define FEATURES_LONG_RECONNECT_SECONDS	32
#define FEATURES_END_MAX_SECONDS		4

#define FEATURES_RECEIVE_FLAGS			0

#define FEATURES_AUTH_MAX_TRIES			2

#define FEATURES_PING_DATA_SIZE			64
#define FEATURES_USERNAME				"ermiry"
#define FEATURES_PASSWORD				"049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b"

typedef struct FeaturesCerver {

	Cerver *cerver;

	pthread_t thread;
	u8 start_result;

	unsigned int started;

	// the cerver binds this address, NULL for every address
	const char *bind_address;

} FeaturesCerver;

static u16 features_port = FEATURES_PORT_BASE;

static unsigned int echoes = 0;
static unsigned int test_responses = 0;

static unsigned int connected_events = 0;
static unsigned int close_events = 0;
static unsigned int failed_events = 0;

static unsigned int success_auth_events = 0;
static unsigned int new_connection_events = 0;
static unsigned int failed_auth_events = 0;

static unsigned int large_packets = 0;

static void features_counters_reset (void) {

	echoes = 0;
	large_packets = 0;
	test_responses = 0;

	connected_events = 0;
	close_events = 0;
	failed_events = 0;

	success_auth_events = 0;
	new_connection_events = 0;
	failed_auth_events = 0;

}

#pragma region cerver

static void features_cerver_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == FEATURES_REQUEST_LARGE) {
		if (packet->data_size == FEATURES_LARGE_PACKET_SIZE) test_counter_add (&large_packets);
	}

	else if (packet->header.request_type == FEATURES_REQUEST_ECHO) {
		Packet *response = packet_generate_request (
			PACKET_TYPE_APP, FEATURES_REQUEST_ECHO, NULL, 0
		);

		test_check_ptr (response);

		packet_set_network_values (
			response, packet->cerver, packet->client, packet->connection, NULL
		);

		(void) packet_send (response, 0, NULL, false);

		packet_delete (response);
	}

}

static void *features_cerver_started (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

static void *features_cerver_thread (void *features_ptr) {

	FeaturesCerver *features = (FeaturesCerver *) features_ptr;

	features->start_result = cerver_start (features->cerver);

	return NULL;

}

static void features_cerver_start_with (
	FeaturesCerver *features, const u16 port, const bool sessions
);

static void features_cerver_start (FeaturesCerver *features, const u16 port) {

	features_cerver_start_with (features, port, false);

}

static void *features_cerver_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

// the cerver authenticates its clients and gives them a session token
static void features_cerver_sessions (Cerver *cerver) {

	test_check_unsigned_eq (cerver_set_auth (cerver, FEATURES_AUTH_MAX_TRIES, app_auth_method), 0, NULL);
	test_check_unsigned_eq (cerver_set_sessions (cerver, session_default_generate_id), 0, NULL);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_CLIENT_SUCCESS_AUTH,
		features_cerver_event, &success_auth_events, NULL,
		false, false
	);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_CLIENT_NEW_CONNECTION,
		features_cerver_event, &new_connection_events, NULL,
		false, false
	);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_CLIENT_FAILED_AUTH,
		features_cerver_event, &failed_auth_events, NULL,
		false, false
	);

}

static void features_cerver_start_with (
	FeaturesCerver *features, const u16 port, const bool sessions
) {

	features->started = 0;
	features->start_result = 0;

	features->cerver = cerver_create (
		CERVER_TYPE_CUSTOM, FEATURES_NAME,
		port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (features->cerver);

	cerver_set_reusable_address_flags (features->cerver, true);
	cerver_set_thpool_n_threads (features->cerver, FEATURES_THREADS);
	cerver_set_handler_type (features->cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (features->cerver, FEATURES_POLL_TIMEOUT);
	cerver_set_max_received_packet_size (features->cerver, FEATURES_MAX_PACKET_SIZE);

	if (features->bind_address) {
		test_check_unsigned_eq (cerver_set_bind_address (features->cerver, features->bind_address), 0, NULL);
	}

	Handler *app_handler = handler_create (features_cerver_app_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (features->cerver, app_handler, NULL);

	if (sessions) features_cerver_sessions (features->cerver);

	(void) cerver_event_register (
		features->cerver, CERVER_EVENT_STARTED,
		features_cerver_started, &features->started, NULL,
		false, false
	);

	test_check_int_eq (pthread_create (&features->thread, NULL, features_cerver_thread, features), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&features->started, 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

}

static void features_cerver_stop (FeaturesCerver *features) {

	test_check_unsigned_eq (cerver_shutdown (features->cerver), 0, NULL);

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += FEATURES_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (features->thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (features->start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (features->cerver), 0, NULL);
	features->cerver = NULL;

}

#pragma endregion

#pragma region client

static void features_client_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == FEATURES_REQUEST_ECHO) {
		test_counter_add (&echoes);
	}

}

static void *features_client_event (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	client_event_data_delete (event_data);

	return NULL;

}

static Client *features_client_create (void) {

	Client *client = client_create ();
	test_check_ptr (client);

	Handler *app_handler = handler_create (features_client_app_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (client, app_handler, NULL);

	(void) client_event_register (
		client, CLIENT_EVENT_CONNECTED,
		features_client_event, &connected_events, NULL,
		false, false
	);

	(void) client_event_register (
		client, CLIENT_EVENT_CONNECTION_CLOSE,
		features_client_event, &close_events, NULL,
		false, false
	);

	(void) client_event_register (
		client, CLIENT_EVENT_CONNECTION_FAILED,
		features_client_event, &failed_events, NULL,
		false, false
	);

	return client;

}

static Connection *features_connection_create (Client *client, const u16 port) {

	Connection *connection = client_connection_create (
		client, FEATURES_ADDRESS, port, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	return connection;

}

static Packet *features_echo_request (Client *client, Connection *connection) {

	Packet *packet = packet_create_request (PACKET_TYPE_APP, FEATURES_REQUEST_ECHO);
	test_check_ptr (packet);

	packet_set_network_values (packet, NULL, client, connection, NULL);

	return packet;

}

static void features_send_echoes (
	Client *client, Connection *connection, const unsigned int count
) {

	for (unsigned int i = 0; i < count; i++) {
		Packet *packet = features_echo_request (client, connection);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);
		packet_delete (packet);
	}

}

static void features_check_echoes (const unsigned int expected) {

	test_check_unsigned_eq (
		test_wait_counter (&echoes, expected, TEST_WAIT_SECONDS),
		expected, "the echoes did not arrive"
	);

}

#pragma endregion

#pragma region tests

static void test_features_states_strings (void) {

	test_check_str_eq (connection_state_string (CONNECTION_STATE_NONE), "None", NULL);
	test_check_str_eq (connection_state_string (CONNECTION_STATE_WORKING), "Working", NULL);
	test_check_str_eq (connection_state_string (CONNECTION_STATE_DISCONNECTED), "Disconnected", NULL);

	test_check_ptr (connection_state_description (CONNECTION_STATE_NONE));
	test_check_ptr (connection_state_description (CONNECTION_STATE_DISCONNECTING));

}

// none -> ready after the connect -> working after the start
static void test_features_states (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	connection_set_receive_flags (connection, FEATURES_RECEIVE_FLAGS);
	test_check_int_eq (connection->receive_flags, FEATURES_RECEIVE_FLAGS, NULL);

	test_check_int_eq (connection_get_state (connection), CONNECTION_STATE_NONE, NULL);

	test_check_unsigned_eq (client_connect_to_cerver (client, connection), 0, NULL);
	test_check_int_eq (connection_get_state (connection), CONNECTION_STATE_READY, NULL);
	test_check_ptr (connection->cerver_report);

	test_check_int_eq (client_connection_start (client, connection), 0, NULL);
	test_check_int_eq (connection_get_state (connection), CONNECTION_STATE_WORKING, NULL);

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (FEATURES_PACKETS);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// a connection to a cerver that is down
static void test_features_states_unavailable (void) {

	features_counters_reset ();

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, features_port++);

	// a single attempt
	connection_set_max_sleep (connection, 1);

	test_check_unsigned_ne (client_connect (client, connection), 0);
	test_check_int_eq (connection_get_state (connection), CONNECTION_STATE_UNAVAILABLE, NULL);
	test_check_unsigned_eq (failed_events, 1, NULL);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

}

static void test_features_connect_to_cerver_async (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	test_check_unsigned_eq (client_connect_to_cerver_async (client, connection), 0, NULL);

	// the async thread handled the cerver info
	client_requests_wait (client);

	test_check_bool_eq (connection->active, true, NULL);
	test_check_ptr (connection->cerver_report);
	test_check_unsigned_eq (connected_events, 1, NULL);

	test_check_int_eq (client_connection_start (client, connection), 0, NULL);

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (FEATURES_PACKETS);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

static void test_features_send_queue (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	connection_set_send_queue (connection, 0);
	test_check_bool_eq (connection->use_send_queue, true, NULL);
	test_check_ptr (connection->send_queue);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);
	test_check_bool_eq (connection->send_thread_running, true, NULL);

	// the send thread deletes every packet
	for (unsigned int i = 0; i < FEATURES_PACKETS; i++) {
		connection_send_packet (connection, features_echo_request (client, connection));
	}

	features_check_echoes (FEATURES_PACKETS);

	// the send thread is joined by the end of the connection
	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// the packets that were not sent are deleted with the connection
static void test_features_send_queue_pending (void) {

	features_counters_reset ();

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, features_port++);

	connection_set_send_queue (connection, 0);

	// the connection never started, so its packets stay in the queue
	for (unsigned int i = 0; i < FEATURES_PACKETS; i++) {
		connection_send_packet (connection, features_echo_request (client, connection));
	}

	test_check_unsigned_eq ((unsigned int) job_queue_size (connection->send_queue), FEATURES_PACKETS, NULL);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

}

// the cerver stops and starts again in the same port
static void test_features_reconnect (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	connection_set_attempt_reconnect (connection, FEATURES_RECONNECT_SECONDS);
	test_check_bool_eq (connection->attempt_reconnect, true, NULL);
	test_check_unsigned_eq (connection->reconnect_wait_time, FEATURES_RECONNECT_SECONDS, NULL);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (FEATURES_PACKETS);

	features_cerver_stop (&features);

	// the update thread closes the socket that the cerver ended
	test_check_unsigned_eq (test_wait_counter (&close_events, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the client keeps the connection while it reconnects
	test_check_bool_eq (client_is_running (client), true, NULL);
	test_check_unsigned_eq (dlist_size (client->connections), 1, NULL);

	features_cerver_start (&features, port);

	test_check_unsigned_eq (test_wait_counter (&connected_events, 2, TEST_WAIT_SECONDS), 2, "the connection did not reconnect");
	test_check_int_eq (connection_get_state (connection), CONNECTION_STATE_WORKING, NULL);

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (2 * FEATURES_PACKETS);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// the cerver comes back in the other address of the host name,
// each reconnection resolves the host again and tries every address,
// before it only tried the first address that was resolved when the connection was created
static void test_features_reconnect_address (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features.bind_address = FEATURES_ADDRESS;
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = client_connection_create (
		client, FEATURES_LOCALHOST, port, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_attempt_reconnect (connection, FEATURES_RECONNECT_SECONDS);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);
	test_check_int_eq (connection->address.ss_family, AF_INET, NULL);

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (FEATURES_PACKETS);

	features_cerver_stop (&features);

	test_check_unsigned_eq (test_wait_counter (&close_events, 1, TEST_WAIT_SECONDS), 1, NULL);

	features.bind_address = FEATURES_IPV6_ADDRESS;
	features_cerver_start (&features, port);

	test_check_unsigned_eq (test_wait_counter (&connected_events, 2, TEST_WAIT_SECONDS), 2, "the connection did not reconnect");
	test_check_int_eq (connection->address.ss_family, AF_INET6, NULL);

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (2 * FEATURES_PACKETS);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// connection_reconnect () closes the socket, and the update thread connects again
static void test_features_reconnect_request (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	// without attempt reconnect the request is rejected
	test_check_unsigned_ne (connection_reconnect (connection), 0);

	connection_set_attempt_reconnect (connection, FEATURES_RECONNECT_SECONDS);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);

	test_check_unsigned_eq (connection_reconnect (connection), 0, NULL);

	test_check_unsigned_eq (test_wait_counter (&connected_events, 2, TEST_WAIT_SECONDS), 2, "the connection did not reconnect");

	features_send_echoes (client, connection, FEATURES_PACKETS);
	features_check_echoes (FEATURES_PACKETS);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// the teardown wakes up the update thread that waits to reconnect
static void test_features_reconnect_end (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	connection_set_attempt_reconnect (connection, FEATURES_LONG_RECONNECT_SECONDS);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);

	features_cerver_stop (&features);

	test_check_unsigned_eq (test_wait_counter (&close_events, 1, TEST_WAIT_SECONDS), 1, NULL);

	const double start = test_wait_now ();

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	test_check ((test_wait_now () - start) < FEATURES_END_MAX_SECONDS, "the teardown waited the reconnect time");

}

static void test_features_packet_init (void) {

	const PacketVersion local = packet_version_local ();

	Packet ping;
	packet_init_ping (&ping);

	test_check_int_eq (ping.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_int_eq (ping.header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (ping.header.packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_unsigned_eq (ping.header.request_type, 0, NULL);
	test_check_unsigned_eq (ping.header.version.protocol_id, local.protocol_id, NULL);
	test_check_unsigned_eq (ping.header.version.protocol_version.major, local.protocol_version.major, NULL);
	test_check_ptr_eq (ping.packet, ping.encoded_header);
	test_check_unsigned_eq (ping.packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_bool_eq (ping.packet_ref, true, NULL);

	Packet request;
	packet_init_request (&request, PACKET_TYPE_APP, FEATURES_REQUEST_ECHO);

	test_check_int_eq (request.header.packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (request.header.request_type, FEATURES_REQUEST_ECHO, NULL);
	test_check_null_ptr (request.data);

}

// a ping in the stack is answered by the cerver's test handler
static void test_features_packet_init_send (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	test_check_unsigned_eq (client_connect_to_cerver (client, connection), 0, NULL);

	Packet ping;
	packet_init_ping (&ping);

	test_check_unsigned_eq (client_request_to_cerver (client, connection, &ping), 0, NULL);
	test_check_unsigned_eq (
		CERVER_STATS_GET (client->stats->received_packets->n_test_packets), 1, NULL
	);

	Packet request;
	packet_init_request (&request, PACKET_TYPE_APP, FEATURES_REQUEST_ECHO);

	test_check_int_eq (client_connection_start (client, connection), 0, NULL);

	packet_set_network_values (&request, NULL, client, connection, NULL);
	test_check_unsigned_eq (packet_send (&request, 0, NULL, false), 0, NULL);

	features_check_echoes (1);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// the answer to a test packet with data is only its header, and it says so,
// it used to announce the data that it did not send, and the client waited for it
static void test_features_ping_with_data (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	test_check_unsigned_eq (client_connect_to_cerver (client, connection), 0, NULL);

	char data[FEATURES_PING_DATA_SIZE] = { 0 };
	Packet *ping = packet_generate_request (PACKET_TYPE_TEST, 0, data, FEATURES_PING_DATA_SIZE);
	test_check_ptr (ping);

	test_check_unsigned_eq (client_request_to_cerver (client, connection, ping), 0, NULL);
	test_check_unsigned_eq (
		CERVER_STATS_GET (client->stats->received_packets->n_test_packets), 1, NULL
	);

	packet_delete (ping);

	test_check_int_eq (client_connection_start (client, connection), 0, NULL);

	Packet request;
	packet_init_request (&request, PACKET_TYPE_APP, FEATURES_REQUEST_ECHO);
	packet_set_network_values (&request, NULL, client, connection, NULL);
	test_check_unsigned_eq (packet_send (&request, 0, NULL, false), 0, NULL);

	features_check_echoes (1);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// a packet bigger than the socket buffers in a non blocking socket
// waits for space until the cerver reads it, instead of failing with EAGAIN
static void test_features_large_packet (void) {

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start (&features, port);

	Client *client = features_client_create ();
	Connection *connection = features_connection_create (client, port);

	// without the update thread, nothing else reads the non blocking socket
	test_check_unsigned_eq (client_connect_to_cerver (client, connection), 0, NULL);
	test_check_true (sock_set_blocking (connection->socket->sock_fd, false));

	char *data = (char *) calloc (FEATURES_LARGE_PACKET_SIZE, sizeof (char));
	test_check_ptr (data);

	Packet *packet = packet_generate_request (
		PACKET_TYPE_APP, FEATURES_REQUEST_LARGE, data, FEATURES_LARGE_PACKET_SIZE
	);

	test_check_ptr (packet);
	packet_set_network_values (packet, NULL, client, connection, NULL);

	size_t sent = 0;
	test_check_unsigned_eq (packet_send (packet, 0, &sent, false), 0, NULL);
	test_check_unsigned_eq ((unsigned int) sent, (unsigned int) packet->packet_size, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&large_packets, 1, TEST_WAIT_SECONDS),
		1, "the cerver did not receive the large packet"
	);

	packet_delete (packet);
	free (data);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	features_cerver_stop (&features);

}

// the client triggers CLIENT_EVENT_SUCCESS_AUTH after it saved its session token,
// so the token is read after the event, never while the update thread writes it
static unsigned int client_success_auth_events = 0;

static bool features_session_received (const Client *client) {

	return (test_wait_counter (&client_success_auth_events, 1, TEST_WAIT_SECONDS) == 1)
		&& (client->session_id != NULL);

}

static void test_features_sessions (void) {

	client_success_auth_events = 0;

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start_with (&features, port, true);

	// the first connection authenticates with the credentials
	Client *first = features_client_create ();
	Connection *first_connection = features_connection_create (first, port);
	connection_set_auth_data (
		first_connection,
		credentials_new (FEATURES_USERNAME, FEATURES_PASSWORD), sizeof (Credentials),
		credentials_delete, false
	);

	// registered before the connection, so the event is never missed
	(void) client_event_register (
		first, CLIENT_EVENT_SUCCESS_AUTH,
		features_client_event, &client_success_auth_events, NULL,
		false, false
	);

	test_check_int_eq (client_connect_and_start (first, first_connection), 0, NULL);

	test_check (features_session_received (first), "the client did not get its session token");
	test_check_unsigned_eq (test_wait_counter (&success_auth_events, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the second connection authenticates with the token of the first one
	Client *second = features_client_create ();
	Connection *second_connection = features_connection_create (second, port);
	test_check_unsigned_eq (
		connection_set_auth_token (second_connection, first->session_id->str, false), 0, NULL
	);

	test_check_int_eq (client_connect_and_start (second, second_connection), 0, NULL);

	// a connection with a token is added to the existing client, it is not a new authentication
	test_check_unsigned_eq (test_wait_counter (&new_connection_events, 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_unsigned_eq (success_auth_events, 1, NULL);

	// a single client in the cerver with both connections
	test_check_unsigned_eq (CERVER_STATS_GET (features.cerver->stats->current_n_connected_clients), 1, NULL);

	features_send_echoes (second, second_connection, FEATURES_PACKETS);
	features_check_echoes (FEATURES_PACKETS);

	test_check_unsigned_eq (client_teardown (second), 0, NULL);
	test_check_unsigned_eq (client_teardown (first), 0, NULL);

	features_cerver_stop (&features);

}

// a cerver with an auth schema takes the credentials as an object of it,
// the bytes of the same credentials are not an object and fail the auth
static void test_features_auth_schema (void) {

	client_success_auth_events = 0;

	features_counters_reset ();

	const u16 port = features_port++;

	FeaturesCerver features = { 0 };
	features_cerver_start_with (&features, port, true);
	cerver_set_auth_schema (features.cerver, &credentials_schema);

	Credentials *credentials = credentials_new (FEATURES_USERNAME, FEATURES_PASSWORD);
	test_check_ptr (credentials);

	Client *object_client = features_client_create ();
	Connection *object_connection = features_connection_create (object_client, port);
	test_check_unsigned_eq (
		connection_set_auth_object (object_connection, &credentials_schema, credentials, false), 0, NULL
	);

	(void) client_event_register (
		object_client, CLIENT_EVENT_SUCCESS_AUTH,
		features_client_event, &client_success_auth_events, NULL,
		false, false
	);

	test_check_int_eq (client_connect_and_start (object_client, object_connection), 0, NULL);
	test_check (features_session_received (object_client), "the object credentials did not authenticate");
	test_check_unsigned_eq (test_wait_counter (&success_auth_events, 1, TEST_WAIT_SECONDS), 1, NULL);

	Client *bytes_client = features_client_create ();
	Connection *bytes_connection = features_connection_create (bytes_client, port);
	connection_set_auth_data (bytes_connection, credentials, sizeof (Credentials), credentials_delete, false);

	test_check_int_eq (client_connect_and_start (bytes_client, bytes_connection), 0, NULL);
	test_check_unsigned_eq (test_wait_counter (&failed_auth_events, 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_unsigned_eq (success_auth_events, 1, NULL);

	test_check_unsigned_eq (client_teardown (bytes_client), 0, NULL);
	test_check_unsigned_eq (client_teardown (object_client), 0, NULL);

	features_cerver_stop (&features);

}

#pragma endregion

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) printf ("Testing CLIENT features...\n");

	test_features_states_strings ();
	test_features_states ();
	test_features_states_unavailable ();

	test_features_connect_to_cerver_async ();

	test_features_send_queue ();
	test_features_send_queue_pending ();

	test_features_reconnect ();
	test_features_reconnect_address ();
	test_features_reconnect_request ();
	test_features_reconnect_end ();

	test_features_packet_init ();
	test_features_packet_init_send ();
	test_features_ping_with_data ();

	test_features_large_packet ();

	test_features_sessions ();
	test_features_auth_schema ();

	(void) printf ("Done with CLIENT features tests!\n\n");

	return 0;

}
