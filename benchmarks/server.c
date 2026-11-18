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

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
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
// the connections are handled with a thread for each one (the default) or with event loops
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
#define BENCH_SERVER_ARGUMENT_HANDLER		3
#define BENCH_SERVER_ARGUMENT_IO_THREADS	4
#define BENCH_SERVER_HANDLER_EVENTS			"events"
#define BENCH_SERVER_JSON_SIZE				256

static Cerver *bench_cerver = NULL;

static const char *bench_json_key = { "name" };

// what GET /plaintext and GET /json answer, the same that the other servers of benchmarks/competitors/server do
static const char *bench_message_key = { "message" };
static const char bench_message[] = { "Hello, World!" };

// the response of GET /plaintext, created once
static HttpResponse *bench_plaintext_response = NULL;

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
static void bench_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// POST /bench/json
// parses the body and answers with one of its values
static void bench_json_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	char response[BENCH_SERVER_JSON_SIZE];
	CerverJsonWriter writer;

	// the body is read in the arena of the request and the response is written in the stack: no allocations
	const CerverJsonValue *name = cerver_json_object_get (http_request_json (request, NULL), bench_json_key);

	cerver_json_writer_init (&writer, response, BENCH_SERVER_JSON_SIZE, CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, bench_json_key);
	cerver_json_writer_string (&writer, cerver_json_string_value (name), cerver_json_string_length (name));
	cerver_json_writer_object_end (&writer);

	// a body without the name as a string leaves the writer with its error
	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
		(void) http_response_json_custom_reference_send (
			http_receive, HTTP_STATUS_OK, response, cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) http_response_send (bad_request_error, http_receive);
	}

}

// GET /bench/auth
// the bearer of the request was verified before its handler
static void bench_auth_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// GET /plaintext
static void bench_plaintext_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (bench_plaintext_response, http_receive);

}

// GET /json
// serializes its object in each request, in the stack
static void bench_message_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char response[BENCH_SERVER_JSON_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, response, BENCH_SERVER_JSON_SIZE, CERVER_JSON_WRITE_COMPACT);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, bench_message_key);
	cerver_json_writer_string (&writer, bench_message, sizeof (bench_message) - 1);
	cerver_json_writer_object_end (&writer);

	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
		(void) http_response_json_custom_reference_send (
			http_receive, HTTP_STATUS_OK, response, cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// GET /slow, a handler that keeps its thread for a while
static void bench_slow_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	struct timespec interval = { 0, BENCH_SERVER_SLOW_MILLISECONDS * BENCH_SERVER_NANOSECONDS_PER_MILLISECOND };
	(void) nanosleep (&interval, NULL);

	(void) http_response_send (oki_doki, http_receive);

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
static void bench_server_auth (HttpCerver *http_cerver, HttpRoute *bench_route) {

	const char *secret = getenv (BENCH_SERVER_AUTH_VARIABLE);
	if (secret) {
		(void) http_cerver_auth_set_jwt_algorithm (http_cerver, CERVER_JWT_ALGORITHM_HS256);
		http_cerver_auth_set_jwt_priv_key_filename (http_cerver, secret);
		http_cerver_auth_set_jwt_pub_key_filename (http_cerver, secret);

		HttpRoute *auth_route = http_route_create (REQUEST_METHOD_GET, "auth", bench_auth_handler);
		http_route_set_auth (auth_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
		http_route_child_add (bench_route, auth_route);
	}

}

// the routes that every server of benchmarks/competitors/server has: a text and a JSON that is serialized
static void bench_server_messages (HttpCerver *http_cerver) {

	bench_plaintext_response = http_response_create (HTTP_STATUS_OK, bench_message, sizeof (bench_message) - 1);
	if (bench_plaintext_response) {
		(void) http_response_add_content_type_header (bench_plaintext_response, HTTP_CONTENT_TYPE_TXT);
		(void) http_response_add_content_length_header (bench_plaintext_response, sizeof (bench_message) - 1);
		(void) http_response_compile (bench_plaintext_response);

		http_cerver_route_register (
			http_cerver, http_route_create (REQUEST_METHOD_GET, "plaintext", bench_plaintext_handler)
		);
	}

	http_cerver_route_register (http_cerver, http_route_create (REQUEST_METHOD_GET, "json", bench_message_handler));

}

static void bench_server_routes (HttpCerver *http_cerver) {

	HttpRoute *bench_route = http_route_create (REQUEST_METHOD_GET, "bench", bench_handler);
	http_cerver_route_register (http_cerver, bench_route);

	HttpRoute *json_route = http_route_create (REQUEST_METHOD_POST, "json", bench_json_handler);
	http_route_child_add (bench_route, json_route);

	bench_server_auth (http_cerver, bench_route);

	bench_server_messages (http_cerver);

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");
	HttpEndpoint *echo = http_group_web_socket_add (ws, "echo", &bench_web_socket_handlers);

	// the pings would be measured as messages
	http_endpoint_set_web_socket_ping_milliseconds (echo, 0, 0);

}

// the route that takes long runs in its own pool when the environment asks for one,
// and the cerver tells whether it is alive and whether it can take a request
static void bench_server_pools (HttpCerver *http_cerver) {

	HttpGroup *slow = http_cerver_group_create (http_cerver, "slow");
	(void) http_group_root_add (slow, REQUEST_METHOD_GET, bench_slow_handler);

	// a handler that keeps its thread always runs in a pool of threads, whatever pool the others have
	http_group_set_pool (slow, HTTP_POOL_DEFAULT_NAME);

	const char *threads = getenv (BENCH_SERVER_POOL_VARIABLE);
	if (threads && http_cerver_pool_create (http_cerver, bench_pool_name, (unsigned int) atoi (threads))) {
		http_group_set_pool (slow, bench_pool_name);
	}

	const char *handlers = getenv (BENCH_SERVER_HANDLERS_VARIABLE);
	if (handlers && !strcmp (handlers, BENCH_SERVER_HANDLERS_LOOP)) {
		(void) http_cerver_set_handlers_pool (http_cerver, HTTP_POOL_LOOP_NAME);
	}

	http_cerver_set_health_route (http_cerver, bench_health_route);
	http_cerver_set_ready_route (http_cerver, bench_ready_route);

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

	const CerverHandlerType handler_type = (
		(argc > BENCH_SERVER_ARGUMENT_HANDLER)
		&& !strcmp (argv[BENCH_SERVER_ARGUMENT_HANDLER], BENCH_SERVER_HANDLER_EVENTS)
	) ? CERVER_HANDLER_TYPE_EVENTS : CERVER_HANDLER_TYPE_THREADS;

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
		cerver_set_handler_type (bench_cerver, handler_type);
		cerver_set_io_threads (bench_cerver, io_threads);
		cerver_set_thpool_n_threads (bench_cerver, threads);
		cerver_set_reusable_address_flags (bench_cerver, true);

		const char *keep_alive = getenv (BENCH_SERVER_KEEP_ALIVE_VARIABLE);
		if (keep_alive) {
			http_cerver_set_keep_alive (
				(HttpCerver *) bench_cerver->cerver_data,
				strcmp (keep_alive, BENCH_SERVER_KEEP_ALIVE_DISABLED) != 0
			);
		}

		const char *keep_alive_requests = getenv (BENCH_SERVER_KEEP_ALIVE_REQUESTS_VARIABLE);
		if (keep_alive_requests) {
			http_cerver_set_keep_alive_requests (
				(HttpCerver *) bench_cerver->cerver_data, (unsigned int) atoi (keep_alive_requests)
			);
		}

		bench_server_backend (bench_cerver);

		bench_server_routes ((HttpCerver *) bench_cerver->cerver_data);

		bench_server_pools ((HttpCerver *) bench_cerver->cerver_data);

		(void) cerver_start (bench_cerver);

		cerver_stats_print (bench_cerver, false, false);
		http_cerver_all_stats_print ((HttpCerver *) bench_cerver->cerver_data);

		(void) cerver_teardown (bench_cerver);

		http_response_delete (bench_plaintext_response);
	}

	cerver_end ();

	return 0;

}
