#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/content.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../handler.h"

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

	cerver_http_all_stats_print (cerver_http_get (features_cerver));
	(void) cerver_teardown (features_cerver);

}

// GET /features/small, a compiled response
static void features_small_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /features/items, a body that is compressed
static void features_items_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json (
		context, features_items, features_items_length
	);

}

// GET /features/stream
static void features_stream_handler (CerverHttpContext *context) {

	(void) cerver_http_response_set_header (
		context, HTTP_HEADER_CONTENT_TYPE, cerver_http_content_type_mime (HTTP_CONTENT_TYPE_TXT)
	);

	if (!cerver_http_response_stream_begin (context)) {
		for (size_t i = 0; i < features_stream_n_pieces; i++) {
			(void) cerver_http_response_stream_write (
				context, features_stream_pieces[i], strlen (features_stream_pieces[i])
			);
		}

		(void) cerver_http_response_stream_end (context);
	}

}

// POST /features/echo, the body that was received
static void features_echo_handler (CerverHttpContext *context) {

	const CerverHttpView body = cerver_http_request_body (context);

	(void) cerver_http_response_send_text (
		context, cerver_http_view_is_missing (body) ? "" : body.data, body.length
	);

}

// GET /features/client, the client's address, the peer and the request's id
static void features_client_handler (CerverHttpContext *context) {

	const CerverHttpView ip = cerver_http_request_client_ip (context);
	const CerverHttpView peer = cerver_http_request_peer_ip (context);
	const CerverHttpView id = cerver_http_request_id (context);

	char json[FEATURES_JSON_SIZE] = { 0 };
	const int length = snprintf (
		json, FEATURES_JSON_SIZE, "{\"ip\": \"%s\", \"peer\": \"%s\", \"id\": \"%s\"}",
		cerver_http_view_is_missing (ip) ? "" : ip.data,
		cerver_http_view_is_missing (peer) ? "" : peer.data,
		cerver_http_view_is_missing (id) ? "" : id.data
	);

	(void) cerver_http_response_send_json (context, json, (size_t) length);

}

// GET /plain/small, without the settings of the features group
static void features_plain_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /features/unanswered returns without a response, cerver answers it with 500
static void features_unanswered_handler (CerverHttpContext *context) {

	(void) context;

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

static void features_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *features = cerver_http_group_create (http_cerver, "features");
	cerver_http_group_set_cors (features, true);
	cerver_http_group_set_etag (features, true);
	cerver_http_group_set_compression (features, true);

	(void) cerver_http_group_get (features, "small", features_small_handler);
	(void) cerver_http_group_get (features, "items", features_items_handler);
	(void) cerver_http_group_get (features, "stream", features_stream_handler);
	(void) cerver_http_group_post (features, "echo", features_echo_handler);
	(void) cerver_http_group_get (features, "client", features_client_handler);
	(void) cerver_http_group_get (features, "unanswered", features_unanswered_handler);

	CerverHttpGroup *plain = cerver_http_group_create (http_cerver, "plain");
	(void) cerver_http_group_get (plain, "small", features_plain_handler);

}

static void features_configure (CerverHttp *http_cerver) {

	// the static paths read files: the handlers run in threads
	(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

	if (test_keep_alive_is_set ()) cerver_http_set_keep_alive (http_cerver, test_keep_alive ());

	cerver_http_set_not_found_handler (http_cerver);

	cerver_http_set_request_id (http_cerver, true);

	// the client runs in the same host, or behind nginx in a private network
	cerver_http_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	(void) cerver_http_add_origin_to_whitelist (http_cerver, features_origin);

	(void) cerver_http_static_path_add (http_cerver, "./public");

	// GET /features/cerver/metrics
	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_admin_set_path (http_cerver, "/features");

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
		(void) cerver_set_bind_address (features_cerver, test_address ());
		cerver_set_receive_buffer_size (features_cerver, FEATURES_RECEIVE_BUFFER_SIZE);
		cerver_set_thpool_n_threads (features_cerver, FEATURES_THREADS);
		cerver_set_events_backend (features_cerver, test_events_backend ());
		cerver_set_io_threads (features_cerver, test_io_threads ());
		cerver_set_reusable_address_flags (features_cerver, true);

		features_configure (cerver_http_get (features_cerver));

		if (!cerver_start (features_cerver)) {
			features_finish ();
		}

		else {
			cerver_log_error ("Failed to start %s!", features_cerver->info->name);
			(void) cerver_teardown (features_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
