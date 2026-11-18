#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/data.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/core/types.h>
#include <cerver/events/events.h>
#include "cerver/events/internal.h"
#include <cerver/handler/events.h>
#include <cerver/network/network.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/headers/types.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

#include "../handler.h"
#include "../test.h"
#include "http.h"

// runs a real web cerver with event loops in a thread and talks to it with raw sockets, to test the handlers
// that run in the event loop itself (the pool HTTP_POOL_LOOP_NAME) and the requests of a pipeline that the
// loop handles in a row

// what this suite pins:
// - every request crossed from its loop to a pool of threads and back; a handler that never waits runs in the
//   loop that read its request, chosen by its endpoint, one of its groups, its route or its cerver
// - each request of a pipeline cost a step of the loop and a send; the ones that the loop handles end when
//   their response is written, and their responses leave in one send
// - the responses that the loop wrote must not wait for anything else: a request of the pipeline that is not
//   complete, or one that goes to a pool of threads

#define LOOP_PORT_BASE						((1 << 13) + (1 << 11))
#define LOOP_ADDRESS						"127.0.0.1"

#define LOOP_CONNECT_RETRIES				(1 << 8)
// 2^22 ns
#define LOOP_PAUSE_INTERVAL					4194304L

// secs that a client waits for a response
#define LOOP_RECEIVE_TIMEOUT				16

// secs that a cerver has to stop
#define LOOP_STOP_TIMEOUT					(1 << 4)
#define LOOP_DRAIN_TIMEOUT					(1 << 3)

// secs that the test waits for a state of its cerver
#define LOOP_WAIT_TIMEOUT					(1 << 4)

#define LOOP_ALARM							256

#define LOOP_THREADS						2
#define LOOP_QUEUE_SIZE						(1 << 4)

#define LOOP_THREAD_NAME_SIZE				(1 << 4)

// what a client keeps of the responses that it has not read
#define LOOP_BUFFER_SIZE					(1 << 15)
// the body of a response that a test checks
#define LOOP_BODY_SIZE						(1 << 6)

// the requests of a pipeline that a client writes at once
#define LOOP_PIPELINE_SIZE					(1 << 12)

// a pipeline that the loop handles in one batch, and its only send
#define LOOP_PIPELINE_REQUESTS				(1 << 4)
#define LOOP_PIPELINE_SENDS					1

// a pipeline of more requests than a batch: the first batch and the request that ends it leave in one send,
// the rest in another one
#define LOOP_PIPELINE_LONG_REQUESTS			40
#define LOOP_PIPELINE_LONG_SENDS			2

// a request of the loop, one of a pool of threads and one of the loop: a send for each one
#define LOOP_PIPELINE_MIXED_SENDS			3

// requests of the loop whose responses are joined in one part, and then one of a pool of threads: a send for
// the joined responses and one for the response of the pool. The responses joined after the first one have more
// bytes than the one of the pool
#define LOOP_PIPELINE_JOINED_ANSWERS		4
#define LOOP_PIPELINE_JOINED_SENDS			2

// two pipelines of one connection, that together have more requests than a batch: a send for each one
#define LOOP_PIPELINE_AGAIN_REQUESTS		20
#define LOOP_PIPELINE_AGAIN_SENDS			2

// the requests of a pipeline whose second one closes the connection, and the ones that are answered
#define LOOP_PIPELINE_CLOSED_ANSWERS		2

// the complete requests of a pipeline that ends in the middle of a request
#define LOOP_PIPELINE_PARTIAL_ANSWERS		2

// the requests of the pipeline of a client that resets its connection before any response is sent
#define LOOP_PIPELINE_RESET_REQUESTS		4

// the requests of a pipeline that no endpoint handles: cerver answers each one in the loop, and its response
// leaves before the next one is read, a send for each one
#define LOOP_PIPELINE_SHORT_REQUESTS		4

// the requests of a pipeline whose cerver stops while the loop handles the first one, and the ones answered
#define LOOP_PIPELINE_STOPPING_REQUESTS		4
#define LOOP_PIPELINE_STOPPING_ANSWERS		1

// the body of the response that does not fit in a socket: 2^22 bytes
#define LOOP_BIG_SIZE						((size_t) 1 << 22)
#define LOOP_BIG_LETTERS					26
#define LOOP_BIG_CHUNK						(1 << 14)

// the file that a response sends, the first letters of the big body: it fits in the body that a test checks
#define LOOP_FILE_SIZE						LOOP_BIG_LETTERS
#define LOOP_FILE_PATH_SIZE					(1 << 6)

#define LOOP_STATUS_BASE					10

// the connections for each event loop of the cerver that has one for each processor
#define LOOP_PER_CORE_CONNECTIONS			4

static const char *loop_cerver_name = { "loop" };

static const char *loop_file_template = { "/tmp/cerver-loop-XXXXXX" };
static const char *loop_missing_name = { "missing" };

// how the name of the thread of an event loop starts, and the one of a thread of a pool
static const char *loop_events_thread = { "events-" };
static const char *loop_pool_thread = { "thpool-" };

static const char *loop_status_prefix = { "HTTP/1.1 " };
static const char *loop_header_end = { "\r\n\r\n" };
static const char *loop_content_length = { "Content-Length: " };

static const char *loop_valid_authorization = { "valid" };

static const char *loop_thread_request = { "GET /loop/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_thread_close_request = {
	"GET /loop/thread HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *loop_pool_request = { "GET /loop/pool/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_close_request = {
	"GET /loop/pool/thread HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};
static const char *loop_pool_gate_request = { "GET /loop/pool/gate HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_explicit_request = { "GET /explicit/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_endpoint_request = { "GET /plain/chosen HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_plain_request = { "GET /plain/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_old_request = { "GET /old HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_private_request = {
	"GET /loop/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\n\r\n"
};

static const char *loop_private_bad_request = {
	"GET /loop/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\n\r\n"
};

// with a body: the parser pauses when the headers are complete, to authenticate before the body is received
static const char *loop_upload_request = {
	"POST /loop/upload HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\nContent-Length: 4\r\n\r\nbody"
};

static const char *loop_upload_bad_request = {
	"POST /loop/upload HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\nContent-Length: 4\r\n\r\nbody"
};

static const char *loop_count_request = { "GET /loop/count HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_count_close_request = {
	"GET /loop/count HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *loop_gate_request = { "GET /loop/gate HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// a path that no endpoint handles
static const char *loop_missing_request = { "GET /loop/missing HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_missing_close_request = {
	"GET /loop/missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *loop_big_request = { "GET /loop/big HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" };

static const char *loop_file_request = { "GET /loop/file HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// a request in two pieces
static const char *loop_count_start = { "GET /loop/count HTTP/1.1\r\nHo" };
static const char *loop_count_end = { "st: localhost\r\nConnection: close\r\n\r\n" };

static const char *loop_processors_request = { "GET /loop/processors HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static unsigned int loop_port = LOOP_PORT_BASE;

// the cervers of the test that runs now have an event loop for each processor, each one in its own
static bool loop_per_core = false;

#pragma region routes

// the requests that the count and the gate routes handled, added by the threads of the cervers
static unsigned int loop_handled = 0;

// the endpoint GET /loop/count of the last cerver: its stats count the requests that ended
static HttpEndpoint *loop_count_endpoint = NULL;

// the requests of the last cerver that its loops ended, added by their observer
static unsigned int loop_observed = 0;

// the name of the thread that ran the last authentication
static pthread_mutex_t loop_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static char loop_authentication_thread[LOOP_THREAD_NAME_SIZE] = { 0 };

// keeps the gate requests working until it is opened
static pthread_mutex_t loop_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t loop_gate_cond = PTHREAD_COND_INITIALIZER;
static bool loop_gate_is_open = false;
static unsigned int loop_gate_waiting = 0;

// the body of the big response, the letters in order
static char *loop_big_body = NULL;

// the file that GET /loop/file sends, created by its test
static char loop_file_path[LOOP_FILE_PATH_SIZE] = { 0 };

static void loop_pause (void) {

	struct timespec interval = { 0, LOOP_PAUSE_INTERVAL };
	(void) nanosleep (&interval, NULL);

}

static void loop_gate_reset (void) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	loop_gate_is_open = false;
	loop_gate_waiting = 0;
	(void) pthread_mutex_unlock (&loop_gate_mutex);

}

static void loop_gate_open (void) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	loop_gate_is_open = true;
	(void) pthread_cond_broadcast (&loop_gate_cond);
	(void) pthread_mutex_unlock (&loop_gate_mutex);

}

static unsigned int loop_gate_count (void) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	const unsigned int waiting = loop_gate_waiting;
	(void) pthread_mutex_unlock (&loop_gate_mutex);

	return waiting;

}

static void loop_gate_wait_for (const unsigned int waiting) {

	const double deadline = test_wait_now () + (double) (LOOP_WAIT_TIMEOUT * test_time_scale ());
	while ((loop_gate_count () < waiting) && (test_wait_now () < deadline)) loop_pause ();

	test_check_unsigned_eq (loop_gate_count (), waiting, NULL);

}

static void loop_authentication_thread_reset (void) {

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memset (loop_authentication_thread, 0, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

}

static void loop_authentication_thread_expect (const char *prefix) {

	char saved[LOOP_THREAD_NAME_SIZE] = { 0 };

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memcpy (saved, loop_authentication_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	if (strncmp (saved, prefix, strlen (prefix))) {
		(void) fprintf (stderr, "the thread \"%s\" is not one of \"%s\"\n", saved, prefix);
	}

	test_check_int_eq (strncmp (saved, prefix, strlen (prefix)), 0, NULL);

}

// answers with the name of the thread that handles the request
static void loop_thread_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char name[LOOP_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, LOOP_THREAD_NAME_SIZE);

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, name, strlen (name));

}

// the processors that the thread of the caller can run in, 0 when they can not be read
static unsigned int loop_processors (void) {

	cpu_set_t processors;
	CPU_ZERO (&processors);

	return !sched_getaffinity (0, sizeof (cpu_set_t), &processors) ? (unsigned int) CPU_COUNT (&processors) : 0;

}

// answers with the name of the thread that handles the request and the processors that it can run in
static void loop_processors_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char name[LOOP_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, LOOP_THREAD_NAME_SIZE);

	char text[LOOP_BODY_SIZE] = { 0 };
	const int length = snprintf (text, LOOP_BODY_SIZE, "%s %u", name, loop_processors ());

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, text, (size_t) length);

}

static void loop_count_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	test_counter_add (&loop_handled);

	(void) http_response_send (oki_doki, http_receive);

}

// answers when the gate is opened: only a test keeps a loop waiting
static void loop_gate_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) pthread_mutex_lock (&loop_gate_mutex);
	loop_gate_waiting += 1;
	while (!loop_gate_is_open) (void) pthread_cond_wait (&loop_gate_cond, &loop_gate_mutex);
	(void) pthread_mutex_unlock (&loop_gate_mutex);

	test_counter_add (&loop_handled);

	(void) http_response_send (oki_doki, http_receive);

}

static void loop_big_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, loop_big_body, LOOP_BIG_SIZE);

}

// its headers are written as bytes and its body as a file, that the loop sends after them
static void loop_file_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send_file (http_receive, HTTP_STATUS_OK, loop_file_path);

}

// only with the valid authorization, and tells which thread checked it
static unsigned int loop_authentication_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) pthread_getname_np (pthread_self (), loop_authentication_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);

	return (authorization && !strcmp (authorization->str, loop_valid_authorization)) ? 0 : 1;

}

// the group /loop chooses no pool, its requests run where the cerver says (GET /loop/private and
// POST /loop/upload have a custom authentication); its group /loop/pool goes to the thpool of the cerver;
// the group /explicit chooses the pool of the event loops;
// the group /plain chooses no pool, and its endpoint /plain/chosen the one of the event loops;
// GET /old, a route of the first routes API in the pool of the event loops
static void loop_routes (HttpCerver *http_cerver) {

	HttpGroup *group = http_cerver_group_create (http_cerver, "loop");
	test_check_ptr (group);

	test_check_ptr (http_group_get (group, "thread", loop_thread_handler));

	loop_count_endpoint = http_group_get (group, "count", loop_count_handler);
	test_check_ptr (loop_count_endpoint);

	test_check_ptr (http_group_get (group, "gate", loop_gate_handler));
	test_check_ptr (http_group_get (group, "big", loop_big_handler));
	test_check_ptr (http_group_get (group, "file", loop_file_handler));
	test_check_ptr (http_group_get (group, "processors", loop_processors_handler));

	HttpEndpoint *private_endpoint = http_group_get (group, "private", loop_thread_handler);
	test_check_ptr (private_endpoint);
	http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (private_endpoint, loop_authentication_handler);

	HttpEndpoint *upload_endpoint = http_group_post (group, "upload", loop_thread_handler);
	test_check_ptr (upload_endpoint);
	http_endpoint_set_auth (upload_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (upload_endpoint, loop_authentication_handler);

	HttpGroup *pool = http_group_create (group, "pool");
	test_check_ptr (pool);
	http_group_set_pool (pool, HTTP_POOL_DEFAULT_NAME);

	test_check_ptr (http_group_get (pool, "thread", loop_thread_handler));
	test_check_ptr (http_group_get (pool, "gate", loop_gate_handler));

	HttpGroup *explicit_group = http_cerver_group_create (http_cerver, "explicit");
	test_check_ptr (explicit_group);
	http_group_set_pool (explicit_group, HTTP_POOL_LOOP_NAME);

	test_check_ptr (http_group_get (explicit_group, "thread", loop_thread_handler));

	HttpGroup *plain = http_cerver_group_create (http_cerver, "plain");
	test_check_ptr (plain);

	test_check_ptr (http_group_get (plain, "thread", loop_thread_handler));

	HttpEndpoint *chosen = http_group_get (plain, "chosen", loop_thread_handler);
	test_check_ptr (chosen);
	http_endpoint_set_pool (chosen, HTTP_POOL_LOOP_NAME);

	HttpRoute *old_route = http_route_create (REQUEST_METHOD_GET, "old", loop_thread_handler);
	test_check_ptr (old_route);
	http_route_set_pool (old_route, HTTP_POOL_LOOP_NAME);
	http_cerver_route_register (http_cerver, old_route);

}

#pragma endregion

#pragma region server

typedef struct LoopServer {

	Cerver *cerver;
	HttpCerver *http_cerver;
	u16 port;

	pthread_t thread;

} LoopServer;

static void *loop_server_thread (void *server_ptr) {

	LoopServer *server = (LoopServer *) server_ptr;

	test_check_unsigned_eq (cerver_start (server->cerver), 0, NULL);

	return NULL;

}

static Cerver *loop_cerver_create (const CerverHandlerType handler_type) {

	Cerver *cerver = cerver_create_web (loop_cerver_name, (u16) loop_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, handler_type);
	cerver_set_events_backend (cerver, test_events_backend ());
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, LOOP_THREADS);
	cerver_set_thpool_queue_size (cerver, LOOP_QUEUE_SIZE);
	cerver_set_drain_timeout (cerver, LOOP_DRAIN_TIMEOUT);

	if (loop_per_core) {
		cerver_set_io_threads (cerver, CERVER_IO_THREADS_PER_CORE);
		cerver_set_io_threads_affinity (cerver, true);
	}

	return cerver;

}

// the loop calls it with each request that ended, right before its data is released
static void loop_observe (const HttpReceive *http_receive, void *args) {

	(void) http_receive;
	(void) args;

	test_counter_add (&loop_observed);

}

// the loops exist when their cerver starts and their threads do not yet: they read their observer after this
static void *loop_observer_set (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	const EventLoopGroup *group = event_data->cerver->event_loops;
	for (unsigned int idx = 0; group && (idx < group->n_loops); idx++) {
		group->loops[idx]->observer = loop_observe;
	}

	cerver_event_data_delete (event_data);

	return NULL;

}

// a cerver whose handlers run in its event loops when in_loop is true, in its thpool otherwise;
// the clients connect when it listens: no request is sent to wait for it, the tests count its sends
static LoopServer *loop_server_start_with (const CerverHandlerType handler_type, const bool in_loop) {

	LoopServer *server = (LoopServer *) calloc (1, sizeof (LoopServer));
	test_check_ptr (server);

	Cerver *cerver = loop_cerver_create (handler_type);

	__atomic_store_n (&loop_observed, 0, __ATOMIC_SEQ_CST);

	test_check_unsigned_eq (
		cerver_event_register (cerver, CERVER_EVENT_STARTED, loop_observer_set, NULL, NULL, false, false), 0, NULL
	);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_not_found_handler (http_cerver);

	if (in_loop) {
		test_check_unsigned_eq (http_cerver_set_handlers_pool (http_cerver, HTTP_POOL_LOOP_NAME), 0, NULL);
	}

	server->cerver = cerver;
	server->http_cerver = http_cerver;
	server->port = (u16) (loop_port - 1);

	loop_routes (http_cerver);

	test_check_int_eq (pthread_create (&server->thread, NULL, loop_server_thread, server), 0, NULL);

	return server;

}

static LoopServer *loop_server_start (const bool in_loop) {

	return loop_server_start_with (CERVER_HANDLER_TYPE_EVENTS, in_loop);

}

// cerver_start () must return in time after it was asked to stop; the threads of its loops are joined by
// the teardown, until then they can still end their connections
static void loop_server_shutdown (LoopServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) (LOOP_STOP_TIMEOUT * test_time_scale ());

	const int result = pthread_timedjoin_np (server->thread, NULL, &deadline);
	if (result) (void) fprintf (stderr, "the cerver in port %u did not stop\n", (unsigned int) server->port);
	test_check_int_eq (result, 0, NULL);

}

static void loop_server_end (LoopServer *server) {

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

static void loop_server_stop (LoopServer *server) {

	loop_server_shutdown (server);

	loop_server_end (server);

}

static void loop_server_wait (
	const LoopServer *server, unsigned int (*value) (const LoopServer *), const unsigned int expected
) {

	const double deadline = test_wait_now () + (double) (LOOP_WAIT_TIMEOUT * test_time_scale ());
	while ((value (server) != expected) && (test_wait_now () < deadline)) loop_pause ();

	test_check_unsigned_eq (value (server), expected, NULL);

}

// the connections that the loops of a cerver still have: a loop counts one out after its last send
static unsigned int loop_server_connections (const LoopServer *server) {

	const EventLoopGroup *group = server->cerver->event_loops;
	test_check_ptr (group);

	unsigned int connections = 0;
	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		connections += event_loop_connections (group->loops[idx]);
	}

	return connections;

}

// the sends that the loops of a cerver that stopped asked their backends for
static u64 loop_server_sends (const LoopServer *server) {

	const EventLoopGroup *group = server->cerver->event_loops;
	test_check_ptr (group);

	u64 sends = 0;
	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		sends += group->loops[idx]->backend->stats.n_sends;
	}

	return sends;

}

// stops the cerver, that must have asked for some sends
static void loop_server_stop_with_sends (LoopServer *server, const u64 sends) {

	loop_server_shutdown (server);

	// the loops are joined only by the teardown: their counters are read when their connections ended,
	// after the sends of those connections
	loop_server_wait (server, loop_server_connections, 0);

	const u64 asked = loop_server_sends (server);
	if (asked != sends) {
		(void) fprintf (
			stderr, "the cerver asked for %lu sends instead of %lu\n", (unsigned long) asked, (unsigned long) sends
		);
	}

	test_check_true (asked == sends);

	loop_server_end (server);

}

static unsigned int loop_failed_responses (const LoopServer *server) {

	return (unsigned int) CERVER_STATS_GET (server->http_cerver->n_failed_responses);

}

static unsigned int loop_observed_requests (const LoopServer *server) {

	(void) server;

	return __atomic_load_n (&loop_observed, __ATOMIC_SEQ_CST);

}

// the requests of GET /loop/count that were logged and counted in its stats
static unsigned int loop_counted_requests (const LoopServer *server) {

	(void) server;

	HttpRouteStats snapshot;
	http_route_stats_snapshot (loop_count_endpoint->stats, &snapshot);

	return (unsigned int) snapshot.n_requests;

}

#pragma endregion

#pragma region client

// a connection of a client and the bytes of the responses that it has not read
typedef struct LoopClient {

	int fd;

	char buffer[LOOP_BUFFER_SIZE];
	size_t length;

	// its last read ended with its receive timeout, not with the end of the connection
	bool timed_out;

} LoopClient;

// a response
typedef struct LoopAnswer {

	unsigned int status;

	char body[LOOP_BODY_SIZE];
	size_t body_length;

} LoopAnswer;

static int loop_connect_once (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, LOOP_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static LoopClient *loop_client_connect (const LoopServer *server) {

	LoopClient *client = (LoopClient *) calloc (1, sizeof (LoopClient));
	test_check_ptr (client);

	client->fd = -1;
	for (unsigned int i = 0; (i < LOOP_CONNECT_RETRIES) && (client->fd < 0); i++) {
		client->fd = loop_connect_once (server->port);

		if (client->fd < 0) loop_pause ();
	}

	test_check (client->fd > -1, "failed to connect to the test cerver");

	(void) sock_set_timeout (client->fd, LOOP_RECEIVE_TIMEOUT);

	return client;

}

static void loop_client_close (LoopClient *client) {

	(void) close (client->fd);

	free (client);

}

// the cerver gets a reset instead of the end of the connection
static void loop_client_reset (LoopClient *client) {

	struct linger reset = { 1, 0 };
	test_check_int_eq (setsockopt (client->fd, SOL_SOCKET, SO_LINGER, &reset, sizeof (struct linger)), 0, NULL);

	loop_client_close (client);

}

static void loop_client_send_bytes (const LoopClient *client, const char *bytes, const size_t length) {

	test_check (send (client->fd, bytes, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

}

static void loop_client_send (const LoopClient *client, const char *request) {

	loop_client_send_bytes (client, request, strlen (request));

}

// the bytes of the first response that the client has, 0 when it is not complete
static size_t loop_client_answer_size (const LoopClient *client, size_t *header_size) {

	size_t size = 0;

	const char *end = (const char *) memmem (
		client->buffer, client->length, loop_header_end, strlen (loop_header_end)
	);

	if (end) {
		*header_size = (size_t) (end - client->buffer) + strlen (loop_header_end);

		const char *length = (const char *) memmem (
			client->buffer, *header_size, loop_content_length, strlen (loop_content_length)
		);

		const size_t body_size = length ?
			(size_t) strtoul (length + strlen (loop_content_length), NULL, LOOP_STATUS_BASE) : 0;

		if (client->length >= (*header_size + body_size)) size = *header_size + body_size;
	}

	return size;

}

// takes the first response of the buffer of the client
static void loop_client_answer_take (
	LoopClient *client, const size_t size, const size_t header_size, LoopAnswer *answer
) {

	const size_t body_size = size - header_size;

	(void) memset (answer, 0, sizeof (LoopAnswer));

	test_check_int_eq (strncmp (client->buffer, loop_status_prefix, strlen (loop_status_prefix)), 0, NULL);

	answer->status = (unsigned int) strtoul (
		client->buffer + strlen (loop_status_prefix), NULL, LOOP_STATUS_BASE
	);

	answer->body_length = (body_size < (LOOP_BODY_SIZE - 1)) ? body_size : (LOOP_BODY_SIZE - 1);
	(void) memcpy (answer->body, client->buffer + header_size, answer->body_length);

	client->length -= size;
	(void) memmove (client->buffer, client->buffer + size, client->length);

}

// reads the next response of the connection: the ones of a pipeline can arrive together
// returns true with a complete response, false when the cerver closed the connection before one
static bool loop_client_answer (LoopClient *client, LoopAnswer *answer) {

	size_t header_size = 0;
	size_t size = loop_client_answer_size (client, &header_size);

	ssize_t received = 1;
	while (!size && (received > 0) && (client->length < LOOP_BUFFER_SIZE)) {
		received = recv (client->fd, client->buffer + client->length, LOOP_BUFFER_SIZE - client->length, 0);

		if (received > 0) {
			client->length += (size_t) received;

			size = loop_client_answer_size (client, &header_size);
		}

		else if (received < 0) {
			client->timed_out = (errno == EAGAIN) || (errno == EWOULDBLOCK);
		}
	}

	if (size) loop_client_answer_take (client, size, header_size, answer);

	return size != 0;

}

// the next response is a 200 whose body starts with a prefix
static void loop_client_expect_thread (LoopClient *client, const char *prefix) {

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));

	test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);

	if (strncmp (answer.body, prefix, strlen (prefix))) {
		(void) fprintf (stderr, "the thread \"%s\" is not one of \"%s\"\n", answer.body, prefix);
	}

	test_check_int_eq (strncmp (answer.body, prefix, strlen (prefix)), 0, NULL);

}

static void loop_client_expect_status (LoopClient *client, const unsigned int status) {

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));

	test_check_unsigned_eq (answer.status, status, NULL);

}

// the cerver closed the connection, and it sent nothing else
static void loop_client_expect_end (LoopClient *client) {

	LoopAnswer answer;
	test_check_false (loop_client_answer (client, &answer));

	// a client that waited until its receive timeout did not see its connection end
	test_check_false (client->timed_out);

	test_check_true (client->length == 0);

}

// a request and its response, in the thread of the expected kind
static void loop_client_thread (LoopClient *client, const char *request, const char *prefix) {

	loop_client_send (client, request);

	loop_client_expect_thread (client, prefix);

}

// writes a pipeline: a request some times, and then the one that ends it
static size_t loop_pipeline_write (
	char *pipeline, const char *request, const unsigned int times, const char *last
) {

	size_t length = 0;

	for (unsigned int idx = 0; idx < times; idx++) {
		test_check_true ((length + strlen (request)) < LOOP_PIPELINE_SIZE);

		(void) memcpy (pipeline + length, request, strlen (request));
		length += strlen (request);
	}

	test_check_true ((length + strlen (last)) < LOOP_PIPELINE_SIZE);

	(void) memcpy (pipeline + length, last, strlen (last));
	length += strlen (last);

	return length;

}

#pragma endregion

#pragma region pool

// every cerver has the pool of its event loops: it has no threads, it is not one of the pools that are counted,
// and no pool can be created with its name
static void test_loop_pool (void) {

	Cerver *cerver = loop_cerver_create (CERVER_HANDLER_TYPE_EVENTS);
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	const HttpPool *pool = http_cerver_pool_get_with_name (http_cerver, HTTP_POOL_LOOP_NAME);
	test_check_ptr (pool);
	test_check_str_eq (http_pool_get_name (pool), HTTP_POOL_LOOP_NAME, NULL);
	test_check_true (pool->in_loop);
	test_check_null_ptr (pool->thpool);
	test_check_unsigned_eq (pool->n_threads, 0, NULL);
	test_check_ptr_eq (http_cerver->loop_pool, pool);

	const HttpPool *default_pool = http_cerver_pool_get_with_name (http_cerver, HTTP_POOL_DEFAULT_NAME);
	test_check_ptr (default_pool);
	test_check_false (default_pool->in_loop);
	test_check_unsigned_eq (http_cerver->n_pools, 1, NULL);

	test_check_null_ptr (http_cerver_pool_create (http_cerver, HTTP_POOL_LOOP_NAME, LOOP_THREADS));
	test_check_unsigned_eq (http_cerver->n_pools, 1, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the pool of the requests that chose none: any pool of the cerver, and none again
static void test_loop_handlers_pool (void) {

	Cerver *cerver = loop_cerver_create (CERVER_HANDLER_TYPE_EVENTS);
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	test_check_null_ptr (http_cerver->handlers_pool);

	test_check_unsigned_eq (http_cerver_set_handlers_pool (http_cerver, HTTP_POOL_LOOP_NAME), 0, NULL);
	test_check_ptr_eq (http_cerver->handlers_pool, http_cerver->loop_pool);

	// a pool that the cerver does not have keeps the one that was set
	test_check_unsigned_eq (http_cerver_set_handlers_pool (http_cerver, loop_missing_name), 1, NULL);
	test_check_ptr_eq (http_cerver->handlers_pool, http_cerver->loop_pool);

	test_check_unsigned_eq (http_cerver_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME), 0, NULL);
	test_check_ptr_eq (
		http_cerver->handlers_pool, http_cerver_pool_get_with_name (http_cerver, HTTP_POOL_DEFAULT_NAME)
	);

	test_check_unsigned_eq (http_cerver_set_handlers_pool (http_cerver, NULL), 0, NULL);
	test_check_null_ptr (http_cerver->handlers_pool);

	test_check_unsigned_eq (http_cerver_set_handlers_pool (NULL, HTTP_POOL_LOOP_NAME), 1, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

#pragma region handlers

// the cerver sets the pool of the event loops for its handlers: they run in the loop that read the request,
// and the ones of a group that chose the thpool of the cerver in one of its threads
static void test_loop_cerver (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_thread (client, loop_thread_request, loop_events_thread);
	loop_client_thread (client, loop_pool_request, loop_pool_thread);
	loop_client_thread (client, loop_plain_request, loop_events_thread);
	loop_client_thread (client, loop_thread_close_request, loop_events_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

}

// a cerver that sets nothing runs its handlers in its thpool, and in its event loops only the ones of the
// group, the endpoint and the route that chose their pool
static void test_loop_chosen (void) {

	LoopServer *server = loop_server_start (false);

	LoopClient *client = loop_client_connect (server);

	loop_client_thread (client, loop_thread_request, loop_pool_thread);
	loop_client_thread (client, loop_explicit_request, loop_events_thread);
	loop_client_thread (client, loop_plain_request, loop_pool_thread);
	loop_client_thread (client, loop_endpoint_request, loop_events_thread);
	loop_client_thread (client, loop_old_request, loop_events_thread);
	loop_client_thread (client, loop_thread_close_request, loop_pool_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

}

// the authentication of a request of the pool of the event loops runs in its loop too:
// one that passes is handled there, one that does not is answered and never handled
static void test_loop_authentication (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_authentication_thread_reset ();

	loop_client_thread (client, loop_private_request, loop_events_thread);
	loop_authentication_thread_expect (loop_events_thread);

	loop_authentication_thread_reset ();

	loop_client_send (client, loop_private_bad_request);
	loop_client_expect_status (client, HTTP_STATUS_UNAUTHORIZED);
	loop_authentication_thread_expect (loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a request with a body is authenticated when its headers are complete, before its body: in its loop too, the
// requests without a body above are authenticated by the job that handles them and never stop there
static void test_loop_authentication_body (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_authentication_thread_reset ();

	loop_client_thread (client, loop_upload_request, loop_events_thread);
	loop_authentication_thread_expect (loop_events_thread);

	loop_authentication_thread_reset ();

	loop_client_send (client, loop_upload_bad_request);
	loop_client_expect_status (client, HTTP_STATUS_UNAUTHORIZED);
	loop_authentication_thread_expect (loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a cerver with a thread for each connection has no event loops: the pool of its handlers changes nothing
static void test_loop_without_events (void) {

	LoopServer *server = loop_server_start_with (CERVER_HANDLER_TYPE_THREADS, true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send (client, loop_thread_close_request);

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));
	test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);
	test_check_int_ne (strncmp (answer.body, loop_events_thread, strlen (loop_events_thread)), 0);

	loop_client_close (client);

	loop_server_stop (server);

}

// a cerver that asks for an event loop for each processor, each one in its own: it has as many loops as
// processors this process can run in, and the thread of every loop that handles a request can run in only one
static void test_loop_per_core (void) {

	const unsigned int processors = loop_processors ();
	test_check_unsigned_gt (processors, 0);

	loop_per_core = true;

	LoopServer *server = loop_server_start (true);

	loop_per_core = false;

	// the kernel shares the connections among the loops
	for (unsigned int idx = 0; idx < (processors * LOOP_PER_CORE_CONNECTIONS); idx++) {
		LoopClient *client = loop_client_connect (server);

		loop_client_send (client, loop_processors_request);

		LoopAnswer answer;
		test_check_true (loop_client_answer (client, &answer));
		test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);
		test_check_int_eq (strncmp (answer.body, loop_events_thread, strlen (loop_events_thread)), 0, NULL);

		const char *count = strchr (answer.body, ' ');
		test_check_ptr (count);
		test_check_unsigned_eq ((unsigned int) strtoul (count, NULL, LOOP_STATUS_BASE), 1, NULL);

		loop_client_close (client);
	}

	loop_server_shutdown (server);

	test_check_ptr (server->cerver->event_loops);
	test_check_unsigned_eq (server->cerver->event_loops->n_loops, processors, NULL);

	loop_server_end (server);

}

// a response that does not fit in the socket of its client: the loop that wrote it sends it in parts, whole
static void test_loop_big (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send (client, loop_big_request);

	// the headers, with the first bytes of the body behind them
	ssize_t received = recv (client->fd, client->buffer, LOOP_BIG_CHUNK, 0);
	test_check_true (received > 0);
	client->length = (size_t) received;

	const char *end = (const char *) memmem (
		client->buffer, client->length, loop_header_end, strlen (loop_header_end)
	);

	test_check_ptr (end);
	test_check_int_eq (strncmp (client->buffer, loop_status_prefix, strlen (loop_status_prefix)), 0, NULL);

	size_t offset = (size_t) (end - client->buffer) + strlen (loop_header_end);
	size_t position = 0;
	bool matches = true;

	while (received > 0) {
		for (; offset < client->length; offset++, position++) {
			matches = matches && (client->buffer[offset] == (char) ('a' + (position % LOOP_BIG_LETTERS)));
		}

		received = recv (client->fd, client->buffer, LOOP_BIG_CHUNK, 0);

		client->length = (received > 0) ? (size_t) received : 0;
		offset = 0;
	}

	test_check_true (position == LOOP_BIG_SIZE);
	test_check_true (matches);

	loop_client_close (client);

	loop_server_stop (server);

}

#pragma endregion

#pragma region pipeline

// the requests of a pipeline that the loop handles: every one is answered, and counted
static void loop_pipeline (const unsigned int requests, const u64 sends) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, requests - 1, loop_count_close_request)
	);

	for (unsigned int idx = 0; idx < requests; idx++) loop_client_expect_status (client, HTTP_STATUS_OK);

	loop_client_expect_end (client);

	loop_client_close (client);

	test_check_unsigned_eq (__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + requests, NULL);

	// each request ended once, also the ones whose response left with the ones behind them
	loop_server_wait (server, loop_counted_requests, requests);
	loop_server_wait (server, loop_observed_requests, requests);

	loop_server_stop_with_sends (server, sends);

}

// the responses of the requests of a pipeline that arrived together leave in one send
static void test_loop_pipeline (void) {

	loop_pipeline (LOOP_PIPELINE_REQUESTS, LOOP_PIPELINE_SENDS);

}

// a pipeline of more requests than the loop handles in a row: it sends the responses that it has, handles
// its other connections, and goes on in its next step
static void test_loop_pipeline_long (void) {

	loop_pipeline (LOOP_PIPELINE_LONG_REQUESTS, LOOP_PIPELINE_LONG_SENDS);

}

// a request of a pool of threads between two of the loop: the responses arrive in the order of their requests,
// and the one that the loop wrote first leaves before the job of the second one is given to its pool, it does
// not wait for a handler that takes long
static void test_loop_pipeline_mixed (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	loop_gate_reset ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	size_t length = loop_pipeline_write (pipeline, loop_thread_request, 1, loop_pool_gate_request);
	(void) memcpy (pipeline + length, loop_thread_close_request, strlen (loop_thread_close_request));
	length += strlen (loop_thread_close_request);

	loop_client_send_bytes (client, pipeline, length);

	// the handler of the second request has not answered
	loop_client_expect_thread (client, loop_events_thread);

	loop_gate_wait_for (1);
	loop_gate_open ();

	loop_client_expect_status (client, HTTP_STATUS_OK);
	loop_client_expect_thread (client, loop_events_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_MIXED_SENDS);

}

// the responses that the loop joined in one part count in the bytes that their connection has to send: once they
// left, the thread of a pool that writes the response of the next request does not wait for the client to read
static void test_loop_pipeline_joined (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, LOOP_PIPELINE_JOINED_ANSWERS, loop_pool_close_request)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_JOINED_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_thread (client, loop_pool_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_JOINED_SENDS);

}

// the batch of a loop starts again with the bytes of each read: the requests of a second pipeline of the same
// connection leave in one send too
static void test_loop_pipeline_again (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, LOOP_PIPELINE_AGAIN_REQUESTS - 1, loop_count_request)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_AGAIN_REQUESTS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (
			pipeline, loop_count_request, LOOP_PIPELINE_AGAIN_REQUESTS - 1, loop_count_close_request
		)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_AGAIN_REQUESTS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_AGAIN_SENDS);

}

// the short responses that cerver writes by itself in the loop are not a batch of the requests of its pool:
// each one leaves on its own, also in a cerver whose handlers run in its loops
static void test_loop_pipeline_short (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (
			pipeline, loop_missing_request, LOOP_PIPELINE_SHORT_REQUESTS - 1, loop_missing_close_request
		)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_SHORT_REQUESTS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_NOT_FOUND);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_SHORT_REQUESTS);

}

// the file of GET /loop/file, with the first letters of the big body
static void loop_file_create (void) {

	(void) strncpy (loop_file_path, loop_file_template, LOOP_FILE_PATH_SIZE - 1);

	const int fd = mkstemp (loop_file_path);
	test_check (fd > -1, "mkstemp () failed");

	test_check (write (fd, loop_big_body, LOOP_FILE_SIZE) == (ssize_t) LOOP_FILE_SIZE, "write () failed");

	(void) close (fd);

}

// a response with a file in a pipeline: the bytes of a part are sent before its file, so the response that the
// loop writes behind it is never joined to that part, it leaves after the file
static void test_loop_pipeline_file (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	loop_file_create ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline, loop_pipeline_write (pipeline, loop_file_request, 1, loop_count_close_request)
	);

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));
	test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);
	test_check_true (answer.body_length == LOOP_FILE_SIZE);
	test_check_int_eq (memcmp (answer.body, loop_big_body, LOOP_FILE_SIZE), 0, NULL);

	loop_client_expect_status (client, HTTP_STATUS_OK);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

	test_check_int_eq (unlink (loop_file_path), 0, NULL);

}

// a request of a pipeline that closes its connection: the ones behind it are not handled
static void test_loop_pipeline_closed (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	size_t length = loop_pipeline_write (pipeline, loop_count_request, 1, loop_count_close_request);
	(void) memcpy (pipeline + length, loop_count_request, strlen (loop_count_request));
	length += strlen (loop_count_request);

	loop_client_send_bytes (client, pipeline, length);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_CLOSED_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	test_check_unsigned_eq (
		__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + LOOP_PIPELINE_CLOSED_ANSWERS, NULL
	);

	loop_server_stop (server);

}

// a pipeline that ends in the middle of a request: the responses of the complete ones do not wait for the
// rest of that request
static void test_loop_pipeline_partial (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, LOOP_PIPELINE_PARTIAL_ANSWERS, loop_count_start)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_PARTIAL_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_send (client, loop_count_end);

	loop_client_expect_status (client, HTTP_STATUS_OK);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

}

// a client that resets its connection before the responses of its pipeline are sent: the loop handled every
// request, their only send fails, and it is counted once
static void test_loop_pipeline_reset (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	loop_gate_reset ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_gate_request, LOOP_PIPELINE_RESET_REQUESTS - 1, loop_gate_request)
	);

	// the loop has the whole pipeline and waits in the handler of its first request
	loop_gate_wait_for (1);

	loop_client_reset (client);

	// the reset reaches the socket of the cerver before its loop goes on
	loop_pause ();

	loop_gate_open ();

	loop_server_wait (server, loop_failed_responses, 1);

	test_check_unsigned_eq (
		__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + LOOP_PIPELINE_RESET_REQUESTS, NULL
	);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_SENDS);

}

// a cerver that stops while its loop handles the first request of a pipeline: that one is answered, the loop
// does not go on with the ones behind it and closes the connection, like one that it does not keep
static void test_loop_pipeline_stopping (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	loop_gate_reset ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_gate_request, LOOP_PIPELINE_STOPPING_REQUESTS - 1, loop_gate_request)
	);

	// the loop has the whole pipeline and waits in the handler of its first request
	loop_gate_wait_for (1);

	// cerver_start () returns when its loops were asked to stop, while this one still waits in the handler
	loop_server_shutdown (server);

	loop_gate_open ();

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_STOPPING_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	test_check_unsigned_eq (
		__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + LOOP_PIPELINE_STOPPING_ANSWERS, NULL
	);

	loop_server_end (server);

}

#pragma endregion

static void loop_big_body_create (void) {

	loop_big_body = (char *) malloc (LOOP_BIG_SIZE);
	test_check_ptr (loop_big_body);

	for (size_t position = 0; position < LOOP_BIG_SIZE; position++) {
		loop_big_body[position] = (char) ('a' + (position % LOOP_BIG_LETTERS));
	}

}

void http_tests_loop (void) {

	(void) printf ("Testing HTTP loop handlers...\n");

	loop_big_body_create ();

	// a hang fails the suite instead of blocking it forever
	(void) alarm (LOOP_ALARM * test_time_scale ());

	test_run (test_loop_pool);
	test_run (test_loop_handlers_pool);

	test_run (test_loop_cerver);
	test_run (test_loop_chosen);
	test_run (test_loop_authentication);
	test_run (test_loop_authentication_body);
	test_run (test_loop_without_events);
	test_run (test_loop_per_core);
	test_run (test_loop_big);

	test_run (test_loop_pipeline);
	test_run (test_loop_pipeline_long);
	test_run (test_loop_pipeline_mixed);
	test_run (test_loop_pipeline_joined);
	test_run (test_loop_pipeline_again);
	test_run (test_loop_pipeline_short);
	test_run (test_loop_pipeline_file);
	test_run (test_loop_pipeline_closed);
	test_run (test_loop_pipeline_partial);
	test_run (test_loop_pipeline_reset);
	test_run (test_loop_pipeline_stopping);

	(void) alarm (0);

	free (loop_big_body);
	loop_big_body = NULL;

	(void) printf ("Done!\n");

}
