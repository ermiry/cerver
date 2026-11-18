#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include "cerver/events/internal.h"
#include <cerver/network/network.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include "cerver/http/core/types.h"
#include <cerver/http/headers/types.h>
#include <cerver/http/requests/request.h>
#include "cerver/http/responses/prepared.h"
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/websockets/settings.h>

#include "../handler.h"
#include "../test.h"
#include "http.h"

// runs a real web cerver in a thread and talks to it with raw sockets that send more than one request,
// to test the connections that are kept between requests by its event loops

// what this suite pins:
// - every response had "Connection: close" and its connection was closed, a client paid a connect by request
// - the requests that arrived behind another one in the same buffer (pipelined) were dropped
// - a connection that is kept must be closed when its client can not find the end of its response,
//   when its request was rejected, when it reached its requests or when its response says "close"
// - what a request leaves (its body, its headers, its params) must not reach the next one of its connection
// - the idle connections must not keep a cerver that stops, nor new clients out at the limit of connections

#define KEEPALIVE_PORT_BASE					((1 << 13) + (1 << 9))
#define KEEPALIVE_ADDRESS					"127.0.0.1"

#define KEEPALIVE_BUFFER_SIZE				(1 << 14)
#define KEEPALIVE_RESPONSE_SIZE				(1 << 12)
#define KEEPALIVE_REQUEST_SIZE				(1 << 9)

#define KEEPALIVE_CONNECT_RETRIES			(1 << 8)
// 2^22 ns
#define KEEPALIVE_CONNECT_INTERVAL			4194304L

// secs that a client waits for a response
#define KEEPALIVE_RECEIVE_TIMEOUT			16

// milliseconds that a client waits to see that its connection was closed, or that it was not
#define KEEPALIVE_CLOSED_TIMEOUT			4096
#define KEEPALIVE_OPEN_TIMEOUT				128

// milliseconds that a connection of the cerver of a test waits for its next request
#define KEEPALIVE_SHORT_TIMEOUT				256

// secs that the cervers of the tests keep an idle connection, more than any test takes:
// a test that passes because that time ended is a test that hangs
#define KEEPALIVE_LONG_TIMEOUT				(1 << 7)

// secs that a cerver has to stop, far less than the time that it keeps an idle connection
#define KEEPALIVE_STOP_TIMEOUT				(1 << 4)
#define KEEPALIVE_DRAIN_TIMEOUT				(1 << 3)

#define KEEPALIVE_REQUESTS					(1 << 4)
#define KEEPALIVE_MAX_REQUESTS				3
#define KEEPALIVE_PIPELINED					3

#define KEEPALIVE_THREADS					4
#define KEEPALIVE_QUEUE_SIZE				(1 << 4)

// the connections of the cerver of the limit test: the idle ones and the one that takes the place of the oldest
#define KEEPALIVE_LIMIT_CONNECTIONS			2

// the event loops of the cervers of the tests that act on one loop: its idle connections, its rejecting
#define KEEPALIVE_SINGLE_LOOP				1

// the idle connections of the test of a busy pool: the one that waited and the one that had the thread
#define KEEPALIVE_BUSY_IDLE					2

#define KEEPALIVE_ALARM						256

#define KEEPALIVE_NANOSECONDS_PER_MILLISECOND	1000000L
#define KEEPALIVE_MILLISECONDS_PER_SECOND		1000

// what the options of a test do with the keep alive of its cerver
#define KEEPALIVE_CONFIGURE_DEFAULT			(-1)
#define KEEPALIVE_CONFIGURE_DISABLED		0
#define KEEPALIVE_CONFIGURE_ENABLED			1

static const char *keepalive_header_end = { "\r\n\r\n" };
static const char *keepalive_content_length = { "Content-Length:" };
static const char *keepalive_connection_close = { "Connection: close\r\n" };
static const char *keepalive_connection_keep = { "Connection: keep-alive\r\n" };
static const char *keepalive_connection_header = { "Connection:" };

static const char *keepalive_none = { "none" };
static const char *keepalive_unframed_body = { "unframed-body" };
static const char *keepalive_closing_body = { "closing-body" };
static const char *keepalive_valid_authorization = { "valid" };

static const char *keepalive_ok_request = {
	"GET /keepalive/ok HTTP/1.1\r\nHost: localhost\r\n\r\n"
};

static const char *keepalive_ok_close_request = {
	"GET /keepalive/ok HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static unsigned int keepalive_port = KEEPALIVE_PORT_BASE;

#pragma region gate

// keeps the /keepalive/block requests working until it is opened
static pthread_mutex_t keepalive_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t keepalive_gate_cond = PTHREAD_COND_INITIALIZER;
static bool keepalive_gate_is_open = false;
static unsigned int keepalive_gate_waiting = 0;

static void keepalive_gate_reset (void) {

	(void) pthread_mutex_lock (&keepalive_gate_mutex);
	keepalive_gate_is_open = false;
	keepalive_gate_waiting = 0;
	(void) pthread_mutex_unlock (&keepalive_gate_mutex);

}

static void keepalive_gate_open (void) {

	(void) pthread_mutex_lock (&keepalive_gate_mutex);
	keepalive_gate_is_open = true;
	(void) pthread_cond_broadcast (&keepalive_gate_cond);
	(void) pthread_mutex_unlock (&keepalive_gate_mutex);

}

static unsigned int keepalive_gate_count (void) {

	(void) pthread_mutex_lock (&keepalive_gate_mutex);
	const unsigned int waiting = keepalive_gate_waiting;
	(void) pthread_mutex_unlock (&keepalive_gate_mutex);

	return waiting;

}

static void keepalive_sleep (const long milliseconds) {

	struct timespec interval = { 0, 0 };
	interval.tv_sec = (time_t) (milliseconds / KEEPALIVE_MILLISECONDS_PER_SECOND);
	interval.tv_nsec = (milliseconds % KEEPALIVE_MILLISECONDS_PER_SECOND) * KEEPALIVE_NANOSECONDS_PER_MILLISECOND;

	(void) nanosleep (&interval, NULL);

}

// waits until the /keepalive/block requests are working
static void keepalive_gate_wait_for (const unsigned int waiting) {

	while (keepalive_gate_count () < waiting) {
		struct timespec interval = { 0, KEEPALIVE_CONNECT_INTERVAL };
		(void) nanosleep (&interval, NULL);
	}

}

#pragma endregion

#pragma region routes

// the requests that the ok route handled, added by the threads of the cervers:
// a test that waits for it reads what its cerver wrote when it started after its thread did
static unsigned int keepalive_handled = 0;

// GET /keepalive/ok
static void keepalive_ok_handler (CerverHttpContext *context) {

	test_counter_add (&keepalive_handled);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /keepalive/items/:id, answers with its param
static void keepalive_items_handler (CerverHttpContext *context) {

	const CerverHttpView id = cerver_http_request_param (context, "id");

	const char *value = !cerver_http_view_is_missing (id) ? id.data : keepalive_none;

	(void) cerver_http_response_send_text (context, value, strlen (value));

}

// GET and POST /keepalive/state, answers with the body and the Accept header that the request has
static void keepalive_state_handler (CerverHttpContext *context) {

	const CerverHttpView body = cerver_http_request_body (context);
	const CerverHttpView accept = cerver_http_request_header (context, HTTP_HEADER_ACCEPT);

	char state[KEEPALIVE_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		state, sizeof (state), "%s|%s",
		!cerver_http_view_is_missing (body) ? body.data : keepalive_none,
		!cerver_http_view_is_missing (accept) ? accept.data : keepalive_none
	);

	(void) cerver_http_response_send_text (context, state, strlen (state));

}

// GET /keepalive/silent, the handler does not answer
static void keepalive_silent_handler (CerverHttpContext *context) {

	(void) context;

}

// GET /keepalive/unframed, a body without its length ends when the connection is closed:
// the responses of the context always have their length, so it is sent with the internal methods
static void keepalive_unframed_handler (CerverHttpContext *context) {

	(void) http_response_create_and_send (
		HTTP_STATUS_OK, keepalive_unframed_body, strlen (keepalive_unframed_body), context
	);

}

// GET /keepalive/closing, a response that closes its connection with its own header
static void keepalive_closing_handler (CerverHttpContext *context) {

	(void) cerver_http_response_set_header (context, HTTP_HEADER_CONNECTION, HTTP_RESPONSE_CONNECTION_CLOSE);
	(void) cerver_http_response_send (context, keepalive_closing_body, strlen (keepalive_closing_body));

}

// GET /keepalive/block, answers when the gate is opened
static void keepalive_block_handler (CerverHttpContext *context) {

	(void) pthread_mutex_lock (&keepalive_gate_mutex);
	keepalive_gate_waiting += 1;
	while (!keepalive_gate_is_open) (void) pthread_cond_wait (&keepalive_gate_cond, &keepalive_gate_mutex);
	(void) pthread_mutex_unlock (&keepalive_gate_mutex);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// POST /keepalive/private, only with the valid authorization
static unsigned int keepalive_authentication_handler (CerverHttpContext *context) {

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	return cerver_http_view_equals (authorization, keepalive_valid_authorization) ? 0 : 1;

}

static const HttpWebSocketHandlers keepalive_web_socket_handlers = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void keepalive_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *group = cerver_http_group_create (http_cerver, "keepalive");
	test_check_ptr (group);

	test_check_ptr (cerver_http_group_get (group, "ok", keepalive_ok_handler));
	test_check_ptr (cerver_http_group_get (group, "items/:id", keepalive_items_handler));
	test_check_ptr (cerver_http_group_get (group, "state", keepalive_state_handler));
	test_check_ptr (cerver_http_group_post (group, "state", keepalive_state_handler));
	test_check_ptr (cerver_http_group_get (group, "silent", keepalive_silent_handler));
	test_check_ptr (cerver_http_group_get (group, "unframed", keepalive_unframed_handler));
	test_check_ptr (cerver_http_group_get (group, "closing", keepalive_closing_handler));
	test_check_ptr (cerver_http_group_get (group, "block", keepalive_block_handler));

	CerverHttpEndpoint *private_endpoint = cerver_http_group_post (group, "private", keepalive_state_handler);
	test_check_ptr (private_endpoint);
	cerver_http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_endpoint_set_authentication_handler (private_endpoint, keepalive_authentication_handler);

	// GET /keepalive/socket, a WebSocket endpoint that only takes the connection
	test_check_ptr (cerver_http_group_web_socket_add (group, "socket", &keepalive_web_socket_handlers));

}

#pragma endregion

#pragma region server

typedef struct KeepAliveOptions {

	// KEEPALIVE_CONFIGURE_*: by default a cerver is not configured (its event loops keep its connections)
	int configure;

	// milliseconds, 0 keeps a long time
	unsigned int timeout;
	// 0 keeps the default
	unsigned int requests;

	// 0 keeps the default
	unsigned int max_connections;

	// the threads of its pool, 0 for KEEPALIVE_THREADS
	unsigned int threads;

	// its event loops, 0 keeps the default (one for each processor)
	unsigned int io_threads;

} KeepAliveOptions;

typedef struct KeepAliveServer {

	Cerver *cerver;
	CerverHttp *http_cerver;
	u16 port;

	pthread_t thread;

} KeepAliveServer;

static KeepAliveOptions keepalive_options_default (void) {

	KeepAliveOptions options = { KEEPALIVE_CONFIGURE_DEFAULT, 0, 0, 0, 0, 0 };

	return options;

}

static void *keepalive_server_thread (void *server_ptr) {

	KeepAliveServer *server = (KeepAliveServer *) server_ptr;

	test_check_unsigned_eq (cerver_start (server->cerver), 0, NULL);

	return NULL;

}

static void keepalive_server_configure (
	CerverHttp *http_cerver, const KeepAliveOptions *options
) {

	switch (options->configure) {
		case KEEPALIVE_CONFIGURE_DISABLED: cerver_http_set_keep_alive (http_cerver, false); break;
		case KEEPALIVE_CONFIGURE_ENABLED: cerver_http_set_keep_alive (http_cerver, true); break;

		default: break;
	}

	if (options->timeout) cerver_http_set_keep_alive_timeout_milliseconds (http_cerver, options->timeout);
	else cerver_http_set_keep_alive_timeout (http_cerver, KEEPALIVE_LONG_TIMEOUT);

	if (options->requests) cerver_http_set_keep_alive_requests (http_cerver, options->requests);

}

static int keepalive_connect (const u16 port);

// waits until the cerver is listening and one of its threads handled a complete request,
// in a connection that the cerver closes
static void keepalive_server_ready (const KeepAliveServer *server) {

	const unsigned int handled = __atomic_load_n (&keepalive_handled, __ATOMIC_SEQ_CST);

	const int fd = keepalive_connect (server->port);

	const size_t length = strlen (keepalive_ok_close_request);
	test_check (send (fd, keepalive_ok_close_request, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

	char response[KEEPALIVE_RESPONSE_SIZE];
	while (recv (fd, response, sizeof (response), 0) > 0);

	(void) close (fd);

	test_check_unsigned_gt (test_wait_counter (&keepalive_handled, handled + 1, TEST_WAIT_SECONDS), handled);

}

static KeepAliveServer *keepalive_server_start (const KeepAliveOptions *options) {

	KeepAliveServer *server = (KeepAliveServer *) calloc (1, sizeof (KeepAliveServer));
	test_check_ptr (server);

	server->port = (u16) keepalive_port++;

	Cerver *cerver = cerver_create_web ("keepalive-test", server->port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_events_backend (cerver, test_events_backend ());
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, (u16) (options->threads ? options->threads : KEEPALIVE_THREADS));
	cerver_set_thpool_queue_size (cerver, KEEPALIVE_QUEUE_SIZE);
	cerver_set_drain_timeout (cerver, KEEPALIVE_DRAIN_TIMEOUT);

	if (options->max_connections) cerver_set_max_connections (cerver, options->max_connections);
	if (options->io_threads) cerver_set_io_threads (cerver, options->io_threads);

	CerverHttp *http_cerver = cerver_http_get (cerver);

	// the /keepalive/block handler waits, and the tests count the jobs of the thpool:
	// the handlers run in its threads, not in the event loops
	(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

	// a path without a route is answered with 404 by cerver
	cerver_http_set_not_found_handler (http_cerver);

	keepalive_server_configure (http_cerver, options);

	keepalive_routes (http_cerver);

	server->cerver = cerver;
	server->http_cerver = http_cerver;

	test_check_int_eq (pthread_create (&server->thread, NULL, keepalive_server_thread, server), 0, NULL);

	keepalive_server_ready (server);

	return server;

}

// cerver_start () must return in time after it was asked to stop:
// a cerver that waits for its idle connections takes the time that it keeps them
static void keepalive_server_join (KeepAliveServer *server) {

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) (KEEPALIVE_STOP_TIMEOUT * test_time_scale ());

	const int result = pthread_timedjoin_np (server->thread, NULL, &deadline);
	if (result) (void) fprintf (stderr, "the cerver in port %u did not stop\n", (unsigned int) server->port);
	test_check_int_eq (result, 0, NULL);

}

static void keepalive_server_end (KeepAliveServer *server) {

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

static void keepalive_server_stop (KeepAliveServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	keepalive_server_join (server);

	keepalive_server_end (server);

}

static size_t keepalive_server_reused (const KeepAliveServer *server) {

	return __atomic_load_n (&server->http_cerver->n_reused_connections, __ATOMIC_RELAXED);

}

// waits until the event loops of the cerver have a number of connections:
// the cerver ends a connection a moment after its client sees it closed
static void keepalive_server_wait_connections (const KeepAliveServer *server, const unsigned int expected) {

	const double deadline = test_wait_now () + (double) (KEEPALIVE_STOP_TIMEOUT * test_time_scale ());
	while ((event_loop_group_connections (server->cerver) != expected) && (test_wait_now () < deadline)) {
		struct timespec interval = { 0, KEEPALIVE_CONNECT_INTERVAL };
		(void) nanosleep (&interval, NULL);
	}

	test_check_unsigned_eq (event_loop_group_connections (server->cerver), expected, NULL);

}

// waits until the event loops of the cerver have a number of idle connections: a connection waits idle once
// its loop sent its response, a moment after its client read it
static void keepalive_server_wait_idle (const KeepAliveServer *server, const unsigned int expected) {

	const double deadline = test_wait_now () + (double) (KEEPALIVE_STOP_TIMEOUT * test_time_scale ());
	while ((event_loop_group_idle (server->cerver) != expected) && (test_wait_now () < deadline)) {
		struct timespec interval = { 0, KEEPALIVE_CONNECT_INTERVAL };
		(void) nanosleep (&interval, NULL);
	}

	test_check_unsigned_eq (event_loop_group_idle (server->cerver), expected, NULL);

}

#pragma endregion

#pragma region client

// a client that keeps what it received behind a response: the next response of its connection
typedef struct KeepAliveClient {

	int fd;

	char buffer[KEEPALIVE_BUFFER_SIZE];
	size_t length;

	// the cerver closed the connection
	bool closed;

} KeepAliveClient;

static int keepalive_connect_once (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, KEEPALIVE_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static int keepalive_connect (const u16 port) {

	int fd = -1;
	for (unsigned int i = 0; (i < KEEPALIVE_CONNECT_RETRIES) && (fd < 0); i++) {
		fd = keepalive_connect_once (port);

		if (fd < 0) {
			struct timespec interval = { 0, KEEPALIVE_CONNECT_INTERVAL };
			(void) nanosleep (&interval, NULL);
		}
	}

	test_check (fd > -1, "failed to connect to the test cerver");

	(void) sock_set_timeout (fd, KEEPALIVE_RECEIVE_TIMEOUT);

	return fd;

}

static void keepalive_client_start (KeepAliveClient *client, const KeepAliveServer *server) {

	(void) memset (client, 0, sizeof (KeepAliveClient));

	client->fd = keepalive_connect (server->port);

}

static void keepalive_client_end (KeepAliveClient *client) {

	if (client->fd > -1) (void) close (client->fd);

	client->fd = -1;

}

static void keepalive_client_send (const KeepAliveClient *client, const char *data) {

	const size_t length = strlen (data);
	test_check (send (client->fd, data, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

}

// reads more bytes of the connection, false when the cerver closed it or nothing arrived in time
static bool keepalive_client_receive (KeepAliveClient *client) {

	bool received = false;

	if (!client->closed && (client->length < (KEEPALIVE_BUFFER_SIZE - 1))) {
		const ssize_t rc = recv (
			client->fd, client->buffer + client->length, KEEPALIVE_BUFFER_SIZE - 1 - client->length, 0
		);

		if (rc > 0) {
			client->length += (size_t) rc;
			client->buffer[client->length] = '\0';

			received = true;
		}

		else if (!rc) {
			client->closed = true;
		}
	}

	return received;

}

// the bytes of the body that a header announces, false when it does not say them
static bool keepalive_header_length (const char *header, const char *header_end, size_t *length) {

	bool found = false;

	const size_t name_length = strlen (keepalive_content_length);

	for (const char *line = header; !found && (line < header_end); line++) {
		if (!strncasecmp (line, keepalive_content_length, name_length)) {
			*length = (size_t) strtoul (line + name_length, NULL, 10);
			found = true;
		}
	}

	return found;

}

// takes the first bytes of what the client received as a response
static void keepalive_client_take (KeepAliveClient *client, const size_t size, char *response) {

	const size_t copied = (size < (KEEPALIVE_RESPONSE_SIZE - 1)) ? size : (KEEPALIVE_RESPONSE_SIZE - 1);

	(void) memcpy (response, client->buffer, copied);
	response[copied] = '\0';

	(void) memmove (client->buffer, client->buffer + size, client->length - size);
	client->length -= size;
	client->buffer[client->length] = '\0';

}

// reads one response of the connection: its header and the body that it announces,
// or everything until the cerver closes the connection when its header does not say its length
// what arrived behind it stays for the next call
// returns the bytes of the response, 0 when the connection ended without one
static size_t keepalive_client_response (KeepAliveClient *client, char *response) {

	size_t size = 0;

	const char *header_end = NULL;
	while (!(header_end = strstr (client->buffer, keepalive_header_end)) && keepalive_client_receive (client));

	if (header_end) {
		const size_t header_size = (size_t) (header_end - client->buffer) + strlen (keepalive_header_end);

		size_t body_size = 0;
		if (keepalive_header_length (client->buffer, header_end, &body_size)) {
			while ((client->length < (header_size + body_size)) && keepalive_client_receive (client));

			size = header_size + body_size;
			if (size > client->length) size = client->length;
		}

		else {
			while (keepalive_client_receive (client));

			size = client->length;
		}

		keepalive_client_take (client, size, response);
	}

	else {
		response[0] = '\0';
	}

	return size;

}

// reads the header of a response that has no body and no length: the 101 of an upgrade
// returns its bytes, 0 when the connection ended without it
static size_t keepalive_client_header (KeepAliveClient *client, char *response) {

	size_t size = 0;

	const char *header_end = NULL;
	while (!(header_end = strstr (client->buffer, keepalive_header_end)) && keepalive_client_receive (client));

	if (header_end) {
		size = (size_t) (header_end - client->buffer) + strlen (keepalive_header_end);

		keepalive_client_take (client, size, response);
	}

	else {
		response[0] = '\0';
	}

	return size;

}

static bool keepalive_starts_with (const char *response, const char *prefix) {

	return !strncmp (response, prefix, strlen (prefix));

}

static bool keepalive_ends_with (const char *response, const char *suffix) {

	const size_t length = strlen (response);
	const size_t suffix_length = strlen (suffix);

	return (length >= suffix_length) && !strcmp (response + length - suffix_length, suffix);

}

static bool keepalive_has (const char *response, const char *text) {

	return strstr (response, text) != NULL;

}

// the next response of the connection has a status and a body
static void keepalive_client_expect (
	KeepAliveClient *client, const char *status, const char *body, char *response
) {

	const size_t size = keepalive_client_response (client, response);

	const bool expected = size && keepalive_starts_with (response, status)
		&& (!body || keepalive_ends_with (response, body));

	if (!expected) {
		(void) fprintf (
			stderr, "expected %s with the body %s - received %zu bytes: %s\n",
			status, body ? body : "(any)", size, response
		);
	}

	test_check (expected, "the response is not the expected one");

}

// a response of a connection that is kept does not say anything about it to an HTTP/1.1 client
static void keepalive_client_expect_kept (
	KeepAliveClient *client, const char *status, const char *body
) {

	char response[KEEPALIVE_RESPONSE_SIZE];
	keepalive_client_expect (client, status, body, response);

	test_check (!keepalive_has (response, keepalive_connection_header), "a kept connection said something about it");

}

// a response of a connection that is closed after it says so
static void keepalive_client_expect_close (
	KeepAliveClient *client, const char *status, const char *body
) {

	char response[KEEPALIVE_RESPONSE_SIZE];
	keepalive_client_expect (client, status, body, response);

	test_check (keepalive_has (response, keepalive_connection_close), "a connection that is closed did not say it");

}

// waits for what the cerver does with the connection: true when it closed it in time
static bool keepalive_client_wait_closed (KeepAliveClient *client, const unsigned int milliseconds) {

	struct pollfd descriptor;
	(void) memset (&descriptor, 0, sizeof (struct pollfd));
	descriptor.fd = client->fd;
	descriptor.events = POLLIN;

	bool closed = client->closed;

	if (!closed && (poll (&descriptor, 1, (int) test_milliseconds (milliseconds)) > 0)) {
		char byte = 0;
		closed = (recv (client->fd, &byte, sizeof (byte), MSG_PEEK | MSG_DONTWAIT) == 0);
	}

	return closed;

}

// the cerver closed the connection, and it sent nothing more before that
static void keepalive_client_expect_closed (KeepAliveClient *client) {

	test_check (
		keepalive_client_wait_closed (client, KEEPALIVE_CLOSED_TIMEOUT),
		"the cerver did not close the connection"
	);

	test_check_unsigned_eq (client->length, 0, "the cerver sent more bytes before it closed the connection");

}

// the cerver keeps the connection open
static void keepalive_client_expect_open (KeepAliveClient *client) {

	test_check (
		!keepalive_client_wait_closed (client, KEEPALIVE_OPEN_TIMEOUT),
		"the cerver closed a connection that it had to keep"
	);

}

// a request of the items route with a value
static void keepalive_items_request (char *request, const unsigned int value) {

	(void) snprintf (
		request, KEEPALIVE_REQUEST_SIZE,
		"GET /keepalive/items/item-%u HTTP/1.1\r\nHost: localhost\r\n\r\n", value
	);

}

static void keepalive_items_body (char *body, const unsigned int value) {

	(void) snprintf (body, KEEPALIVE_REQUEST_SIZE, "item-%u", value);

}

#pragma endregion

#pragma region tests

// many requests in one connection: each one is answered, none says that the connection is closed,
// and the cerver counts every request after the first one as a reuse
static void test_keepalive_requests (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	for (unsigned int idx = 0; idx < KEEPALIVE_REQUESTS; idx++) {
		char request[KEEPALIVE_REQUEST_SIZE];
		keepalive_items_request (request, idx);

		char body[KEEPALIVE_REQUEST_SIZE];
		keepalive_items_body (body, idx);

		keepalive_client_send (&client, request);
		keepalive_client_expect_kept (&client, "HTTP/1.1 200", body);
	}

	keepalive_client_expect_open (&client);

	test_check_unsigned_eq (keepalive_server_reused (server), KEEPALIVE_REQUESTS - 1, NULL);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// requests that arrive together are answered one by one, in the order that they arrived,
// also the ones that cerver answers by itself (a path without a route)
static void test_keepalive_pipelined (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	char requests[KEEPALIVE_REQUEST_SIZE * KEEPALIVE_PIPELINED] = { 0 };
	for (unsigned int idx = 0; idx < KEEPALIVE_PIPELINED; idx++) {
		keepalive_items_request (requests + strlen (requests), idx);
	}

	keepalive_client_send (&client, requests);

	for (unsigned int idx = 0; idx < KEEPALIVE_PIPELINED; idx++) {
		char body[KEEPALIVE_REQUEST_SIZE];
		keepalive_items_body (body, idx);

		keepalive_client_expect_kept (&client, "HTTP/1.1 200", body);
	}

	keepalive_client_send (
		&client,
		"GET /keepalive/items/before HTTP/1.1\r\nHost: localhost\r\n\r\n"
		"GET /nothing/first HTTP/1.1\r\nHost: localhost\r\n\r\n"
		"GET /nothing/second HTTP/1.1\r\nHost: localhost\r\n\r\n"
		"GET /keepalive/items/after HTTP/1.1\r\nHost: localhost\r\n\r\n"
	);

	keepalive_client_expect_kept (&client, "HTTP/1.1 200", "before");
	keepalive_client_expect_kept (&client, "HTTP/1.1 404", NULL);
	keepalive_client_expect_kept (&client, "HTTP/1.1 404", NULL);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", "after");

	keepalive_client_expect_open (&client);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a request that arrives in pieces after the connection waited idle
static void test_keepalive_pieces (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_sleep ((long) test_milliseconds (KEEPALIVE_OPEN_TIMEOUT));

	keepalive_client_send (&client, "GET /keepalive/items/pie");
	keepalive_sleep ((long) test_milliseconds (KEEPALIVE_OPEN_TIMEOUT));
	keepalive_client_send (&client, "ces HTTP/1.1\r\nHost: local");
	keepalive_sleep ((long) test_milliseconds (KEEPALIVE_OPEN_TIMEOUT));
	keepalive_client_send (&client, "host\r\n\r\n");

	keepalive_client_expect_kept (&client, "HTTP/1.1 200", "pieces");

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// an HTTP/1.0 client only keeps its connection when it asks for it, and the response tells it
static void test_keepalive_old_clients (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	char response[KEEPALIVE_RESPONSE_SIZE];

	keepalive_client_send (&client, "GET /keepalive/ok HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
	keepalive_client_expect (&client, "HTTP/1.1 200", NULL, response);
	test_check (keepalive_has (response, keepalive_connection_keep), "the response did not say keep-alive");

	keepalive_client_send (&client, "GET /keepalive/ok HTTP/1.0\r\n\r\n");
	keepalive_client_expect_close (&client, "HTTP/1.1 200", NULL);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a client that asks to close its connection
static void test_keepalive_client_close (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_send (&client, keepalive_ok_close_request);
	keepalive_client_expect_close (&client, "HTTP/1.1 200", NULL);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a connection handles the requests that its cerver allows, the last one says that it is closed
static void test_keepalive_max_requests (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.requests = KEEPALIVE_MAX_REQUESTS;

	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	for (unsigned int idx = 1; idx < KEEPALIVE_MAX_REQUESTS; idx++) {
		keepalive_client_send (&client, keepalive_ok_request);
		keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);
	}

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_close (&client, "HTTP/1.1 200", NULL);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a connection that does not send its next request in time is closed, without a response
static void test_keepalive_idle_timeout (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.timeout = test_milliseconds (KEEPALIVE_SHORT_TIMEOUT);

	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	// a client that leaves while it is idle is not a request that failed
	KeepAliveClient gone;
	keepalive_client_start (&gone, server);

	keepalive_client_send (&gone, keepalive_ok_request);
	keepalive_client_expect_kept (&gone, "HTTP/1.1 200", NULL);

	keepalive_client_end (&gone);

	const CerverHttp *http_cerver = server->http_cerver;
	const size_t incompleted = __atomic_load_n (&http_cerver->n_incompleted_requests, __ATOMIC_RELAXED);

	keepalive_server_stop (server);

	test_check_unsigned_eq (incompleted, 0, NULL);

}

// a response without its length ends with its connection, that can not take another request,
// and it says so (its client reads until the close)
static void test_keepalive_unframed (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_send (
		&client,
		"GET /keepalive/unframed HTTP/1.1\r\nHost: localhost\r\n\r\n"
		"GET /keepalive/ok HTTP/1.1\r\nHost: localhost\r\n\r\n"
	);

	char response[KEEPALIVE_RESPONSE_SIZE];
	keepalive_client_expect (&client, "HTTP/1.1 200", keepalive_unframed_body, response);

	test_check (client.closed, "the connection of a response without its length was not closed");
	test_check (
		keepalive_has (response, keepalive_connection_close), "a response without its length did not say the close"
	);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a handler that does not answer gets a 500 from cerver, that has an end: the connection is kept
static void test_keepalive_unanswered (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, "GET /keepalive/silent HTTP/1.1\r\nHost: localhost\r\n\r\n");
	keepalive_client_expect_kept (&client, "HTTP/1.1 500", NULL);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// what a request had (its body, its headers) is not in the next one of its connection
static void test_keepalive_state (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (
		&client,
		"POST /keepalive/state HTTP/1.1\r\nHost: localhost\r\nAccept: text/first\r\nContent-Length: 10\r\n\r\n"
		"first-body"
	);

	keepalive_client_expect_kept (&client, "HTTP/1.1 200", "first-body|text/first");

	keepalive_client_send (&client, "GET /keepalive/state HTTP/1.1\r\nHost: localhost\r\n\r\n");
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", "none|none");

	keepalive_client_send (
		&client,
		"POST /keepalive/state HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nlast"
	);

	keepalive_client_expect_kept (&client, "HTTP/1.1 200", "last|none");

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a request that cerver rejects closes its connection, and its response says it:
// one that can not be parsed, and one that is not authorized before its body arrives
static void test_keepalive_rejected (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_send (&client, "GET /keepalive/ok HTTP/1.1\r\nBad Header\r\n\r\n");
	keepalive_client_expect_close (&client, "HTTP/1.1 400", NULL);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	KeepAliveClient denied;
	keepalive_client_start (&denied, server);

	keepalive_client_send (
		&denied,
		"POST /keepalive/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\nContent-Length: 4\r\n\r\nbody"
	);

	keepalive_client_expect_kept (&denied, "HTTP/1.1 200", "body|none");

	// the body of this request never arrives
	keepalive_client_send (
		&denied,
		"POST /keepalive/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: wrong\r\nContent-Length: 4\r\n\r\n"
	);

	keepalive_client_expect_close (&denied, "HTTP/1.1 401", NULL);

	keepalive_client_end (&denied);

	// a complete request that is not authorized: an event loop authorizes a request without a body with its
	// handler, when the cerver had already decided to keep its connection
	KeepAliveClient refused;
	keepalive_client_start (&refused, server);

	keepalive_client_send (&refused, keepalive_ok_request);
	keepalive_client_expect_kept (&refused, "HTTP/1.1 200", NULL);

	keepalive_client_send (
		&refused,
		"POST /keepalive/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: wrong\r\nContent-Length: 0\r\n\r\n"
	);

	keepalive_client_expect_close (&refused, "HTTP/1.1 401", NULL);
	keepalive_client_expect_closed (&refused);

	keepalive_client_end (&refused);

	keepalive_server_stop (server);

}

// a response with its own "Connection: close" closes its connection
static void test_keepalive_own_close (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, "GET /keepalive/closing HTTP/1.1\r\nHost: localhost\r\n\r\n");
	keepalive_client_expect_close (&client, "HTTP/1.1 200", keepalive_closing_body);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// a connection that was kept can become a WebSocket: its upgrade is answered with 101
// and the connection leaves the ones of HTTP, the cerver stops with it open
static void test_keepalive_upgrade (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_send (
		&client,
		"GET /keepalive/socket HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"
	);

	char response[KEEPALIVE_RESPONSE_SIZE];
	test_check (keepalive_client_header (&client, response) > 0, "the upgrade was not answered");
	test_check (keepalive_starts_with (response, "HTTP/1.1 101"), "the upgrade was not accepted");
	test_check (keepalive_has (response, "Sec-WebSocket-Accept:"), "the 101 has no accept");

	keepalive_client_expect_open (&client);

	keepalive_server_stop (server);

	keepalive_client_end (&client);

}

// a cerver that does not keep its connections closes each one after its response, and says it
static void test_keepalive_disabled (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.configure = KEEPALIVE_CONFIGURE_DISABLED;

	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_close (&client, "HTTP/1.1 200", NULL);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	test_check_unsigned_eq (keepalive_server_reused (server), 0, NULL);

	keepalive_server_stop (server);

}

// a cerver that stops closes its idle connections at once: it does not wait the time that it keeps them
static void test_keepalive_drain_idle (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient first;
	keepalive_client_start (&first, server);
	keepalive_client_send (&first, keepalive_ok_request);
	keepalive_client_expect_kept (&first, "HTTP/1.1 200", NULL);

	KeepAliveClient second;
	keepalive_client_start (&second, server);
	keepalive_client_send (&second, keepalive_ok_request);
	keepalive_client_expect_kept (&second, "HTTP/1.1 200", NULL);

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	keepalive_client_expect_closed (&first);
	keepalive_client_expect_closed (&second);

	keepalive_client_end (&first);
	keepalive_client_end (&second);

	keepalive_server_join (server);

	keepalive_server_end (server);

}

// a request that is being handled when the cerver stops is answered completely,
// and its connection is closed after it
static void test_keepalive_drain_request (void) {

	keepalive_gate_reset ();

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_send (&client, "GET /keepalive/block HTTP/1.1\r\nHost: localhost\r\n\r\n");
	keepalive_gate_wait_for (1);

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	keepalive_gate_open ();

	char response[KEEPALIVE_RESPONSE_SIZE];
	keepalive_client_expect (&client, "HTTP/1.1 200", NULL, response);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	keepalive_server_join (server);

	keepalive_server_end (server);

}

// a request that is complete after the cerver was asked to stop is answered, and its response says that its
// connection is closed: a cerver that stops keeps no connection for a request that it will not take
static void test_keepalive_drain_partial (void) {

	const KeepAliveOptions options = keepalive_options_default ();
	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	// the first bytes of its next request reach the cerver before it is asked to stop
	keepalive_client_send (&client, "GET /keepalive/ok HTTP/1.1\r\nHost: local");
	keepalive_sleep ((long) test_milliseconds (KEEPALIVE_OPEN_TIMEOUT));

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	keepalive_client_send (&client, "host\r\n\r\n");

	keepalive_client_expect_close (&client, "HTTP/1.1 200", NULL);
	keepalive_client_expect_closed (&client);

	keepalive_client_end (&client);

	keepalive_server_join (server);

	keepalive_server_end (server);

}

// a cerver that is asked to keep its connections says that it was configured,
// and the keep alive of a new cerver is not configured and has its defaults
static void test_keepalive_configured (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.configure = KEEPALIVE_CONFIGURE_ENABLED;

	KeepAliveServer *server = keepalive_server_start (&options);
	test_check_bool_eq (server->http_cerver->keep_alive_configured, true, NULL);
	keepalive_server_stop (server);

	Cerver *cerver = cerver_create_web ("keepalive-default", (u16) keepalive_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	const CerverHttp *http_cerver = cerver_http_get (cerver);
	test_check_bool_eq (http_cerver->keep_alive, false, NULL);
	test_check_bool_eq (http_cerver->keep_alive_configured, false, NULL);
	test_check_unsigned_eq (http_cerver->keep_alive_timeout, CERVER_HTTP_DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS, NULL);
	test_check_unsigned_eq (http_cerver->keep_alive_requests, CERVER_HTTP_DEFAULT_KEEP_ALIVE_REQUESTS, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the setters: a timeout of 0 keeps the default, and 0 requests disables their limit
static void test_keepalive_setters (void) {

	Cerver *cerver = cerver_create_web ("keepalive-setters", (u16) keepalive_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	CerverHttp *http_cerver = cerver_http_get (cerver);

	cerver_http_set_keep_alive (http_cerver, true);
	test_check_bool_eq (http_cerver->keep_alive, true, NULL);
	test_check_bool_eq (http_cerver->keep_alive_configured, true, NULL);

	cerver_http_set_keep_alive (http_cerver, false);
	test_check_bool_eq (http_cerver->keep_alive, false, NULL);
	test_check_bool_eq (http_cerver->keep_alive_configured, true, NULL);

	cerver_http_set_keep_alive_timeout (http_cerver, KEEPALIVE_DRAIN_TIMEOUT);
	test_check_unsigned_eq (
		http_cerver->keep_alive_timeout, KEEPALIVE_DRAIN_TIMEOUT * KEEPALIVE_MILLISECONDS_PER_SECOND, NULL
	);

	cerver_http_set_keep_alive_timeout_milliseconds (http_cerver, KEEPALIVE_SHORT_TIMEOUT);
	test_check_unsigned_eq (http_cerver->keep_alive_timeout, KEEPALIVE_SHORT_TIMEOUT, NULL);

	cerver_http_set_keep_alive_timeout (http_cerver, 0);
	test_check_unsigned_eq (http_cerver->keep_alive_timeout, CERVER_HTTP_DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS, NULL);

	cerver_http_set_keep_alive_requests (http_cerver, KEEPALIVE_REQUESTS);
	test_check_unsigned_eq (http_cerver->keep_alive_requests, KEEPALIVE_REQUESTS, NULL);

	cerver_http_set_keep_alive_requests (http_cerver, 0);
	test_check_unsigned_eq (http_cerver->keep_alive_requests, 0, NULL);

	// nothing is done without a cerver
	cerver_http_set_keep_alive (NULL, true);
	cerver_http_set_keep_alive_timeout (NULL, KEEPALIVE_DRAIN_TIMEOUT);
	cerver_http_set_keep_alive_timeout_milliseconds (NULL, KEEPALIVE_SHORT_TIMEOUT);
	cerver_http_set_keep_alive_requests (NULL, KEEPALIVE_REQUESTS);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// a cerver with event loops keeps its connections without being asked to
static void test_keepalive_events_default (void) {

	const KeepAliveOptions options = keepalive_options_default ();

	KeepAliveServer *server = keepalive_server_start (&options);

	test_check_bool_eq (server->http_cerver->keep_alive_configured, false, NULL);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// with event loops, each request of a connection arrives after the connection waited idle for it: the loop reads
// it and a thread of the pool handles it, and a client that leaves while its connection is idle ends it
static void test_keepalive_events_idle (void) {

	const KeepAliveOptions options = keepalive_options_default ();

	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);

	for (unsigned int idx = 0; idx < KEEPALIVE_REQUESTS; idx++) {
		char request[KEEPALIVE_REQUEST_SIZE];
		keepalive_items_request (request, idx);

		char body[KEEPALIVE_REQUEST_SIZE];
		keepalive_items_body (body, idx);

		keepalive_client_send (&client, request);
		keepalive_client_expect_kept (&client, "HTTP/1.1 200", body);

		// the connection is idle before the next request arrives
		keepalive_server_wait_idle (server, 1);
	}

	test_check_unsigned_eq (keepalive_server_reused (server), KEEPALIVE_REQUESTS - 1, NULL);

	keepalive_client_end (&client);

	keepalive_server_wait_connections (server, 0);

	keepalive_server_stop (server);

}

// the jobs that wait in the queue of a thpool, for a test that waits for them
static void keepalive_server_wait_queued (const KeepAliveServer *server, const unsigned int expected) {

	Thpool *thpool = server->cerver->thpool;

	const double deadline = test_wait_now () + (double) (KEEPALIVE_STOP_TIMEOUT * test_time_scale ());
	while ((thpool_get_queued (thpool) != expected) && (test_wait_now () < deadline)) {
		struct timespec interval = { 0, KEEPALIVE_CONNECT_INTERVAL };
		(void) nanosleep (&interval, NULL);
	}

	test_check_unsigned_eq (thpool_get_queued (thpool), expected, NULL);

}

// with every thread of the pool busy, the next request of an idle connection is read by its loop and waits in the
// queue of the pool like any other; once the thread is free again it is answered, and the connection waits idle
// for the one after it
static void test_keepalive_events_pool_busy (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.threads = 1;

	keepalive_gate_reset ();

	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient idle;
	keepalive_client_start (&idle, server);
	keepalive_client_send (&idle, keepalive_ok_request);
	keepalive_client_expect_kept (&idle, "HTTP/1.1 200", NULL);

	keepalive_server_wait_idle (server, 1);

	// the only thread works for another client
	KeepAliveClient busy;
	keepalive_client_start (&busy, server);
	keepalive_client_send (&busy, "GET /keepalive/block HTTP/1.1\r\nHost: localhost\r\n\r\n");
	keepalive_gate_wait_for (1);

	keepalive_client_send (&idle, keepalive_ok_request);
	keepalive_server_wait_queued (server, 1);

	keepalive_gate_open ();

	char response[KEEPALIVE_RESPONSE_SIZE];
	keepalive_client_expect (&busy, "HTTP/1.1 200", NULL, response);
	keepalive_client_expect_kept (&idle, "HTTP/1.1 200", NULL);

	keepalive_server_wait_idle (server, KEEPALIVE_BUSY_IDLE);

	keepalive_client_send (&idle, keepalive_ok_request);
	keepalive_client_expect_kept (&idle, "HTTP/1.1 200", NULL);

	keepalive_client_end (&idle);
	keepalive_client_end (&busy);

	keepalive_server_stop (server);

}

// the next request of an idle connection that arrives while its loop rejects its requests is answered with 503
static void test_keepalive_events_rejecting (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.threads = 1;
	options.io_threads = KEEPALIVE_SINGLE_LOOP;

	KeepAliveServer *server = keepalive_server_start (&options);

	KeepAliveClient client;
	keepalive_client_start (&client, server);
	keepalive_client_send (&client, keepalive_ok_request);
	keepalive_client_expect_kept (&client, "HTTP/1.1 200", NULL);

	keepalive_server_wait_idle (server, 1);

	EventLoop *loop = server->cerver->event_loops->loops[0];
	__atomic_store_n (&loop->rejecting, true, __ATOMIC_RELEASE);

	keepalive_client_send (&client, keepalive_ok_request);

	char response[KEEPALIVE_RESPONSE_SIZE];
	keepalive_client_expect (&client, "HTTP/1.1 503", NULL, response);

	__atomic_store_n (&loop->rejecting, false, __ATOMIC_RELEASE);

	keepalive_client_end (&client);

	keepalive_server_stop (server);

}

// at the limit of connections of the cerver, the connection that has been idle the longest
// gives its place to a new client; the other idle ones are kept
static void test_keepalive_events_limit (void) {

	KeepAliveOptions options = keepalive_options_default ();
	options.max_connections = KEEPALIVE_LIMIT_CONNECTIONS;
	options.io_threads = KEEPALIVE_SINGLE_LOOP;

	KeepAliveServer *server = keepalive_server_start (&options);

	// the connection that found the cerver listening has ended
	keepalive_server_wait_connections (server, 0);

	// each one is idle before the next one starts: the oldest idle connection is the first one that became idle,
	// a moment after its response
	KeepAliveClient oldest;
	keepalive_client_start (&oldest, server);
	keepalive_client_send (&oldest, keepalive_ok_request);
	keepalive_client_expect_kept (&oldest, "HTTP/1.1 200", NULL);

	keepalive_server_wait_idle (server, 1);

	KeepAliveClient newer;
	keepalive_client_start (&newer, server);
	keepalive_client_send (&newer, keepalive_ok_request);
	keepalive_client_expect_kept (&newer, "HTTP/1.1 200", NULL);

	keepalive_server_wait_idle (server, KEEPALIVE_LIMIT_CONNECTIONS);

	keepalive_server_wait_connections (server, KEEPALIVE_LIMIT_CONNECTIONS);

	KeepAliveClient last;
	keepalive_client_start (&last, server);
	keepalive_client_send (&last, keepalive_ok_request);
	keepalive_client_expect_kept (&last, "HTTP/1.1 200", NULL);

	keepalive_client_expect_closed (&oldest);

	keepalive_client_send (&newer, keepalive_ok_request);
	keepalive_client_expect_kept (&newer, "HTTP/1.1 200", NULL);

	test_check_unsigned_eq (event_loop_group_connections (server->cerver), KEEPALIVE_LIMIT_CONNECTIONS, NULL);

	keepalive_client_end (&oldest);
	keepalive_client_end (&newer);
	keepalive_client_end (&last);

	keepalive_server_stop (server);

}

#pragma endregion

static void keepalive_tests_connections (void) {

	// a hang fails the suite instead of blocking it forever
	(void) alarm (KEEPALIVE_ALARM * test_time_scale ());

	test_run (test_keepalive_requests);
	test_run (test_keepalive_pipelined);
	test_run (test_keepalive_pieces);
	test_run (test_keepalive_old_clients);
	test_run (test_keepalive_client_close);
	test_run (test_keepalive_max_requests);
	test_run (test_keepalive_idle_timeout);
	test_run (test_keepalive_unframed);
	test_run (test_keepalive_unanswered);
	test_run (test_keepalive_state);
	test_run (test_keepalive_rejected);
	test_run (test_keepalive_own_close);
	test_run (test_keepalive_upgrade);
	test_run (test_keepalive_disabled);
	test_run (test_keepalive_drain_idle);
	test_run (test_keepalive_drain_request);
	test_run (test_keepalive_drain_partial);

	(void) alarm (0);

}

void http_tests_keepalive (void) {

	(void) printf ("Testing HTTP keep alive...\n");

	(void) alarm (KEEPALIVE_ALARM * test_time_scale ());

	test_run (test_keepalive_setters);
	test_run (test_keepalive_configured);
	test_run (test_keepalive_events_default);
	test_run (test_keepalive_events_idle);
	test_run (test_keepalive_events_pool_busy);
	test_run (test_keepalive_events_rejecting);
	test_run (test_keepalive_events_limit);

	(void) alarm (0);

	keepalive_tests_connections ();

	(void) printf ("Done!\n");

}
