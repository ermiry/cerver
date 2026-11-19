#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/clients/client.h>
#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/headers/types.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

// the handlers of this cerver run in its event loops and none of them waits: the ones that need something that
// takes time (the work of another thread, the response of another service) defer their response and return

#define ASYNC_PORT						8080

#define ASYNC_THPOOL_THREADS			4

// the microseconds that the work of the worker takes
#define ASYNC_WORK_TIME					100000

// the pieces of the stream, and the bytes of the buffer of each one
#define ASYNC_STREAM_PIECES				8
#define ASYNC_PIECE_SIZE				32

#define ASYNC_STATUS_OK					200

static const char *async_data_url = { "http://127.0.0.1:8080/async/data" };
static const char *async_check_url = { "http://127.0.0.1:8080/async/check" };

static const char *async_valid_authorization = { "Bearer secret" };

static const char *async_work_done = { "the work is done" };
static const char *async_data = { "the data of another endpoint" };
static const char *async_secret = { "only for the ones that the other service accepts" };

Cerver *web_cerver = NULL;

// the HTTP client of the cerver: its requests are sent by the event loops, nobody waits for them
static CerverHttpClient *async_client = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

// the signals only stop the cerver, it is released when cerver_start () returns;
// the client of the cerver is deleted before it
static void finish (void) {

	cerver_http_client_delete (async_client);
	async_client = NULL;

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

}

#pragma endregion

#pragma region work

// the work of another thread: when it is done it writes the response and completes the request
static void *async_worker (void *context_ptr) {

	CerverHttpContext *context = (CerverHttpContext *) context_ptr;

	(void) usleep (ASYNC_WORK_TIME);

	(void) cerver_http_response_send_text (context, async_work_done, strlen (async_work_done));

	(void) cerver_http_context_complete (context);

	return NULL;

}

// GET /async/work
// defers its response and gives its request to a thread: the event loop goes on with its other connections
void async_work_handler (CerverHttpContext *context) {

	pthread_t worker = 0;

	if (!cerver_http_context_defer (context)) {
		if (!pthread_create (&worker, NULL, async_worker, context)) {
			(void) pthread_detach (worker);
		}

		// a deferred request is always completed: without a response it is answered with a 500
		else {
			(void) cerver_http_context_complete (context);
		}
	}

}

#pragma endregion

#pragma region client

// GET /async/data
void async_data_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_text (context, async_data, strlen (async_data));

}

// the response of the other endpoint arrived, in the event loop that sent its request
static void async_proxy_response (const CerverHttpClientResponse *response, void *context_ptr) {

	CerverHttpContext *context = (CerverHttpContext *) context_ptr;

	const CerverHttpView body = cerver_http_client_response_body (response);

	if (!cerver_http_client_response_error (response) && body.data) {
		(void) cerver_http_response_send_text (context, body.data, body.length);
	}

	else {
		cerver_log_error (
			"The request to the other endpoint failed: %s",
			cerver_http_client_error_description (cerver_http_client_response_error (response))
		);

		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_SERVER_ERROR);
	}

	(void) cerver_http_context_complete (context);

}

// GET /async/proxy
// answers with what another endpoint answers: it asks for it with the client of the cerver and returns
void async_proxy_handler (CerverHttpContext *context) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		async_client, REQUEST_METHOD_GET, async_data_url
	);

	if (request && !cerver_http_context_defer (context)) {
		// a request that could not be sent was deleted, and its handler is not called
		if (cerver_http_client_request_send (request, async_proxy_response, context)) {
			(void) cerver_http_context_complete (context);
		}
	}

	else {
		cerver_http_client_request_delete (request);

		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_SERVER_ERROR);
	}

}

#pragma endregion

#pragma region authentication

// GET /async/check
// the service that decides who gets in
void async_check_handler (CerverHttpContext *context) {

	const bool valid = cerver_http_view_equals (
		cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION), async_valid_authorization
	);

	(void) cerver_http_response_send_common (
		context, valid ? CERVER_HTTP_COMMON_RESPONSE_OK : CERVER_HTTP_COMMON_RESPONSE_BAD_AUTH
	);

}

// the service answered: only a 200 lets the request in
static bool async_authentication_response (
	CerverHttpContext *context, const CerverHttpClientResponse *response
) {

	(void) context;

	return !cerver_http_client_response_error (response)
		&& (cerver_http_client_response_status (response) == ASYNC_STATUS_OK);

}

// the authentication of GET /async/private: it asks the service with the authorization of the request, and
// returns without waiting. What it returns is not read once the verdict was deferred
unsigned int async_authentication_handler (CerverHttpContext *context) {

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		async_client, REQUEST_METHOD_GET, async_check_url
	);

	if (request && authorization.data) {
		(void) cerver_http_client_request_add_header (request, "Authorization", authorization.data);
	}

	return cerver_http_context_authentication_request (context, request, async_authentication_response);

}

// GET /async/private
void async_private_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_text (context, async_secret, strlen (async_secret));

}

#pragma endregion

#pragma region stream

// the pieces of the stream that were written; a real handler keeps its state in the data of its request
static unsigned int async_stream_written = 0;

static void async_stream_piece (CerverHttpContext *context) {

	char piece[ASYNC_PIECE_SIZE] = { 0 };
	const int length = snprintf (piece, ASYNC_PIECE_SIZE, "piece %u\n", async_stream_written);

	(void) cerver_http_response_stream_write (context, piece, (size_t) length);

	async_stream_written += 1;

}

// what was written of the stream was sent: the next piece, or its end
static void async_stream_drained (CerverHttpContext *context, void *data) {

	(void) data;

	if (async_stream_written < ASYNC_STREAM_PIECES) {
		async_stream_piece (context);
	}

	else {
		(void) cerver_http_response_stream_end (context);

		(void) cerver_http_context_complete (context);
	}

}

// GET /async/stream
// a stream that is written piece by piece: each one when the one before it left, nothing waits in memory
void async_stream_handler (CerverHttpContext *context) {

	async_stream_written = 0;

	if (!cerver_http_response_stream_begin (context)) {
		async_stream_piece (context);

		if (
			cerver_http_response_stream_on_drained (context, async_stream_drained, NULL)
			|| cerver_http_context_defer (context)
		) {
			cerver_log_error ("The stream could not be deferred!");
		}
	}

}

#pragma endregion

#pragma region parts

static CerverHttpStatus async_part_started (CerverHttpContext *context, const CerverHttpPart *part) {

	(void) context;

	cerver_log_debug ("The part %s starts", cerver_http_part_name (part).data);

	return HTTP_STATUS_NONE;

}

// the bytes of a part while they arrive: cerver saves nothing of the requests of this endpoint
static CerverHttpStatus async_part_data (
	CerverHttpContext *context, const CerverHttpPart *part, const char *data, const size_t length
) {

	(void) context;
	(void) data;

	cerver_log_debug ("%zu bytes of the part %s", length, cerver_http_part_name (part).data);

	return HTTP_STATUS_NONE;

}

static const CerverHttpPartHandlers async_part_handlers = {
	.on_part = async_part_started,
	.on_data = async_part_data,
	.on_end = NULL
};

// POST /async/parts
void async_parts_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

#pragma endregion

#pragma region start

static void async_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *group = cerver_http_group_create (http_cerver, "/async");

	// GET /async/work
	(void) cerver_http_group_get (group, "work", async_work_handler);

	// GET /async/data
	(void) cerver_http_group_get (group, "data", async_data_handler);

	// GET /async/proxy
	(void) cerver_http_group_get (group, "proxy", async_proxy_handler);

	// GET /async/check
	(void) cerver_http_group_get (group, "check", async_check_handler);

	// GET /async/private
	CerverHttpEndpoint *private_endpoint = cerver_http_group_get (group, "private", async_private_handler);
	cerver_http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_endpoint_set_authentication_handler (private_endpoint, async_authentication_handler);

	// GET /async/stream
	(void) cerver_http_group_get (group, "stream", async_stream_handler);

	// POST /async/parts
	CerverHttpEndpoint *parts_endpoint = cerver_http_group_post (group, "parts", async_parts_handler);
	cerver_http_endpoint_set_modifier (parts_endpoint, HTTP_ROUTE_MODIFIER_MULTI_PART);
	cerver_http_endpoint_set_part_handlers (parts_endpoint, &async_part_handlers);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	srand ((unsigned int) time (NULL));

	// register to the quit signal
	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Async Web Cerver Example");
	printf ("\n");

	web_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"async-cerver",
		ASYNC_PORT,
		PROTOCOL_TCP,
		false,
		2
	);

	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_alias (web_cerver, "async");

		cerver_set_thpool_n_threads (web_cerver, ASYNC_THPOOL_THREADS);

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (web_cerver);

		async_routes (http_cerver);

		async_client = cerver_http_client_create (http_cerver);

		if (async_client && !cerver_start (web_cerver)) {
			finish ();
		}

		else {
			cerver_log_error (
				"Failed to start %s!",
				web_cerver->info->name
			);

			cerver_http_client_delete (async_client);

			(void) cerver_teardown (web_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion
