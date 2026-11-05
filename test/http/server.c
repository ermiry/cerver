#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <pthread.h>

#include <cerver/cerver.h>
#include <cerver/network.h>

#include <cerver/threads/thpool.h>

#include <cerver/http/http.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>

#include "../test.h"

#include "http.h"

// runs a real web cerver in a thread and talks to it with raw sockets,
// to test the receive loop, the overload policies, the drain and the shutdown

// bugs that this suite pinned:
// - a client that sent a byte every few secs kept a thread forever (slowloris)
// - a client that never read its response blocked a thread in send () forever
// - a full thpool created detached threads without any limit
// - cerver_shutdown () did not wake up accept (), and an interrupted accept ()
//   stopped the cerver without draining its connections
// - cerver_teardown () released the routes while the threads still used them
// - the queued connections were freed without closing their fds at teardown
// - sock_set_blocking () was inverted and SO_REUSEADDR also set SO_REUSEPORT
// coverage holes:
// - a drain that times out keeps the cerver memory on purpose (see thpool.c)

#define SERVER_TEST_PORT_BASE				(1 << 13)
#define SERVER_TEST_ADDRESS					"127.0.0.1"

#define SERVER_TEST_RESPONSE_SIZE			(1 << 12)

#define SERVER_TEST_CONNECT_RETRIES			(1 << 8)
// 2^22 ns
#define SERVER_TEST_CONNECT_INTERVAL		4194304L
// 2^25 ns
#define SERVER_TEST_SHORT_SLEEP				33554432L
// 2^28 ns
#define SERVER_TEST_SLOW_SLEEP				268435456L

#define SERVER_TEST_RECEIVE_TIMEOUT			16
#define SERVER_TEST_POLL_TIMEOUT			256

#define SERVER_TEST_HEADERS_TIMEOUT			1
#define SERVER_TEST_REQUEST_TIMEOUT			2
#define SERVER_TEST_SEND_TIMEOUT			1
#define SERVER_TEST_DRAIN_TIMEOUT			8
#define SERVER_TEST_SLOW_BODY_SIZE			8

// a response that does not fit in the socket buffers
#define SERVER_TEST_BIG_RESPONSE_SIZE		(1 << 24)

#define SERVER_TEST_FILES_LIMIT				(1 << 5)
#define SERVER_TEST_FILES_CONNECTIONS		(1 << 6)

#define SERVER_TEST_PRELOAD_VARIABLE		"LD_PRELOAD"
#define SERVER_TEST_VALGRIND_PRELOAD		"vgpreload"

// generous, so the suite also passes under valgrind
#define SERVER_TEST_JOIN_TIMEOUT			32

#define SERVER_TEST_ALARM					256

static const char *ok_request = {
	"GET /ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *block_request = {
	"GET /block HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *big_request = {
	"GET /big HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *slow_headers_request = {
	"GET /ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"X-Slow: abcdefghijklmnopqrstuvwxyz\r\n"
	"\r\n"
};

static const char *slow_body_headers = {
	"POST /echo HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 8\r\n"
	"\r\n"
};

static const char *slow_body = { "12345678" };

static const char *status_ok = { "HTTP/1.1 200" };
static const char *status_request_timeout = { "HTTP/1.1 408" };
static const char *status_service_unavailable = { "HTTP/1.1 503" };
static const char *retry_after_header = { "Retry-After: " HTTP_RESPONSE_RETRY_AFTER_SECONDS };
static const char *content_length_header = { "Content-Length: " };
static const char *headers_end = { "\r\n\r\n" };

static unsigned int server_test_port = SERVER_TEST_PORT_BASE;

#pragma region gate

// keeps the /block requests working until it is opened
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static bool gate_open = false;
static unsigned int gate_waiting = 0;

static void server_test_gate_reset (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_open = false;
	gate_waiting = 0;
	(void) pthread_mutex_unlock (&gate_mutex);

}

static void server_test_gate_open (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_open = true;
	(void) pthread_cond_broadcast (&gate_cond);
	(void) pthread_mutex_unlock (&gate_mutex);

}

static unsigned int server_test_gate_waiting (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	unsigned int waiting = gate_waiting;
	(void) pthread_mutex_unlock (&gate_mutex);

	return waiting;

}

static void server_test_sleep (const long nanoseconds) {

	struct timespec interval = { 0, nanoseconds };
	(void) nanosleep (&interval, NULL);

}

// waits until the /block requests are working
static void server_test_gate_wait_for (const unsigned int waiting) {

	while (server_test_gate_waiting () < waiting) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

}

#pragma endregion

#pragma region routes

// GET /ok
static void server_test_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// GET /block
static void server_test_block_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) pthread_mutex_lock (&gate_mutex);
	gate_waiting += 1;
	while (!gate_open) (void) pthread_cond_wait (&gate_cond, &gate_mutex);
	(void) pthread_mutex_unlock (&gate_mutex);

	(void) http_response_send (oki_doki, http_receive);

}

static char *big_response = NULL;
static unsigned int big_sent = 0;
static unsigned int big_failed = 0;

// GET /big, the client never reads it
static void server_test_big_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	if (http_response_render_text (
		http_receive, HTTP_STATUS_OK, big_response, SERVER_TEST_BIG_RESPONSE_SIZE
	)) {
		(void) __atomic_add_fetch (&big_failed, 1, __ATOMIC_SEQ_CST);
	}

	else {
		(void) __atomic_add_fetch (&big_sent, 1, __ATOMIC_SEQ_CST);
	}

}

// POST /echo
static void server_test_echo_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const String *body = http_request_get_body (request);

	if (body && (body->len == SERVER_TEST_SLOW_BODY_SIZE)) {
		(void) http_response_send (oki_doki, http_receive);
	}

	else {
		(void) http_response_send (bad_request_error, http_receive);
	}

}

#pragma endregion

#pragma region server

typedef struct ServerTestOptions {

	u16 threads;
	unsigned int queue_size;

	CerverOverloadPolicy overload_policy;

	CerverDrainPolicy drain_policy;
	u32 drain_timeout;

	u32 send_timeout;

	unsigned int headers_timeout;
	unsigned int request_timeout;

} ServerTestOptions;

typedef struct ServerTest {

	Cerver *cerver;
	u16 port;

	pthread_t thread;
	u8 start_result;
	bool started;

} ServerTest;

static ServerTestOptions server_test_options_default (void) {

	ServerTestOptions options = {
		1, 1,
		CERVER_OVERLOAD_POLICY_WAIT,
		CERVER_DRAIN_POLICY_RUN, SERVER_TEST_DRAIN_TIMEOUT,
		CERVER_DEFAULT_SEND_TIMEOUT,
		HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT, HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT
	};

	return options;

}

static void *server_test_thread (void *server_ptr) {

	ServerTest *server = (ServerTest *) server_ptr;

	server->start_result = cerver_start (server->cerver);

	return NULL;

}

static void server_test_routes (HttpCerver *http_cerver) {

	HttpRoute *ok_route = http_route_create (REQUEST_METHOD_GET, "ok", server_test_ok_handler);
	http_cerver_route_register (http_cerver, ok_route);

	HttpRoute *block_route = http_route_create (REQUEST_METHOD_GET, "block", server_test_block_handler);
	http_cerver_route_register (http_cerver, block_route);

	HttpRoute *big_route = http_route_create (REQUEST_METHOD_GET, "big", server_test_big_handler);
	http_cerver_route_register (http_cerver, big_route);

	HttpRoute *echo_route = http_route_create (REQUEST_METHOD_POST, "echo", server_test_echo_handler);
	http_cerver_route_register (http_cerver, echo_route);

}

static Cerver *server_test_cerver_create (
	const u16 port, const ServerTestOptions *options
) {

	Cerver *cerver = cerver_create_web ("server-test", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, options->threads);
	cerver_set_thpool_queue_size (cerver, options->queue_size);
	cerver_set_overload_policy (cerver, options->overload_policy);
	cerver_set_drain_policy (cerver, options->drain_policy);
	cerver_set_drain_timeout (cerver, options->drain_timeout);
	cerver_set_send_timeout (cerver, options->send_timeout);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	http_cerver_set_headers_timeout (http_cerver, options->headers_timeout);
	http_cerver_set_request_timeout (http_cerver, options->request_timeout);

	server_test_routes (http_cerver);

	return cerver;

}

static int server_test_connect_once (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, SERVER_TEST_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static int server_test_connect (const u16 port) {

	int fd = -1;
	for (unsigned int i = 0; (i < SERVER_TEST_CONNECT_RETRIES) && (fd < 0); i++) {
		fd = server_test_connect_once (port);
		if (fd < 0) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	test_check (fd > -1, "failed to connect to the test cerver");

	(void) sock_set_timeout (fd, SERVER_TEST_RECEIVE_TIMEOUT);

	return fd;

}

static void server_test_send (const int fd, const char *data);

static size_t server_test_read (const int fd, char *response);

static ServerTest *server_test_start (const ServerTestOptions *options) {

	ServerTest *server = (ServerTest *) calloc (1, sizeof (ServerTest));
	test_check_ptr (server);

	server->port = (u16) server_test_port++;
	server->cerver = server_test_cerver_create (server->port, options);

	test_check_int_eq (pthread_create (&server->thread, NULL, server_test_thread, server), 0, NULL);
	server->started = true;

	// waits until the cerver is listening and has handled a complete request
	int fd = server_test_connect (server->port);
	server_test_send (fd, ok_request);
	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);
	(void) close (fd);

	return server;

}

// cerver_start () must return in time after it was asked to stop
static void server_test_join (ServerTest *server) {

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += SERVER_TEST_JOIN_TIMEOUT;

	const int result = pthread_timedjoin_np (server->thread, NULL, &deadline);
	if (result) (void) fprintf (stderr, "the cerver in port %u did not stop\n", (unsigned int) server->port);
	test_check_int_eq (result, 0, NULL);
	server->started = false;

}

static void server_test_stop (ServerTest *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	server_test_join (server);

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

#pragma endregion

#pragma region client

static void server_test_send (const int fd, const char *data) {

	const size_t length = strlen (data);
	test_check (send (fd, data, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

}

// reads the whole response until the cerver closes the connection
static size_t server_test_read (const int fd, char *response) {

	size_t total = 0;
	ssize_t received = 0;
	while (
		(total < (SERVER_TEST_RESPONSE_SIZE - 1))
		&& ((received = recv (fd, response + total, SERVER_TEST_RESPONSE_SIZE - 1 - total, 0)) > 0)
	) {
		total += (size_t) received;
	}

	response[total] = '\0';

	return total;

}

static bool server_test_starts_with (const char *response, const char *prefix) {

	return !strncmp (response, prefix, strlen (prefix));

}

static void server_test_expect (const int fd, const char *status) {

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);

	if (!server_test_starts_with (response, status)) {
		(void) fprintf (stderr, "expected %s - received: %s\n", status, response);
	}

	test_check (server_test_starts_with (response, status), "unexpected response status");

}

// sends a request in a new connection and checks its response
static void server_test_request (const u16 port, const char *request, const char *status) {

	int fd = server_test_connect (port);
	server_test_send (fd, request);
	server_test_expect (fd, status);
	(void) close (fd);

}

// returns true if the cerver has sent something to the connection
static bool server_test_has_response (const int fd) {

	struct pollfd poll_fd = { fd, POLLIN, 0 };

	return (poll (&poll_fd, 1, SERVER_TEST_POLL_TIMEOUT) > 0);

}

static double server_test_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / 1e9);

}

#pragma endregion

#pragma region deadlines

static void test_server_ok (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

// a client that sends its headers one byte at a time never finishes them in time
static void test_server_slowloris (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	const double start = server_test_now ();

	int fd = server_test_connect (server->port);

	// every byte arrives before the recv () timeout, but the headers deadline expires
	const size_t length = strlen (slow_headers_request);
	for (size_t i = 0; (i < length) && !server_test_has_response (fd); i++) {
		(void) send (fd, slow_headers_request + i, 1, MSG_NOSIGNAL);
		server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	}

	server_test_expect (fd, status_request_timeout);
	(void) close (fd);

	// answered with the headers deadline, not the recv () timeout
	test_check ((server_test_now () - start) < CERVER_DEFAULT_SOCKET_RECV_TIMEOUT, "the slowloris was not stopped in time");

	// the thread is free for the next request
	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

// a connection that never sends anything is closed without a response
static void test_server_idle (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);

	char response[SERVER_TEST_RESPONSE_SIZE];
	test_check (server_test_read (fd, response) == 0, "an idle connection received a response");
	(void) close (fd);

	server_test_stop (server);

}

static void server_test_send_slow_body (const int fd) {

	server_test_send (fd, slow_body_headers);

	const size_t length = strlen (slow_body);
	for (size_t i = 0; (i < length) && !server_test_has_response (fd); i++) {
		server_test_sleep (SERVER_TEST_SLOW_SLEEP);
		(void) send (fd, slow_body + i, 1, MSG_NOSIGNAL);
	}

}

// the headers deadline does not apply to a body that keeps making progress
static void test_server_slow_body (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);
	server_test_send_slow_body (fd);
	server_test_expect (fd, status_ok);
	(void) close (fd);

	server_test_stop (server);

}

// the total deadline stops a body that is too slow
static void test_server_request_timeout (void) {

	ServerTestOptions options = server_test_options_default ();
	options.request_timeout = SERVER_TEST_REQUEST_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);
	server_test_send_slow_body (fd);
	server_test_expect (fd, status_request_timeout);
	(void) close (fd);

	HttpCerver *http_cerver = (HttpCerver *) server->cerver->cerver_data;
	(void) pthread_mutex_lock (http_cerver->mutex);
	const size_t n_timeout_requests = http_cerver->n_timeout_requests;
	(void) pthread_mutex_unlock (http_cerver->mutex);
	test_check (n_timeout_requests == 1, NULL);

	server_test_stop (server);

}

// a client that never reads its response does not block the thread forever
static void test_server_send_timeout (void) {

	ServerTestOptions options = server_test_options_default ();
	options.send_timeout = SERVER_TEST_SEND_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	big_response = (char *) malloc (SERVER_TEST_BIG_RESPONSE_SIZE);
	test_check_ptr (big_response);
	(void) memset (big_response, 'a', SERVER_TEST_BIG_RESPONSE_SIZE);

	int slow_reader = server_test_connect (server->port);
	server_test_send (slow_reader, big_request);

	// the only thread is free again after the send timeout
	server_test_request (server->port, ok_request, status_ok);

	test_check (__atomic_load_n (&big_failed, __ATOMIC_SEQ_CST) == 1, "the big response did not fail");

	(void) close (slow_reader);

	server_test_stop (server);

	free (big_response);
	big_response = NULL;

}

#pragma endregion

#pragma region overload

static void server_test_check_unavailable (const char *response) {

	test_check (server_test_starts_with (response, status_service_unavailable), "the response is not a 503");
	test_check_ptr (strstr (response, retry_after_header));

	// the Content-Length header matches the body
	const char *length_header = strstr (response, content_length_header);
	const char *body = strstr (response, headers_end);
	test_check_ptr (length_header);
	test_check_ptr (body);

	const size_t length = (size_t) strtoul (length_header + strlen (content_length_header), NULL, 10);
	test_check (strlen (body + strlen (headers_end)) == length, "the 503 Content-Length is wrong");

}

// the probe connection of server_test_start () must be done,
// otherwise it can take the only queue slot
static void server_test_wait_idle (Cerver *cerver) {

	while (
		thpool_get_queued (cerver->thpool)
		|| thpool_get_num_threads_working (cerver->thpool)
	) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

}

// fills the only thread and the only queue slot
static void server_test_fill (Cerver *cerver, const u16 port, int *working, int *queued) {

	server_test_wait_idle (cerver);

	*working = server_test_connect (port);
	server_test_send (*working, block_request);
	server_test_gate_wait_for (1);

	*queued = server_test_connect (port);
	server_test_send (*queued, block_request);

}

static void test_server_overload_reject (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.overload_policy = CERVER_OVERLOAD_POLICY_REJECT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);

	// waits until the second connection is queued
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int rejected = server_test_connect (server->port);
	server_test_send (rejected, ok_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (rejected, response);
	server_test_check_unavailable (response);
	(void) close (rejected);

	test_check (__atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST) == 1, NULL);

	// the connections that were accepted are still handled
	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	(void) close (working);
	(void) close (queued);

	server_test_stop (server);

}

// the new connection waits in the listen backlog until there is space
static void test_server_overload_wait (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);

	int waiting = server_test_connect (server->port);
	server_test_send (waiting, ok_request);

	test_check_false (server_test_has_response (waiting));
	test_check_unsigned_eq (thpool_get_num_threads_alive (server->cerver->thpool), 1, NULL);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	server_test_expect (waiting, status_ok);
	(void) close (working);
	(void) close (queued);
	(void) close (waiting);

	test_check (__atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST) == 0, NULL);

	server_test_stop (server);

}

#pragma endregion

#pragma region drain

typedef struct ServerTestTeardown {

	ServerTest *server;
	bool done;

} ServerTestTeardown;

static void *server_test_teardown_thread (void *teardown_ptr) {

	ServerTestTeardown *teardown = (ServerTestTeardown *) teardown_ptr;

	server_test_stop (teardown->server);
	__atomic_store_n (&teardown->done, true, __ATOMIC_SEQ_CST);

	return NULL;

}

static void server_test_check_refused (const u16 port) {

	int fd = server_test_connect_once (port);
	test_check (fd < 0, "the cerver still accepts connections");

}

// the connections in progress and the queued ones are completed
static void test_server_drain_run (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);
	const u16 port = server->port;

	int working = -1, queued = -1;
	server_test_fill (server->cerver, port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	ServerTestTeardown teardown = { server, false };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, server_test_teardown_thread, &teardown), 0, NULL);

	// the teardown waits for the connections
	server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	test_check_false (__atomic_load_n (&teardown.done, __ATOMIC_SEQ_CST));
	server_test_check_refused (port);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);

	(void) pthread_join (thread_id, NULL);
	test_check_true (__atomic_load_n (&teardown.done, __ATOMIC_SEQ_CST));

	(void) close (working);
	(void) close (queued);

}

// the queued connections are answered with 503 right away
static void test_server_drain_reject (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.drain_policy = CERVER_DRAIN_POLICY_REJECT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	ServerTestTeardown teardown = { server, false };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, server_test_teardown_thread, &teardown), 0, NULL);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (queued, response);
	server_test_check_unavailable (response);

	server_test_gate_open ();
	server_test_expect (working, status_ok);

	(void) pthread_join (thread_id, NULL);

	(void) close (working);
	(void) close (queued);

}

static void server_test_signal_handler (int signal_number) {

	(void) signal_number;

}

// a signal without SA_RESTART interrupts accept () and stops the cerver,
// so a handler like the one of python can run when cerver_start () returns
static void test_server_signal (void) {

	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = server_test_signal_handler;
	(void) sigemptyset (&action.sa_mask);
	action.sa_flags = 0;
	(void) sigaction (SIGUSR1, &action, NULL);

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	test_check_int_eq (pthread_kill (server->thread, SIGUSR1), 0, NULL);

	server_test_join (server);
	test_check_false (cerver_is_running (server->cerver));
	test_check_unsigned_eq (server->start_result, 0, NULL);

	// shutdown is still valid after the signal
	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

	(void) signal (SIGUSR1, SIG_DFL);

}

#pragma endregion

#pragma region files

// the child runs out of fds, the cerver must keep accepting (and closing)
// connections and serve again when the fds are released
static void server_test_files_child (const u16 port) {

	struct rlimit limit = { SERVER_TEST_FILES_LIMIT, SERVER_TEST_FILES_LIMIT };
	if (setrlimit (RLIMIT_NOFILE, &limit)) _exit (EXIT_FAILURE);

	// SIGTERM interrupts accept () and stops the cerver, like in the services
	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = server_test_signal_handler;
	(void) sigemptyset (&action.sa_mask);
	(void) sigaction (SIGTERM, &action, NULL);

	// every accepted connection is queued, so the cerver runs out of fds
	ServerTestOptions options = server_test_options_default ();
	options.threads = 2;
	options.queue_size = SERVER_TEST_FILES_CONNECTIONS;
	options.overload_policy = CERVER_OVERLOAD_POLICY_REJECT;

	Cerver *cerver = server_test_cerver_create (port, &options);

	(void) cerver_start (cerver);
	(void) cerver_teardown (cerver);

	_exit (EXIT_SUCCESS);

}

// returns true if the cerver closed the connection
static bool server_test_is_closed (const int fd) {

	char byte = 0;

	return (recv (fd, &byte, 1, MSG_DONTWAIT) == 0);

}

// valgrind keeps its own fds, so the child can not start with the limit
static bool server_test_files_limit_is_valid (void) {

	const char *preload = getenv (SERVER_TEST_PRELOAD_VARIABLE);

	return !(preload && strstr (preload, SERVER_TEST_VALGRIND_PRELOAD));

}

static void test_server_out_of_fds (void) {

	if (!server_test_files_limit_is_valid ()) return;

	const u16 port = (u16) server_test_port++;

	(void) fflush (stdout);
	pid_t pid = fork ();
	test_check (pid >= 0, "fork () failed");

	if (pid == 0) server_test_files_child (port);

	int fds[SERVER_TEST_FILES_CONNECTIONS];
	for (unsigned int i = 0; i < SERVER_TEST_FILES_CONNECTIONS; i++) {
		fds[i] = server_test_connect (port);
	}

	server_test_sleep (SERVER_TEST_SLOW_SLEEP);

	// the extra connections are closed by the cerver, instead of staying in the backlog
	unsigned int closed = 0;
	for (unsigned int i = 0; i < SERVER_TEST_FILES_CONNECTIONS; i++) {
		if (server_test_is_closed (fds[i])) closed++;
	}

	test_check (closed > 0, "the cerver did not close the connections that it could not handle");

	for (unsigned int i = 0; i < SERVER_TEST_FILES_CONNECTIONS; i++) (void) close (fds[i]);

	// the cerver recovers
	server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	server_test_request (port, ok_request, status_ok);

	(void) kill (pid, SIGTERM);

	int status = 0;
	(void) waitpid (pid, &status, 0);
	test_check (WIFEXITED (status) && (WEXITSTATUS (status) == EXIT_SUCCESS), "the out of fds cerver did not stop");

}

#pragma endregion

#pragma region network

static void test_server_blocking (void) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, NULL);

	test_check_true (sock_set_blocking (fd, false));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) != 0, "the socket is not non blocking");

	test_check_true (sock_set_blocking (fd, true));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) == 0, "the socket is not blocking");

	test_check_false (sock_set_blocking (-1, true));

	(void) close (fd);

}

static int server_test_listen (const u16 port, const bool reusable_port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, NULL);

	test_check_int_eq (sock_set_reusable (fd), 0, NULL);
	if (reusable_port) test_check_int_eq (sock_set_reusable_port (fd), 0, NULL);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (bind (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in)) || listen (fd, SOMAXCONN)) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

// SO_REUSEADDR alone does not let two sockets listen in the same port
static void test_server_reusable (void) {

	const u16 port = (u16) server_test_port++;

	int first = server_test_listen (port, false);
	test_check (first > -1, NULL);
	test_check (server_test_listen (port, false) < 0, "two sockets listen in the same port");
	(void) close (first);

	const u16 shared_port = (u16) server_test_port++;
	first = server_test_listen (shared_port, true);
	int second = server_test_listen (shared_port, true);
	test_check (first > -1, NULL);
	test_check (second > -1, "SO_REUSEPORT did not share the port");
	(void) close (first);
	(void) close (second);

}

static void test_server_policies_strings (void) {

	test_check_str_eq (cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_WAIT), "Wait", NULL);
	test_check_str_eq (cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_REJECT), "Reject", NULL);
	test_check_str_eq (cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_MAX), "None", NULL);
	test_check_str_eq (cerver_drain_policy_to_string (CERVER_DRAIN_POLICY_RUN), "Run", NULL);
	test_check_str_eq (cerver_drain_policy_to_string (CERVER_DRAIN_POLICY_REJECT), "Reject", NULL);

	// invalid policies are ignored
	Cerver *cerver = cerver_create_web ("policies", (u16) server_test_port, CERVER_DEFAULT_CONNECTION_QUEUE);
	cerver_set_overload_policy (cerver, CERVER_OVERLOAD_POLICY_MAX);
	cerver_set_drain_policy (cerver, CERVER_DRAIN_POLICY_NONE);
	test_check (cerver->overload_policy == CERVER_DEFAULT_OVERLOAD_POLICY, NULL);
	test_check (cerver->drain_policy == CERVER_DEFAULT_DRAIN_POLICY, NULL);
	test_check (cerver->connection_queue == CERVER_DEFAULT_CONNECTION_QUEUE, NULL);
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

void http_tests_server (void) {

	(void) printf ("Testing HTTP server...\n");

	// a hang fails the suite instead of blocking it forever
	(void) alarm (SERVER_TEST_ALARM);

	test_server_blocking ();
	test_server_reusable ();
	test_server_policies_strings ();

	test_server_ok ();
	test_server_slowloris ();
	test_server_idle ();
	test_server_slow_body ();
	test_server_request_timeout ();
	test_server_send_timeout ();

	test_server_overload_reject ();
	test_server_overload_wait ();

	test_server_drain_run ();
	test_server_drain_reject ();

	test_server_signal ();

	test_server_out_of_fds ();

	(void) alarm (0);

	(void) printf ("Done!\n");

}
