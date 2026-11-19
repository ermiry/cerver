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

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/network/network.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/headers/buffer.h>
#include <cerver/http/headers/fields.h>
#include "cerver/http/requests/internal.h"
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/compression.h>
#include <cerver/http/responses/conditional.h>
#include <cerver/http/responses/date.h>
#include "cerver/http/responses/prepared.h"
#include <cerver/http/responses/response.h>
#include "cerver/http/responses/stream.h"
#include <cerver/http/routes/tree.h>

#include "../handler.h"
#include "../test.h"
#include "http.h"

// a real web cerver with the headers that cerver adds to every response
// (Date, X-Request-Id, CORS, Vary), the entity tags and 304, the compression,
// the streams, the chunked bodies, the client's address and the metrics

#define FEATURES_TEST_PORT					((1 << 13) + (1 << 10))
#define FEATURES_TEST_ADDRESS				"127.0.0.1"

#define FEATURES_TEST_RESPONSE_SIZE			(1 << 16)

#define FEATURES_TEST_CONNECT_RETRIES		(1 << 8)
// 2^22 ns
#define FEATURES_TEST_CONNECT_INTERVAL		4194304L

#define FEATURES_TEST_RECEIVE_TIMEOUT		16
#define FEATURES_TEST_JOIN_TIMEOUT			32
#define FEATURES_TEST_ALARM					128

// a JSON body bigger than the minimum that is compressed
#define FEATURES_TEST_JSON_ITEMS			(1 << 7)
#define FEATURES_TEST_JSON_SIZE				(1 << 13)

#define FEATURES_TEST_VALUE_SIZE			(1 << 8)

static const char *features_origin = { "https://ermiry.com" };
static const char *features_other_origin = { "https://other.com" };

static const char *features_stream_first = { "first" };
static const char *features_stream_second = { "second" };

static const char *features_twice_first = { "the response of the request" };
static const char *features_twice_second = { "a second response" };

// a JSON shorter than the smallest body that a cerver compresses, and that gzip makes smaller
static const char *features_short_json = {
	"{\"value\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}"
};

// the smallest body that the endpoint that compresses the short JSON compresses
#define FEATURES_TEST_SHORT_MIN_SIZE		1

// what the handler of /plain/views/:id answers to the request of its test
static const char *features_views_body = {
	"method=GET path=/plain/views/64 query=first=one&second=two%20words id=64 first=one second=two words "
	"custom=custom value host=localhost params=1 values=2 terminated=7 missing=5 equal=5"
};

static const char *features_first_cookie = { "session=first; Path=/" };
static const char *features_second_cookie = { "theme=dark; Path=/" };
static const char *features_trace_name = { "X-Trace" };
static const char *features_trace_value = { "abc-123" };

static const char *features_video_file = { "./test/web/public/index.html" };

static char features_json[FEATURES_TEST_JSON_SIZE] = { 0 };
static size_t features_json_length = 0;

static Cerver *features_cerver = NULL;
static pthread_t features_thread;

#pragma region routes

// GET /features/small, a compiled response of cerver
static void features_small_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /features/short and /features/shortest, a JSON under the smallest body that the cerver compresses
static void features_short_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json (context, features_short_json, strlen (features_short_json));

}

// GET /features/json, a body that is compressed
static void features_json_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json (
		context, features_json, features_json_length
	);

}

// the headers of the responses with the same body that cerver filters: one of them has its headers
// in its list, the other one has them as the bytes of its header
#define FEATURES_TEST_CACHE_CONTROL			"max-age=60"
#define FEATURES_TEST_KEPT_NAME				"X-Kept"
#define FEATURES_TEST_KEPT_VALUE			"yes"

// a Date of the response: cerver does not add its own to the response that has it,
// and a 304 does not keep it, so its Date is the one of cerver
#define FEATURES_TEST_OWN_DATE				"Sun, 06 Nov 1994 08:49:37 GMT"

// GET /features/listed, the body with headers of its own in the list of the response
static void features_listed_handler (CerverHttpContext *context) {

	(void) cerver_http_response_set_header (context, HTTP_HEADER_CACHE_CONTROL, FEATURES_TEST_CACHE_CONTROL);
	(void) cerver_http_response_set_header (context, HTTP_HEADER_DATE, FEATURES_TEST_OWN_DATE);
	(void) cerver_http_response_add_header_with_name (context, FEATURES_TEST_KEPT_NAME, FEATURES_TEST_KEPT_VALUE);
	(void) cerver_http_response_send_json (context, features_json, features_json_length);

}

// GET /features/raw, the same response with its header set as bytes
// the application can not set the bytes of a header anymore: its headers are set one by one, like in the listed one
static void features_raw_handler (CerverHttpContext *context) {

	(void) cerver_http_response_set_header (context, HTTP_HEADER_CACHE_CONTROL, FEATURES_TEST_CACHE_CONTROL);
	(void) cerver_http_response_set_header (context, HTTP_HEADER_DATE, FEATURES_TEST_OWN_DATE);
	(void) cerver_http_response_set_header_with_name (context, FEATURES_TEST_KEPT_NAME, FEATURES_TEST_KEPT_VALUE);
	(void) cerver_http_response_send_json (context, features_json, features_json_length);

}

// GET /features/image, a body that is never compressed
static void features_image_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_content (
		context, HTTP_CONTENT_TYPE_PNG, features_json, features_json_length
	);

}

// GET /features/stream, the handler does not end its stream
static void features_stream_handler (CerverHttpContext *context) {

	(void) cerver_http_response_set_header (
		context, HTTP_HEADER_CONTENT_TYPE, cerver_http_content_type_mime (HTTP_CONTENT_TYPE_TXT)
	);

	if (!cerver_http_response_stream_begin (context)) {
		(void) cerver_http_response_stream_write (context, features_stream_first, strlen (features_stream_first));

		// an empty piece is not sent, it would end the chunked body
		(void) cerver_http_response_stream_write (context, "", 0);

		(void) cerver_http_response_stream_write (context, features_stream_second, strlen (features_stream_second));

		// a normal response can not be sent in the middle of a stream
		test_check_unsigned_eq (cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK), 1, NULL);
	}

}

// a Content-Length that a stream can not have
#define FEATURES_TEST_STREAM_LENGTH			"1"

// GET /features/stream/ended
static void features_stream_ended_handler (CerverHttpContext *context) {

	// a stream can not announce its length
	(void) cerver_http_response_set_header (context, HTTP_HEADER_CONTENT_LENGTH, FEATURES_TEST_STREAM_LENGTH);
	test_check_unsigned_eq (cerver_http_response_stream_begin (context), 1, NULL);

	test_check_unsigned_eq (cerver_http_response_stream_begin (context), 0, NULL);
	test_check_true (cerver_http_response_stream_is_active (context));
	test_check_unsigned_eq (cerver_http_response_stream_begin (context), 1, NULL);

	(void) cerver_http_response_stream_write (context, features_stream_first, strlen (features_stream_first));
	test_check_unsigned_eq (cerver_http_response_stream_end (context), 0, NULL);
	test_check_unsigned_eq (cerver_http_response_stream_end (context), 0, NULL);
	test_check_false (cerver_http_response_stream_is_active (context));

	test_check_unsigned_eq (cerver_http_response_stream_write (context, "x", 1), 1, NULL);

}

// POST /features/echo, the body is sent back as it was received
static void features_echo_handler (CerverHttpContext *context) {

	const CerverHttpView body = cerver_http_request_body (context);

	(void) cerver_http_response_send_text (
		context, cerver_http_view_is_missing (body) ? "" : body.data, body.length
	);

}

// GET /features/client, the request's id and the client's address
static void features_client_handler (CerverHttpContext *context) {

	char json[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const int written = snprintf (
		json, FEATURES_TEST_VALUE_SIZE, "{\"ip\":\"%s\",\"id\":\"%s\"}",
		cerver_http_request_client_ip (context).data, cerver_http_request_id (context).data
	);

	(void) cerver_http_response_send_json (context, json, (size_t) written);

}

// GET /features/cookies, a header that is repeated and one with a name of the application
static void features_cookies_handler (CerverHttpContext *context) {

	test_check_unsigned_eq (cerver_http_response_add_header (context, HTTP_HEADER_SET_COOKIE, features_first_cookie), 0, NULL);
	test_check_unsigned_eq (cerver_http_response_add_header_with_name (context, features_trace_name, features_trace_value), 0, NULL);
	test_check_unsigned_eq (cerver_http_response_add_header (context, HTTP_HEADER_SET_COOKIE, features_second_cookie), 0, NULL);
	(void) cerver_http_response_send_text (context, features_stream_first, strlen (features_stream_first));

}

// GET /features/connection, a response that says what its connection does
static void features_connection_handler (CerverHttpContext *context) {

	test_check_unsigned_eq (cerver_http_response_add_header (context, HTTP_HEADER_CONNECTION, HTTP_RESPONSE_CONNECTION_CLOSE), 0, NULL);
	(void) cerver_http_response_send (context, features_stream_first, strlen (features_stream_first));

}

// GET /features/created, a body without its length, that ends when the connection is closed:
// the responses of the context always have their length, so it is sent with the internal methods
static void features_created_handler (CerverHttpContext *context) {

	(void) http_response_create_and_send (
		HTTP_STATUS_CREATED, features_stream_first, strlen (features_stream_first), context
	);

}

// GET /features/video, a range of a file
static void features_video_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_video (context, features_video_file);

}

// GET /plain/small, an endpoint without the settings of the features group
static void features_plain_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// what each send of the handler of /plain/twice returned
static unsigned int features_twice_first_result = 0;
static unsigned int features_twice_second_result = 0;
static unsigned int features_twice_common_result = 0;
static unsigned int features_twice_stream_result = 0;

static void features_twice_result (unsigned int *result, const u8 value) {

	__atomic_store_n (result, (unsigned int) value, __ATOMIC_SEQ_CST);

}

// GET /plain/twice, a request takes one response: the ones that its handler sends after it are refused
static void features_twice_handler (CerverHttpContext *context) {

	features_twice_result (
		&features_twice_first_result,
		cerver_http_response_send_text (context, features_twice_first, strlen (features_twice_first))
	);

	cerver_http_response_set_status (context, HTTP_STATUS_BAD_REQUEST);
	features_twice_result (
		&features_twice_second_result,
		cerver_http_response_send_text (context, features_twice_second, strlen (features_twice_second))
	);

	features_twice_result (
		&features_twice_common_result,
		cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_NOT_FOUND)
	);

	features_twice_result (&features_twice_stream_result, cerver_http_response_stream_begin (context));

}

// 1 for a view that ends with '\0' right after its bytes, like every view of a request does
static unsigned int features_view_is_terminated (const CerverHttpView view) {

	return (view.data && (view.data[view.length] == '\0')) ? 1 : 0;

}

// GET /plain/views/:id, what the getters of the request give to a handler, as text
static void features_views_handler (CerverHttpContext *context) {

	const CerverHttpView path = cerver_http_request_path (context);
	const CerverHttpView query = cerver_http_request_query (context);
	const CerverHttpView id = cerver_http_request_param (context, "id");
	const CerverHttpView first = cerver_http_request_query_value (context, "first");
	const CerverHttpView second = cerver_http_request_query_value (context, "second");
	const CerverHttpView custom = cerver_http_request_header_with_name (context, "X-Custom");
	const CerverHttpView host = cerver_http_request_header (context, HTTP_HEADER_HOST);

	const unsigned int terminated = features_view_is_terminated (path) + features_view_is_terminated (query)
		+ features_view_is_terminated (id) + features_view_is_terminated (first)
		+ features_view_is_terminated (second) + features_view_is_terminated (custom)
		+ features_view_is_terminated (host);

	// what the request does not have
	const CerverHttpView none = cerver_http_request_param (context, "none");
	const unsigned int missing = (unsigned int) cerver_http_view_is_missing (none)
		+ (unsigned int) cerver_http_view_is_missing (cerver_http_request_query_value (context, "none"))
		+ (unsigned int) cerver_http_view_is_missing (cerver_http_request_header_with_name (context, "X-None"))
		+ (unsigned int) cerver_http_view_is_missing (cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION))
		+ (unsigned int) cerver_http_view_is_missing (cerver_http_request_body (context));

	// a view is equal only to the text of its whole value, and one without a value to none
	const unsigned int equal = (unsigned int) cerver_http_view_equals (id, "64")
		+ (unsigned int) !cerver_http_view_equals (id, "6")
		+ (unsigned int) !cerver_http_view_equals (id, "645")
		+ (unsigned int) !cerver_http_view_equals (none, "")
		+ (unsigned int) !cerver_http_view_equals (id, NULL);

	char body[FEATURES_TEST_VALUE_SIZE * 2] = { 0 };
	const int length = snprintf (
		body, sizeof (body),
		"method=%s path=%s query=%s id=%s first=%s second=%s custom=%s host=%s "
		"params=%u values=%zu terminated=%u missing=%u equal=%u",
		cerver_http_method_to_string (cerver_http_request_method (context)),
		path.data, query.data, id.data, first.data, second.data, custom.data, host.data,
		cerver_http_request_params_count (context), cerver_http_request_query_values_count (context),
		terminated, missing, equal
	);

	(void) cerver_http_response_send_text (context, body, (size_t) length);

}

static void features_json_create (void) {

	size_t length = 0;
	length += (size_t) snprintf (features_json + length, FEATURES_TEST_JSON_SIZE - length, "[");
	for (unsigned int i = 0; i < FEATURES_TEST_JSON_ITEMS; i++) {
		length += (size_t) snprintf (
			features_json + length, FEATURES_TEST_JSON_SIZE - length,
			"%s{\"index\": %u, \"name\": \"item\"}", i ? ", " : "", i
		);
	}

	length += (size_t) snprintf (features_json + length, FEATURES_TEST_JSON_SIZE - length, "]");

	features_json_length = length;

}

static void features_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *features = cerver_http_group_create (http_cerver, "features");
	cerver_http_group_set_cors (features, true);
	cerver_http_group_set_etag (features, true);
	cerver_http_group_set_compression (features, true);

	test_check_ptr (cerver_http_group_get (features, "small", features_small_handler));

	// the same short body: the first endpoint has the smallest compressed body of the cerver, the second its own
	test_check_ptr (cerver_http_group_get (features, "short", features_short_handler));

	CerverHttpEndpoint *shortest = cerver_http_group_get (features, "shortest", features_short_handler);
	test_check_ptr (shortest);
	cerver_http_endpoint_set_compression_min_size (shortest, FEATURES_TEST_SHORT_MIN_SIZE);
	test_check_unsigned_eq (cerver_http_endpoint_set_compression_level (shortest, HTTP_COMPRESSION_MAX_LEVEL), 0, NULL);

	test_check_ptr (cerver_http_group_get (features, "json", features_json_handler));
	test_check_ptr (cerver_http_group_get (features, "listed", features_listed_handler));
	test_check_ptr (cerver_http_group_get (features, "raw", features_raw_handler));
	test_check_ptr (cerver_http_group_get (features, "image", features_image_handler));
	test_check_ptr (cerver_http_group_get (features, "stream", features_stream_handler));
	test_check_ptr (cerver_http_group_get (features, "stream/ended", features_stream_ended_handler));
	test_check_ptr (cerver_http_group_post (features, "echo", features_echo_handler));
	test_check_ptr (cerver_http_group_get (features, "client", features_client_handler));
	test_check_ptr (cerver_http_group_get (features, "cookies", features_cookies_handler));
	test_check_ptr (cerver_http_group_get (features, "connection", features_connection_handler));
	test_check_ptr (cerver_http_group_get (features, "created", features_created_handler));
	// the handler of the video reads a file, it does not run in the event loop
	CerverHttpEndpoint *video = cerver_http_group_get (features, "video", features_video_handler);
	test_check_ptr (video);
	cerver_http_endpoint_set_pool (video, HTTP_POOL_DEFAULT_NAME);

	CerverHttpGroup *plain = cerver_http_group_create (http_cerver, "plain");
	test_check_ptr (cerver_http_group_get (plain, "small", features_plain_handler));
	test_check_ptr (cerver_http_group_get (plain, "twice", features_twice_handler));
	test_check_ptr (cerver_http_group_get (plain, "views/:id", features_views_handler));

}

#pragma endregion

#pragma region cerver

static void *features_cerver_thread (void *cerver_ptr) {

	(void) cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static int features_connect_once (void) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (FEATURES_TEST_PORT);
	(void) inet_pton (AF_INET, FEATURES_TEST_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static int features_connect (void) {

	int fd = -1;
	for (unsigned int i = 0; (i < FEATURES_TEST_CONNECT_RETRIES) && (fd < 0); i++) {
		fd = features_connect_once ();
		if (fd < 0) {
			const struct timespec interval = { 0, FEATURES_TEST_CONNECT_INTERVAL };
			(void) nanosleep (&interval, NULL);
		}
	}

	test_check (fd > -1, "failed to connect to the features cerver");

	(void) sock_set_timeout (fd, FEATURES_TEST_RECEIVE_TIMEOUT);

	return fd;

}

// the cerver adds the Date header, it is set before the cerver starts because its threads read it without a lock
static bool features_date_header = true;

static void features_cerver_start (void) {

	features_cerver = cerver_create_web ("features-test", FEATURES_TEST_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (features_cerver);

	cerver_set_events_backend (features_cerver, test_events_backend ());
	cerver_set_reusable_address_flags (features_cerver, true);
	cerver_set_thpool_n_threads (features_cerver, 2);

	CerverHttp *http_cerver = cerver_http_get (features_cerver);

	// the clients of these tests read each response until the cerver closes its connection,
	// the connections that are kept have their own tests (keepalive.c)
	cerver_http_set_keep_alive (http_cerver, false);

	cerver_http_set_request_id (http_cerver, true);

	cerver_http_set_date_header (http_cerver, features_date_header);

	// a path without a route is answered with 404 instead of the catch all
	cerver_http_set_not_found_handler (http_cerver);

	// the test client connects from 127.0.0.1, like nginx from its private network
	cerver_http_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	test_check_unsigned_eq (cerver_http_add_origin_to_whitelist (http_cerver, features_origin), 0, NULL);

	(void) cerver_http_static_path_add (http_cerver, "./test/web/public");

	// GET /features/cerver/info, /features/cerver/stats…
	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_admin_set_path (http_cerver, "/features");
	cerver_http_enable_admin_info_route (http_cerver, true);
	cerver_http_enable_admin_head_handlers (http_cerver, true);

	features_routes (http_cerver);

	test_check_int_eq (pthread_create (&features_thread, NULL, features_cerver_thread, features_cerver), 0, NULL);

	// waits until the cerver is listening
	(void) close (features_connect ());

}

static void features_cerver_stop (void) {

	test_check_unsigned_eq (cerver_shutdown (features_cerver), 0, NULL);

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += FEATURES_TEST_JOIN_TIMEOUT;
	test_check_int_eq (pthread_timedjoin_np (features_thread, NULL, &deadline), 0, NULL);

	test_check_unsigned_eq (cerver_teardown (features_cerver), 0, NULL);
	features_cerver = NULL;

}

#pragma endregion

#pragma region client

typedef struct FeaturesResponse {

	char raw[FEATURES_TEST_RESPONSE_SIZE];
	size_t length;

	// the header ends with its empty line
	size_t header_length;

	const char *body;
	size_t body_length;

} FeaturesResponse;

// sends the request and reads the whole response until the cerver closes the connection
static void features_request (const char *request, const size_t request_length, FeaturesResponse *response) {

	int fd = features_connect ();
	test_check (send (fd, request, request_length, MSG_NOSIGNAL) == (ssize_t) request_length, "send () failed");

	response->length = 0;
	ssize_t received = 0;
	while (
		(response->length < (FEATURES_TEST_RESPONSE_SIZE - 1))
		&& ((received = recv (fd, response->raw + response->length, FEATURES_TEST_RESPONSE_SIZE - 1 - response->length, 0)) > 0)
	) {
		response->length += (size_t) received;
	}

	(void) close (fd);

	response->raw[response->length] = '\0';

	const char *end = strstr (response->raw, "\r\n\r\n");
	test_check_ptr (end);
	response->header_length = (size_t) (end - response->raw) + 4;
	response->body = response->raw + response->header_length;
	response->body_length = response->length - response->header_length;

}

static void features_get (const char *path, const char *headers, FeaturesResponse *response) {

	char request[FEATURES_TEST_RESPONSE_SIZE] = { 0 };
	const int written = snprintf (
		request, FEATURES_TEST_RESPONSE_SIZE,
		"GET %s HTTP/1.1\r\nHost: localhost\r\n%s\r\n", path, headers ? headers : ""
	);

	features_request (request, (size_t) written, response);

}

static void features_expect_status (const FeaturesResponse *response, const char *status) {

	if (strncmp (response->raw, status, strlen (status))) {
		(void) fprintf (stderr, "expected %s - received: %.*s\n", status, (int) response->header_length, response->raw);
	}

	test_check (!strncmp (response->raw, status, strlen (status)), "unexpected response status");

}

// returns the value of the header of the response, NULL if it does not have it
static const char *features_header (
	const FeaturesResponse *response, const char *name, char *value
) {

	const char *found = NULL;
	size_t found_length = 0;

	if (http_headers_buffer_find (response->raw, response->header_length - 2, name, strlen (name), &found, &found_length)) {
		(void) snprintf (value, FEATURES_TEST_VALUE_SIZE, "%.*s", (int) found_length, found);
		return value;
	}

	return NULL;

}

static void features_expect_header (
	const FeaturesResponse *response, const char *name, const char *expected
) {

	char value[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const char *found = features_header (response, name, value);

	if (!found) (void) fprintf (stderr, "%s is missing in: %.*s\n", name, (int) response->header_length, response->raw);
	test_check_ptr (found);

	if (expected) test_check_str_eq (found, expected, NULL);

}

static void features_expect_no_header (const FeaturesResponse *response, const char *name) {

	char value[FEATURES_TEST_VALUE_SIZE] = { 0 };
	if (features_header (response, name, value)) {
		(void) fprintf (stderr, "%s was not expected in: %.*s\n", name, (int) response->header_length, response->raw);
	}

	test_check_null_ptr (features_header (response, name, value));

}

// a header that appears more than once in the response
static unsigned int features_header_count (const FeaturesResponse *response, const char *line_start) {

	unsigned int count = 0;
	for (const char *current = response->raw; (current = strstr (current, line_start)); current++) {
		if ((size_t) (current - response->raw) < response->header_length) count += 1;
	}

	return count;

}

static size_t features_content_length (const FeaturesResponse *response) {

	char value[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const char *length = features_header (response, "Content-Length", value);
	test_check_ptr (length);

	return (size_t) strtoul (length, NULL, 10);

}

#pragma endregion

#pragma region tests

// every response has a Date that can be parsed, and Connection: close
static void test_features_date (void) {

	FeaturesResponse response;

	features_get ("/features/small", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Connection", "close");

	char value[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const char *date = features_header (&response, "Date", value);
	test_check_ptr (date);

	time_t parsed = 0;
	test_check_true (cerver_http_date_parse (date, strlen (date), &parsed));

	// the errors that cerver sends also have it
	features_get ("/features/missing", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 404");
	features_expect_header (&response, "Date", NULL);

	// the body of the compiled response is still sent after the headers
	features_get ("/plain/small", NULL, &response);
	test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);
	test_check (strstr (response.body, "doki") != NULL, NULL);

	test_check_unsigned_eq (features_header_count (&response, "\r\nDate: "), 1, NULL);

}

static void test_features_request_id (void) {

	FeaturesResponse response;

	// a valid id is kept
	features_get ("/features/client", "X-Request-Id: nginx-request-42\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "X-Request-Id", "nginx-request-42");
	test_check (strstr (response.body, "\"id\":\"nginx-request-42\"") != NULL, NULL);

	// an invalid one is replaced with a UUID v4
	features_get ("/features/client", "X-Request-Id: has spaces\r\n", &response);
	char value[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const char *id = features_header (&response, "X-Request-Id", value);
	test_check_ptr (id);
	test_check_unsigned_eq (strlen (id), HTTP_REQUEST_ID_UUID_LENGTH, NULL);

	// a request without one gets a new one, also when it is rejected
	features_get ("/features/missing", NULL, &response);
	features_expect_header (&response, "X-Request-Id", NULL);

}

static void test_features_client_ip (void) {

	FeaturesResponse response;

	// the peer is the client without forwarded headers
	features_get ("/features/client", NULL, &response);
	test_check (strstr (response.body, "\"ip\":\"127.0.0.1\"") != NULL, NULL);

	// 127.0.0.1 is a trusted proxy, so its X-Forwarded-For is used
	features_get ("/features/client", "X-Forwarded-For: 198.51.100.4, 203.0.113.9\r\n", &response);
	test_check (strstr (response.body, "\"ip\":\"203.0.113.9\"") != NULL, NULL);

	features_get ("/features/client", "X-Real-IP: 203.0.113.10\r\n", &response);
	test_check (strstr (response.body, "\"ip\":\"203.0.113.10\"") != NULL, NULL);

	features_get ("/features/client", "X-Forwarded-For: not-an-address\r\n", &response);
	test_check (strstr (response.body, "\"ip\":\"127.0.0.1\"") != NULL, NULL);

}

static void test_features_cors (void) {

	FeaturesResponse response;

	features_get ("/features/small", "Origin: https://ermiry.com\r\n", &response);
	features_expect_header (&response, "Access-Control-Allow-Origin", features_origin);
	features_expect_header (&response, "Vary", "Origin, Accept-Encoding");

	// an origin outside the whitelist can not read the response
	features_get ("/features/small", "Origin: https://other.com\r\n", &response);
	features_expect_no_header (&response, "Access-Control-Allow-Origin");
	(void) features_other_origin;

	// an endpoint without CORS
	features_get ("/plain/small", "Origin: https://ermiry.com\r\n", &response);
	features_expect_no_header (&response, "Access-Control-Allow-Origin");
	features_expect_no_header (&response, "Vary");

}

static void test_features_etag (void) {

	FeaturesResponse response;
	char etag[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char header[FEATURES_TEST_VALUE_SIZE * 2] = { 0 };

	features_get ("/features/small", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	test_check_ptr (features_header (&response, "ETag", etag));
	test_check (etag[0] == '"', "the entity tag of a body is a strong one");

	// the same representation is answered with 304, without a body
	(void) snprintf (header, sizeof (header), "If-None-Match: %s\r\n", etag);
	features_get ("/features/small", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");
	features_expect_header (&response, "ETag", etag);
	features_expect_header (&response, "Date", NULL);
	features_expect_header (&response, "Vary", NULL);
	features_expect_no_header (&response, "Content-Length");
	test_check_unsigned_eq (response.body_length, 0, NULL);

	// a weak comparison, a list and "*"
	(void) snprintf (header, sizeof (header), "If-None-Match: \"other\", W/%s\r\n", etag);
	features_get ("/features/small", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");

	features_get ("/features/small", "If-None-Match: *\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 304");

	features_get ("/features/small", "If-None-Match: \"other\"\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");

	// an endpoint without the setting has no entity tag
	features_get ("/plain/small", NULL, &response);
	features_expect_no_header (&response, "ETag");

	// a HEAD has the entity tag of its GET
	FeaturesResponse head;
	static const char *head_request = { "HEAD /features/small HTTP/1.1\r\nHost: localhost\r\n\r\n" };
	features_request (head_request, strlen (head_request), &head);
	features_expect_header (&head, "ETag", etag);
	test_check_unsigned_eq (head.body_length, 0, NULL);

}

// what cerver takes from the headers of a response when it compresses it and when it answers it
// with a 304: the same from the list of the response and from a header that was set as bytes
static void features_filtered (const char *path) {

	FeaturesResponse response;
	char etag[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char date[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char header[FEATURES_TEST_VALUE_SIZE * 2] = { 0 };

	// as it is: its length and every one of its headers, its Date is the only one
	features_get (path, NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Cache-Control", FEATURES_TEST_CACHE_CONTROL);
	features_expect_header (&response, FEATURES_TEST_KEPT_NAME, FEATURES_TEST_KEPT_VALUE);
	features_expect_header (&response, "Date", FEATURES_TEST_OWN_DATE);
	test_check_unsigned_eq (features_header_count (&response, "\r\nDate: "), 1, NULL);
	test_check_unsigned_eq (features_header_count (&response, "\r\nContent-Length: "), 1, NULL);
	test_check_unsigned_eq (features_content_length (&response), features_json_length, NULL);
	test_check_unsigned_eq (response.body_length, features_json_length, NULL);
	test_check_ptr (features_header (&response, "ETag", etag));

	// its 304 keeps the Cache-Control and the entity tag, never the length nor the type nor the others
	(void) snprintf (header, sizeof (header), "If-None-Match: %s\r\n", etag);
	features_get (path, header, &response);
	features_expect_status (&response, "HTTP/1.1 304");
	features_expect_header (&response, "Cache-Control", FEATURES_TEST_CACHE_CONTROL);
	features_expect_header (&response, "ETag", etag);
	features_expect_no_header (&response, "Content-Length");
	features_expect_no_header (&response, "Content-Type");
	features_expect_no_header (&response, FEATURES_TEST_KEPT_NAME);
	test_check_unsigned_eq (response.body_length, 0, NULL);

	// the Date of the response is one of the headers that a 304 does not keep: it is not found
	// in the message anymore, so cerver adds its own (the 304 of the list had no Date)
	test_check_ptr (features_header (&response, "Date", date));
	test_check (strcmp (date, FEATURES_TEST_OWN_DATE) != 0, "the 304 kept the Date of the response");
	test_check_unsigned_eq (features_header_count (&response, "\r\nDate: "), 1, NULL);

	// compressed: only the length of the plain body is taken away, and the one of the gzip is added
	features_get (path, "Accept-Encoding: gzip\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Cache-Control", FEATURES_TEST_CACHE_CONTROL);
	features_expect_header (&response, FEATURES_TEST_KEPT_NAME, FEATURES_TEST_KEPT_VALUE);
	features_expect_header (&response, "Content-Type", "application/json");
	features_expect_header (&response, "Date", FEATURES_TEST_OWN_DATE);
	test_check_unsigned_eq (features_header_count (&response, "\r\nDate: "), 1, NULL);
	test_check_unsigned_eq (features_header_count (&response, "\r\nContent-Length: "), 1, NULL);
	test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);

	if (cerver_http_compression_available ()) {
		features_expect_header (&response, "Content-Encoding", HTTP_COMPRESSION_ENCODING);
		test_check (response.body_length < features_json_length, "the body was not compressed");
	}

	else {
		test_check_unsigned_eq (response.body_length, features_json_length, NULL);
	}

}

static void test_features_filtered (void) {

	features_filtered ("/features/listed");

	features_filtered ("/features/raw");

}

static void test_features_static_files (void) {

	FeaturesResponse response;
	char etag[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char last_modified[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char header[FEATURES_TEST_VALUE_SIZE * 2] = { 0 };

	features_get ("/index.html", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	test_check_ptr (features_header (&response, "ETag", etag));
	test_check (!strncmp (etag, "W/\"", 3), "the entity tag of a file is a weak one");
	test_check_ptr (features_header (&response, "Last-Modified", last_modified));
	features_expect_header (&response, "Date", NULL);
	test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);

	(void) snprintf (header, sizeof (header), "If-None-Match: %s\r\n", etag);
	features_get ("/index.html", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");
	test_check_unsigned_eq (response.body_length, 0, NULL);

	(void) snprintf (header, sizeof (header), "If-Modified-Since: %s\r\n", last_modified);
	features_get ("/index.html", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");

	features_get ("/index.html", "If-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");

}

static void test_features_compression (void) {

	FeaturesResponse response;

	features_get ("/features/json", "Accept-Encoding: gzip, deflate\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Vary", "Origin, Accept-Encoding");

	if (cerver_http_compression_available ()) {
		features_expect_header (&response, "Content-Encoding", HTTP_COMPRESSION_ENCODING);

		// the Content-Length is the one of the compressed body
		test_check_unsigned_eq (features_header_count (&response, "\r\nContent-Length: "), 1, NULL);
		test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);
		test_check (response.body_length < features_json_length, "the body was not compressed");
		test_check_unsigned_eq ((unsigned char) response.body[0], 0x1f, NULL);
		test_check_unsigned_eq ((unsigned char) response.body[1], 0x8b, NULL);

		// the compressed representation has its own entity tag
		char etag[FEATURES_TEST_VALUE_SIZE] = { 0 };
		test_check_ptr (features_header (&response, "ETag", etag));
		test_check (strstr (etag, HTTP_ETAG_COMPRESSED_SUFFIX) != NULL, NULL);
	}

	else {
		features_expect_no_header (&response, "Content-Encoding");
	}

	// a client that does not accept gzip
	features_get ("/features/json", NULL, &response);
	features_expect_no_header (&response, "Content-Encoding");
	test_check_unsigned_eq (response.body_length, features_json_length, NULL);
	test_check (!memcmp (response.body, features_json, features_json_length), NULL);

	features_get ("/features/json", "Accept-Encoding: gzip;q=0\r\n", &response);
	features_expect_no_header (&response, "Content-Encoding");

	// a small body or a type that is not worth it
	features_get ("/features/small", "Accept-Encoding: gzip\r\n", &response);
	features_expect_no_header (&response, "Content-Encoding");

	features_get ("/features/image", "Accept-Encoding: gzip\r\n", &response);
	features_expect_no_header (&response, "Content-Encoding");
	test_check_unsigned_eq (response.body_length, features_json_length, NULL);

	// a body under the smallest one that the cerver compresses is compressed only by the endpoint that set
	// its own smallest one
	features_get ("/features/short", "Accept-Encoding: gzip\r\n", &response);
	features_expect_no_header (&response, "Content-Encoding");
	test_check_unsigned_eq (response.body_length, strlen (features_short_json), NULL);

	features_get ("/features/shortest", "Accept-Encoding: gzip\r\n", &response);

	if (cerver_http_compression_available ()) {
		features_expect_header (&response, "Content-Encoding", HTTP_COMPRESSION_ENCODING);
		test_check (response.body_length < strlen (features_short_json), "the short body was not compressed");
	}

	else {
		features_expect_no_header (&response, "Content-Encoding");
	}

}

static void test_features_stream (void) {

	FeaturesResponse response;

	features_get ("/features/stream", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Transfer-Encoding", HTTP_STREAM_CHUNKED_ENCODING);
	features_expect_no_header (&response, "Content-Length");
	features_expect_no_header (&response, "ETag");
	features_expect_no_header (&response, "Content-Encoding");
	features_expect_header (&response, "Date", NULL);

	// the handler did not end it, cerver did
	test_check_str_eq (response.body, "5\r\nfirst\r\n6\r\nsecond\r\n0\r\n\r\n", NULL);

	// an HTTP/1.0 client receives the body as it is, until the connection is closed
	static const char *old_request = { "GET /features/stream HTTP/1.0\r\n\r\n" };
	features_request (old_request, strlen (old_request), &response);
	features_expect_no_header (&response, "Transfer-Encoding");
	test_check_str_eq (response.body, "firstsecond", NULL);

	features_get ("/features/stream/ended", NULL, &response);
	test_check_str_eq (response.body, "5\r\nfirst\r\n0\r\n\r\n", NULL);

	// a HEAD only receives the header
	static const char *head_request = { "HEAD /features/stream HTTP/1.1\r\nHost: localhost\r\n\r\n" };
	features_request (head_request, strlen (head_request), &response);
	features_expect_header (&response, "Transfer-Encoding", HTTP_STREAM_CHUNKED_ENCODING);
	test_check_unsigned_eq (response.body_length, 0, NULL);

}

// a chunked body arrives decoded, and its trailers are not headers of the request
static void test_features_chunked_body (void) {

	FeaturesResponse response;

	static const char *chunked = {
		"POST /features/echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
		"4\r\nWiki\r\n6;name=value\r\npedia \r\nE\r\nin \r\n\r\nchunks.\r\n0\r\n"
		"X-Request-Id: trailer-id\r\nOrigin: https://ermiry.com\r\n\r\n"
	};

	features_request (chunked, strlen (chunked), &response);
	features_expect_status (&response, "HTTP/1.1 200");
	static const char *decoded = { "Wikipedia in \r\n\r\nchunks." };
	test_check_unsigned_eq (response.body_length, strlen (decoded), NULL);
	test_check (!memcmp (response.body, decoded, strlen (decoded)), NULL);

	// the trailers were ignored
	char value[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const char *id = features_header (&response, "X-Request-Id", value);
	test_check_ptr (id);
	test_check (strcmp (id, "trailer-id"), "a trailer was used as a header");
	features_expect_no_header (&response, "Access-Control-Allow-Origin");

	// Transfer-Encoding with Content-Length is a request smuggling attempt
	static const char *smuggled = {
		"POST /features/echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n"
		"0\r\n\r\n"
	};

	features_request (smuggled, strlen (smuggled), &response);
	features_expect_status (&response, "HTTP/1.1 400");

	// an unknown transfer coding
	static const char *unknown = {
		"POST /features/echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: gzip\r\n\r\n"
		"abcd"
	};

	features_request (unknown, strlen (unknown), &response);
	features_expect_status (&response, "HTTP/1.1 400");

}

static void test_features_metrics (void) {

	FeaturesResponse response;

	features_get ("/features/cerver/metrics", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Content-Type", HTTP_METRICS_CONTENT_TYPE);
	test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);

	static const char *expected[] = {
		"# TYPE cerver_info gauge\n",
		"cerver_info{version=\"" CERVER_VERSION "\"} 1\n",
		"# TYPE cerver_http_responses_total counter\n",
		"cerver_http_responses_total{code=\"200\"} ",
		"cerver_http_responses_total{code=\"304\"} ",
		"cerver_http_responses_total{code=\"404\"} ",
		"cerver_http_requests_unhandled_total ",
		"cerver_thpool_threads 2\n",
		"# TYPE cerver_thpool_jobs_total counter\n",
		"cerver_thpool_jobs_total ",
		"cerver_thpool_jobs_completed_total ",
		"cerver_thpool_rejected_total ",
		"cerver_thpool_dropped_total ",
		"cerver_thpool_waited_total ",
		"cerver_thpool_queued_max ",
		"# TYPE cerver_thpool_wait_seconds histogram\n",
		"cerver_thpool_wait_seconds_bucket{le=\"0.001\"} ",
		"cerver_thpool_wait_seconds_bucket{le=\"+Inf\"} ",
		"cerver_thpool_wait_seconds_sum ",
		"cerver_thpool_run_seconds_bucket{le=\"262.144\"} ",
		"cerver_thpool_run_seconds_count ",
		"cerver_http_requests_total{method=\"GET\",path=\"/features/small\"} ",
		"# TYPE cerver_http_request_duration_seconds histogram\n",
		"cerver_http_request_duration_seconds_bucket{method=\"GET\",path=\"/features/small\",le=\"0.005\"} ",
		"cerver_http_request_duration_seconds_bucket{method=\"GET\",path=\"/features/small\",le=\"+Inf\"} ",
		"cerver_http_request_duration_seconds_count{method=\"GET\",path=\"/features/small\"} ",
		"cerver_http_request_duration_seconds_sum{method=\"POST\",path=\"/features/echo\"} "
	};

	for (size_t i = 0; i < (sizeof (expected) / sizeof (const char *)); i++) {
		if (!strstr (response.body, expected[i])) (void) fprintf (stderr, "missing metric: %s\n", expected[i]);
		test_check (strstr (response.body, expected[i]) != NULL, "a metric is missing");
	}

	// a HEAD has the headers of the metrics without them
	FeaturesResponse head;
	static const char *head_request = { "HEAD /features/cerver/metrics HTTP/1.1\r\nHost: localhost\r\n\r\n" };
	features_request (head_request, strlen (head_request), &head);
	features_expect_status (&head, "HTTP/1.1 200");
	test_check_unsigned_eq (head.body_length, 0, NULL);

}

// the date header can be disabled
// the response says once that its connection is closed
static void features_expect_close (const FeaturesResponse *response) {

	if (features_header_count (response, "\r\nConnection: ") != 1) {
		(void) fprintf (stderr, "unexpected Connection headers in: %.*s\n", (int) response->header_length, response->raw);
	}

	test_check_unsigned_eq (features_header_count (response, "\r\nConnection: "), 1, NULL);
	features_expect_header (response, "Connection", HTTP_RESPONSE_CONNECTION_CLOSE);

}

static void features_send (const char *request, FeaturesResponse *response) {

	features_request (request, strlen (request), response);

}

// cerver closes the connection after every response, and each one says it once,
// whatever the way it was created and sent
static void test_features_connection (void) {

	FeaturesResponse response;
	char etag[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char header[FEATURES_TEST_VALUE_SIZE * 2] = { 0 };

	// a compiled response of cerver
	features_get ("/features/small", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);
	test_check_ptr (features_header (&response, "ETag", etag));

	features_get ("/plain/small", NULL, &response);
	features_expect_close (&response);

	// its 304
	(void) snprintf (header, sizeof (header), "If-None-Match: %s\r\n", etag);
	features_get ("/features/small", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");
	features_expect_close (&response);

	// its HEAD
	features_send ("HEAD /features/small HTTP/1.1\r\nHost: localhost\r\n\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);

	// a rendered body, plain and compressed
	features_get ("/features/json", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);

	features_get ("/features/json", "Accept-Encoding: gzip\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);
	test_check_unsigned_eq (features_header_count (&response, "\r\nContent-Length: "), 1, NULL);

	// a response with its own headers
	features_get ("/features/image", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);

	// the errors of cerver
	features_get ("/features/missing", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 404");
	features_expect_close (&response);

	features_send ("DELETE /features/small HTTP/1.1\r\nHost: localhost\r\n\r\n", &response);
	features_expect_close (&response);

	features_send ("GET /features/small HTTP/1.1\r\nHost: localhost\r\nBad Header\r\n\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 400");
	features_expect_close (&response);

	// the answer to a preflight
	features_send (
		"OPTIONS /features/small HTTP/1.1\r\nHost: localhost\r\n"
		"Origin: https://ermiry.com\r\nAccess-Control-Request-Method: GET\r\n\r\n",
		&response
	);

	features_expect_close (&response);

	// a redirect to the path without its last slash
	features_get ("/features/small/", NULL, &response);
	features_expect_close (&response);

	// a stream, chunked and until the connection is closed
	features_get ("/features/stream", NULL, &response);
	features_expect_header (&response, "Transfer-Encoding", HTTP_STREAM_CHUNKED_ENCODING);
	features_expect_close (&response);

	features_send ("GET /features/stream HTTP/1.0\r\n\r\n", &response);
	features_expect_close (&response);

	// a file, its 304 and a range of it
	features_get ("/index.html", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);
	test_check_ptr (features_header (&response, "ETag", etag));

	(void) snprintf (header, sizeof (header), "If-None-Match: %s\r\n", etag);
	features_get ("/index.html", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");
	features_expect_close (&response);

	features_get ("/features/video", "Range: bytes=0-15\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 206");
	features_expect_close (&response);
	features_expect_header (&response, "Content-Range", NULL);
	test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);

	// a body without its length
	features_get ("/features/created", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 201");
	features_expect_close (&response);
	features_expect_no_header (&response, "Content-Length");
	test_check_str_eq (response.body, features_stream_first, NULL);

	// a response that already says it is not told twice
	features_get ("/features/connection", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);
	test_check_str_eq (response.body, features_stream_first, NULL);

	// the admin routes, that are endpoints of their own group
	features_get ("/features/cerver/stats", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_close (&response);

}

static void features_head (const char *path, FeaturesResponse *response) {

	char request[FEATURES_TEST_RESPONSE_SIZE] = { 0 };
	const int written = snprintf (
		request, FEATURES_TEST_RESPONSE_SIZE,
		"HEAD %s HTTP/1.1\r\nHost: localhost\r\n\r\n", path
	);

	features_request (request, (size_t) written, response);

}

// the json of an admin route: its GET has it whole, with its length, and its HEAD has only the headers;
// a json that does not change between two requests is the expected one and has the same length in both
static void features_admin_route (const char *path, const char *start, const char *expected) {

	FeaturesResponse response;

	features_get (path, NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Content-Type", "application/json");
	test_check_unsigned_eq (response.body_length, features_content_length (&response), NULL);
	test_check (!strncmp (response.body, start, strlen (start)), response.body);
	test_check (response.body[response.body_length - 1] == '}', response.body);

	if (expected) test_check_str_eq (response.body, expected, NULL);

	features_head (path, &response);
	features_expect_status (&response, "HTTP/1.1 200");
	features_expect_header (&response, "Content-Type", "application/json");
	test_check_unsigned_eq (response.body_length, 0, "a HEAD with a body");
	test_check (features_content_length (&response) > 0, "a HEAD without the length of its json");

	if (expected) {
		test_check_unsigned_eq (
			features_content_length (&response), strlen (expected), "the length of a HEAD is not the one of its GET"
		);
	}

}

// the admin routes answer with their json, written in the arena of the request
static void test_features_admin (void) {

	features_admin_route ("/features/cerver/info", "{\"name\": \"", NULL);
	features_admin_route ("/features/cerver/stats", "{\"groups\": ", NULL);
	features_admin_route ("/features/cerver/stats/filesystems", "{\"filesystems\": [", "{\"filesystems\": []}");
	features_admin_route ("/features/cerver/stats/workers", "{\"workers\": [", "{\"workers\": []}");

}

static unsigned int features_twice_read (const unsigned int *result) {

	return __atomic_load_n (result, __ATOMIC_SEQ_CST);

}

// a request takes one response: its handler sent another one, a common one and the start of a stream after it,
// each of them was refused and nothing of them reached the client
static void test_features_one_response (void) {

	FeaturesResponse response;

	features_get ("/plain/twice", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");

	test_check_unsigned_eq (response.body_length, strlen (features_twice_first), NULL);
	test_check_str_eq (response.body, features_twice_first, NULL);

	test_check (strstr (response.raw, features_twice_second) == NULL, "the second response was sent");
	test_check (strstr (response.body, "HTTP/1.1 ") == NULL, "another response followed the first one");

	test_check_unsigned_eq (features_twice_read (&features_twice_first_result), 0, NULL);
	test_check_unsigned_eq (features_twice_read (&features_twice_second_result), 1, NULL);
	test_check_unsigned_eq (features_twice_read (&features_twice_common_result), 1, NULL);
	test_check_unsigned_eq (features_twice_read (&features_twice_stream_result), 1, NULL);

}

// the values of a request are views of its memory: they end with '\0', one that the request does not have has
// no data, and each getter gives what the request brought (the path and the query as they arrived, the values of
// the query decoded)
static void test_features_views (void) {

	FeaturesResponse response;

	features_get ("/plain/views/64?first=one&second=two%20words", "X-Custom: custom value\r\n", &response);
	features_expect_status (&response, "HTTP/1.1 200");
	test_check_str_eq (response.body, features_views_body, NULL);

	// without a context there is no request and nothing to answer
	test_check_true (cerver_http_view_is_missing (cerver_http_request_path (NULL)));
	test_check_true (cerver_http_view_is_missing (cerver_http_request_param (NULL, "id")));
	test_check_true (cerver_http_view_is_missing (cerver_http_request_peer_ip (NULL)));
	test_check_unsigned_eq (cerver_http_request_params_count (NULL), 0, NULL);
	test_check_int_eq ((int) cerver_http_request_method (NULL), (int) REQUEST_METHOD_UNDEFINED, NULL);
	test_check_null_ptr (cerver_http_request_head (NULL));

	test_check_int_eq (cerver_http_context_socket (NULL), -1, NULL);
	test_check_false (cerver_http_context_client_is_gone (NULL));
	test_check_int_eq ((int) cerver_http_context_client_state (NULL), (int) HTTP_CLIENT_STATE_RESET, NULL);

	test_check_unsigned_eq (cerver_http_response_set_header (NULL, HTTP_HEADER_ETAG, "\"tag\""), 1, NULL);
	test_check_unsigned_eq (cerver_http_response_send (NULL, features_twice_first, strlen (features_twice_first)), 1, NULL);
	test_check_unsigned_eq (cerver_http_response_send_common (NULL, CERVER_HTTP_COMMON_RESPONSE_OK), 1, NULL);
	test_check_unsigned_eq (cerver_http_response_stream_begin (NULL), 1, NULL);

}

// a header can be repeated, and they arrive in the order that the handler added them
static void test_features_repeated_headers (void) {

	FeaturesResponse response;

	features_get ("/features/cookies", NULL, &response);
	features_expect_status (&response, "HTTP/1.1 200");

	test_check_unsigned_eq (features_header_count (&response, "\r\nSet-Cookie: "), 2, NULL);
	features_expect_header (&response, features_trace_name, features_trace_value);

	const char *first = strstr (response.raw, "\r\nSet-Cookie: session=first; Path=/\r\n");
	const char *trace = strstr (response.raw, "\r\nX-Trace: abc-123\r\n");
	const char *second = strstr (response.raw, "\r\nSet-Cookie: theme=dark; Path=/\r\n");
	const char *type = strstr (response.raw, "\r\nContent-Type: ");
	const char *date = strstr (response.raw, "\r\nDate: ");

	test_check_ptr (first);
	test_check_ptr (trace);
	test_check_ptr (second);
	test_check_ptr (type);
	test_check_ptr (date);

	test_check (first < trace, NULL);
	test_check (trace < second, NULL);
	test_check (second < type, NULL);

	// the headers that cerver adds for the request go after the ones of the response
	test_check (type < date, NULL);

	features_expect_close (&response);
	test_check_str_eq (response.body, features_stream_first, NULL);

	// the 304 of the response keeps none of them
	char etag[FEATURES_TEST_VALUE_SIZE] = { 0 };
	char header[FEATURES_TEST_VALUE_SIZE * 2] = { 0 };
	test_check_ptr (features_header (&response, "ETag", etag));

	(void) snprintf (header, sizeof (header), "If-None-Match: %s\r\n", etag);
	features_get ("/features/cookies", header, &response);
	features_expect_status (&response, "HTTP/1.1 304");
	test_check_unsigned_eq (features_header_count (&response, "\r\nSet-Cookie: "), 0, NULL);
	features_expect_no_header (&response, features_trace_name);
	features_expect_close (&response);

}

// a cerver without the Date header, the cerver of the other tests is started again with it
static void test_features_without_date (void) {

	features_cerver_stop ();

	features_date_header = false;
	features_cerver_start ();

	FeaturesResponse response;
	features_get ("/plain/small", NULL, &response);
	features_expect_no_header (&response, "Date");

	features_cerver_stop ();

	features_date_header = true;
	features_cerver_start ();

}

#pragma endregion

// the features of a cerver, that handles its connections with event loops
static void features_tests (void) {

	(void) alarm (FEATURES_TEST_ALARM);

	features_cerver_start ();

	test_features_date ();
	test_features_request_id ();
	test_features_client_ip ();
	test_features_cors ();
	test_features_etag ();
	test_features_static_files ();
	test_features_compression ();
	test_features_filtered ();
	test_features_stream ();
	test_features_chunked_body ();
	test_features_metrics ();
	test_features_connection ();
	test_features_admin ();
	test_features_one_response ();
	test_features_views ();
	test_features_repeated_headers ();
	test_features_without_date ();

	features_cerver_stop ();

	(void) alarm (0);

}

void http_tests_features (void) {

	(void) printf ("Testing HTTP features...\n");

	features_json_create ();

	features_tests ();

	(void) printf ("Done!\n");

}
