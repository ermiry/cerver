#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerver/cerver.h>

#include <cerver/http/http.h>
#include <cerver/http/tree.h>

#include <cerver/http/websockets/client.h>
#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/handshake.h>
#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include "../address.h"
#include "../test.h"

#include "../websockets/raw.h"

#include "faults.h"
#include "websockets.h"

// after the ports of the other suites
#define FAULTS_TEST_PORT_BASE				(1 << 14)

#define FAULTS_TEST_THREADS					4

#define FAULTS_TEST_URL_SIZE				(1 << 8)

// milliseconds of the handshakes of the clients and of the waits of the fake server
#define FAULTS_TEST_TIMEOUT					(test_milliseconds (1 << 10))

// the payload of the frame that the fake server sends in the same packet as its 101:
// its frame has a size that no other allocation of an attempt has, so a fault of that size
// is the one of the bytes that followed the response
#define FAULTS_TEST_EARLY_PAYLOAD			113
#define FAULTS_TEST_EARLY_FRAME				(FAULTS_TEST_EARLY_PAYLOAD + 2)
#define FAULTS_TEST_EARLY_CHARACTER			'e'

static const char *faults_test_key_header = { "Sec-WebSocket-Key: " };

static u16 faults_test_port = FAULTS_TEST_PORT_BASE;

// what the methods of the connections saw, read and written atomically
typedef struct FaultsTestState {

	unsigned int opened;
	unsigned int closed;
	unsigned int errors;
	unsigned int last_error;

} FaultsTestState;

static FaultsTestState state;

static void faults_test_on_open (HttpWebSocket *web_socket) {

	(void) web_socket;

	test_counter_add (&state.opened);

}

static void faults_test_on_close (
	HttpWebSocket *web_socket, u16 code, const char *reason, size_t length
) {

	(void) web_socket;
	(void) code;
	(void) reason;
	(void) length;

	test_counter_add (&state.closed);

}

static void faults_test_on_error (HttpWebSocket *web_socket, HttpWebSocketError error) {

	(void) web_socket;

	__atomic_store_n (&state.last_error, (unsigned int) error, __ATOMIC_SEQ_CST);
	test_counter_add (&state.errors);

}

static const HttpWebSocketHandlers faults_test_handlers = {
	NULL, faults_test_on_open, NULL, NULL,
	NULL, NULL, faults_test_on_close, faults_test_on_error
};

#pragma region fake

// a server that answers the handshake of a client with a valid 101,
// and with a text frame in the same packet when early is true
typedef struct FaultsTestFake {

	int listener;
	u16 port;
	bool early;
	pthread_t thread;

} FaultsTestFake;

static int faults_test_listen (const u16 port) {

	const char *address = test_address ();
	const bool ipv6 = (strchr (address, ':') != NULL);

	struct sockaddr_storage storage;
	(void) memset (&storage, 0, sizeof (struct sockaddr_storage));
	socklen_t length = 0;

	if (ipv6) {
		struct sockaddr_in6 *address6 = (struct sockaddr_in6 *) &storage;
		address6->sin6_family = AF_INET6;
		address6->sin6_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET6, address, &address6->sin6_addr), 1, NULL);
		length = sizeof (struct sockaddr_in6);
	}

	else {
		struct sockaddr_in *address4 = (struct sockaddr_in *) &storage;
		address4->sin_family = AF_INET;
		address4->sin_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET, address, &address4->sin_addr), 1, NULL);
		length = sizeof (struct sockaddr_in);
	}

	const int listener = socket (ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	test_check (listener > -1, "socket () failed");

	const int enabled = 1;
	(void) setsockopt (listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof (int));
	test_check_int_eq (bind (listener, (const struct sockaddr *) &storage, length), 0, NULL);
	test_check_int_eq (listen (listener, 1), 0, NULL);

	return listener;

}

// reads the request of the client and computes its accept
static void faults_test_fake_request (const int fd, char *accept) {

	char request[RAW_RESPONSE_SIZE] = { 0 };
	size_t size = 0;
	while (!strstr (request, "\r\n\r\n") && (size < (RAW_RESPONSE_SIZE - 1))) {
		const ssize_t received = recv (fd, request + size, RAW_RESPONSE_SIZE - 1 - size, 0);
		test_check (received > 0, "the request did not arrive");
		size += (size_t) received;
	}

	const char *key = strstr (request, faults_test_key_header);
	test_check_ptr (key);
	key += strlen (faults_test_key_header);
	test_check_true (http_web_socket_accept_compute (key, HTTP_WEB_SOCKET_KEY_LENGTH, accept));

}

static void faults_test_fake_respond (const FaultsTestFake *fake, const int fd, const char *accept) {

	char response[RAW_RESPONSE_SIZE];
	int length = snprintf (
		response, RAW_RESPONSE_SIZE,
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
		accept
	);

	if (fake->early) {
		char payload[FAULTS_TEST_EARLY_PAYLOAD];
		(void) memset (payload, FAULTS_TEST_EARLY_CHARACTER, FAULTS_TEST_EARLY_PAYLOAD);
		length += (int) raw_frame (
			(u8 *) response + length, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT,
			payload, FAULTS_TEST_EARLY_PAYLOAD, false
		);
	}

	raw_send (fd, response, (size_t) length);

}

// the client ends the TCP connection after it fails, or when it is deleted
static void faults_test_fake_wait_end (const int fd) {

	char discarded[RAW_RESPONSE_SIZE];

	struct pollfd pfd = { fd, POLLIN, 0 };
	bool open = true;
	while (open && (poll (&pfd, 1, FAULTS_TEST_TIMEOUT * 4) > 0)) {
		open = (recv (fd, discarded, RAW_RESPONSE_SIZE, 0) > 0);
	}

}

static void *faults_test_fake_thread (void *fake_ptr) {

	FaultsTestFake *fake = (FaultsTestFake *) fake_ptr;

	const int fd = accept (fake->listener, NULL, NULL);
	if (fd >= 0) {
		char accept_value[HTTP_WEB_SOCKET_ACCEPT_SIZE];
		faults_test_fake_request (fd, accept_value);
		faults_test_fake_respond (fake, fd, accept_value);
		faults_test_fake_wait_end (fd);

		(void) close (fd);
	}

	return NULL;

}

static void faults_test_fake_start (FaultsTestFake *fake, const bool early) {

	(void) memset (fake, 0, sizeof (FaultsTestFake));
	fake->port = faults_test_port++;
	fake->early = early;
	fake->listener = faults_test_listen (fake->port);
	test_check_int_eq (pthread_create (&fake->thread, NULL, faults_test_fake_thread, fake), 0, NULL);

}

static void faults_test_fake_stop (FaultsTestFake *fake) {

	test_check_int_eq (pthread_join (fake->thread, NULL), 0, NULL);
	(void) close (fake->listener);

}

static HttpWebSocketClient *faults_test_client (const u16 port) {

	HttpWebSocketClient *client = http_web_socket_client_create ();
	test_check_ptr (client);

	const char *address = test_address ();
	const bool ipv6 = (strchr (address, ':') != NULL);

	char url[FAULTS_TEST_URL_SIZE];
	(void) snprintf (
		url, FAULTS_TEST_URL_SIZE, ipv6 ? "ws://[%s]:%u/fake" : "ws://%s:%u/fake",
		address, (unsigned int) port
	);

	test_check_unsigned_eq (http_web_socket_client_set_url (client, url), 0, NULL);

	http_web_socket_client_set_handlers (client, &faults_test_handlers);
	http_web_socket_client_set_timeouts_milliseconds (client, FAULTS_TEST_TIMEOUT, FAULTS_TEST_TIMEOUT);

	return client;

}

#pragma endregion

#pragma region client

// C14: the frames that came with the 101 and that have no memory fail the attempt,
// they are never lost in silence
static void test_client_early_frames_without_memory (void) {

	(void) memset (&state, 0, sizeof (FaultsTestState));

	FaultsTestFake fake;
	faults_test_fake_start (&fake, true);

	HttpWebSocketClient *client = faults_test_client (fake.port);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);
	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_TEST_EARLY_FRAME);
	const u8 result = http_web_socket_client_connect (client);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, NULL);
	test_check_unsigned_eq (result, 1, "the attempt lost the frames that came with the response");
	test_check_unsigned_eq (
		__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_MEMORY, NULL
	);

	test_check_unsigned_eq (http_web_socket_client_get_connections (client), 0, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 0, NULL);

	http_web_socket_client_delete (client);
	faults_test_fake_stop (&fake);

}

// C8: a synchronous connect () whose thread can not be created abandons its connection:
// its methods report it, and it is not one of the connections that were opened
static void test_client_thread_not_created (void) {

	(void) memset (&state, 0, sizeof (FaultsTestState));

	FaultsTestFake fake;
	faults_test_fake_start (&fake, false);

	HttpWebSocketClient *client = faults_test_client (fake.port);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_THREAD);
	faults_arm (FAULTS_FUNCTION_THREAD, true, FAULTS_ANY_SIZE);
	const u8 result = http_web_socket_client_connect (client);
	faults_disarm (FAULTS_FUNCTION_THREAD);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_THREAD), triggered + 1, NULL);
	test_check_unsigned_eq (result, 1, NULL);
	test_check_unsigned_eq (
		__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_MEMORY, NULL
	);

	test_check_unsigned_eq (http_web_socket_client_get_connections (client), 0, "the abandoned connection was counted");
	test_check_unsigned_eq (__atomic_load_n (&state.closed, __ATOMIC_SEQ_CST), 1, NULL);

	http_web_socket_client_delete (client);
	faults_test_fake_stop (&fake);

}

#pragma endregion

#pragma region server

typedef struct FaultsTestServer {

	Cerver *cerver;
	HttpCerver *http_cerver;
	pthread_t thread;
	u16 port;

} FaultsTestServer;

static void *faults_test_server_thread (void *server_ptr) {

	FaultsTestServer *server = (FaultsTestServer *) server_ptr;

	(void) cerver_start (server->cerver);

	return NULL;

}

// a web cerver with one WebSocket endpoint, /ws/echo, that takes one connection at a time
static void faults_test_server_start (FaultsTestServer *server) {

	(void) memset (server, 0, sizeof (FaultsTestServer));

	server->port = faults_test_port++;

	Cerver *cerver = cerver_create_web ("faults-test", server->port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);
	cerver_set_thpool_n_threads (cerver, FAULTS_TEST_THREADS);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	http_cerver_set_web_socket_max_connections (http_cerver, 1);

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");
	test_check_ptr (ws);
	test_check_ptr (http_group_web_socket_add (ws, "echo", &faults_test_handlers));

	server->cerver = cerver;
	server->http_cerver = http_cerver;

	test_check_int_eq (pthread_create (&server->thread, NULL, faults_test_server_thread, server), 0, NULL);

	// the first connection waits until it listens: every thread of the cerver exists after it
	(void) close (raw_connect (server->port));

}

static void faults_test_server_stop (FaultsTestServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

}

// the fds of the process without a cerver: the reserve fd of accept () stays open for the life
// of the process, so a first cerver opens it before they are counted
static unsigned int faults_test_server_fds (void) {

	FaultsTestServer first;
	faults_test_server_start (&first);
	faults_test_server_stop (&first);

	return raw_open_fds ();

}

static u64 faults_test_server_connections (const FaultsTestServer *server) {

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);

	return stats.n_connections;

}

// waits until the connections of the cerver are the expected ones
static u64 faults_test_server_wait_connections (const FaultsTestServer *server, const u64 expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;

	u64 connections = faults_test_server_connections (server);
	while ((connections != expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		connections = faults_test_server_connections (server);
	}

	return connections;

}

// S4: a connection whose thread can not be created ends: it is not left open in the registry,
// its place goes back, its socket goes back to its connection, and the cerver takes the next one
static void test_server_thread_not_created (void) {

	const unsigned int fds = faults_test_server_fds ();

	(void) memset (&state, 0, sizeof (FaultsTestState));

	FaultsTestServer server;
	faults_test_server_start (&server);

	// the TCP connection is taken before the fault, only the thread of the WebSocket can fail
	const int fd = raw_connect (server.port);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_THREAD);
	faults_arm (FAULTS_FUNCTION_THREAD, false, FAULTS_ANY_SIZE);

	char response[RAW_RESPONSE_SIZE];
	(void) raw_upgrade (fd, "/ws/echo", "", response);

	RawReader reader;
	raw_reader_init (&reader, fd);
	test_check (raw_wait_end (&reader, FAULTS_TEST_TIMEOUT * 4), "the connection without a thread was not ended");
	raw_reader_end (&reader);
	(void) close (fd);

	faults_disarm (FAULTS_FUNCTION_THREAD);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_THREAD), triggered + 1, NULL);

	test_check_unsigned_eq (faults_test_server_wait_connections (&server, 0), 0, "the connection was left in the registry");
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 0, NULL);

	// the only place of the cerver was given back
	const int next = raw_open (server.port, "/ws/echo", "");
	test_check_unsigned_eq (test_wait_counter (&state.opened, 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_unsigned_eq (faults_test_server_wait_connections (&server, 1), 1, NULL);

	raw_send_close (next, HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL);
	test_check_unsigned_eq (test_wait_counter (&state.closed, 1, TEST_WAIT_SECONDS), 1, NULL);
	(void) close (next);

	faults_test_server_stop (&server);

	// every connection and its thread ended with the cerver
	if (raw_open_fds () != fds) raw_print_fds ();
	test_check_unsigned_eq (raw_open_fds (), fds, NULL);

}

#pragma endregion

void faults_tests_websockets (void) {

	(void) printf ("Testing FAULTS websockets...\n");

	test_run (test_client_early_frames_without_memory);
	test_run (test_client_thread_not_created);
	test_run (test_server_thread_not_created);

	(void) printf ("Done!\n");

}
