#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/http/core/http.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/security/origin.h>

#include "../../test.h"

#define TEST_HEADER_LEN		256
#define TEST_COMPILED_LEN	2048
#define TEST_LONG_VALUE_LEN	700
#define TEST_FORMAT_LEN		(HTTP_RESPONSE_HEADER_FORMAT_SIZE + 16)

static char json_header[TEST_HEADER_LEN] = { 0 };
static char header_content_len[TEST_HEADER_LEN] = { 0 };
static char header_content_type[TEST_HEADER_LEN] = { 0 };

static const char *content_type_header = { "Content-Type: application/json\r\n" };
static const char *content_length_header = { "Content-Length: 128\r\n" };
static const char *json_data = { "{ \"oki\": \"doki\" }" };

// cors
static const char *domain = { "https://ermiry.com" };
static const char *allow_origin_header = { "Access-Control-Allow-Origin: https://ermiry.com\r\n" };
static const char *allow_credentials_header = { "Access-Control-Allow-Credentials: true\r\n" };
static const char *allow_single_method_header = { "Access-Control-Allow-Methods: GET\r\n" };
static const char *allow_multiple_methods_header = { "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n" };

static HttpCerver *http_cerver = NULL;

// the response has one header with the type and its line is the expected one
static void responses_check_header (
	const HttpResponse *response, const http_header type, const char *expected_line
) {

	test_check_unsigned_eq (http_response_header_count (response, type), 1, NULL);

	const HttpHeaderField *field = http_headers_find (&response->headers, type, 0);
	test_check_ptr (field);

	size_t line_length = 0;
	const char *line = http_headers_field_line (&response->headers, field, &line_length);

	test_check_unsigned_eq (line_length, strlen (expected_line), NULL);
	test_check (!memcmp (line, expected_line, line_length), NULL);

}

// the compiled header of the response is its status line, the Server header,
// the expected lines and the empty line
static void responses_check_compiled (
	HttpResponse *response, const char *status, const char *lines
) {

	char expected[TEST_COMPILED_LEN] = { 0 };
	(void) snprintf (
		expected, TEST_COMPILED_LEN,
		"HTTP/1.1 %s\r\nServer: Cerver/%s\r\n%s\r\n",
		status, CERVER_VERSION, lines
	);

	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq ((const char *) response->header, expected, NULL);
	test_check_unsigned_eq (response->header_len, strlen (expected), NULL);

}

static void test_http_responses_init (void) {

	(void) snprintf (
		json_header, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\nContent-Length: 128\r\n\r\n",
		CERVER_VERSION
	);

	(void) snprintf (
		header_content_len, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nContent-Length: 128\r\n\r\n",
		CERVER_VERSION
	);

	(void) snprintf (
		header_content_type, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\n\r\n",
		CERVER_VERSION
	);

	http_cerver = http_cerver_new ();

	test_check_ptr (http_cerver);

	test_check_unsigned_eq (
		http_responses_init (http_cerver), 0, "Failed to init HTTP responses!"
	);

}

static void test_http_responses_end (void) {

	http_responses_end ();

	http_cerver_delete (http_cerver);

}

#pragma region main

static HttpResponse *test_http_response_new (void) {

	HttpResponse *response = (HttpResponse *) http_response_new ();

	test_check_ptr (response);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 0, NULL);

	test_check_unsigned_eq (response->headers.length, 0, NULL);
	test_check_null_ptr (response->headers.fields);
	test_check_null_ptr (response->headers.buffer);

	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	return response;

}

static void test_http_response_reset (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_reset (response);

	test_check_ptr (response);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 0, NULL);

	test_check_unsigned_eq (response->headers.length, 0, NULL);
	test_check_null_ptr (response->headers.fields);
	test_check_null_ptr (response->headers.buffer);

	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_set_status (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_status (response, HTTP_STATUS_BAD_REQUEST);
	test_check_unsigned_eq (response->status, HTTP_STATUS_BAD_REQUEST, NULL);

	http_response_set_status (response, HTTP_STATUS_INTERNAL_SERVER_ERROR);
	test_check_unsigned_eq (response->status, HTTP_STATUS_INTERNAL_SERVER_ERROR, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region headers

static void test_http_response_set_header (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_header (response, strdup (json_header), strlen (json_header));
	test_check_ptr (response->header);
	test_check_str_eq (response->header, json_header, NULL);
	test_check_str_len (response->header, strlen (json_header), NULL);

	http_response_delete (response);

}

static void test_http_response_add_header (void) {

	u8 result = 0;
	HttpResponse *response = test_http_response_new ();

	// add one header
	result = http_response_add_header (
		response, HTTP_HEADER_CONTENT_TYPE,
		http_content_type_mime (HTTP_CONTENT_TYPE_JSON)
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);
	responses_check_header (response, HTTP_HEADER_CONTENT_TYPE, content_type_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// add another header
	result = http_response_add_header (
		response, HTTP_HEADER_CONTENT_LENGTH, "128"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 2, NULL);
	responses_check_header (response, HTTP_HEADER_CONTENT_LENGTH, content_length_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, json_header, NULL);
	test_check_unsigned_eq (response->header_len, strlen (json_header), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_content_type_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_content_type_header (
		response, HTTP_CONTENT_TYPE_JSON
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_CONTENT_TYPE, content_type_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, header_content_type, NULL);
	test_check_unsigned_eq (response->header_len, strlen (header_content_type), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_content_length_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_content_length_header (
		response, 128
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_CONTENT_LENGTH, content_length_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, header_content_len, NULL);
	test_check_unsigned_eq (response->header_len, strlen (header_content_len), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_json_headers (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_add_json_headers (
		response, 128
	);

	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 2, NULL);

	responses_check_header (response, HTTP_HEADER_CONTENT_TYPE, content_type_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	responses_check_header (response, HTTP_HEADER_CONTENT_LENGTH, content_length_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, json_header, NULL);
	test_check_unsigned_eq (response->header_len, strlen (json_header), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region list

static const char *first_cookie = { "session=first; Path=/" };
static const char *second_cookie = { "theme=dark; Path=/" };

static void responses_check_value (
	const HttpResponse *response, const http_header type,
	const unsigned int position, const char *expected
) {

	size_t value_length = 0;
	const char *value = http_response_header_get (response, type, position, &value_length);

	test_check_ptr (value);
	test_check_unsigned_eq (value_length, strlen (expected), NULL);
	test_check (!memcmp (value, expected, value_length), NULL);

}

static void responses_check_value_by_name (
	const HttpResponse *response, const char *name,
	const unsigned int position, const char *expected
) {

	size_t value_length = 0;
	const char *value = http_response_header_get_with_name (response, name, position, &value_length);

	test_check_ptr (value);
	test_check_unsigned_eq (value_length, strlen (expected), NULL);
	test_check (!memcmp (value, expected, value_length), NULL);

}

// a header can be repeated and the headers are sent in the order they were added
static void test_http_response_header_add (void) {

	HttpResponse *response = test_http_response_new ();

	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_SET_COOKIE, first_cookie), 0, NULL);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_CONTENT_TYPE, "text/plain"), 0, NULL);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_SET_COOKIE, second_cookie), 0, NULL);

	test_check_unsigned_eq (response->headers.count, 3, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_SET_COOKIE), 2, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_CONTENT_TYPE), 1, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_ETAG), 0, NULL);

	responses_check_value (response, HTTP_HEADER_SET_COOKIE, 0, first_cookie);
	responses_check_value (response, HTTP_HEADER_SET_COOKIE, 1, second_cookie);
	responses_check_value (response, HTTP_HEADER_CONTENT_TYPE, 0, "text/plain");

	size_t value_length = 1;
	test_check_null_ptr (http_response_header_get (response, HTTP_HEADER_SET_COOKIE, 2, &value_length));
	test_check_unsigned_eq (value_length, 0, NULL);
	test_check_null_ptr (http_response_header_get (response, HTTP_HEADER_ETAG, 0, NULL));

	responses_check_compiled (
		response, "200 OK",
		"Set-Cookie: session=first; Path=/\r\n"
		"Content-Type: text/plain\r\n"
		"Set-Cookie: theme=dark; Path=/\r\n"
	);

	http_response_delete (response);

}

// a set replaces the headers with the type, as the methods that add a header always did
static void test_http_response_header_set (void) {

	HttpResponse *response = test_http_response_new ();

	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_SET_COOKIE, first_cookie), 0, NULL);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_SET_COOKIE, second_cookie), 0, NULL);
	test_check_unsigned_eq (http_response_header_set (response, HTTP_HEADER_SET_COOKIE, "only=one"), 0, NULL);

	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_SET_COOKIE), 1, NULL);
	responses_check_value (response, HTTP_HEADER_SET_COOKIE, 0, "only=one");

	// http_response_add_header () keeps replacing the header of its type
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, "/first"), 0, NULL);
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, "/second"), 0, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_LOCATION), 1, NULL);
	responses_check_value (response, HTTP_HEADER_LOCATION, 0, "/second");

	test_check_unsigned_eq (http_response_add_custom_header (response, HTTP_HEADER_LOCATION, "/item/%d", 16), 0, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_LOCATION), 1, NULL);
	responses_check_value (response, HTTP_HEADER_LOCATION, 0, "/item/16");

	test_check_unsigned_eq (http_response_add_content_length_header (response, 16), 0, NULL);
	test_check_unsigned_eq (http_response_add_content_length_header (response, 128), 0, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_CONTENT_LENGTH), 1, NULL);
	responses_check_value (response, HTTP_HEADER_CONTENT_LENGTH, 0, "128");

	test_check_unsigned_eq (http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_HTML), 0, NULL);
	test_check_unsigned_eq (http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_JSON), 0, NULL);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_CONTENT_TYPE), 1, NULL);
	responses_check_value (response, HTTP_HEADER_CONTENT_TYPE, 0, "application/json");

	responses_check_compiled (
		response, "200 OK",
		"Set-Cookie: only=one\r\n"
		"Location: /item/16\r\n"
		"Content-Length: 128\r\n"
		"Content-Type: application/json\r\n"
	);

	test_check_unsigned_eq (http_response_header_remove (response, HTTP_HEADER_LOCATION), 1, NULL);
	test_check_unsigned_eq (http_response_header_remove (response, HTTP_HEADER_LOCATION), 0, NULL);
	test_check_unsigned_eq (response->headers.count, 3, NULL);

	http_response_delete (response);

}

// a header with any name, it is found without case, and a known name is its type
static void test_http_response_header_by_name (void) {

	HttpResponse *response = test_http_response_new ();

	test_check_unsigned_eq (http_response_header_add_with_name (response, "X-Trace", "one"), 0, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "x-trace", "two"), 0, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "Vary", "Accept"), 0, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "content-type", "text/plain"), 0, NULL);

	test_check_unsigned_eq (http_response_header_count_with_name (response, "X-TRACE"), 2, NULL);
	test_check_unsigned_eq (http_response_header_count_with_name (response, "Vary"), 1, NULL);
	test_check_unsigned_eq (http_response_header_count_with_name (response, "Content-Type"), 1, NULL);
	test_check_unsigned_eq (http_response_header_count_with_name (response, "X-Other"), 0, NULL);

	// the known name was added as the header of its type
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_CONTENT_TYPE), 1, NULL);
	responses_check_value (response, HTTP_HEADER_CONTENT_TYPE, 0, "text/plain");

	responses_check_value_by_name (response, "x-TRACE", 0, "one");
	responses_check_value_by_name (response, "x-TRACE", 1, "two");
	responses_check_value_by_name (response, "CONTENT-TYPE", 0, "text/plain");
	test_check_null_ptr (http_response_header_get_with_name (response, "x-trace", 2, NULL));
	test_check_null_ptr (http_response_header_get_with_name (response, "X-Other", 0, NULL));

	responses_check_compiled (
		response, "200 OK",
		"X-Trace: one\r\n"
		"x-trace: two\r\n"
		"Vary: Accept\r\n"
		"Content-Type: text/plain\r\n"
	);

	test_check_unsigned_eq (http_response_header_set_with_name (response, "X-Trace", "three"), 0, NULL);
	test_check_unsigned_eq (http_response_header_count_with_name (response, "x-trace"), 1, NULL);
	responses_check_value_by_name (response, "x-trace", 0, "three");

	test_check_unsigned_eq (http_response_header_remove_with_name (response, "VARY"), 1, NULL);
	test_check_unsigned_eq (http_response_header_remove_with_name (response, "Content-type"), 1, NULL);
	test_check_unsigned_eq (http_response_header_remove_with_name (response, "X-Other"), 0, NULL);

	responses_check_compiled (response, "200 OK", "X-Trace: three\r\n");

	http_response_delete (response);

}

// a value that would end its line, a name that is not a token and a value
// that does not fit in its format are rejected, and nothing is added cut
static void test_http_response_header_invalid (void) {

	HttpResponse *response = test_http_response_new ();

	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_LOCATION, "/home\r\nSet-Cookie: stolen=1"), 1, NULL);
	test_check_unsigned_eq (http_response_header_set (response, HTTP_HEADER_LOCATION, "/home\nX-Other: 1"), 1, NULL);
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, "/home\r"), 1, NULL);
	test_check_unsigned_eq (http_response_add_custom_header (response, HTTP_HEADER_LOCATION, "/home%s", "\r\nX-Other: 1"), 1, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "X-Trace", "one\r\ntwo"), 1, NULL);
	test_check_unsigned_eq (http_response_add_cors_header (response, "https://ermiry.com\r\nX-Other: 1"), 1, NULL);

	test_check_unsigned_eq (http_response_header_add_with_name (response, "X Trace", "one"), 1, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "X-Trace:", "one"), 1, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "", "one"), 1, NULL);
	test_check_unsigned_eq (http_response_header_set_with_name (response, "X-Trace\r\n", "one"), 1, NULL);

	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_UNDEFINED, "one"), 1, NULL);
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_UNDEFINED, "one"), 1, NULL);
	test_check_unsigned_eq (http_response_add_header (response, (http_header) HTTP_HEADERS_SIZE, "one"), 1, NULL);

	test_check_unsigned_eq (http_response_header_add (NULL, HTTP_HEADER_LOCATION, "/home"), 1, NULL);
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_LOCATION, NULL), 1, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, NULL, "one"), 1, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "X-Trace", NULL), 1, NULL);
	test_check_unsigned_eq (http_response_header_count (NULL, HTTP_HEADER_LOCATION), 0, NULL);
	test_check_unsigned_eq (http_response_header_remove (NULL, HTTP_HEADER_LOCATION), 0, NULL);
	test_check_null_ptr (http_response_header_get (NULL, HTTP_HEADER_LOCATION, 0, NULL));

	// a value written with a format that does not fit is not added cut
	char format_value[TEST_FORMAT_LEN] = { 0 };
	(void) memset (format_value, 'f', TEST_FORMAT_LEN - 1);
	test_check_unsigned_eq (http_response_add_custom_header (response, HTTP_HEADER_LOCATION, "%s", format_value), 1, NULL);

	test_check_unsigned_eq (response->headers.count, 0, NULL);
	test_check_unsigned_eq (response->headers.length, 0, NULL);

	// a value longer than the 256 bytes that a header had is complete
	char long_value[TEST_LONG_VALUE_LEN] = { 0 };
	(void) memset (long_value, 'v', TEST_LONG_VALUE_LEN - 1);
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_SET_COOKIE, long_value), 0, NULL);
	responses_check_value (response, HTTP_HEADER_SET_COOKIE, 0, long_value);

	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_ptr_ne (response->header, (void *) response->header_buffer);
	test_check_unsigned_eq (response->header_len, strlen ((const char *) response->header), NULL);
	test_check_true (strstr ((const char *) response->header, long_value) != NULL);
	test_check_true (!memcmp ((const char *) response->header + response->header_len - 4, "\r\n\r\n", 4));

	http_response_delete (response);

}

// a header that changes after the response was compiled is in the next header,
// a header that was set as bytes is kept as it is
static void test_http_response_header_compiled_again (void) {

	HttpResponse *response = http_response_create (HTTP_STATUS_OK, json_data, strlen (json_data));
	test_check_ptr (response);

	test_check_unsigned_eq (http_response_add_content_length_header (response, strlen (json_data)), 0, NULL);
	test_check_unsigned_eq (http_response_compile (response), 0, NULL);

	test_check_ptr (response->header);
	test_check_ptr (response->res);

	// the header and the compiled response do not have the new header
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_ETAG, "\"tag\""), 0, NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);
	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	responses_check_compiled (response, "200 OK", "Content-Length: 17\r\nETag: \"tag\"\r\n");

	// the status is part of the header
	http_response_set_status (response, HTTP_STATUS_CREATED);
	test_check_null_ptr (response->header);
	responses_check_compiled (response, "201 Created", "Content-Length: 17\r\nETag: \"tag\"\r\n");

	// the same status keeps the header
	http_response_set_status (response, HTTP_STATUS_CREATED);
	test_check_ptr (response->header);

	test_check_unsigned_eq (http_response_header_remove (response, HTTP_HEADER_ETAG), 1, NULL);
	test_check_null_ptr (response->header);
	responses_check_compiled (response, "201 Created", "Content-Length: 17\r\n");

	// a header that the response does not have does not change it
	test_check_unsigned_eq (http_response_header_remove (response, HTTP_HEADER_ETAG), 0, NULL);
	test_check_ptr (response->header);

	http_response_delete (response);

	// the bytes of a header that was set are sent as they are
	response = test_http_response_new ();

	http_response_set_header (response, strdup (header_content_len), strlen (header_content_len));
	test_check_bool_eq (response->header_is_raw, true, NULL);

	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_ETAG, "\"tag\""), 0, NULL);
	test_check_ptr (response->header);
	test_check_str_eq ((const char *) response->header, header_content_len, NULL);
	test_check_bool_eq (response->header_is_raw, true, NULL);

	// a compiled header replaces it
	responses_check_compiled (response, "200 OK", "ETag: \"tag\"\r\n");
	test_check_bool_eq (response->header_is_raw, false, NULL);

	http_response_delete (response);

}

// the headers of the cerver are added to a response that does not have them
static void test_http_response_header_defaults (void) {

	test_check_unsigned_eq (http_cerver_add_responses_header (http_cerver, HTTP_HEADER_CACHE_CONTROL, "no-cache"), 0, NULL);
	test_check_unsigned_eq (http_cerver_add_responses_header (http_cerver, HTTP_HEADER_CACHE_CONTROL, "no-store"), 0, NULL);
	test_check_unsigned_eq (http_cerver_add_responses_header_with_name (http_cerver, "X-Frame-Options", "DENY"), 0, NULL);
	test_check_unsigned_eq (http_cerver_add_responses_header (http_cerver, HTTP_HEADER_LOCATION, "/home\r\nX-Other: 1"), 1, NULL);
	test_check_unsigned_eq (http_cerver_add_responses_header (http_cerver, HTTP_HEADER_UNDEFINED, "one"), 1, NULL);
	test_check_unsigned_eq (http_cerver_add_responses_header (NULL, HTTP_HEADER_LOCATION, "/home"), 1, NULL);

	test_check_unsigned_eq (http_cerver->response_headers.count, 2, NULL);

	// a response without headers
	HttpResponse *response = test_http_response_new ();
	responses_check_compiled (response, "200 OK", "Cache-Control: no-store\r\nX-Frame-Options: DENY\r\n");
	http_response_delete (response);

	// they go after the headers of the response
	response = test_http_response_new ();
	test_check_unsigned_eq (http_response_add_content_length_header (response, 128), 0, NULL);
	responses_check_compiled (
		response, "200 OK",
		"Content-Length: 128\r\nCache-Control: no-store\r\nX-Frame-Options: DENY\r\n"
	);

	http_response_delete (response);

	// the header of the response is used instead of the one of the cerver
	response = test_http_response_new ();
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_CACHE_CONTROL, "max-age=64"), 0, NULL);
	test_check_unsigned_eq (http_response_header_add_with_name (response, "x-frame-options", "SAMEORIGIN"), 0, NULL);
	responses_check_compiled (
		response, "200 OK",
		"Cache-Control: max-age=64\r\nx-frame-options: SAMEORIGIN\r\n"
	);

	http_response_delete (response);

	// the json responses have them
	response = http_response_json_key_value (HTTP_STATUS_OK, "oki", "doki");
	test_check_ptr (response);
	test_check_true (strstr ((const char *) response->header, "\r\nCache-Control: no-store\r\n") != NULL);
	test_check_true (strstr ((const char *) response->header, "\r\nX-Frame-Options: DENY\r\n") != NULL);
	http_response_delete (response);

	// the next tests have no headers of the cerver
	http_headers_reset (&http_cerver->response_headers);

	response = test_http_response_new ();
	responses_check_compiled (response, "200 OK", "");
	http_response_delete (response);

}

// the header of a response does not say what the connection does,
// that is added when it is sent
static void test_http_response_header_connection (void) {

	HttpResponse *response = http_response_json_key_value (HTTP_STATUS_OK, "oki", "doki");
	test_check_ptr (response);
	test_check_ptr (response->header);
	test_check_true (strstr ((const char *) response->header, "Connection") == NULL);
	test_check_ptr (response->res);
	test_check_true (memmem (response->res, response->res_len, "Connection", strlen ("Connection")) == NULL);

	// a json response has its type and its length as headers
	responses_check_header (response, HTTP_HEADER_CONTENT_TYPE, content_type_header);
	test_check_unsigned_eq (http_response_header_count (response, HTTP_HEADER_CONTENT_LENGTH), 1, NULL);
	test_check_unsigned_eq (response->headers.count, 2, NULL);

	http_response_delete (response);

	response = http_response_create (HTTP_STATUS_NOT_FOUND, NULL, 0);
	test_check_ptr (response);
	responses_check_compiled (response, "404 Not Found", "");
	http_response_delete (response);

	// the one that a response says is its own header
	response = test_http_response_new ();
	test_check_unsigned_eq (http_response_header_add (response, HTTP_HEADER_CONNECTION, "keep-alive"), 0, NULL);
	responses_check_compiled (response, "200 OK", "Connection: keep-alive\r\n");
	http_response_delete (response);

}

#pragma endregion

#pragma region cors

static void test_http_response_add_cors_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_header (
		response, domain
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, allow_origin_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_header_from_origin (void) {

	HttpResponse *response = test_http_response_new ();

	HttpOrigin origin = CERVER_ZERO_INITIALIZER;
	http_origin_init (&origin, domain);

	u8 result = http_response_add_cors_header_from_origin (
		response, &origin
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, allow_origin_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_allow_credentials_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_allow_credentials_header (
		response
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS, allow_credentials_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_allow_methods_header_single (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_allow_methods_header (
		response, "GET"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS, allow_single_method_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_allow_methods_header_multiple (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_allow_methods_header (
		response, "GET, HEAD, OPTIONS"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->headers.count, 1, NULL);

	responses_check_header (response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS, allow_multiple_methods_header);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region data

static void test_http_response_set_data (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_data (response, strdup (json_data), strlen (json_data));
	test_check_ptr (response->data);
	test_check_unsigned_eq (response->data_len, strlen (json_data), NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	http_response_delete (response);

}

static void test_http_response_set_data_ref (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_data_ref (response, (void *) json_data, strlen (json_data));
	test_check_ptr (response->data);
	test_check_unsigned_eq (response->data_len, strlen (json_data), NULL);
	test_check_bool_eq (response->data_ref, true, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region json

// the body of a compiled response, after its header
static const char *test_http_response_json_body (const HttpResponse *response) {

	test_check_ptr (response);
	test_check_ptr (response->data);
	test_check_unsigned_eq (response->data_len, strlen ((const char *) response->data), NULL);

	return (const char *) response->data;

}

// a message with quotes, a backslash and a control character is still valid json
static void test_http_response_json_escaped (void) {

	const char *message = "near '\"admin\": true' \\ \n";

	HttpResponse *response = http_response_json_error (HTTP_STATUS_BAD_REQUEST, message);
	const char *body = test_http_response_json_body (response);

	json_error_t error;
	json_t *json = json_loads (body, 0, &error);
	test_check_ptr (json);
	test_check_unsigned_eq (json_object_size (json), 1, "the message added keys to the json");
	test_check_str_eq (json_string_value (json_object_get (json, "error")), message, NULL);
	json_decref (json);

	http_response_delete (response);

	response = http_response_create_json_key_value (HTTP_STATUS_OK, "key\"", "value\"");
	json = json_loads (test_http_response_json_body (response), 0, &error);
	test_check_ptr (json);
	test_check_str_eq (json_string_value (json_object_get (json, "key\"")), "value\"", NULL);
	json_decref (json);
	http_response_delete (response);

	// a message that is not UTF-8 can not be json
	test_check_null_ptr (http_response_json_msg (HTTP_STATUS_OK, "\xff"));

}

// the bodies of the values keep the separators that they had
static void test_http_response_json_values (void) {

	HttpResponse *response = http_response_json_int_value (HTTP_STATUS_OK, "value", 7);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": 7}", NULL);
	http_response_delete (response);

	response = http_response_json_large_int_value (HTTP_STATUS_OK, "value", 4102444800L);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": 4102444800}", NULL);
	http_response_delete (response);

	// a real is not written with %f nor with the locale
	response = http_response_json_real_value (HTTP_STATUS_OK, "value", 1.5);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": 1.5}", NULL);
	http_response_delete (response);

	response = http_response_json_bool_value (HTTP_STATUS_OK, "value", true);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": true}", NULL);
	http_response_delete (response);

	response = http_response_json_msg (HTTP_STATUS_OK, "oki doki");
	test_check_str_eq (test_http_response_json_body (response), "{\"msg\": \"oki doki\"}", NULL);
	http_response_delete (response);

	// NaN is not json
	test_check_null_ptr (http_response_json_real_value (HTTP_STATUS_OK, "value", (double) NAN));

}

// the helpers freed the body that the response still had, a second compile read it
static void test_http_response_json_owned (void) {

	HttpResponse *responses[] = {
		http_response_json_int_value (HTTP_STATUS_OK, "value", 7),
		http_response_json_large_int_value (HTTP_STATUS_OK, "value", 7),
		http_response_json_real_value (HTTP_STATUS_OK, "value", 7.5),
		http_response_json_bool_value (HTTP_STATUS_OK, "value", false)
	};

	for (size_t i = 0; i < (sizeof (responses) / sizeof (responses[0])); i++) {
		test_check_ptr (responses[i]);
		test_check_bool_eq (responses[i]->data_ref, false, NULL);

		const char *body = test_http_response_json_body (responses[i]);
		test_check (strstr (body, "\"value\"") != NULL, body);

		test_check_unsigned_eq (http_response_compile (responses[i]), 0, NULL);
		test_check (strstr ((const char *) responses[i]->res, "\"value\"") != NULL, NULL);

		http_response_delete (responses[i]);
	}

}

#pragma endregion

void http_tests_responses (void) {

	(void) printf ("Testing HTTP responses...\n");

	test_http_responses_init ();

	// main
	test_http_response_reset ();
	test_http_response_set_status ();

	// headers
	test_http_response_set_header ();
	test_http_response_add_header ();
	test_http_response_add_content_type_header ();
	test_http_response_add_content_length_header ();
	test_http_response_add_json_headers ();

	// list
	test_http_response_header_add ();
	test_http_response_header_set ();
	test_http_response_header_by_name ();
	test_http_response_header_invalid ();
	test_http_response_header_compiled_again ();
	test_http_response_header_defaults ();
	test_http_response_header_connection ();

	// cors
	test_http_response_add_cors_header ();
	test_http_response_add_cors_header_from_origin ();
	test_http_response_add_cors_allow_credentials_header ();
	test_http_response_add_cors_allow_methods_header_single ();
	test_http_response_add_cors_allow_methods_header_multiple ();

	// data
	test_http_response_set_data ();
	test_http_response_set_data_ref ();

	// json
	test_http_response_json_escaped ();
	test_http_response_json_values ();
	test_http_response_json_owned ();

	test_http_responses_end ();

	(void) printf ("Done!\n");

}
