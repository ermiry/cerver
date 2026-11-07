#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/packets.h>

#include "../test.h"

// the ends of the connections of a client, against a fake cerver in the same process:
// - the app ends a connection while its update thread is in recv () (it used to hang the client)
// - the app ends a connection before its update thread started
// - the cerver closes the connection (the update thread ends it, removes it and stops the client)
// - the cerver sends CLOSE_CONNECTION (a packet handler ends it from the update thread)
// - client_disconnect () and client_teardown () with active connections

#define LIFECYCLE_ADDRESS					"127.0.0.1"

#define LIFECYCLE_REPEATS					(1 << 6)
#define LIFECYCLE_BUFFER_SIZE				(1 << 10)

#define LIFECYCLE_WAIT_SECONDS				4
#define LIFECYCLE_POLL_NANOSECONDS			1000000L
#define LIFECYCLE_NANOSECONDS				1000000000L

#define LIFECYCLE_TEARDOWN_MAX_SECONDS		2

#define LIFECYCLE_UPDATE_TIMEOUT			1

typedef enum LifecycleBehavior {

	LIFECYCLE_BEHAVIOR_NONE			= 0,
	LIFECYCLE_BEHAVIOR_HOLD			= 1,	// keeps the connection until the client closes it
	LIFECYCLE_BEHAVIOR_CLOSE		= 2,	// closes the connection after accepting it
	LIFECYCLE_BEHAVIOR_PACKET		= 3,	// sends CLOSE_CONNECTION and waits for the client
	LIFECYCLE_BEHAVIOR_ECHO			= 4,	// sends back what it receives until the client closes it
	LIFECYCLE_BEHAVIOR_BAD			= 5,	// sends packets of an unknown type and waits for the client

	LIFECYCLE_BEHAVIOR_MAX			= 6

} LifecycleBehavior;

// a fake cerver that accepts connections and handles each one in a thread
typedef struct LifecycleServer {

	int sock_fd;
	u16 port;

	LifecycleBehavior behavior;
	bool running;

	pthread_t thread;

} LifecycleServer;

typedef struct LifecycleConnection {

	int sock_fd;
	LifecycleBehavior behavior;

} LifecycleConnection;

#define LIFECYCLE_EVENTS					(CLIENT_EVENT_UNKNOWN + 1)

#define LIFECYCLE_FAILED_MAX_SLEEP			2

#define LIFECYCLE_CLIENTS					8
#define LIFECYCLE_CLIENTS_REPEATS			16

#define LIFECYCLE_REQUEST_TYPE				3

#define LIFECYCLE_UNKNOWN_PACKET_TYPE		200
#define LIFECYCLE_MAX_BAD_PACKETS			2

static unsigned int lifecycle_closed_events = 0;

static unsigned int lifecycle_events[LIFECYCLE_EVENTS] = { 0 };

static void lifecycle_events_reset (void) {

	for (unsigned int i = 0; i < LIFECYCLE_EVENTS; i++) {
		__atomic_store_n (&lifecycle_events[i], 0, __ATOMIC_SEQ_CST);
	}

	__atomic_store_n (&lifecycle_closed_events, 0, __ATOMIC_SEQ_CST);

}

#pragma region server

// waits until the client closes the connection
static void lifecycle_server_wait_close (const int sock_fd) {

	char buffer[LIFECYCLE_BUFFER_SIZE];
	while (recv (sock_fd, buffer, LIFECYCLE_BUFFER_SIZE, 0) > 0);

}

static void lifecycle_server_send_close (const int sock_fd) {

	Packet *packet = packet_create (
		PACKET_TYPE_CLIENT, CLIENT_PACKET_TYPE_CLOSE_CONNECTION, NULL, 0
	);

	test_check_ptr (packet);
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);

	(void) send (sock_fd, packet->packet, packet->packet_size, MSG_NOSIGNAL);

	packet_delete (packet);

}

static unsigned int lifecycle_bad_packets = 0;

static void lifecycle_server_send_bad (const int sock_fd) {

	const unsigned int count = __atomic_load_n (&lifecycle_bad_packets, __ATOMIC_SEQ_CST);

	for (unsigned int i = 0; i < count; i++) {
		Packet *packet = packet_create_request ((PacketType) LIFECYCLE_UNKNOWN_PACKET_TYPE, 0);
		test_check_ptr (packet);

		(void) send (sock_fd, packet->packet, packet->packet_size, MSG_NOSIGNAL);

		packet_delete (packet);
	}

}

static void lifecycle_server_echo (const int sock_fd) {

	char buffer[LIFECYCLE_BUFFER_SIZE];

	ssize_t received = 0;
	while ((received = recv (sock_fd, buffer, LIFECYCLE_BUFFER_SIZE, 0)) > 0) {
		(void) send (sock_fd, buffer, (size_t) received, MSG_NOSIGNAL);
	}

}

static void *lifecycle_server_connection (void *connection_ptr) {

	LifecycleConnection *connection = (LifecycleConnection *) connection_ptr;

	switch (connection->behavior) {
		case LIFECYCLE_BEHAVIOR_HOLD:
			lifecycle_server_wait_close (connection->sock_fd);
			break;

		case LIFECYCLE_BEHAVIOR_PACKET:
			lifecycle_server_send_close (connection->sock_fd);
			lifecycle_server_wait_close (connection->sock_fd);
			break;

		case LIFECYCLE_BEHAVIOR_ECHO:
			lifecycle_server_echo (connection->sock_fd);
			break;

		case LIFECYCLE_BEHAVIOR_BAD:
			lifecycle_server_send_bad (connection->sock_fd);
			lifecycle_server_wait_close (connection->sock_fd);
			break;

		default: break;
	}

	(void) close (connection->sock_fd);
	free (connection);

	return NULL;

}

static void *lifecycle_server_accept (void *server_ptr) {

	LifecycleServer *server = (LifecycleServer *) server_ptr;

	while (__atomic_load_n (&server->running, __ATOMIC_SEQ_CST)) {
		const int sock_fd = accept (server->sock_fd, NULL, NULL);
		if (sock_fd < 0) break;

		LifecycleConnection *connection = (LifecycleConnection *) malloc (sizeof (LifecycleConnection));
		test_check_ptr (connection);
		connection->sock_fd = sock_fd;
		connection->behavior = server->behavior;

		pthread_t thread;
		test_check_int_eq (pthread_create (&thread, NULL, lifecycle_server_connection, connection), 0, NULL);
		(void) pthread_detach (thread);
	}

	return NULL;

}

static LifecycleServer *lifecycle_server_start (const LifecycleBehavior behavior) {

	LifecycleServer *server = (LifecycleServer *) calloc (1, sizeof (LifecycleServer));
	test_check_ptr (server);

	server->behavior = behavior;
	server->running = true;

	server->sock_fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (server->sock_fd >= 0, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = 0;
	(void) inet_pton (AF_INET, LIFECYCLE_ADDRESS, &address.sin_addr);

	test_check_int_eq (bind (server->sock_fd, (struct sockaddr *) &address, sizeof (struct sockaddr_in)), 0, NULL);
	test_check_int_eq (listen (server->sock_fd, SOMAXCONN), 0, NULL);

	socklen_t length = sizeof (struct sockaddr_in);
	test_check_int_eq (getsockname (server->sock_fd, (struct sockaddr *) &address, &length), 0, NULL);
	server->port = ntohs (address.sin_port);

	test_check_int_eq (pthread_create (&server->thread, NULL, lifecycle_server_accept, server), 0, NULL);

	return server;

}

static void lifecycle_server_stop (LifecycleServer *server) {

	__atomic_store_n (&server->running, false, __ATOMIC_SEQ_CST);
	(void) shutdown (server->sock_fd, SHUT_RDWR);

	(void) pthread_join (server->thread, NULL);
	(void) close (server->sock_fd);

	free (server);

}

#pragma endregion

#pragma region client

// the event's work owns its data
static void *lifecycle_connection_closed (void *event_data_ptr) {

	client_event_data_delete ((ClientEventData *) event_data_ptr);

	(void) __atomic_add_fetch (&lifecycle_closed_events, 1, __ATOMIC_SEQ_CST);

	return NULL;

}

// counts the event in the counter of its args
static void *lifecycle_event (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	client_event_data_delete (event_data);

	return NULL;

}

static void lifecycle_event_register (Client *client, const ClientEventType type) {

	test_check_unsigned_eq (
		client_event_register (
			client, type,
			lifecycle_event, &lifecycle_events[type], NULL,
			false, false
		), 0, NULL
	);

}

static Client *lifecycle_client_create (void) {

	Client *client = client_create ();
	test_check_ptr (client);

	test_check_unsigned_eq (
		client_event_register (
			client, CLIENT_EVENT_CONNECTION_CLOSE,
			lifecycle_connection_closed, NULL, NULL,
			false, false
		), 0, NULL
	);

	lifecycle_event_register (client, CLIENT_EVENT_CONNECTED);
	lifecycle_event_register (client, CLIENT_EVENT_CONNECTION_FAILED);

	return client;

}

// a port where nothing is listening
static u16 lifecycle_closed_port (void) {

	const int sock_fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (sock_fd >= 0, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	(void) inet_pton (AF_INET, LIFECYCLE_ADDRESS, &address.sin_addr);

	test_check_int_eq (bind (sock_fd, (struct sockaddr *) &address, sizeof (struct sockaddr_in)), 0, NULL);

	socklen_t length = sizeof (struct sockaddr_in);
	test_check_int_eq (getsockname (sock_fd, (struct sockaddr *) &address, &length), 0, NULL);

	(void) close (sock_fd);

	return ntohs (address.sin_port);

}

static Connection *lifecycle_connection_create (Client *client, const u16 port) {

	Connection *connection = client_connection_create (
		client, LIFECYCLE_ADDRESS, port, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	connection_set_update_timeout (connection, LIFECYCLE_UPDATE_TIMEOUT);

	return connection;

}

static Connection *lifecycle_connect (Client *client, const LifecycleServer *server) {

	Connection *connection = client_connection_create (
		client, LIFECYCLE_ADDRESS, server->port, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	// the update thread wakes up from recv () every second, the ends must not wait for it
	connection_set_update_timeout (connection, LIFECYCLE_UPDATE_TIMEOUT);

	test_check_int_eq (client_connect_and_start (client, connection), 0, "failed to connect to the fake cerver");

	return connection;

}

static size_t lifecycle_connections_count (Client *client) {

	(void) pthread_mutex_lock (client->lock);
	const size_t count = dlist_size (client->connections);
	(void) pthread_mutex_unlock (client->lock);

	return count;

}

static double lifecycle_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / (double) LIFECYCLE_NANOSECONDS);

}

// waits until the client has the expected number of connections
static bool lifecycle_wait_connections (Client *client, const size_t expected) {

	const struct timespec interval = { 0, LIFECYCLE_POLL_NANOSECONDS };
	const double deadline = lifecycle_now () + LIFECYCLE_WAIT_SECONDS;

	while ((lifecycle_connections_count (client) != expected) && (lifecycle_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	return lifecycle_connections_count (client) == expected;

}

#pragma endregion

#pragma region tests

// the app ends the connection while its update thread is in recv (),
// and right after starting it, before the update thread runs
static void test_lifecycle_app_end (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);
	Client *client = lifecycle_client_create ();

	for (unsigned int i = 0; i < LIFECYCLE_REPEATS; i++) {
		Connection *connection = lifecycle_connect (client, server);
		test_check_int_eq (client_connection_end (client, connection), 0, NULL);
		test_check_unsigned_eq (lifecycle_connections_count (client), 0, NULL);
	}

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// the cerver closes the connection: its update thread ends it and the client stops
static void test_lifecycle_remote_close (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_CLOSE);
	Client *client = lifecycle_client_create ();

	__atomic_store_n (&lifecycle_closed_events, 0, __ATOMIC_SEQ_CST);

	(void) lifecycle_connect (client, server);

	test_check (lifecycle_wait_connections (client, 0), "the closed connection was not removed");
	test_check_false (client_is_running (client));
	test_check_unsigned_eq (__atomic_load_n (&lifecycle_closed_events, __ATOMIC_SEQ_CST), 1, NULL);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// the cerver sends CLOSE_CONNECTION, the packet handler ends it from the update thread
static void test_lifecycle_close_packet (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_PACKET);
	Client *client = lifecycle_client_create ();

	for (unsigned int i = 0; i < LIFECYCLE_REPEATS; i++) {
		(void) lifecycle_connect (client, server);
		test_check (lifecycle_wait_connections (client, 0), "the connection was not ended by its close packet");
	}

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// ending one connection keeps the other ones
static void test_lifecycle_many_connections (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);
	Client *client = lifecycle_client_create ();

	Connection *first = lifecycle_connect (client, server);
	Connection *second = lifecycle_connect (client, server);
	test_check_unsigned_eq (lifecycle_connections_count (client), 2, NULL);

	test_check_int_eq (client_connection_end (client, first), 0, NULL);
	test_check_unsigned_eq (lifecycle_connections_count (client), 1, NULL);
	test_check_true (second->active);
	test_check_true (client_is_running (client));

	// an end from the app keeps the client running, only the cerver stops it
	test_check_int_eq (client_connection_end (client, second), 0, NULL);
	test_check_true (client_is_running (client));

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// client_disconnect () closes every connection through its update thread
static void test_lifecycle_disconnect (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);
	Client *client = lifecycle_client_create ();

	(void) lifecycle_connect (client, server);
	(void) lifecycle_connect (client, server);

	test_check_unsigned_eq (client_disconnect (client), 0, NULL);
	test_check (lifecycle_wait_connections (client, 0), "the disconnected connections were not removed");

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// client_teardown () ends active connections without waiting for fixed times
static void test_lifecycle_teardown (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);

	for (unsigned int i = 0; i < LIFECYCLE_REPEATS; i++) {
		Client *client = lifecycle_client_create ();

		(void) lifecycle_connect (client, server);
		(void) lifecycle_connect (client, server);

		const double start = lifecycle_now ();
		test_check_unsigned_eq (client_teardown (client), 0, NULL);
		test_check ((lifecycle_now () - start) < LIFECYCLE_TEARDOWN_MAX_SECONDS, "client_teardown () took too long");
	}

	lifecycle_server_stop (server);

}

// client_connect_async () connects in its own thread and triggers CONNECTED once
static void test_lifecycle_connect_async (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);
	Client *client = lifecycle_client_create ();

	lifecycle_events_reset ();

	Connection *connection = lifecycle_connection_create (client, server->port);
	test_check_unsigned_eq (client_connect_async (client, connection), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&lifecycle_events[CLIENT_EVENT_CONNECTED], 1, TEST_WAIT_SECONDS),
		1, "the async connection did not connect"
	);

	test_check_true (connection->active);
	test_check_true (client_is_running (client));
	test_check_unsigned_eq (lifecycle_events[CLIENT_EVENT_CONNECTION_FAILED], 0, NULL);

	test_check_int_eq (client_connection_end (client, connection), 0, NULL);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// a failed async connection triggers CONNECTION_FAILED once and does not start the client
static void test_lifecycle_connect_async_failed (void) {

	Client *client = lifecycle_client_create ();

	lifecycle_events_reset ();

	Connection *connection = lifecycle_connection_create (client, lifecycle_closed_port ());
	connection_set_max_sleep (connection, LIFECYCLE_FAILED_MAX_SLEEP);

	test_check_unsigned_eq (client_connect_async (client, connection), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&lifecycle_events[CLIENT_EVENT_CONNECTION_FAILED], 1, TEST_WAIT_SECONDS),
		1, "the failed connection was not reported"
	);

	test_check_false (connection->active);
	test_check_false (client_is_running (client));
	test_check_unsigned_eq (lifecycle_events[CLIENT_EVENT_CONNECTED], 0, NULL);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

}

// client_connect_and_start_async () connects and starts the update thread,
// that ends the connection when the cerver closes it
static void test_lifecycle_connect_and_start_async (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_CLOSE);
	Client *client = lifecycle_client_create ();

	lifecycle_events_reset ();

	Connection *connection = lifecycle_connection_create (client, server->port);
	test_check_unsigned_eq (client_connect_and_start_async (client, connection), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&lifecycle_events[CLIENT_EVENT_CONNECTED], 1, TEST_WAIT_SECONDS),
		1, "the async connection did not connect"
	);

	test_check_unsigned_eq (
		test_wait_counter (&lifecycle_closed_events, 1, TEST_WAIT_SECONDS),
		1, "the update thread did not see the remote close"
	);

	test_check (lifecycle_wait_connections (client, 0), "the closed connection was not removed");

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// the same client connects again after its connections ended,
// from the app (the client keeps running) and from the cerver (the client stopped)
static void test_lifecycle_reconnect (void) {

	LifecycleServer *hold = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);
	LifecycleServer *closing = lifecycle_server_start (LIFECYCLE_BEHAVIOR_CLOSE);
	Client *client = lifecycle_client_create ();

	lifecycle_events_reset ();

	Connection *first = lifecycle_connect (client, hold);
	test_check_int_eq (client_connection_end (client, first), 0, NULL);

	Connection *second = lifecycle_connect (client, hold);
	test_check_true (second->active);
	test_check_int_eq (client_connection_end (client, second), 0, NULL);

	(void) lifecycle_connect (client, closing);
	test_check (lifecycle_wait_connections (client, 0), "the closed connection was not removed");
	test_check_false (client_is_running (client));

	Connection *third = lifecycle_connect (client, hold);
	test_check_true (third->active);
	test_check_true (client_is_running (client));
	test_check_int_eq (client_connection_end (client, third), 0, NULL);

	test_check_unsigned_eq (lifecycle_events[CLIENT_EVENT_CONNECTED], 4, NULL);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (closing);
	lifecycle_server_stop (hold);

}

static void *lifecycle_client_thread (void *server_ptr) {

	const LifecycleServer *server = (const LifecycleServer *) server_ptr;

	for (unsigned int i = 0; i < LIFECYCLE_CLIENTS_REPEATS; i++) {
		Client *client = lifecycle_client_create ();

		Connection *connection = lifecycle_connect (client, server);
		(void) lifecycle_connect (client, server);

		test_check_int_eq (client_connection_end (client, connection), 0, NULL);
		test_check_unsigned_eq (client_teardown (client), 0, NULL);
	}

	return NULL;

}

static unsigned int lifecycle_responses = 0;
static unsigned int lifecycle_handled_at_event = 0;

static void lifecycle_app_handler (void *packet_ptr) {

	(void) packet_ptr;

	test_counter_add (&lifecycle_responses);

}

// CONNECTION_DATA comes after the response was handled
static void *lifecycle_request_done (void *event_data_ptr) {

	__atomic_store_n (
		&lifecycle_handled_at_event,
		__atomic_load_n (&lifecycle_responses, __ATOMIC_SEQ_CST), __ATOMIC_SEQ_CST
	);

	return lifecycle_event (event_data_ptr);

}

// client_request_to_cerver_async () triggers CONNECTION_DATA when its response was handled,
// and client_requests_wait () returns when no request is left
static void test_lifecycle_request_async (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_ECHO);
	Client *client = lifecycle_client_create ();

	lifecycle_events_reset ();
	__atomic_store_n (&lifecycle_responses, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&lifecycle_handled_at_event, 0, __ATOMIC_SEQ_CST);

	Handler *app_handler = handler_create (lifecycle_app_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (client, app_handler, NULL);

	test_check_unsigned_eq (
		client_event_register (
			client, CLIENT_EVENT_CONNECTION_DATA,
			lifecycle_request_done, &lifecycle_events[CLIENT_EVENT_CONNECTION_DATA], NULL,
			false, false
		), 0, NULL
	);

	// connected but not started, the request thread receives the response
	Connection *connection = lifecycle_connection_create (client, server->port);
	test_check_unsigned_eq (client_connect (client, connection), 0, NULL);

	Packet *request = packet_create_request (PACKET_TYPE_APP, LIFECYCLE_REQUEST_TYPE);
	test_check_ptr (request);
	test_check_unsigned_eq (client_request_to_cerver_async (client, connection, request), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&lifecycle_events[CLIENT_EVENT_CONNECTION_DATA], 1, TEST_WAIT_SECONDS),
		1, "the async request did not end"
	);

	test_check_unsigned_eq (__atomic_load_n (&lifecycle_handled_at_event, __ATOMIC_SEQ_CST), 1, NULL);

	client_requests_wait (client);
	test_check_unsigned_eq (client->n_async_threads, 0, NULL);

	packet_delete (request);

	test_check_int_eq (client_connection_end (client, connection), 0, NULL);
	test_check_unsigned_eq (client_teardown (client), 0, NULL);
	lifecycle_server_stop (server);

}

// the client ends a connection that receives its limit of bad packets, and keeps one below it
static void test_lifecycle_bad_packets (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_BAD);

	// below the limit
	__atomic_store_n (&lifecycle_bad_packets, LIFECYCLE_MAX_BAD_PACKETS - 1, __ATOMIC_SEQ_CST);

	Client *kept = lifecycle_client_create ();
	client_set_max_bad_packets (kept, LIFECYCLE_MAX_BAD_PACKETS);
	(void) lifecycle_connect (kept, server);

	test_check_unsigned_eq (
		test_wait_counter_64 (&kept->stats->received_packets->n_bad_packets, LIFECYCLE_MAX_BAD_PACKETS - 1, TEST_WAIT_SECONDS),
		LIFECYCLE_MAX_BAD_PACKETS - 1, "the bad packets were not counted"
	);

	test_check_unsigned_eq (lifecycle_connections_count (kept), 1, "the connection was ended before the limit");
	test_check_unsigned_eq (client_teardown (kept), 0, NULL);

	// at the limit
	__atomic_store_n (&lifecycle_bad_packets, LIFECYCLE_MAX_BAD_PACKETS, __ATOMIC_SEQ_CST);

	Client *ended = lifecycle_client_create ();
	client_set_max_bad_packets (ended, LIFECYCLE_MAX_BAD_PACKETS);
	(void) lifecycle_connect (ended, server);

	test_check (lifecycle_wait_connections (ended, 0), "the connection was not ended at the limit");
	test_check_unsigned_eq (client_teardown (ended), 0, NULL);

	lifecycle_server_stop (server);

}

// many clients connect, end and are released at the same time (checked with TSan)
static void test_lifecycle_many_clients (void) {

	LifecycleServer *server = lifecycle_server_start (LIFECYCLE_BEHAVIOR_HOLD);

	pthread_t threads[LIFECYCLE_CLIENTS];
	for (unsigned int i = 0; i < LIFECYCLE_CLIENTS; i++) {
		test_check_int_eq (pthread_create (&threads[i], NULL, lifecycle_client_thread, server), 0, NULL);
	}

	for (unsigned int i = 0; i < LIFECYCLE_CLIENTS; i++) {
		(void) pthread_join (threads[i], NULL);
	}

	lifecycle_server_stop (server);

}

#pragma endregion

int main (int argc, char **argv) {

	test_output_init ();

	(void) argc;
	(void) argv;

	(void) printf ("Testing CLIENT lifecycle...\n");

	test_lifecycle_app_end ();
	test_lifecycle_remote_close ();
	test_lifecycle_close_packet ();
	test_lifecycle_many_connections ();
	test_lifecycle_disconnect ();
	test_lifecycle_teardown ();
	test_lifecycle_connect_async ();
	test_lifecycle_connect_async_failed ();
	test_lifecycle_connect_and_start_async ();
	test_lifecycle_reconnect ();
	test_lifecycle_request_async ();
	test_lifecycle_bad_packets ();
	test_lifecycle_many_clients ();

	(void) printf ("Done!\n");

	return 0;

}
