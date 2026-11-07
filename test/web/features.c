#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <cerver/types/string.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/stream.h>
#include <cerver/http/tree.h>

#include <cerver/utils/log.h>

// the headers that cerver adds to the responses, the entity tags, the compression,
// the streams, the chunked bodies, the client's address and the metrics,
// its client runs standalone and behind nginx (test/nginx)

#define FEATURES_PORT						8080
#define FEATURES_THREADS					4
#define FEATURES_RECEIVE_BUFFER_SIZE		4096
#define FEATURES_JSON_SIZE					256

// a JSON body bigger than the minimum that is compressed
#define FEATURES_ITEMS						(1 << 7)
#define FEATURES_ITEMS_SIZE					(1 << 13)

static const char *features_origin = { "http://allowed.com" };

static const char *features_stream_pieces[] = { "first", "second" };
static const size_t features_stream_n_pieces = 2;

static char features_items[FEATURES_ITEMS_SIZE] = { 0 };
static size_t features_items_length = 0;

static Cerver *features_cerver = NULL;

static void end (int dummy) {

	(void) dummy;

	if (features_cerver) (void) cerver_shutdown (features_cerver);

}

static void features_finish (void) {

	http_cerver_all_stats_print ((HttpCerver *) features_cerver->cerver_data);
	(void) cerver_teardown (features_cerver);

}

// GET /features/small, a compiled response
static void features_small_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// GET /features/items, a body that is compressed
static void features_items_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_render_json (
		http_receive, HTTP_STATUS_OK, features_items, features_items_length
	);

}

// GET /features/stream
static void features_stream_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	if (response) {
		http_response_set_status (response, HTTP_STATUS_OK);
		(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_TXT);

		if (!http_response_stream_begin (http_receive, response)) {
			for (size_t i = 0; i < features_stream_n_pieces; i++) {
				(void) http_response_stream_write (
					http_receive, features_stream_pieces[i], strlen (features_stream_pieces[i])
				);
			}

			(void) http_response_stream_end (http_receive);
		}

		http_response_return (response);
	}

}

// POST /features/echo, the body that was received
static void features_echo_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const String *body = http_request_get_body (request);

	(void) http_response_render_text (
		http_receive, HTTP_STATUS_OK, body ? body->str : "", body ? body->len : 0
	);

}

// GET /features/client, the client's address, the peer and the request's id
static void features_client_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const CerverReceive *cerver_receive = http_receive_get_cerver_receive (http_receive);

	char json[FEATURES_JSON_SIZE] = { 0 };
	const int length = snprintf (
		json, FEATURES_JSON_SIZE, "{\"ip\": \"%s\", \"peer\": \"%s\", \"id\": \"%s\"}",
		http_request_get_client_ip (request),
		cerver_receive->connection->ip,
		http_request_get_id (request)
	);

	(void) http_response_render_json (http_receive, HTTP_STATUS_OK, json, (size_t) length);

}

// GET /plain/small, without the settings of the features group
static void features_plain_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

static void features_items_create (void) {

	size_t length = (size_t) snprintf (features_items, FEATURES_ITEMS_SIZE, "[");
	for (unsigned int i = 0; i < FEATURES_ITEMS; i++) {
		length += (size_t) snprintf (
			features_items + length, FEATURES_ITEMS_SIZE - length,
			"%s{\"index\": %u, \"name\": \"item\"}", i ? ", " : "", i
		);
	}

	length += (size_t) snprintf (features_items + length, FEATURES_ITEMS_SIZE - length, "]");

	features_items_length = length;

}

static void features_routes (HttpCerver *http_cerver) {

	HttpGroup *features = http_cerver_group_create (http_cerver, "features");
	http_group_set_cors (features, true);
	http_group_set_etag (features, true);
	http_group_set_compression (features, true);

	(void) http_group_get (features, "small", features_small_handler);
	(void) http_group_get (features, "items", features_items_handler);
	(void) http_group_get (features, "stream", features_stream_handler);
	(void) http_group_post (features, "echo", features_echo_handler);
	(void) http_group_get (features, "client", features_client_handler);

	HttpGroup *plain = http_cerver_group_create (http_cerver, "plain");
	(void) http_group_get (plain, "small", features_plain_handler);

}

static void features_configure (HttpCerver *http_cerver) {

	http_cerver_set_not_found_handler (http_cerver);

	http_cerver_set_request_id (http_cerver, true);

	// the client runs in the same host, or behind nginx in a private network
	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	(void) http_cerver_add_origin_to_whitelist (http_cerver, features_origin);

	(void) http_cerver_static_path_add (http_cerver, "./public");

	http_cerver_enable_admin_routes (http_cerver, true);

	features_routes (http_cerver);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	features_items_create ();

	features_cerver = cerver_create_web ("features-cerver", FEATURES_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (features_cerver) {
		cerver_set_receive_buffer_size (features_cerver, FEATURES_RECEIVE_BUFFER_SIZE);
		cerver_set_thpool_n_threads (features_cerver, FEATURES_THREADS);
		cerver_set_handler_type (features_cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_reusable_address_flags (features_cerver, true);

		features_configure ((HttpCerver *) features_cerver->cerver_data);

		if (!cerver_start (features_cerver)) {
			features_finish ();
		}

		else {
			cerver_log_error ("Failed to start %s!", features_cerver->info->name);
			cerver_delete (features_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
