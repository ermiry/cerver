#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/json/internal.h>
#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/utils/log.h>

// a minimal web cerver to measure the requests per second, the latency
// and the allocations per request (with valgrind) of every phase
// usage: server [port] [threads]

#define BENCH_SERVER_DEFAULT_PORT			8080
#define BENCH_SERVER_DEFAULT_THREADS		4
#define BENCH_SERVER_ARGUMENT_PORT			1
#define BENCH_SERVER_ARGUMENT_THREADS		2
#define BENCH_SERVER_JSON_SIZE				256

static Cerver *bench_cerver = NULL;

static const char *bench_json_key = { "name" };

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

	const String *body = http_request_get_body (request);

	json_error_t error;
	json_t *json = body ? json_loads (body->str, 0, &error) : NULL;
	const char *name = json ? json_string_value (json_object_get (json, bench_json_key)) : NULL;

	if (name) {
		char response[BENCH_SERVER_JSON_SIZE];
		const int length = snprintf (response, BENCH_SERVER_JSON_SIZE, "{\"name\": \"%s\"}", name);

		(void) http_response_json_custom_reference_send (
			http_receive, HTTP_STATUS_OK, response, (size_t) length
		);
	}

	else {
		(void) http_response_send (bad_request_error, http_receive);
	}

	if (json) json_decref (json);

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

static void bench_server_routes (HttpCerver *http_cerver) {

	HttpRoute *bench_route = http_route_create (REQUEST_METHOD_GET, "bench", bench_handler);
	http_cerver_route_register (http_cerver, bench_route);

	HttpRoute *json_route = http_route_create (REQUEST_METHOD_POST, "json", bench_json_handler);
	http_route_child_add (bench_route, json_route);

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");
	HttpEndpoint *echo = http_group_web_socket_add (ws, "echo", &bench_web_socket_handlers);

	// the pings would be measured as messages
	http_endpoint_set_web_socket_ping_milliseconds (echo, 0, 0);

}

int main (int argc, char **argv) {

	const u16 port = (argc > BENCH_SERVER_ARGUMENT_PORT) ?
		(u16) atoi (argv[BENCH_SERVER_ARGUMENT_PORT]) : BENCH_SERVER_DEFAULT_PORT;

	const u16 threads = (argc > BENCH_SERVER_ARGUMENT_THREADS) ?
		(u16) atoi (argv[BENCH_SERVER_ARGUMENT_THREADS]) : BENCH_SERVER_DEFAULT_THREADS;

	cerver_init ();

	// the signals stop the cerver, then it is drained and released
	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = bench_server_end;
	(void) sigemptyset (&action.sa_mask);
	(void) sigaction (SIGINT, &action, NULL);
	(void) sigaction (SIGTERM, &action, NULL);

	bench_cerver = cerver_create_web ("bench", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (bench_cerver) {
		cerver_set_handler_type (bench_cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_thpool_n_threads (bench_cerver, threads);
		cerver_set_reusable_address_flags (bench_cerver, true);

		bench_server_routes ((HttpCerver *) bench_cerver->cerver_data);

		(void) cerver_start (bench_cerver);

		cerver_stats_print (bench_cerver, false, false);
		http_cerver_all_stats_print ((HttpCerver *) bench_cerver->cerver_data);

		(void) cerver_teardown (bench_cerver);
	}

	cerver_end ();

	return 0;

}
