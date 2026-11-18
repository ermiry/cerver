#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/core/types.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/headers/types.h>
#include <cerver/http/requests/method.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/security/remote.h>

#include "../test.h"
#include "http.h"

#ifdef CERVER_CURL

// the remote authentication (cerver/http/security/remote.h): a custom authentication asks a service of the test,
// a web cerver with threads that answers 200 to the good token and 401 to any other one, at once or slowly; with an
// event loop the requests wait in the loop without a thread of the pool, so a pool of one thread still answers the
// other routes while they wait

// the ports after the ones of the TLS tests
#define REMOTE_PORT_BASE					((1 << 13) + (1 << 12) + (1 << 11))
#define REMOTE_ADDRESS						"127.0.0.1"

// a port where nothing listens
#define REMOTE_DOWN_PORT					(REMOTE_PORT_BASE + (1 << 6))

#define REMOTE_SERVICE_THREADS				(1 << 4)

// the pool of the cerver of the test: one thread, every request that waits for its authentication holds it without
// the loop
#define REMOTE_CERVER_THREADS				1

// the milliseconds of the slow answers of the service, and the timeout that never waits for them
#define REMOTE_SLOW_MILLISECONDS			(1 << 9)
#define REMOTE_SHORT_TIMEOUT_MILLISECONDS	(1 << 6)
// what a request to the fast service takes at most, half a tick of the wheel of the loop
#define REMOTE_FAST_MILLISECONDS			(1 << 7)
// what the test waits for a remote request that should never be answered
#define REMOTE_SETTLE_MILLISECONDS			(1 << 8)
#define REMOTE_MICROSECONDS_PER_MILLISECOND	1000
#define REMOTE_MILLISECONDS_PER_SECOND		1000
#define REMOTE_NANOSECONDS_PER_MILLISECOND	1000000

// the clients that wait for their authentication at once
#define REMOTE_WAITING_CLIENTS				(1 << 3)

#define REMOTE_URL_SIZE						(1 << 7)
#define REMOTE_HEADER_SIZE					(1 << 7)
#define REMOTE_REQUEST_SIZE					(1 << 9)
#define REMOTE_RESPONSE_SIZE				(1 << 12)

#define REMOTE_LISTEN_ATTEMPTS				(1 << 9)
#define REMOTE_LISTEN_INTERVAL				(1 << 12)

#define REMOTE_STATUS_OFFSET				9

static const char *remote_good_token = "good";
static const char *remote_bad_token = "bad";

static const char *remote_body = "{\"user\": \"test\"}";

static u16 remote_port = REMOTE_PORT_BASE;

// the port of the service of the current cerver
static u16 remote_service_port = 0;

static unsigned int remote_answers = 0;

typedef struct RemoteServer {

	Cerver *cerver;
	pthread_t thread;
	u16 port;

} RemoteServer;

#pragma region service

static void remote_service_answer (const HttpReceive *receive, const HttpRequest *request) {

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);
	const bool good = authorization && !strcmp (authorization->str, remote_good_token);

	(void) http_response_json_msg_send (
		receive, good ? HTTP_STATUS_OK : HTTP_STATUS_UNAUTHORIZED, good ? "good" : "bad"
	);

}

static void remote_service_slow (const HttpReceive *receive, const HttpRequest *request) {

	(void) usleep ((useconds_t) (test_milliseconds (REMOTE_SLOW_MILLISECONDS) * REMOTE_MICROSECONDS_PER_MILLISECOND));

	remote_service_answer (receive, request);

}

#pragma endregion

#pragma region cerver

static unsigned int remote_answered (
	const HttpReceive *receive, const HttpRequest *request, const HttpAuthResponse *response, void *data
) {

	(void) receive;
	(void) request;
	(void) data;

	(void) __atomic_add_fetch (&remote_answers, 1, __ATOMIC_SEQ_CST);

	return (response->status == HTTP_STATUS_OK) ? 0 : 1;

}

// where the authentication of a path asks: the service at once or slowly, with a timeout shorter than the slow
// answer, or a port where nothing listens
static void remote_destination (const HttpRequest *request, char *url, const size_t size, unsigned int *timeout) {

	const char *path = request->url->str;

	*timeout = 0;

	if (strstr (path, "down")) {
		(void) snprintf (url, size, "http://%s:%u/check", REMOTE_ADDRESS, (unsigned int) REMOTE_DOWN_PORT);
	}

	else if (strstr (path, "timeout")) {
		(void) snprintf (url, size, "http://%s:%u/slow", REMOTE_ADDRESS, (unsigned int) remote_service_port);
		*timeout = test_milliseconds (REMOTE_SHORT_TIMEOUT_MILLISECONDS);
	}

	else if (strstr (path, "slow")) {
		(void) snprintf (url, size, "http://%s:%u/slow", REMOTE_ADDRESS, (unsigned int) remote_service_port);
	}

	else {
		(void) snprintf (url, size, "http://%s:%u/check", REMOTE_ADDRESS, (unsigned int) remote_service_port);
	}

}

// the token of the request goes to the service
static unsigned int remote_authenticate (const HttpReceive *receive, const HttpRequest *request) {

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);

	char header[REMOTE_HEADER_SIZE];
	(void) snprintf (header, sizeof (header), "Authorization: %s", authorization ? authorization->str : "");
	const char *headers[] = { header };

	char url[REMOTE_URL_SIZE];
	unsigned int timeout = 0;
	remote_destination (request, url, sizeof (url), &timeout);

	const HttpAuthRequest remote = { REQUEST_METHOD_GET, url, headers, 1, NULL, 0, timeout };

	return http_auth_request (receive, &remote, remote_answered, NULL);

}

// an authentication that leaves a remote request and does not return what http_auth_request () returned: its request
// is never sent, and the value of the handler decides
static unsigned int remote_authenticate_ignored (const HttpReceive *receive, const HttpRequest *request) {

	(void) remote_authenticate (receive, request);

	return 1;

}

static void remote_secret (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, "secret");

}

static void remote_open (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, "open");

}

static void *remote_server_thread (void *server_ptr) {

	(void) cerver_start (((RemoteServer *) server_ptr)->cerver);

	return NULL;

}

static void remote_wait_listen (const u16 port) {

	bool listening = false;
	for (unsigned int attempt = 0; !listening && (attempt < REMOTE_LISTEN_ATTEMPTS); attempt++) {
		const int fd = socket (AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in address;
		(void) memset (&address, 0, sizeof (address));
		address.sin_family = AF_INET;
		address.sin_port = htons (port);
		address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

		listening = !connect (fd, (struct sockaddr *) &address, sizeof (address));
		(void) close (fd);

		if (!listening) (void) usleep (REMOTE_LISTEN_INTERVAL);
	}

	test_check_true (listening);

}

static Cerver *remote_cerver_create (const char *name, const u16 port, const CerverHandlerType type, const u16 threads) {

	Cerver *cerver = cerver_create_web (name, port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, type);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, threads);

	return cerver;

}

static void remote_server_start (RemoteServer *server) {

	test_check_int_eq (pthread_create (&server->thread, NULL, remote_server_thread, server), 0, NULL);
	remote_wait_listen (server->port);

}

static void remote_server_stop (RemoteServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

}

static void remote_service_start (RemoteServer *service) {

	service->port = remote_port++;
	service->cerver = remote_cerver_create ("remote-service", service->port, CERVER_HANDLER_TYPE_THREADS, REMOTE_SERVICE_THREADS);

	HttpGroup *root = http_cerver_group_create ((HttpCerver *) service->cerver->cerver_data, "");
	(void) http_group_get (root, "check", remote_service_answer);
	(void) http_group_get (root, "slow", remote_service_slow);

	remote_server_start (service);

	remote_service_port = service->port;

}

static void remote_cerver_start (RemoteServer *server, const CerverHandlerType type) {

	server->port = remote_port++;
	server->cerver = remote_cerver_create ("remote-test", server->port, type, REMOTE_CERVER_THREADS);

	HttpCerver *http_cerver = (HttpCerver *) server->cerver->cerver_data;

	HttpGroup *root = http_cerver_group_create (http_cerver, "");
	(void) http_group_get (root, "open", remote_open);

	HttpGroup *secret = http_cerver_group_create (http_cerver, "secret");
	http_group_set_auth (secret, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_group_set_authentication_handler (secret, remote_authenticate);
	(void) http_group_get (secret, "fast", remote_secret);
	(void) http_group_post (secret, "fast", remote_secret);
	(void) http_group_get (secret, "slow", remote_secret);
	(void) http_group_get (secret, "timeout", remote_secret);
	(void) http_group_get (secret, "down", remote_secret);

	HttpGroup *ignored = http_cerver_group_create (http_cerver, "ignored");
	http_group_set_auth (ignored, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_group_set_authentication_handler (ignored, remote_authenticate_ignored);
	(void) http_group_get (ignored, "fast", remote_secret);

	remote_server_start (server);

}

#pragma endregion

#pragma region client

static int remote_connect (const u16 port) {

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check_int_gt (fd, -1);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	// a response that never comes fails the read
	struct timeval timeout = { (time_t) (TEST_WAIT_SECONDS * test_time_scale ()), 0 };
	test_check_int_eq (setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)), 0, NULL);

	return fd;

}

static void remote_send (const int fd, const char *method, const char *path, const char *token, const char *body) {

	char request[REMOTE_REQUEST_SIZE];
	const int length = body ?
		snprintf (
			request, sizeof (request),
			"%s %s HTTP/1.1\r\nHost: localhost\r\nAuthorization: %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
			method, path, token, strlen (body), body
		) :
		snprintf (request, sizeof (request), "%s %s HTTP/1.1\r\nHost: localhost\r\nAuthorization: %s\r\n\r\n", method, path, token);

	test_check_int_eq ((int) send (fd, request, (size_t) length, MSG_NOSIGNAL), length, NULL);

}

// reads one response whole (its headers and the body of its Content-Length), returns its status, 0 without one
static unsigned int remote_read (const int fd) {

	char response[REMOTE_RESPONSE_SIZE];
	size_t size = 0;
	char *end = NULL;
	size_t expected = 0;

	bool reading = true;
	while (reading && (size < (sizeof (response) - 1))) {
		const ssize_t received = recv (fd, response + size, (end ? expected : (sizeof (response) - 1)) - size, 0);
		reading = (received > 0);

		if (reading) {
			size += (size_t) received;
			response[size] = '\0';

			if (!end && (end = strstr (response, "\r\n\r\n"))) {
				const char *length = strstr (response, "Content-Length: ");
				expected = (size_t) (end - response) + 4 + (length ? (size_t) atol (length + strlen ("Content-Length: ")) : 0);
			}

			reading = !end || (size < expected);
		}
	}

	return (end && (size > REMOTE_STATUS_OFFSET)) ? (unsigned int) atoi (response + REMOTE_STATUS_OFFSET) : 0;

}

static unsigned int remote_status (const u16 port, const char *method, const char *path, const char *token, const char *body) {

	const int fd = remote_connect (port);
	remote_send (fd, method, path, token, body);
	const unsigned int status = remote_read (fd);
	(void) close (fd);

	return status;

}

static u64 remote_milliseconds (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((u64) now.tv_sec * REMOTE_MILLISECONDS_PER_SECOND) + ((u64) now.tv_nsec / REMOTE_NANOSECONDS_PER_MILLISECOND);

}

#pragma endregion

#pragma region tests

// the answer decides: the good token is authenticated, any other one is answered with 401; without a body the
// request is authenticated by the job that handles it, with one before its body is read
static void remote_check_answers (const u16 port) {

	test_check_unsigned_eq (remote_status (port, "GET", "/secret/fast", remote_good_token, NULL), 200, NULL);
	test_check_unsigned_eq (remote_status (port, "GET", "/secret/fast", remote_bad_token, NULL), 401, NULL);

	test_check_unsigned_eq (remote_status (port, "POST", "/secret/fast", remote_good_token, remote_body), 200, NULL);
	test_check_unsigned_eq (remote_status (port, "POST", "/secret/fast", remote_bad_token, remote_body), 401, NULL);

	// a request that failed has the status 0
	test_check_unsigned_eq (remote_status (port, "GET", "/secret/timeout", remote_good_token, NULL), 401, NULL);
	test_check_unsigned_eq (remote_status (port, "GET", "/secret/down", remote_good_token, NULL), 401, NULL);

}

// what follows the authentication before the body runs with its answer: the "100 Continue" of a client that waits
// for it to send its body
static void remote_check_continue (const u16 port) {

	const int fd = remote_connect (port);

	char request[REMOTE_REQUEST_SIZE];
	const int length = snprintf (
		request, sizeof (request),
		"POST /secret/fast HTTP/1.1\r\nHost: localhost\r\nAuthorization: %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nExpect: 100-continue\r\n\r\n",
		remote_good_token, strlen (remote_body)
	);
	test_check_int_eq ((int) send (fd, request, (size_t) length, MSG_NOSIGNAL), length, NULL);

	test_check_unsigned_eq (remote_read (fd), 100, NULL);

	test_check_int_eq ((int) send (fd, remote_body, strlen (remote_body), MSG_NOSIGNAL), (int) strlen (remote_body), NULL);
	test_check_unsigned_eq (remote_read (fd), 200, NULL);

	(void) close (fd);

}

// the timers of libcurl are deadlines of the loop: a fast service answers long before a tick of the wheel of the loop
static void remote_check_latency (const u16 port) {

	const u64 started = remote_milliseconds ();
	test_check_unsigned_eq (remote_status (port, "GET", "/secret/fast", remote_good_token, NULL), 200, NULL);
	test_check_true ((remote_milliseconds () - started) < (u64) test_milliseconds (REMOTE_FAST_MILLISECONDS));

	// the timeout of a request that the slow service does not answer in time ends with its own timer
	const u64 timed = remote_milliseconds ();
	test_check_unsigned_eq (remote_status (port, "GET", "/secret/timeout", remote_good_token, NULL), 401, NULL);
	test_check_true (
		(remote_milliseconds () - timed) < (u64) test_milliseconds (REMOTE_SHORT_TIMEOUT_MILLISECONDS + REMOTE_FAST_MILLISECONDS)
	);

}

// a connection that is kept: the requests after a remote authentication are read as any other
static void remote_check_kept (const u16 port) {

	const int fd = remote_connect (port);

	remote_send (fd, "GET", "/secret/fast", remote_good_token, NULL);
	test_check_unsigned_eq (remote_read (fd), 200, NULL);

	remote_send (fd, "POST", "/secret/fast", remote_good_token, remote_body);
	test_check_unsigned_eq (remote_read (fd), 200, NULL);

	remote_send (fd, "GET", "/open", remote_good_token, NULL);
	test_check_unsigned_eq (remote_read (fd), 200, NULL);

	(void) close (fd);

}

// the requests that wait for a slow service do not hold the only thread of the pool: another route is answered
// long before their answers
static void remote_check_waits_without_thread (const u16 port) {

	int waiting[REMOTE_WAITING_CLIENTS];
	for (unsigned int index = 0; index < REMOTE_WAITING_CLIENTS; index++) {
		waiting[index] = remote_connect (port);
		remote_send (waiting[index], "GET", "/secret/slow", remote_good_token, NULL);
	}

	const u64 started = remote_milliseconds ();
	test_check_unsigned_eq (remote_status (port, "GET", "/open", remote_good_token, NULL), 200, NULL);
	test_check_true ((remote_milliseconds () - started) < (u64) (test_milliseconds (REMOTE_SLOW_MILLISECONDS) / 2));

	for (unsigned int index = 0; index < REMOTE_WAITING_CLIENTS; index++) {
		test_check_unsigned_eq (remote_read (waiting[index]), 200, NULL);
		(void) close (waiting[index]);
	}

}

static void remote_check_model (const CerverHandlerType type) {

	RemoteServer service;
	remote_service_start (&service);

	RemoteServer server;
	remote_cerver_start (&server, type);

	(void) __atomic_store_n (&remote_answers, 0, __ATOMIC_SEQ_CST);

	remote_check_answers (server.port);

	// every authentication had its answer
	test_check_unsigned_eq (__atomic_load_n (&remote_answers, __ATOMIC_SEQ_CST), 6, NULL);

	// the value of a handler that ignored its remote request decides, and its request is never sent with an event
	// loop (with threads it was answered inside the call)
	test_check_unsigned_eq (remote_status (server.port, "GET", "/ignored/fast", remote_good_token, NULL), 401, NULL);

	// a request that went out anyway would be answered after the 401
	(void) usleep ((useconds_t) (test_milliseconds (REMOTE_SETTLE_MILLISECONDS) * REMOTE_MICROSECONDS_PER_MILLISECOND));
	test_check_unsigned_eq (
		__atomic_load_n (&remote_answers, __ATOMIC_SEQ_CST), (type == CERVER_HANDLER_TYPE_EVENTS) ? 6 : 7, NULL
	);

	remote_check_continue (server.port);

	// a cerver with threads does not keep its connections by default
	if (type == CERVER_HANDLER_TYPE_EVENTS) {
		remote_check_latency (server.port);
		remote_check_kept (server.port);
		remote_check_waits_without_thread (server.port);
	}

	remote_server_stop (&server);
	remote_server_stop (&service);

}

// a cerver that stops while a request waits for its remote authentication answers it, and ends in time
static void remote_check_stop (void) {

	RemoteServer service;
	remote_service_start (&service);

	RemoteServer server;
	remote_cerver_start (&server, CERVER_HANDLER_TYPE_EVENTS);

	const int fd = remote_connect (server.port);
	remote_send (fd, "GET", "/secret/slow", remote_good_token, NULL);

	// the request waits in the loop
	(void) usleep ((useconds_t) (test_milliseconds (REMOTE_SLOW_MILLISECONDS) * REMOTE_MICROSECONDS_PER_MILLISECOND / 4));

	remote_server_stop (&server);

	const unsigned int status = remote_read (fd);
	test_check_true ((status == 0) || (status == 503) || (status == 200));
	(void) close (fd);

	remote_server_stop (&service);

}

#pragma endregion

static void remote_tests (void) {

	(void) printf ("\tthreads\n");
	remote_check_model (CERVER_HANDLER_TYPE_THREADS);

	(void) printf ("\tevents\n");
	remote_check_model (CERVER_HANDLER_TYPE_EVENTS);

	(void) printf ("\tstop\n");
	remote_check_stop ();

}

#else

static unsigned int remote_never_answered (
	const HttpReceive *receive, const HttpRequest *request, const HttpAuthResponse *response, void *data
) {

	(void) receive;
	(void) request;
	(void) response;
	(void) data;

	return 0;

}

// without libcurl a remote request always fails
static void remote_tests (void) {

	const HttpAuthRequest request = { REQUEST_METHOD_GET, "http://127.0.0.1:1/", NULL, 0, NULL, 0, 0 };
	test_check_unsigned_eq (http_auth_request (NULL, &request, remote_never_answered, NULL), 1, NULL);

}

#endif

void http_tests_remote (void) {

	(void) printf ("Testing HTTP remote authentication...\n");

	remote_tests ();

	(void) printf ("Done!\n");

}
