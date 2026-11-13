#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <pthread.h>

#include <cerver/cerver.h>
#include <cerver/network.h>
#include <cerver/version.h>

#include <cerver/http/compression.h>
#include <cerver/http/conditional.h>
#include <cerver/http/date.h>
#include <cerver/http/headers.h>
#include <cerver/http/http.h>
#include <cerver/http/metrics.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/stream.h>
#include <cerver/http/tree.h>

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
static void features_small_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// GET /features/json, a body that is compressed
static void features_json_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_render_json (
		http_receive, HTTP_STATUS_OK, features_json, features_json_length
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

#define FEATURES_TEST_RAW_HEADER																	\
	"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n"					\
	"Cache-Control: " FEATURES_TEST_CACHE_CONTROL "\r\n"											\
	"Date: " FEATURES_TEST_OWN_DATE "\r\n"															\
	FEATURES_TEST_KEPT_NAME ": " FEATURES_TEST_KEPT_VALUE "\r\n\r\n"

// GET /features/listed, the body with headers of its own in the list of the response
static void features_listed_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	(void) http_response_add_header (response, HTTP_HEADER_CONTENT_TYPE, "application/json");
	(void) http_response_add_content_length_header (response, features_json_length);
	(void) http_response_add_header (response, HTTP_HEADER_CACHE_CONTROL, FEATURES_TEST_CACHE_CONTROL);
	(void) http_response_add_header (response, HTTP_HEADER_DATE, FEATURES_TEST_OWN_DATE);
	(void) http_response_header_add_by_name (response, FEATURES_TEST_KEPT_NAME, FEATURES_TEST_KEPT_VALUE);
	(void) http_response_set_data_ref (response, features_json, features_json_length);
	(void) http_response_send (response, http_receive);
	http_response_return (response);

}

// GET /features/raw, the same response with its header set as bytes
static void features_raw_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char header[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const int length = snprintf (header, FEATURES_TEST_VALUE_SIZE, FEATURES_TEST_RAW_HEADER, features_json_length);

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	http_response_set_header (response, strdup (header), (size_t) length);
	(void) http_response_set_data_ref (response, features_json, features_json_length);
	(void) http_response_send (response, http_receive);
	http_response_return (response);

}

// GET /features/image, a body that is never compressed
static void features_image_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	(void) http_response_add_header (response, HTTP_HEADER_CONTENT_TYPE, "image/png");
	(void) http_response_add_content_length_header (response, features_json_length);
	(void) http_response_set_data_ref (response, features_json, features_json_length);
	(void) http_response_send (response, http_receive);
	http_response_return (response);

}

// GET /features/stream, the handler does not end its stream
static void features_stream_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_TXT);

	if (!http_response_stream_begin (http_receive, response)) {
		(void) http_response_stream_write (http_receive, features_stream_first, strlen (features_stream_first));

		// an empty piece is not sent, it would end the chunked body
		(void) http_response_stream_write (http_receive, "", 0);

		(void) http_response_stream_write (http_receive, features_stream_second, strlen (features_stream_second));

		// a normal response can not be sent in the middle of a stream
		test_check_unsigned_eq (http_response_send (oki_doki, http_receive), 1, NULL);
	}

	http_response_return (response);

}

// GET /features/stream/ended
static void features_stream_ended_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);

	// a stream can not announce its length
	(void) http_response_add_content_length_header (response, 1);
	test_check_unsigned_eq (http_response_stream_begin (http_receive, response), 1, NULL);
	http_response_reset (response);
	http_response_set_status (response, HTTP_STATUS_OK);

	test_check_unsigned_eq (http_response_stream_begin (http_receive, response), 0, NULL);
	test_check_true (http_response_stream_is_active (http_receive));
	test_check_unsigned_eq (http_response_stream_begin (http_receive, response), 1, NULL);

	(void) http_response_stream_write (http_receive, features_stream_first, strlen (features_stream_first));
	test_check_unsigned_eq (http_response_stream_end (http_receive), 0, NULL);
	test_check_unsigned_eq (http_response_stream_end (http_receive), 0, NULL);
	test_check_false (http_response_stream_is_active (http_receive));

	test_check_unsigned_eq (http_response_stream_write (http_receive, "x", 1), 1, NULL);

	http_response_return (response);

}

// POST /features/echo, the body is sent back as it was received
static void features_echo_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const String *body = http_request_get_body (request);

	(void) http_response_render_text (
		http_receive, HTTP_STATUS_OK, body ? body->str : "", body ? body->len : 0
	);

}

// GET /features/client, the request's id and the client's address
static void features_client_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	char json[FEATURES_TEST_VALUE_SIZE] = { 0 };
	const int written = snprintf (
		json, FEATURES_TEST_VALUE_SIZE, "{\"ip\":\"%s\",\"id\":\"%s\"}",
		http_request_get_client_ip (request), http_request_get_id (request)
	);

	(void) http_response_render_json (http_receive, HTTP_STATUS_OK, json, (size_t) written);

}

// GET /features/cookies, a header that is repeated and one with a name of the application
static void features_cookies_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_SET_COOKIE, features_first_cookie), 0, NULL);
	test_check_unsigned_eq (http_response_header_add_by_name (response, features_trace_name, features_trace_value), 0, NULL);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_SET_COOKIE, features_second_cookie), 0, NULL);
	(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_TXT);
	(void) http_response_add_content_length_header (response, strlen (features_stream_first));
	(void) http_response_set_data_ref (response, features_stream_first, strlen (features_stream_first));
	(void) http_response_send (response, http_receive);
	http_response_return (response);

}

// GET /features/connection, a response that says what its connection does
static void features_connection_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_CONNECTION, HTTP_RESPONSE_CONNECTION_CLOSE), 0, NULL);
	(void) http_response_add_content_length_header (response, strlen (features_stream_first));
	(void) http_response_set_data_ref (response, features_stream_first, strlen (features_stream_first));
	(void) http_response_send (response, http_receive);
	http_response_return (response);

}

// GET /features/created, a body without its length, that ends when the connection is closed
static void features_created_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_create_and_send (
		HTTP_STATUS_CREATED, features_stream_first, strlen (features_stream_first), http_receive
	);

}

// GET /features/video, a range of a file
static void features_video_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_handle_video (http_receive, features_video_file);

}

// GET /plain/small, an endpoint without the settings of the features group
static void features_plain_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

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

static void features_routes (HttpCerver *http_cerver) {

	HttpGroup *features = http_cerver_group_create (http_cerver, "features");
	http_group_set_cors (features, true);
	http_group_set_etag (features, true);
	http_group_set_compression (features, true);

	test_check_ptr (http_group_get (features, "small", features_small_handler));
	test_check_ptr (http_group_get (features, "json", features_json_handler));
	test_check_ptr (http_group_get (features, "listed", features_listed_handler));
	test_check_ptr (http_group_get (features, "raw", features_raw_handler));
	test_check_ptr (http_group_get (features, "image", features_image_handler));
	test_check_ptr (http_group_get (features, "stream", features_stream_handler));
	test_check_ptr (http_group_get (features, "stream/ended", features_stream_ended_handler));
	test_check_ptr (http_group_post (features, "echo", features_echo_handler));
	test_check_ptr (http_group_get (features, "client", features_client_handler));
	test_check_ptr (http_group_get (features, "cookies", features_cookies_handler));
	test_check_ptr (http_group_get (features, "connection", features_connection_handler));
	test_check_ptr (http_group_get (features, "created", features_created_handler));
	test_check_ptr (http_group_get (features, "video", features_video_handler));

	HttpGroup *plain = http_cerver_group_create (http_cerver, "plain");
	test_check_ptr (http_group_get (plain, "small", features_plain_handler));

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

static void features_cerver_start (void) {

	features_cerver = cerver_create_web ("features-test", FEATURES_TEST_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (features_cerver);

	cerver_set_handler_type (features_cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (features_cerver, true);
	cerver_set_thpool_n_threads (features_cerver, 2);

	HttpCerver *http_cerver = (HttpCerver *) features_cerver->cerver_data;

	http_cerver_set_request_id (http_cerver, true);

	// a path without a route is answered with 404 instead of the catch all
	http_cerver_set_not_found_handler (http_cerver);

	// the test client connects from 127.0.0.1, like nginx from its private network
	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	test_check_unsigned_eq (http_cerver_add_origin_to_whitelist (http_cerver, features_origin), 0, NULL);

	(void) http_cerver_static_path_add (http_cerver, "./test/web/public");

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_head_handlers (http_cerver, true);

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
	test_check_true (http_date_parse (date, strlen (date), &parsed));

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

	if (http_compression_available ()) {
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

	if (http_compression_available ()) {
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

	// the admin routes
	features_get ("/cerver/stats", NULL, &response);
	features_expect_close (&response);

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

static void test_features_without_date (void) {

	HttpCerver *http_cerver = (HttpCerver *) features_cerver->cerver_data;
	http_cerver_set_date_header (http_cerver, false);

	FeaturesResponse response;
	features_get ("/plain/small", NULL, &response);
	features_expect_no_header (&response, "Date");

	http_cerver_set_date_header (http_cerver, true);

}

#pragma endregion

void http_tests_features (void) {

	(void) printf ("Testing HTTP features...\n");

	(void) alarm (FEATURES_TEST_ALARM);

	features_json_create ();

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
	test_features_repeated_headers ();
	test_features_without_date ();

	features_cerver_stop ();

	(void) alarm (0);

	(void) printf ("Done!\n");

}
