#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/core/types.h>
#include "cerver/events/internal.h"
#include <cerver/network/network.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/limits.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/headers/types.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

#include <cerver/threads/thpool.h>

#include "../test.h"
#include "http.h"

// runs a real web cerver in a thread, with a named pool of threads next to its thpool, and talks to it with
// raw sockets, to test the pools of an HTTP cerver and its health and ready routes

// what this suite pins:
// - every request ran in the only thpool of its cerver: the handlers that take long kept the threads that the
//   fast ones needed, and a pool that was full rejected or delayed the requests of every route
// - a route must run in the pool that its endpoint or one of its groups chose, with the policy and the wait of
//   that pool, and a pool that no cerver has must stop the cerver when it starts
// - the health route must be answered when every thread is busy, and the ready route must tell that a pool has
//   no place for a request

#define POOLS_PORT_BASE						((1 << 13) + (1 << 10))
#define POOLS_ADDRESS						"127.0.0.1"

#define POOLS_RESPONSE_SIZE					(1 << 12)

#define POOLS_CONNECT_RETRIES				(1 << 8)
// 2^22 ns
#define POOLS_CONNECT_INTERVAL				4194304L

// secs that a client waits for a response
#define POOLS_RECEIVE_TIMEOUT				16

// secs that a cerver has to stop
#define POOLS_STOP_TIMEOUT					(1 << 4)
#define POOLS_DRAIN_TIMEOUT					(1 << 3)

// secs that the test waits for a state of its cerver
#define POOLS_WAIT_TIMEOUT					(1 << 4)

#define POOLS_ALARM							256

// the threads and the queue of the thpool of the cervers of the tests
#define POOLS_THREADS						4
#define POOLS_QUEUE_SIZE					(1 << 4)

// the pool of the routes that take long: its threads, the jobs that wait for them, and both
#define POOLS_SLOW_THREADS					2
#define POOLS_SLOW_QUEUE_SIZE				2
#define POOLS_SLOW_PLACES					(POOLS_SLOW_THREADS + POOLS_SLOW_QUEUE_SIZE)

// a thpool with one thread, to fill it with a few requests
#define POOLS_SINGLE_THREADS				1
#define POOLS_SINGLE_QUEUE_SIZE				2
#define POOLS_SINGLE_PLACES					(POOLS_SINGLE_THREADS + POOLS_SINGLE_QUEUE_SIZE)

// secs that a request waits for a place in the slow pool
#define POOLS_SLOW_WAIT						1

// the values of the setters
#define POOLS_SET_QUEUE_SIZE				(1 << 3)
#define POOLS_SET_WAIT						3

// what the options of a test do with the wait of the slow pool
#define POOLS_WAIT_NOT_SET					(-1)

#define POOLS_NAME_SIZE						(1 << 6)
#define POOLS_THREAD_NAME_SIZE				(1 << 4)

// the requests of the health route in its test: a GET, a HEAD and a GET with a query
#define POOLS_HEALTH_REQUESTS				3
// the responses of the requests that a client sends in one connection
#define POOLS_KEPT_REQUESTS					3
// the bucket of a connection that ended with those requests: up to 2^2 of them
#define POOLS_KEPT_BUCKET					2
// the bucket of a connection that ended with one request
#define POOLS_SINGLE_BUCKET					0
// the microseconds that a thread looks for the next request of its connection in the test that needs it to
// find that request: more than a client takes to send it, in any machine
#define POOLS_FOLLOW_TIME					(1 << 22)
// the requests of that test that the thread that answered the one before them handled
#define POOLS_FOLLOWED_REQUESTS				2
// the connections of the test of a cerver without event loops, with the one that waits for the cerver: one request each
#define POOLS_WITHOUT_EVENTS_CONNECTIONS	5

static const char *pools_cerver_name = { "pools" };
static const char *pools_slow_name = { "slow" };
static const char *pools_fast_name = { "fast" };
static const char *pools_missing_name = { "missing" };
static const char *pools_long_name = { "a-name-that-is-longer-than-a-pool-can-have" };
static const char *pools_words_name = { "two words" };

// the threads of a thpool are named after it
static const char *pools_default_thread = { "thpool-pools-" };
static const char *pools_slow_thread = { "thpool-slow-" };

static const char *pools_health_route = { "/health" };
static const char *pools_ready_route = { "/ready" };

static const char *pools_status_ok = { "HTTP/1.1 200" };
static const char *pools_status_not_found = { "HTTP/1.1 404" };
static const char *pools_status_unavailable = { "HTTP/1.1 503" };

static const char *pools_header_end = { "\r\n\r\n" };
static const char *pools_retry_after = { "Retry-After: 1\r\n" };
static const char *pools_valid_authorization = { "valid" };

static const char *pools_ok_request = {
	"GET /pools/ok HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_block_request = {
	"GET /pools/block HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_slow_ok_request = {
	"GET /pools/slow/ok HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_slow_block_request = {
	"GET /pools/slow/block HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_slow_fast_request = {
	"GET /pools/slow/fast HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_slow_private_request = {
	"GET /pools/slow/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\nConnection: close\r\n\r\n"
};

static const char *pools_old_request = {
	"GET /old HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_health_request = {
	"GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_health_head_request = {
	"HEAD /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_health_query_request = {
	"GET /health?check=1 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_health_post_request = {
	"POST /health HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
};

static const char *pools_health_slash_request = {
	"GET /health/ HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

// two requests of a connection that is kept and the one that closes it
static const char *pools_health_kept_requests = {
	"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
	"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
	"GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *pools_ready_request = {
	"GET /ready HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

// the requests of one connection that is kept: a route of the slow pool, one of the default pool
// and the first one again, each one answered with the name of its thread
static const char *pools_kept_requests = {
	"GET /pools/slow/thread HTTP/1.1\r\nHost: localhost\r\n\r\n"
	"GET /pools/thread HTTP/1.1\r\nHost: localhost\r\n\r\n"
	"GET /pools/slow/thread HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

// the requests of a connection that a thread follows, each one answered with the name of its thread
static const char *pools_slow_thread_request = { "GET /pools/slow/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *pools_thread_request = { "GET /pools/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// how the name of every thread of a pool starts
static const char *pools_thread_prefix = { "thpool-" };

// a request of the ready route in two pieces: the cerver is asked to stop between them
static const char *pools_ready_start = { "GET /ready HTTP/1.1\r\nHost: local" };
static const char *pools_ready_end = { "host\r\nConnection: close\r\n\r\n" };

static unsigned int pools_port = POOLS_PORT_BASE;

// how the cervers of the tests that run now handle their connections
static CerverHandlerType pools_handler_type = CERVER_HANDLER_TYPE_THREADS;

#pragma region gate

// keeps the block requests working until it is opened
static pthread_mutex_t pools_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pools_gate_cond = PTHREAD_COND_INITIALIZER;
static bool pools_gate_is_open = false;
static unsigned int pools_gate_waiting = 0;

static void pools_gate_reset (void) {

	(void) pthread_mutex_lock (&pools_gate_mutex);
	pools_gate_is_open = false;
	pools_gate_waiting = 0;
	(void) pthread_mutex_unlock (&pools_gate_mutex);

}

static void pools_gate_open (void) {

	(void) pthread_mutex_lock (&pools_gate_mutex);
	pools_gate_is_open = true;
	(void) pthread_cond_broadcast (&pools_gate_cond);
	(void) pthread_mutex_unlock (&pools_gate_mutex);

}

static unsigned int pools_gate_count (void) {

	(void) pthread_mutex_lock (&pools_gate_mutex);
	const unsigned int waiting = pools_gate_waiting;
	(void) pthread_mutex_unlock (&pools_gate_mutex);

	return waiting;

}

static void pools_pause (void) {

	struct timespec interval = { 0, POOLS_CONNECT_INTERVAL };
	(void) nanosleep (&interval, NULL);

}

// waits until the block requests are working
static void pools_gate_wait_for (const unsigned int waiting) {

	const double deadline = test_wait_now () + (double) (POOLS_WAIT_TIMEOUT * test_time_scale ());
	while ((pools_gate_count () < waiting) && (test_wait_now () < deadline)) pools_pause ();

	test_check_unsigned_eq (pools_gate_count (), waiting, NULL);

}

#pragma endregion

#pragma region routes

// the requests that the ok routes handled, added by the threads of the cervers
static unsigned int pools_handled = 0;

// the names of the threads that ran the last handler and the last authentication
static pthread_mutex_t pools_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static char pools_handler_thread[POOLS_THREAD_NAME_SIZE] = { 0 };
static char pools_authentication_thread[POOLS_THREAD_NAME_SIZE] = { 0 };

static void pools_thread_save (char *name) {

	(void) pthread_mutex_lock (&pools_threads_mutex);
	(void) pthread_getname_np (pthread_self (), name, POOLS_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&pools_threads_mutex);

}

static void pools_thread_reset (void) {

	(void) pthread_mutex_lock (&pools_threads_mutex);
	(void) memset (pools_handler_thread, 0, POOLS_THREAD_NAME_SIZE);
	(void) memset (pools_authentication_thread, 0, POOLS_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&pools_threads_mutex);

}

static void pools_thread_expect (const char *name, const char *prefix) {

	char saved[POOLS_THREAD_NAME_SIZE] = { 0 };

	(void) pthread_mutex_lock (&pools_threads_mutex);
	(void) memcpy (saved, name, POOLS_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&pools_threads_mutex);

	if (strncmp (saved, prefix, strlen (prefix))) {
		(void) fprintf (stderr, "the thread \"%s\" is not one of \"%s\"\n", saved, prefix);
	}

	test_check_int_eq (strncmp (saved, prefix, strlen (prefix)), 0, NULL);

}

// answers and tells which thread did it
static void pools_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	pools_thread_save (pools_handler_thread);

	test_counter_add (&pools_handled);

	(void) http_response_send (oki_doki, http_receive);

}

// answers with the name of the thread that handles the request
static void pools_thread_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char name[POOLS_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, POOLS_THREAD_NAME_SIZE);

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, name, strlen (name));

}

// answers when the gate is opened
static void pools_block_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) pthread_mutex_lock (&pools_gate_mutex);
	pools_gate_waiting += 1;
	while (!pools_gate_is_open) (void) pthread_cond_wait (&pools_gate_cond, &pools_gate_mutex);
	(void) pthread_mutex_unlock (&pools_gate_mutex);

	(void) http_response_send (oki_doki, http_receive);

}

// only with the valid authorization, and tells which thread checked it
static unsigned int pools_authentication_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	pools_thread_save (pools_authentication_thread);

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);

	return (authorization && !strcmp (authorization->str, pools_valid_authorization)) ? 0 : 1;

}

// GET /pools/ok and /pools/block in the thpool of the cerver;
// the group /pools/slow in the slow pool, with an endpoint that goes back to the default one;
// GET /old, a route of the first routes API in the slow pool
static void pools_routes (HttpCerver *http_cerver) {

	HttpGroup *group = http_cerver_group_create (http_cerver, "pools");
	test_check_ptr (group);

	test_check_ptr (http_group_get (group, "ok", pools_ok_handler));
	test_check_ptr (http_group_get (group, "block", pools_block_handler));
	test_check_ptr (http_group_get (group, "thread", pools_thread_handler));

	HttpGroup *slow = http_group_create (group, "slow");
	test_check_ptr (slow);
	http_group_set_pool (slow, pools_slow_name);

	test_check_ptr (http_group_get (slow, "ok", pools_ok_handler));
	test_check_ptr (http_group_get (slow, "block", pools_block_handler));
	test_check_ptr (http_group_get (slow, "thread", pools_thread_handler));

	HttpEndpoint *fast_endpoint = http_group_get (slow, "fast", pools_ok_handler);
	test_check_ptr (fast_endpoint);
	http_endpoint_set_pool (fast_endpoint, NULL);

	HttpEndpoint *private_endpoint = http_group_get (slow, "private", pools_ok_handler);
	test_check_ptr (private_endpoint);
	http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (private_endpoint, pools_authentication_handler);

	HttpRoute *old_route = http_route_create (REQUEST_METHOD_GET, "old", pools_ok_handler);
	test_check_ptr (old_route);
	http_route_set_pool (old_route, pools_slow_name);
	http_cerver_route_register (http_cerver, old_route);

}

#pragma endregion

#pragma region server

typedef struct PoolsOptions {

	CerverHandlerType handler_type;

	// the thpool of the cerver, 0 for POOLS_THREADS and POOLS_QUEUE_SIZE
	unsigned int threads;
	unsigned int queue_size;

	// CERVER_OVERLOAD_POLICY_NONE keeps the default one
	CerverOverloadPolicy policy;

	// the slow pool, 0 for POOLS_SLOW_THREADS and POOLS_SLOW_QUEUE_SIZE
	unsigned int slow_threads;
	unsigned int slow_queue_size;

	// CERVER_OVERLOAD_POLICY_NONE does not set it: the pool uses the one of its cerver
	CerverOverloadPolicy slow_policy;

	// secs, POOLS_WAIT_NOT_SET does not set it
	int slow_wait;

	// CERVER_DRAIN_POLICY_NONE keeps the default one
	CerverDrainPolicy drain_policy;

	// the microseconds that a thread looks for the next request of its connection, 0 keeps the default
	unsigned int follow;

} PoolsOptions;

typedef struct PoolsServer {

	Cerver *cerver;
	HttpCerver *http_cerver;
	HttpPool *slow;
	u16 port;

	pthread_t thread;

} PoolsServer;

static PoolsOptions pools_options_default (void) {

	PoolsOptions options = {
		pools_handler_type,
		0, 0, CERVER_OVERLOAD_POLICY_NONE,
		0, 0, CERVER_OVERLOAD_POLICY_NONE, POOLS_WAIT_NOT_SET,
		CERVER_DRAIN_POLICY_NONE,
		0
	};

	return options;

}

static void *pools_server_thread (void *server_ptr) {

	PoolsServer *server = (PoolsServer *) server_ptr;

	test_check_unsigned_eq (cerver_start (server->cerver), 0, NULL);

	return NULL;

}

static Cerver *pools_cerver_create (const PoolsOptions *options) {

	Cerver *cerver = cerver_create_web (pools_cerver_name, (u16) pools_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, options->handler_type);
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, (u16) (options->threads ? options->threads : POOLS_THREADS));
	cerver_set_thpool_queue_size (cerver, options->queue_size ? options->queue_size : POOLS_QUEUE_SIZE);
	cerver_set_drain_timeout (cerver, POOLS_DRAIN_TIMEOUT);

	if (options->policy != CERVER_OVERLOAD_POLICY_NONE) cerver_set_overload_policy (cerver, options->policy);

	if (options->drain_policy != CERVER_DRAIN_POLICY_NONE) cerver_set_drain_policy (cerver, options->drain_policy);

	if (options->follow) http_cerver_set_keep_alive_follow ((HttpCerver *) cerver->cerver_data, options->follow);

	return cerver;

}

static HttpPool *pools_slow_create (HttpCerver *http_cerver, const PoolsOptions *options) {

	HttpPool *slow = http_cerver_pool_create (
		http_cerver, pools_slow_name, options->slow_threads ? options->slow_threads : POOLS_SLOW_THREADS
	);

	test_check_ptr (slow);

	http_pool_set_queue_size (slow, options->slow_queue_size ? options->slow_queue_size : POOLS_SLOW_QUEUE_SIZE);

	if (options->slow_policy != CERVER_OVERLOAD_POLICY_NONE) {
		http_pool_set_overload_policy (slow, options->slow_policy);
	}

	if (options->slow_wait != POOLS_WAIT_NOT_SET) {
		http_pool_set_wait_timeout (slow, (unsigned int) options->slow_wait);
	}

	return slow;

}

static int pools_connect (const u16 port);

// waits until the cerver is listening and one of its threads handled a complete request
static void pools_server_ready (const PoolsServer *server) {

	const unsigned int handled = __atomic_load_n (&pools_handled, __ATOMIC_SEQ_CST);

	const int fd = pools_connect (server->port);

	const size_t length = strlen (pools_ok_request);
	test_check (send (fd, pools_ok_request, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

	char response[POOLS_RESPONSE_SIZE];
	while (recv (fd, response, sizeof (response), 0) > 0);

	(void) close (fd);

	test_check_unsigned_gt (test_wait_counter (&pools_handled, handled + 1, TEST_WAIT_SECONDS), handled);

}

static PoolsServer *pools_server_start (const PoolsOptions *options) {

	PoolsServer *server = (PoolsServer *) calloc (1, sizeof (PoolsServer));
	test_check_ptr (server);

	Cerver *cerver = pools_cerver_create (options);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	// a path without a route is answered with 404 by cerver
	http_cerver_set_not_found_handler (http_cerver);

	http_cerver_set_health_route (http_cerver, pools_health_route);
	http_cerver_set_ready_route (http_cerver, pools_ready_route);

	server->cerver = cerver;
	server->http_cerver = http_cerver;
	server->slow = pools_slow_create (http_cerver, options);
	server->port = (u16) (pools_port - 1);

	pools_routes (http_cerver);

	test_check_int_eq (pthread_create (&server->thread, NULL, pools_server_thread, server), 0, NULL);

	pools_server_ready (server);

	return server;

}

// cerver_start () must return in time after it was asked to stop
static void pools_server_join (PoolsServer *server) {

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) (POOLS_STOP_TIMEOUT * test_time_scale ());

	const int result = pthread_timedjoin_np (server->thread, NULL, &deadline);
	if (result) (void) fprintf (stderr, "the cerver in port %u did not stop\n", (unsigned int) server->port);
	test_check_int_eq (result, 0, NULL);

}

static void pools_server_end (PoolsServer *server) {

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

static void pools_server_stop (PoolsServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	pools_server_join (server);

	pools_server_end (server);

}

static unsigned int pools_slow_queued (const PoolsServer *server) {

	return thpool_get_queued (server->slow->thpool);

}

static unsigned int pools_slow_parked (const PoolsServer *server) {

	return event_loop_group_lane_parked (server->cerver, server->slow->index);

}

static unsigned int pools_default_queued (const PoolsServer *server) {

	return thpool_get_queued (server->cerver->thpool);

}

static unsigned int pools_ready (const PoolsServer *server) {

	return http_cerver_is_ready (server->http_cerver) ? 1 : 0;

}

// the connections that the event loops of the cerver have
static unsigned int pools_loop_connections (const PoolsServer *server) {

	return event_loop_group_connections (server->cerver);

}

// the requests of the connections of the cerver that ended: the last thing that is counted of each one
static unsigned int pools_connection_requests (const PoolsServer *server) {

	return (unsigned int) CERVER_STATS_GET (server->http_cerver->n_connection_requests);

}

static unsigned int pools_completed (Thpool *thpool) {

	ThpoolStats stats;
	thpool_get_stats (thpool, &stats);

	return (unsigned int) stats.completed_jobs;

}

// the jobs that the threads of the slow pool finished, their time is counted with them
static unsigned int pools_slow_completed (const PoolsServer *server) {

	return pools_completed (server->slow->thpool);

}

static unsigned int pools_default_completed (const PoolsServer *server) {

	return pools_completed (server->cerver->thpool);

}

// waits until a value of the cerver is the expected one
static void pools_server_wait (
	const PoolsServer *server, unsigned int (*value) (const PoolsServer *), const unsigned int expected
) {

	const double deadline = test_wait_now () + (double) (POOLS_WAIT_TIMEOUT * test_time_scale ());
	while ((value (server) != expected) && (test_wait_now () < deadline)) pools_pause ();

	test_check_unsigned_eq (value (server), expected, NULL);

}

#pragma endregion

#pragma region client

static int pools_connect_once (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, POOLS_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static int pools_connect (const u16 port) {

	int fd = -1;
	for (unsigned int i = 0; (i < POOLS_CONNECT_RETRIES) && (fd < 0); i++) {
		fd = pools_connect_once (port);

		if (fd < 0) pools_pause ();
	}

	test_check (fd > -1, "failed to connect to the test cerver");

	(void) sock_set_timeout (fd, POOLS_RECEIVE_TIMEOUT);

	return fd;

}

// starts a request, its response is read later
static int pools_send (const PoolsServer *server, const char *request) {

	const int fd = pools_connect (server->port);

	const size_t length = strlen (request);
	test_check (send (fd, request, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

	return fd;

}

// reads everything that the cerver sends until it closes the connection, and closes it
static void pools_receive (const int fd, char *response) {

	size_t length = 0;

	ssize_t received = 0;
	while (
		(length < (POOLS_RESPONSE_SIZE - 1))
		&& ((received = recv (fd, response + length, POOLS_RESPONSE_SIZE - 1 - length, 0)) > 0)
	) {
		length += (size_t) received;
	}

	response[length] = '\0';

	(void) close (fd);

}

static void pools_request (const PoolsServer *server, const char *request, char *response) {

	pools_receive (pools_send (server, request), response);

}

static void pools_expect_status (const char *response, const char *status) {

	if (strncmp (response, status, strlen (status))) {
		(void) fprintf (stderr, "expected \"%s\", the response is:\n%s\n", status, response);
	}

	test_check_int_eq (strncmp (response, status, strlen (status)), 0, NULL);

}

// sends a request and checks the status of its response
static void pools_expect (const PoolsServer *server, const char *request, const char *status) {

	char response[POOLS_RESPONSE_SIZE];
	pools_request (server, request, response);

	pools_expect_status (response, status);

}

// reads the response of a request that was started and checks its status
static void pools_expect_answer (const int fd, const char *status) {

	char response[POOLS_RESPONSE_SIZE];
	pools_receive (fd, response);

	pools_expect_status (response, status);

}

static unsigned int pools_count (const char *response, const char *text) {

	unsigned int count = 0;

	const size_t length = strlen (text);
	for (const char *found = strstr (response, text); found; found = strstr (found + length, text)) count += 1;

	return count;

}

// the metrics of the cerver have a line, or they do not have it
static void pools_metrics_expect (const PoolsServer *server, const char *line, const bool expected) {

	String *metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check_ptr (metrics);

	const bool found = (strstr (metrics->str, line) != NULL);
	if (found != expected) {
		(void) fprintf (stderr, "\"%s\" %s in the metrics:\n%s\n", line, found ? "is" : "is not", metrics->str);
	}

	test_check_bool_eq (found, expected, NULL);

	str_delete (metrics);

}

// the stats of the admin routes have a text
static void pools_admin_expect (const PoolsServer *server, const char *text) {

	char *stats = http_cerver_admin_generate_routes_stats_json (server->http_cerver);
	test_check_ptr (stats);

	const bool found = (strstr (stats, text) != NULL);
	if (!found) (void) fprintf (stderr, "\"%s\" is not in the stats:\n%s\n", text, stats);

	test_check (found, "the stats of the admin routes do not have the expected text");

	free (stats);

}

// the requests that fill the threads of the slow pool: they work until the gate is opened
static void pools_slow_fill_threads (const PoolsServer *server, int *fds, const unsigned int threads) {

	for (unsigned int idx = 0; idx < threads; idx++) {
		fds[idx] = pools_send (server, pools_slow_block_request);
	}

	pools_gate_wait_for (threads);

}

// the requests that fill the queue of the slow pool behind the ones that work
static void pools_slow_fill_queue (const PoolsServer *server, int *fds, const unsigned int queued) {

	for (unsigned int idx = 0; idx < queued; idx++) {
		fds[idx] = pools_send (server, pools_slow_block_request);
	}

	pools_server_wait (server, pools_slow_queued, queued);

}

#pragma endregion

#pragma region tests

static void pools_none_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

static Cerver *pools_cerver_plain (void) {

	Cerver *cerver = cerver_create_web (pools_cerver_name, (u16) pools_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	return cerver;

}

// a cerver has its default pool, and the pools that are created with a name that no other has
static void test_pools_create (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	const HttpPool *default_pool = http_cerver_pool_get_with_name (http_cerver, HTTP_POOL_DEFAULT_NAME);
	test_check_ptr (default_pool);
	test_check_str_eq (http_pool_get_name (default_pool), HTTP_POOL_DEFAULT_NAME, NULL);
	test_check_unsigned_eq (default_pool->index, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_pools, 1, NULL);

	test_check_null_ptr (http_cerver_pool_create (NULL, pools_slow_name, POOLS_SLOW_THREADS));
	test_check_null_ptr (http_cerver_pool_create (http_cerver, NULL, POOLS_SLOW_THREADS));
	test_check_null_ptr (http_cerver_pool_create (http_cerver, pools_slow_name, 0));
	test_check_null_ptr (http_cerver_pool_create (http_cerver, "", POOLS_SLOW_THREADS));
	test_check_null_ptr (http_cerver_pool_create (http_cerver, pools_words_name, POOLS_SLOW_THREADS));
	test_check_null_ptr (http_cerver_pool_create (http_cerver, pools_long_name, POOLS_SLOW_THREADS));
	test_check_null_ptr (http_cerver_pool_create (http_cerver, HTTP_POOL_DEFAULT_NAME, POOLS_SLOW_THREADS));
	test_check_unsigned_eq (http_cerver->n_pools, 1, NULL);

	HttpPool *slow = http_cerver_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS);
	test_check_ptr (slow);
	test_check_ptr_eq (http_cerver_pool_get_with_name (http_cerver, pools_slow_name), slow);
	test_check_str_eq (http_pool_get_name (slow), pools_slow_name, NULL);
	test_check_unsigned_eq (slow->index, 1, NULL);
	test_check_unsigned_eq (slow->n_threads, POOLS_SLOW_THREADS, NULL);

	// a name has one pool
	test_check_null_ptr (http_cerver_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS));

	test_check_null_ptr (http_cerver_pool_get_with_name (http_cerver, pools_missing_name));
	test_check_null_ptr (http_cerver_pool_get_with_name (http_cerver, NULL));
	test_check_null_ptr (http_cerver_pool_get_with_name (NULL, pools_slow_name));
	test_check_null_ptr (http_pool_get_name (NULL));

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// a pool uses the policy and the wait of its cerver until its own are set
static void test_pools_setters (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	cerver_set_overload_policy (cerver, CERVER_OVERLOAD_POLICY_WAIT);
	cerver_set_overload_wait_timeout (cerver, POOLS_DRAIN_TIMEOUT);

	HttpPool *slow = http_cerver_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS);
	test_check_ptr (slow);

	test_check_int_eq ((int) http_pool_overload_policy (cerver, slow), (int) CERVER_OVERLOAD_POLICY_WAIT, NULL);
	test_check_unsigned_eq (http_pool_wait_timeout (cerver, slow), POOLS_DRAIN_TIMEOUT, NULL);

	http_pool_set_queue_size (slow, POOLS_SET_QUEUE_SIZE);
	test_check_unsigned_eq (slow->queue_size, POOLS_SET_QUEUE_SIZE, NULL);

	http_pool_set_overload_policy (slow, CERVER_OVERLOAD_POLICY_REJECT);
	test_check_int_eq ((int) http_pool_overload_policy (cerver, slow), (int) CERVER_OVERLOAD_POLICY_REJECT, NULL);

	// a value that is not a policy is ignored
	http_pool_set_overload_policy (slow, CERVER_OVERLOAD_POLICY_MAX);
	http_pool_set_overload_policy (slow, CERVER_OVERLOAD_POLICY_NONE);
	test_check_int_eq ((int) http_pool_overload_policy (cerver, slow), (int) CERVER_OVERLOAD_POLICY_REJECT, NULL);

	http_pool_set_wait_timeout (slow, POOLS_SET_WAIT);
	test_check_unsigned_eq (http_pool_wait_timeout (cerver, slow), POOLS_SET_WAIT, NULL);

	// a wait of 0 secs is a value of the pool, not the one of its cerver
	http_pool_set_wait_timeout (slow, 0);
	test_check_unsigned_eq (http_pool_wait_timeout (cerver, slow), 0, NULL);

	// the default pool keeps the values of its cerver
	test_check_int_eq ((int) http_pool_overload_policy (cerver, NULL), (int) CERVER_OVERLOAD_POLICY_WAIT, NULL);
	test_check_unsigned_eq (http_pool_wait_timeout (cerver, NULL), POOLS_DRAIN_TIMEOUT, NULL);

	// nothing is done without a pool
	http_pool_set_queue_size (NULL, POOLS_SET_QUEUE_SIZE);
	http_pool_set_overload_policy (NULL, CERVER_OVERLOAD_POLICY_REJECT);
	http_pool_set_wait_timeout (NULL, POOLS_SET_WAIT);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// a cerver has HTTP_CERVER_POOLS_SIZE pools at most, with its default one
static void test_pools_limit (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	char name[POOLS_NAME_SIZE] = { 0 };
	for (unsigned int idx = 1; idx < HTTP_CERVER_POOLS_SIZE; idx++) {
		(void) snprintf (name, POOLS_NAME_SIZE, "pool-%u", idx);

		const HttpPool *pool = http_cerver_pool_create (http_cerver, name, POOLS_SLOW_THREADS);
		test_check_ptr (pool);
		test_check_unsigned_eq (pool->index, idx, NULL);
	}

	test_check_unsigned_eq (http_cerver->n_pools, HTTP_CERVER_POOLS_SIZE, NULL);

	test_check_null_ptr (http_cerver_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS));
	test_check_unsigned_eq (http_cerver->n_pools, HTTP_CERVER_POOLS_SIZE, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// an endpoint runs in the pool that it chose, or in the one of the nearest of its groups that chose one
static void test_pools_resolve (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	const HttpPool *default_pool = http_cerver_pool_get_with_name (http_cerver, HTTP_POOL_DEFAULT_NAME);
	const HttpPool *slow = http_cerver_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS);
	const HttpPool *fast = http_cerver_pool_create (http_cerver, pools_fast_name, POOLS_SLOW_THREADS);
	test_check_ptr (slow);
	test_check_ptr (fast);

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	HttpGroup *work = http_group_create (api, "work");
	HttpGroup *inner = http_group_create (work, "inner");
	test_check_ptr (inner);

	http_group_set_pool (work, pools_slow_name);

	const HttpEndpoint *plain_endpoint = http_group_get (api, "plain", pools_none_handler);
	const HttpEndpoint *work_endpoint = http_group_get (work, "job", pools_none_handler);
	const HttpEndpoint *inner_endpoint = http_group_get (inner, "job", pools_none_handler);
	HttpEndpoint *fast_endpoint = http_group_get (inner, "fast", pools_none_handler);
	HttpEndpoint *default_endpoint = http_group_get (inner, "default", pools_none_handler);
	HttpEndpoint *none_endpoint = http_group_get (inner, "none", pools_none_handler);
	test_check_ptr (none_endpoint);

	http_endpoint_set_pool (fast_endpoint, pools_fast_name);
	http_endpoint_set_pool (default_endpoint, HTTP_POOL_DEFAULT_NAME);
	http_endpoint_set_pool (none_endpoint, NULL);

	HttpRoute *old_route = http_route_create (REQUEST_METHOD_GET, "old", pools_none_handler);
	HttpRoute *old_plain_route = http_route_create (REQUEST_METHOD_GET, "plain", pools_none_handler);
	test_check_ptr (old_plain_route);
	http_route_set_pool (old_route, pools_slow_name);
	http_cerver_route_register (http_cerver, old_route);
	http_cerver_route_register (http_cerver, old_plain_route);

	// nothing is done without a group, an endpoint or a route
	http_group_set_pool (NULL, pools_slow_name);
	http_endpoint_set_pool (NULL, pools_slow_name);
	http_route_set_pool (NULL, pools_slow_name);

	test_check_unsigned_eq (http_tree_compile (http_cerver->tree), 0, NULL);

	test_check_null_ptr (plain_endpoint->resolved.pool);
	test_check_ptr_eq (work_endpoint->resolved.pool, slow);
	test_check_ptr_eq (inner_endpoint->resolved.pool, slow);
	test_check_ptr_eq (fast_endpoint->resolved.pool, fast);
	test_check_ptr_eq (default_endpoint->resolved.pool, default_pool);
	test_check_null_ptr (none_endpoint->resolved.pool);

	// the endpoints of the routes of the first routes API, in the order of their routes
	const ListElement *old_element = dlist_start (http_cerver->tree->compatibility_endpoints);
	test_check_ptr (old_element);
	test_check_ptr_eq (((const HttpEndpoint *) old_element->data)->resolved.pool, slow);
	test_check_ptr (old_element->next);
	test_check_null_ptr (((const HttpEndpoint *) old_element->next->data)->resolved.pool);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

static void pools_unknown_expect (Cerver *cerver, const char *text) {

	const HttpTree *tree = ((HttpCerver *) cerver->cerver_data)->tree;

	test_check_unsigned_eq (http_tree_compile (((HttpCerver *) cerver->cerver_data)->tree), 1, NULL);

	test_check_unsigned_gt (tree->n_errors, 0);
	test_check_ptr (strstr (tree->error, text));

	// the cerver does not start with a route that has no pool
	test_check_unsigned_ne (cerver_start (cerver), 0);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// a pool that the cerver does not have is an error of its routes: of an endpoint, of a group
// and of a route of the first routes API
static void test_pools_unknown_endpoint (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	http_endpoint_set_pool (http_group_get (api, "job", pools_none_handler), pools_missing_name);

	pools_unknown_expect (cerver, pools_missing_name);

}

static void test_pools_unknown_group (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	http_group_set_pool (api, pools_missing_name);
	test_check_ptr (http_group_get (api, "job", pools_none_handler));

	pools_unknown_expect (cerver, pools_missing_name);

}

static void test_pools_unknown_route (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpRoute *old_route = http_route_create (REQUEST_METHOD_GET, "old", pools_none_handler);
	http_route_set_pool (old_route, pools_missing_name);
	http_cerver_route_register (http_cerver, old_route);

	pools_unknown_expect (cerver, pools_missing_name);

}

// a name that no pool can have is an error when it is set
static void test_pools_unknown_long (void) {

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	test_check_unsigned_eq (http_cerver->tree->n_errors, 0, NULL);

	http_group_set_pool (api, pools_long_name);
	test_check_unsigned_eq (http_cerver->tree->n_errors, 1, NULL);

	http_endpoint_set_pool (http_group_get (api, "job", pools_none_handler), pools_long_name);
	test_check_unsigned_eq (http_cerver->tree->n_errors, 2, NULL);

	HttpRoute *old_route = http_route_create (REQUEST_METHOD_GET, "old", pools_none_handler);
	http_route_set_pool (old_route, pools_long_name);
	http_cerver_route_register (http_cerver, old_route);

	test_check_unsigned_eq (http_tree_compile (http_cerver->tree), 1, NULL);
	test_check_unsigned_eq (http_cerver->tree->n_errors, 3, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// each route runs in the threads of its pool: its handler and its custom authentication
static void test_pools_threads (void) {

	pools_gate_reset ();

	const PoolsOptions options = pools_options_default ();
	PoolsServer *server = pools_server_start (&options);

	test_check_ptr (server->slow->thpool);
	test_check_ptr_ne (server->slow->thpool, server->cerver->thpool);

	pools_expect (server, pools_ok_request, pools_status_ok);
	pools_thread_expect (pools_handler_thread, pools_default_thread);

	pools_expect (server, pools_slow_ok_request, pools_status_ok);
	pools_thread_expect (pools_handler_thread, pools_slow_thread);

	// an endpoint of the group that went back to the default pool
	pools_expect (server, pools_slow_fast_request, pools_status_ok);
	pools_thread_expect (pools_handler_thread, pools_default_thread);

	// a route of the first routes API
	pools_expect (server, pools_old_request, pools_status_ok);
	pools_thread_expect (pools_handler_thread, pools_slow_thread);

	pools_thread_reset ();
	pools_expect (server, pools_slow_private_request, pools_status_ok);
	pools_thread_expect (pools_authentication_thread, pools_slow_thread);
	pools_thread_expect (pools_handler_thread, pools_slow_thread);

	pools_server_stop (server);

}

// sends a request of a connection that stays open and reads its response: its headers,
// and the name of the thread that answered it
static void pools_exchange (const int fd, const char *request, char *response) {

	const size_t request_length = strlen (request);
	test_check (send (fd, request, request_length, MSG_NOSIGNAL) == (ssize_t) request_length, "send () failed");

	size_t length = 0;
	bool complete = false;

	ssize_t received = 0;
	while (
		!complete && (length < (POOLS_RESPONSE_SIZE - 1))
		&& ((received = recv (fd, response + length, POOLS_RESPONSE_SIZE - 1 - length, 0)) > 0)
	) {
		length += (size_t) received;
		response[length] = '\0';

		const char *body = strstr (response, pools_header_end);
		complete = body && strstr (body, pools_thread_prefix);
	}

	test_check (complete, "the response with the name of its thread did not arrive");

}

// the thread that answered a request handles the next one of its connection only when it is of its pool:
// the request of another pool goes back to the loop, that gives it to that pool
static void test_pools_followed (void) {

	pools_gate_reset ();

	PoolsOptions options = pools_options_default ();
	options.follow = POOLS_FOLLOW_TIME * test_time_scale ();
	PoolsServer *server = pools_server_start (&options);

	const HttpCerver *http_cerver = server->http_cerver;

	const int fd = pools_connect (server->port);

	char response[POOLS_RESPONSE_SIZE];

	// the first request of the connection goes through its loop
	pools_exchange (fd, pools_slow_thread_request, response);
	test_check_ptr (strstr (response, pools_slow_thread));
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->n_followed_requests), 0, NULL);

	// the same pool: the thread that looked for it handles it
	pools_exchange (fd, pools_slow_thread_request, response);
	test_check_ptr (strstr (response, pools_slow_thread));
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->n_followed_requests), 1, NULL);

	// the default pool: the thread of the slow pool read it, and a thread of the default pool answers it
	pools_exchange (fd, pools_thread_request, response);
	test_check_ptr (strstr (response, pools_default_thread));
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->n_followed_requests), 1, NULL);

	// the default pool again: its thread handles it
	pools_exchange (fd, pools_thread_request, response);
	test_check_ptr (strstr (response, pools_default_thread));
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->n_followed_requests), POOLS_FOLLOWED_REQUESTS, NULL);

	// and back to the slow pool
	pools_exchange (fd, pools_slow_thread_request, response);
	test_check_ptr (strstr (response, pools_slow_thread));
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->n_followed_requests), POOLS_FOLLOWED_REQUESTS, NULL);

	pools_metrics_expect (server, "cerver_http_followed_requests_total 2\n", true);

	(void) close (fd);

	pools_server_stop (server);

}

// a connection that is kept gives each of its requests to the pool of its route:
// the pool is chosen for a request, not for its connection
static void test_pools_kept (void) {

	pools_gate_reset ();

	const PoolsOptions options = pools_options_default ();
	PoolsServer *server = pools_server_start (&options);

	char response[POOLS_RESPONSE_SIZE];
	pools_request (server, pools_kept_requests, response);

	test_check_unsigned_eq (pools_count (response, pools_status_ok), POOLS_KEPT_REQUESTS, NULL);
	test_check_unsigned_eq (pools_count (response, pools_slow_thread), POOLS_KEPT_REQUESTS - 1, NULL);
	test_check_unsigned_eq (pools_count (response, pools_default_thread), 1, NULL);

	// in the order of the requests: the slow pool, the default one, the slow pool
	const char *first = strstr (response, pools_slow_thread);
	const char *middle = strstr (response, pools_default_thread);
	test_check_ptr (first);
	test_check_ptr (middle);
	test_check (first < middle, "the first request was not answered by the slow pool");
	test_check_ptr (strstr (middle, pools_slow_thread));

	// two connections ended: the one that waited for the cerver with one request, and this one with three
	pools_server_wait (server, pools_connection_requests, POOLS_KEPT_REQUESTS + 1);

	const HttpCerver *http_cerver = server->http_cerver;
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->connection_requests[POOLS_SINGLE_BUCKET]), 1, NULL);
	test_check_unsigned_eq (CERVER_STATS_GET (http_cerver->connection_requests[POOLS_KEPT_BUCKET]), 1, NULL);

	pools_metrics_expect (server, "cerver_http_connection_requests_bucket{le=\"1\"} 1\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_bucket{le=\"2\"} 1\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_bucket{le=\"4\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_bucket{le=\"1024\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_bucket{le=\"+Inf\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_sum 4\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_count 2\n", true);

	// each pool has the time that its requests waited for a thread and the time that they took
	pools_server_wait (server, pools_slow_completed, POOLS_KEPT_REQUESTS - 1);
	pools_server_wait (server, pools_default_completed, 2);

	pools_metrics_expect (server, "cerver_http_pool_wait_seconds_bucket{pool=\"slow\",le=\"+Inf\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_wait_seconds_count{pool=\"slow\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_run_seconds_bucket{pool=\"slow\",le=\"+Inf\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_run_seconds_count{pool=\"slow\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_run_seconds_sum{pool=\"slow\"} ", true);
	pools_metrics_expect (server, "cerver_http_pool_run_seconds_count{pool=\"default\"} 2\n", true);

	// no upload was written
	pools_metrics_expect (server, "cerver_http_uploads_write_seconds_total 0.000000\n", true);
	pools_metrics_expect (server, "cerver_http_uploads_write_bytes_total 0\n", true);

	// the stats of the admin routes have them too
	pools_admin_expect (server, "\"name\": \"slow\"");
	pools_admin_expect (server, "\"n_ended\": 2");
	pools_admin_expect (server, "\"n_requests\": 4");
	pools_admin_expect (server, "\"n_reused\": 2");
	pools_admin_expect (server, "\"uploads_write_bytes\": 0");

	pools_server_stop (server);

}

// a pool that is full rejects its requests and the other pools answer theirs: the slow handlers
// do not keep the threads of the fast ones; its policy is its own, the cerver waits by default
static void test_pools_isolation (void) {

	pools_gate_reset ();

	PoolsOptions options = pools_options_default ();
	options.slow_policy = CERVER_OVERLOAD_POLICY_REJECT;
	PoolsServer *server = pools_server_start (&options);

	int fds[POOLS_SLOW_PLACES];
	pools_slow_fill_threads (server, fds, POOLS_SLOW_THREADS);
	pools_slow_fill_queue (server, fds + POOLS_SLOW_THREADS, POOLS_SLOW_QUEUE_SIZE);

	// the slow pool has no place: its next request is answered with 503 at once
	pools_expect (server, pools_slow_ok_request, pools_status_unavailable);
	test_check_unsigned_eq (__atomic_load_n (&server->slow->n_unavailable, __ATOMIC_ACQUIRE), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&server->slow->n_parked, __ATOMIC_ACQUIRE), 0, NULL);

	// the default pool has its threads
	pools_expect (server, pools_ok_request, pools_status_ok);
	pools_expect (server, pools_slow_fast_request, pools_status_ok);

	// the cerver is alive, and it can not take every request
	pools_expect (server, pools_health_request, pools_status_ok);

	char response[POOLS_RESPONSE_SIZE];
	pools_request (server, pools_ready_request, response);
	pools_expect_status (response, pools_status_unavailable);
	test_check_ptr (strstr (response, pools_retry_after));

	// the metrics tell which pool is full
	pools_metrics_expect (server, "cerver_http_pool_threads{pool=\"slow\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_threads_working{pool=\"slow\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_queued{pool=\"slow\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_parked{pool=\"slow\"} 0\n", true);
	pools_metrics_expect (server, "cerver_http_pool_unavailable_total{pool=\"slow\"} 1\n", true);
	pools_metrics_expect (server, "cerver_http_pool_threads{pool=\"default\"} 4\n", true);
	pools_metrics_expect (server, "cerver_http_pool_queued{pool=\"default\"} 0\n", true);
	pools_metrics_expect (server, "cerver_http_pool_unavailable_total{pool=\"default\"} 0\n", true);
	pools_metrics_expect (server, "cerver_http_health_requests_total 1\n", true);
	pools_metrics_expect (server, "cerver_http_ready_requests_total 0\n", true);
	pools_metrics_expect (server, "cerver_http_not_ready_requests_total 1\n", true);
	pools_metrics_expect (server, "cerver_http_connections_accepted_total ", true);
	pools_metrics_expect (server, "cerver_http_connections_idle 0\n", true);

	pools_gate_open ();

	for (unsigned int idx = 0; idx < POOLS_SLOW_PLACES; idx++) pools_expect_answer (fds[idx], pools_status_ok);

	pools_server_wait (server, pools_ready, 1);
	pools_expect (server, pools_ready_request, pools_status_ok);
	pools_expect (server, pools_slow_ok_request, pools_status_ok);

	pools_server_stop (server);

}

// the requests of a pool that is full wait for a place in its lane of the loop, for the time of the pool:
// they do not wait in the lane of the default pool, and the cerver of the test rejects by default
static void test_pools_wait (void) {

	pools_gate_reset ();

	PoolsOptions options = pools_options_default ();
	options.policy = CERVER_OVERLOAD_POLICY_REJECT;
	options.slow_threads = POOLS_SINGLE_THREADS;
	options.slow_queue_size = POOLS_SINGLE_QUEUE_SIZE;
	options.slow_policy = CERVER_OVERLOAD_POLICY_WAIT;
	options.slow_wait = POOLS_SLOW_WAIT;
	PoolsServer *server = pools_server_start (&options);

	int fds[POOLS_SINGLE_PLACES];
	pools_slow_fill_threads (server, fds, POOLS_SINGLE_THREADS);
	pools_slow_fill_queue (server, fds + POOLS_SINGLE_THREADS, POOLS_SINGLE_QUEUE_SIZE);

	// the next request waits in the lane of the slow pool
	const int late_fd = pools_send (server, pools_slow_block_request);
	pools_server_wait (server, pools_slow_parked, 1);
	test_check_unsigned_eq (event_loop_group_lane_parked (server->cerver, EVENT_LOOP_LANE_DEFAULT), 0, NULL);
	test_check_unsigned_eq (event_loop_group_parked (server->cerver), 1, NULL);

	// the default pool answers while that request waits
	pools_expect (server, pools_ok_request, pools_status_ok);

	pools_metrics_expect (server, "cerver_http_pool_parked{pool=\"slow\"} 1\n", true);
	pools_metrics_expect (server, "cerver_http_pool_parked{pool=\"default\"} 0\n", true);
	pools_metrics_expect (server, "cerver_http_pool_parked_total{pool=\"slow\"} 1\n", true);

	// no place in the time of the pool
	pools_expect_answer (late_fd, pools_status_unavailable);
	test_check_unsigned_eq (pools_slow_parked (server), 0, NULL);
	test_check_unsigned_eq (__atomic_load_n (&server->slow->n_parked, __ATOMIC_ACQUIRE), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&server->slow->n_unavailable, __ATOMIC_ACQUIRE), 1, NULL);

	// a request that waits takes the place that the pool frees
	const int next_fd = pools_send (server, pools_slow_ok_request);
	pools_server_wait (server, pools_slow_parked, 1);

	pools_gate_open ();

	for (unsigned int idx = 0; idx < POOLS_SINGLE_PLACES; idx++) pools_expect_answer (fds[idx], pools_status_ok);

	pools_expect_answer (next_fd, pools_status_ok);
	test_check_unsigned_eq (__atomic_load_n (&server->slow->n_parked, __ATOMIC_ACQUIRE), 2, NULL);
	test_check_unsigned_eq (__atomic_load_n (&server->slow->n_unavailable, __ATOMIC_ACQUIRE), 1, NULL);

	// the time that each one waited: the one whose wait ended waited the time of the pool
	pools_metrics_expect (server, "cerver_http_pool_parked_seconds_count{pool=\"slow\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_parked_seconds_bucket{pool=\"slow\",le=\"+Inf\"} 2\n", true);
	pools_metrics_expect (server, "cerver_http_pool_parked_seconds_count{pool=\"default\"} 0\n", true);
	test_check (
		__atomic_load_n (&server->slow->parked_total_microseconds, __ATOMIC_ACQUIRE)
			>= ((u64) POOLS_SLOW_WAIT * THPOOL_MICROSECONDS_PER_SECOND),
		"the time that the requests waited is less than the wait of the pool"
	);

	pools_admin_expect (server, "\"n_parked\": 2");

	pools_server_stop (server);

}

// stops a cerver and tears it down: the teardown drains its pools, so it waits for the requests that run
static void *pools_stop_thread (void *server_ptr) {

	pools_server_stop ((PoolsServer *) server_ptr);

	return NULL;

}

// waits until the cerver was asked to stop: it is not ready from then on
static void pools_wait_stopping (const PoolsServer *server) {

	const double deadline = test_wait_now () + (double) (POOLS_WAIT_TIMEOUT * test_time_scale ());
	while (http_cerver_is_ready (server->http_cerver) && (test_wait_now () < deadline)) pools_pause ();

	test_check_bool_eq (http_cerver_is_ready (server->http_cerver), false, NULL);

}

// a cerver that stops ends the requests of every pool, the ones that run and the ones that wait in a queue,
// and it is not ready from then on
static void test_pools_drain (void) {

	pools_gate_reset ();

	const PoolsOptions options = pools_options_default ();
	PoolsServer *server = pools_server_start (&options);

	test_check_bool_eq (http_cerver_is_ready (server->http_cerver), true, NULL);

	int fds[POOLS_SLOW_THREADS];
	pools_slow_fill_threads (server, fds, POOLS_SLOW_THREADS);

	const int default_fd = pools_send (server, pools_block_request);
	pools_gate_wait_for (POOLS_SLOW_THREADS + 1);

	int queued_fd = -1;
	pools_slow_fill_queue (server, &queued_fd, 1);

	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, pools_stop_thread, server), 0, NULL);

	pools_wait_stopping (server);

	pools_gate_open ();

	pools_expect_answer (default_fd, pools_status_ok);
	pools_expect_answer (queued_fd, pools_status_ok);

	for (unsigned int idx = 0; idx < POOLS_SLOW_THREADS; idx++) pools_expect_answer (fds[idx], pools_status_ok);

	test_check_int_eq (pthread_join (thread_id, NULL), 0, NULL);

}

// a cerver that was asked to stop is not ready, although the threads of its pools still take requests:
// it waits for the request that it is reading, the one of the ready route, and answers it with 503
static void test_pools_ready_stopping (void) {

	pools_gate_reset ();

	const PoolsOptions options = pools_options_default ();
	PoolsServer *server = pools_server_start (&options);

	// the connection that waited for the cerver ended, and the loop has the one of the request
	// before the cerver is asked to stop
	pools_server_wait (server, pools_loop_connections, 0);

	const int fd = pools_send (server, pools_ready_start);
	pools_server_wait (server, pools_loop_connections, 1);

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	const size_t length = strlen (pools_ready_end);
	test_check (send (fd, pools_ready_end, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

	char response[POOLS_RESPONSE_SIZE];
	pools_receive (fd, response);

	pools_expect_status (response, pools_status_unavailable);
	test_check_ptr (strstr (response, pools_retry_after));

	pools_server_join (server);

	pools_server_end (server);

}

// a cerver that stops with the reject policy drops the requests that wait in the queue of every pool,
// also in the one of a named pool: they are answered with 503, and the ones that run end
static void test_pools_drain_reject (void) {

	pools_gate_reset ();

	PoolsOptions options = pools_options_default ();
	options.drain_policy = CERVER_DRAIN_POLICY_REJECT;
	PoolsServer *server = pools_server_start (&options);

	int fds[POOLS_SLOW_THREADS];
	pools_slow_fill_threads (server, fds, POOLS_SLOW_THREADS);

	int queued_fd = -1;
	pools_slow_fill_queue (server, &queued_fd, 1);

	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, pools_stop_thread, server), 0, NULL);

	// the request that waited for a thread is answered while the ones that run still do
	pools_expect_answer (queued_fd, pools_status_unavailable);

	pools_gate_open ();

	for (unsigned int idx = 0; idx < POOLS_SLOW_THREADS; idx++) pools_expect_answer (fds[idx], pools_status_ok);

	test_check_int_eq (pthread_join (thread_id, NULL), 0, NULL);

}

// without an event loop the named pools do not start: the thread of a connection handles its requests
static void test_pools_without_events (void) {

	pools_gate_reset ();

	PoolsOptions options = pools_options_default ();
	options.handler_type = CERVER_HANDLER_TYPE_THREADS;
	PoolsServer *server = pools_server_start (&options);

	test_check_null_ptr (server->slow->thpool);
	test_check_unsigned_eq (server->http_cerver->n_pools, 2, NULL);

	pools_expect (server, pools_slow_ok_request, pools_status_ok);
	pools_thread_expect (pools_handler_thread, pools_default_thread);

	pools_expect (server, pools_old_request, pools_status_ok);
	pools_thread_expect (pools_handler_thread, pools_default_thread);

	pools_expect (server, pools_health_request, pools_status_ok);
	pools_expect (server, pools_ready_request, pools_status_ok);

	// the metrics only have the pools with threads, and no connections of event loops
	pools_metrics_expect (server, "cerver_http_pool_threads{pool=\"default\"} 4\n", true);
	pools_metrics_expect (server, "{pool=\"slow\"}", false);
	pools_metrics_expect (server, "cerver_http_connections ", false);
	pools_metrics_expect (server, "cerver_http_health_requests_total 1\n", true);
	pools_metrics_expect (server, "cerver_http_ready_requests_total 1\n", true);

	// the connections of the threads are counted when they end: each one handled one request
	pools_server_wait (server, pools_connection_requests, POOLS_WITHOUT_EVENTS_CONNECTIONS);

	test_check_unsigned_eq (
		CERVER_STATS_GET (server->http_cerver->connection_requests[POOLS_SINGLE_BUCKET]),
		POOLS_WITHOUT_EVENTS_CONNECTIONS, NULL
	);

	pools_metrics_expect (server, "cerver_http_connection_requests_count 5\n", true);
	pools_metrics_expect (server, "cerver_http_connection_requests_sum 5\n", true);

	// the stats of the admin routes: one pool, and no connections of event loops
	pools_admin_expect (server, "\"name\": \"default\"");
	pools_admin_expect (server, "\"n_health_requests\": 1");
	pools_admin_expect (server, "\"n_ended\": 5");

	pools_server_stop (server);

}

// the health and the ready routes: a GET or a HEAD of their exact path, answered by cerver
static void test_pools_health (void) {

	pools_gate_reset ();

	const PoolsOptions options = pools_options_default ();
	PoolsServer *server = pools_server_start (&options);

	char response[POOLS_RESPONSE_SIZE];

	pools_expect (server, pools_health_request, pools_status_ok);
	pools_expect (server, pools_health_query_request, pools_status_ok);

	// a HEAD has no body
	pools_request (server, pools_health_head_request, response);
	pools_expect_status (response, pools_status_ok);
	test_check_ptr (strstr (response, pools_header_end));
	test_check_str_eq (strstr (response, pools_header_end), pools_header_end, NULL);

	// another method and another path are requests like any other
	pools_expect (server, pools_health_post_request, pools_status_not_found);
	pools_expect (server, pools_health_slash_request, pools_status_not_found);

	pools_expect (server, pools_ready_request, pools_status_ok);

	const HttpCerver *http_cerver = server->http_cerver;
	test_check_unsigned_eq (__atomic_load_n (&http_cerver->n_health_requests, __ATOMIC_RELAXED), POOLS_HEALTH_REQUESTS, NULL);
	test_check_unsigned_eq (__atomic_load_n (&http_cerver->n_ready_requests, __ATOMIC_RELAXED), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&http_cerver->n_not_ready_requests, __ATOMIC_RELAXED), 0, NULL);

	pools_server_stop (server);

}

// the routes are removed with a NULL path
static void test_pools_health_removed (void) {

	pools_gate_reset ();

	Cerver *cerver = pools_cerver_plain ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	// a cerver that has not started can take requests, one that does not exist can not
	test_check_bool_eq (http_cerver_is_ready (http_cerver), true, NULL);
	test_check_bool_eq (http_cerver_is_ready (NULL), false, NULL);

	http_cerver_set_health_route (http_cerver, pools_health_route);
	http_cerver_set_ready_route (http_cerver, pools_ready_route);
	test_check_str_eq (http_cerver->health_route->str, pools_health_route, NULL);
	test_check_str_eq (http_cerver->ready_route->str, pools_ready_route, NULL);

	http_cerver_set_health_route (http_cerver, pools_ready_route);
	test_check_str_eq (http_cerver->health_route->str, pools_ready_route, NULL);

	http_cerver_set_health_route (http_cerver, NULL);
	http_cerver_set_ready_route (http_cerver, NULL);
	test_check_null_ptr (http_cerver->health_route);
	test_check_null_ptr (http_cerver->ready_route);

	// nothing is done without a cerver
	http_cerver_set_health_route (NULL, pools_health_route);
	http_cerver_set_ready_route (NULL, pools_ready_route);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// an event loop answers the health route by itself: with every thread of every pool busy and their
// queues full, the requests of the routes wait and the health route is answered, in a connection that is kept
static void test_pools_health_busy (void) {

	pools_gate_reset ();

	PoolsOptions options = pools_options_default ();
	options.threads = POOLS_SINGLE_THREADS;
	options.queue_size = POOLS_SINGLE_QUEUE_SIZE;
	options.slow_threads = POOLS_SINGLE_THREADS;
	options.slow_queue_size = POOLS_SINGLE_QUEUE_SIZE;
	PoolsServer *server = pools_server_start (&options);

	int slow_fds[POOLS_SINGLE_PLACES];
	pools_slow_fill_threads (server, slow_fds, POOLS_SINGLE_THREADS);
	pools_slow_fill_queue (server, slow_fds + POOLS_SINGLE_THREADS, POOLS_SINGLE_QUEUE_SIZE);

	int fds[POOLS_SINGLE_PLACES];
	fds[0] = pools_send (server, pools_block_request);
	pools_gate_wait_for (POOLS_SINGLE_THREADS + 1);

	for (unsigned int idx = POOLS_SINGLE_THREADS; idx < POOLS_SINGLE_PLACES; idx++) {
		fds[idx] = pools_send (server, pools_block_request);
	}

	pools_server_wait (server, pools_default_queued, POOLS_SINGLE_QUEUE_SIZE);

	char response[POOLS_RESPONSE_SIZE];
	pools_request (server, pools_health_kept_requests, response);
	test_check_unsigned_eq (pools_count (response, pools_status_ok), POOLS_KEPT_REQUESTS, NULL);

	pools_request (server, pools_ready_request, response);
	pools_expect_status (response, pools_status_unavailable);
	test_check_ptr (strstr (response, pools_retry_after));

	test_check_unsigned_eq (
		__atomic_load_n (&server->http_cerver->n_not_ready_requests, __ATOMIC_RELAXED), 1, NULL
	);

	pools_gate_open ();

	for (unsigned int idx = 0; idx < POOLS_SINGLE_PLACES; idx++) {
		pools_expect_answer (slow_fds[idx], pools_status_ok);
		pools_expect_answer (fds[idx], pools_status_ok);
	}

	pools_server_wait (server, pools_ready, 1);
	pools_expect (server, pools_ready_request, pools_status_ok);

	pools_server_stop (server);

}

// the tests of the routes that cerver answers, for each way of handling the connections
static void pools_tests_model (const CerverHandlerType handler_type) {

	pools_handler_type = handler_type;

	(void) printf ("%s\n", cerver_handler_type_to_string (handler_type));

	// a hang fails the suite instead of blocking it forever
	(void) alarm (POOLS_ALARM * test_time_scale ());

	test_run (test_pools_health);

	(void) alarm (0);

	pools_handler_type = CERVER_HANDLER_TYPE_THREADS;

}

// the tests of the pools, that only start with event loops
static void pools_tests_events (void) {

	pools_handler_type = CERVER_HANDLER_TYPE_EVENTS;

	(void) alarm (POOLS_ALARM * test_time_scale ());

	test_run (test_pools_threads);
	test_run (test_pools_kept);
	test_run (test_pools_followed);
	test_run (test_pools_isolation);
	test_run (test_pools_wait);
	test_run (test_pools_drain);
	test_run (test_pools_ready_stopping);
	test_run (test_pools_drain_reject);
	test_run (test_pools_health_busy);

	(void) alarm (0);

	pools_handler_type = CERVER_HANDLER_TYPE_THREADS;

}

void http_tests_pools (void) {

	(void) printf ("Testing HTTP pools...\n");

	(void) alarm (POOLS_ALARM * test_time_scale ());

	test_run (test_pools_create);
	test_run (test_pools_setters);
	test_run (test_pools_limit);
	test_run (test_pools_resolve);
	test_run (test_pools_unknown_endpoint);
	test_run (test_pools_unknown_group);
	test_run (test_pools_unknown_route);
	test_run (test_pools_unknown_long);
	test_run (test_pools_health_removed);
	test_run (test_pools_without_events);

	(void) alarm (0);

	pools_tests_events ();

	pools_tests_model (CERVER_HANDLER_TYPE_THREADS);
	pools_tests_model (CERVER_HANDLER_TYPE_EVENTS);

	(void) printf ("Done!\n");

}
