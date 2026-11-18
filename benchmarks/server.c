#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/events/events.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/security/auth.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include <cerver/jwt/jwt.h>

#include <cerver/utils/log.h>

// a minimal web cerver to measure the requests per second, the latency
// and the allocations per request (with valgrind) of every phase
// usage: server [port] [threads] [threads | events] [io threads]
// BENCH_KEEP_ALIVE in the environment sets whether the connections are kept between requests (0 or 1),
// without it the cerver decides: only the event loops keep them
// BENCH_KEEP_ALIVE_REQUESTS in the environment is the most requests of a connection that is kept (0 for no limit),
// without it the default of the cerver
// BENCH_POOL in the environment is the number of threads of a pool for GET /slow, a route that takes long:
// without it that route runs in the thpool of the cerver, with the others
// BENCH_BACKEND in the environment is the backend of the event loops ("uring" or "epoll"),
// without it the cerver chooses: io_uring where it is available
// BENCH_HANDLERS in the environment is where the handlers run with event loops: "loop" runs them in the
// event loop that read their request (GET /slow stays in a pool of threads), without it they run in the thpool
// BENCH_AUTH in the environment is a file with the secret of the HS256 tokens of GET /bench/auth,
// a route that takes a bearer: without it the route is not there
// BENCH_QUIET in the environment keeps the line of each request out of the log (only successes, warnings and
// errors are printed), to measure the cerver without the stream of its log

#define BENCH_SERVER_AUTH_VARIABLE			"BENCH_AUTH"

#define BENCH_SERVER_QUIET_VARIABLE			"BENCH_QUIET"

#define BENCH_SERVER_KEEP_ALIVE_VARIABLE	"BENCH_KEEP_ALIVE"
#define BENCH_SERVER_KEEP_ALIVE_DISABLED	"0"

#define BENCH_SERVER_KEEP_ALIVE_REQUESTS_VARIABLE	"BENCH_KEEP_ALIVE_REQUESTS"

#define BENCH_SERVER_BACKEND_VARIABLE		"BENCH_BACKEND"
#define BENCH_SERVER_BACKEND_URING			"uring"
#define BENCH_SERVER_BACKEND_EPOLL			"epoll"

#define BENCH_SERVER_HANDLERS_VARIABLE		"BENCH_HANDLERS"
#define BENCH_SERVER_HANDLERS_LOOP			"loop"

#define BENCH_SERVER_POOL_VARIABLE			"BENCH_POOL"

// the milliseconds that GET /slow takes
#define BENCH_SERVER_SLOW_MILLISECONDS		(1 << 6)
#define BENCH_SERVER_NANOSECONDS_PER_MILLISECOND	1000000L

#define BENCH_SERVER_DEFAULT_PORT			8080
#define BENCH_SERVER_DEFAULT_THREADS		4
#define BENCH_SERVER_ARGUMENT_PORT			1
#define BENCH_SERVER_ARGUMENT_THREADS		2
#define BENCH_SERVER_ARGUMENT_IO_THREADS	4
#define BENCH_SERVER_JSON_SIZE				256

static Cerver *bench_cerver = NULL;

static const char *bench_json_key = { "name" };

// what GET /plaintext and GET /json answer, the same that the other servers of benchmarks/competitors/server do
static const char *bench_message_key = { "message" };
static const char bench_message[] = { "Hello, World!" };

// the response of GET /plaintext, created once
static CerverHttpPrepared *bench_plaintext_response = NULL;

static const char *bench_pool_name = { "slow" };
static const char *bench_health_route = { "/health" };
static const char *bench_ready_route = { "/ready" };

static void bench_server_end (int signal_number) {

	(void) signal_number;

	if (bench_cerver) {
		(void) cerver_shutdown (bench_cerver);
	}

}

// GET /bench
static void bench_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// POST /bench/json
// parses the body and answers with one of its values
static void bench_json_handler (CerverHttpContext *context) {

	char response[BENCH_SERVER_JSON_SIZE];
	CerverJsonWriter writer;

	// the body is read in the arena of the request and the response is written in the stack: no allocations
	const CerverJsonValue *name = cerver_json_object_get (cerver_http_request_json (context, NULL), bench_json_key);

	cerver_json_writer_init (&writer, response, BENCH_SERVER_JSON_SIZE, CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, bench_json_key);
	cerver_json_writer_string (&writer, cerver_json_string_value (name), cerver_json_string_length (name));
	cerver_json_writer_object_end (&writer);

	// a body without the name as a string leaves the writer with its error
	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
		(void) cerver_http_response_send_json (
			context, response, cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// GET /bench/auth
// the bearer of the request was verified before its handler
static void bench_auth_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /plaintext
static void bench_plaintext_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_prepared (context, bench_plaintext_response);

}

// GET /json
// serializes its object in each request, in the stack
static void bench_message_handler (CerverHttpContext *context) {

	char response[BENCH_SERVER_JSON_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, response, BENCH_SERVER_JSON_SIZE, CERVER_JSON_WRITE_COMPACT);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, bench_message_key);
	cerver_json_writer_string (&writer, bench_message, sizeof (bench_message) - 1);
	cerver_json_writer_object_end (&writer);

	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
		(void) cerver_http_response_send_json (
			context, response, cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_SERVER_ERROR);
	}

}

// GET /slow, a handler that keeps its thread for a while
static void bench_slow_handler (CerverHttpContext *context) {

	struct timespec interval = { 0, BENCH_SERVER_SLOW_MILLISECONDS * BENCH_SERVER_NANOSECONDS_PER_MILLISECOND };
	(void) nanosleep (&interval, NULL);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /ws/echo, a WebSocket that answers every message with itself
static void bench_web_socket_echo (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) http_web_socket_send (web_socket, type, data, size);

}

static const HttpWebSocketHandlers bench_web_socket_handlers = {
	NULL, NULL, bench_web_socket_echo, NULL, NULL, NULL, NULL, NULL
};

// the route with a bearer needs the secret of its tokens, so it is there only with one:
// the cerver signs and verifies with the same file
static void bench_server_auth (CerverHttp *http_cerver, CerverHttpGroup *bench) {

	const char *secret = getenv (BENCH_SERVER_AUTH_VARIABLE);
	if (secret) {
		(void) cerver_http_auth_set_jwt_algorithm (http_cerver, CERVER_JWT_ALGORITHM_HS256);
		cerver_http_auth_set_jwt_priv_key_filename (http_cerver, secret);
		cerver_http_auth_set_jwt_pub_key_filename (http_cerver, secret);

		CerverHttpEndpoint *auth = cerver_http_group_get (bench, "auth", bench_auth_handler);
		cerver_http_endpoint_set_auth (auth, HTTP_ROUTE_AUTH_TYPE_BEARER);
	}

}

// the routes that every server of benchmarks/competitors/server has: a text and a JSON that is serialized
static void bench_server_messages (CerverHttp *http_cerver) {

	CerverHttpGroup *messages = cerver_http_group_create (http_cerver, "/");

	bench_plaintext_response = cerver_http_prepared_create (
		HTTP_STATUS_OK, HTTP_CONTENT_TYPE_TXT, bench_message, sizeof (bench_message) - 1
	);

	if (bench_plaintext_response) {
		(void) cerver_http_group_get (messages, "plaintext", bench_plaintext_handler);
	}

	(void) cerver_http_group_get (messages, "json", bench_message_handler);

}

static void bench_server_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *bench = cerver_http_group_create (http_cerver, "/bench");
	(void) cerver_http_group_root_add (bench, REQUEST_METHOD_GET, bench_handler);

	(void) cerver_http_group_post (bench, "json", bench_json_handler);

	bench_server_auth (http_cerver, bench);

	bench_server_messages (http_cerver);

	CerverHttpGroup *ws = cerver_http_group_create (http_cerver, "ws");
	CerverHttpEndpoint *echo = cerver_http_group_web_socket_add (ws, "echo", &bench_web_socket_handlers);

	// the pings would be measured as messages
	cerver_http_endpoint_set_web_socket_ping_milliseconds (echo, 0, 0);

}

// the route that takes long runs in its own pool when the environment asks for one,
// and the cerver tells whether it is alive and whether it can take a request
static void bench_server_pools (CerverHttp *http_cerver) {

	CerverHttpGroup *slow = cerver_http_group_create (http_cerver, "slow");
	(void) cerver_http_group_root_add (slow, REQUEST_METHOD_GET, bench_slow_handler);

	// a handler that keeps its thread always runs in a pool of threads, whatever pool the others have
	cerver_http_group_set_pool (slow, HTTP_POOL_DEFAULT_NAME);

	const char *threads = getenv (BENCH_SERVER_POOL_VARIABLE);
	if (threads && cerver_http_pool_create (http_cerver, bench_pool_name, (unsigned int) atoi (threads))) {
		cerver_http_group_set_pool (slow, bench_pool_name);
	}

	const char *handlers = getenv (BENCH_SERVER_HANDLERS_VARIABLE);
	if (handlers && !strcmp (handlers, BENCH_SERVER_HANDLERS_LOOP)) {
		(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_LOOP_NAME);
	}

	else {
		(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);
	}

	cerver_http_set_health_route (http_cerver, bench_health_route);
	cerver_http_set_ready_route (http_cerver, bench_ready_route);

}

// the backend of the event loops that the environment asks for, to measure one against the other
static void bench_server_backend (Cerver *cerver) {

	const char *backend = getenv (BENCH_SERVER_BACKEND_VARIABLE);
	if (backend) {
		if (!strcmp (backend, BENCH_SERVER_BACKEND_URING)) {
			cerver_set_events_backend (cerver, CERVER_EVENTS_BACKEND_URING);
		}

		else if (!strcmp (backend, BENCH_SERVER_BACKEND_EPOLL)) {
			cerver_set_events_backend (cerver, CERVER_EVENTS_BACKEND_EPOLL);
		}
	}

}

int main (int argc, char **argv) {

	const u16 port = (argc > BENCH_SERVER_ARGUMENT_PORT) ?
		(u16) atoi (argv[BENCH_SERVER_ARGUMENT_PORT]) : BENCH_SERVER_DEFAULT_PORT;

	const u16 threads = (argc > BENCH_SERVER_ARGUMENT_THREADS) ?
		(u16) atoi (argv[BENCH_SERVER_ARGUMENT_THREADS]) : BENCH_SERVER_DEFAULT_THREADS;

	// 0 keeps the default of the cerver
	const unsigned int io_threads = (argc > BENCH_SERVER_ARGUMENT_IO_THREADS) ?
		(unsigned int) atoi (argv[BENCH_SERVER_ARGUMENT_IO_THREADS]) : 0;

	cerver_init ();

	if (getenv (BENCH_SERVER_QUIET_VARIABLE)) cerver_log_set_quiet (true);

	// the signals stop the cerver, then it is drained and released
	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = bench_server_end;
	(void) sigemptyset (&action.sa_mask);
	(void) sigaction (SIGINT, &action, NULL);
	(void) sigaction (SIGTERM, &action, NULL);

	bench_cerver = cerver_create_web ("bench", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (bench_cerver) {
		cerver_set_io_threads (bench_cerver, io_threads);
		cerver_set_thpool_n_threads (bench_cerver, threads);
		cerver_set_reusable_address_flags (bench_cerver, true);

		CerverHttp *http_cerver = cerver_http_get (bench_cerver);

		const char *keep_alive = getenv (BENCH_SERVER_KEEP_ALIVE_VARIABLE);
		if (keep_alive) {
			cerver_http_set_keep_alive (
				http_cerver,
				strcmp (keep_alive, BENCH_SERVER_KEEP_ALIVE_DISABLED) != 0
			);
		}

		const char *keep_alive_requests = getenv (BENCH_SERVER_KEEP_ALIVE_REQUESTS_VARIABLE);
		if (keep_alive_requests) {
			cerver_http_set_keep_alive_requests (
				http_cerver, (unsigned int) atoi (keep_alive_requests)
			);
		}

		bench_server_backend (bench_cerver);

		bench_server_routes (http_cerver);

		bench_server_pools (http_cerver);

		(void) cerver_start (bench_cerver);

		cerver_stats_print (bench_cerver, false, false);
		cerver_http_all_stats_print (http_cerver);

		(void) cerver_teardown (bench_cerver);

		cerver_http_prepared_delete (bench_plaintext_response);
	}

	cerver_end ();

	return 0;

}
