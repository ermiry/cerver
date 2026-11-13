#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ctype.h>

#include <cerver/http/headers.h>

#include "../test.h"

static const char *HEADER_ACCEPT = { "Accept" };
static const char *HEADER_ACCEPT_CHARSET = { "Accept-Charset" };
static const char *HEADER_ACCEPT_ENCODING = { "Accept-Encoding" };
static const char *HEADER_ACCEPT_LANGUAGE = { "Accept-Language" };

static const char *HEADER_ACCESS_CONTROL_REQUEST_HEADERS = { "Access-Control-Request-Headers" };

static const char *HEADER_AUTHORIZATION = { "Authorization" };

static const char *HEADER_CACHE_CONTROL = { "Cache-Control" };

static const char *HEADER_CONNECTION = { "Connection" };

static const char *HEADER_CONTENT_LENGTH = { "Content-Length" };
static const char *HEADER_CONTENT_TYPE = { "Content-Type" };

static const char *HEADER_COOKIE = { "Cookie" };

static const char *HEADER_DATE = { "Date" };

static const char *HEADER_EXPECT = { "Expect" };

static const char *HEADER_HOST = { "Host" };

static const char *HEADER_ORIGIN = { "Origin" };

static const char *HEADER_PROXY_AUTHORIZATION = { "Proxy-Authorization" };

static const char *HEADER_UPGRADE = { "Upgrade" };

static const char *HEADER_USER_AGENT = { "User-Agent" };

static const char *HEADER_WEB_SOCKET_KEY = { "Sec-WebSocket-Key" };
static const char *HEADER_WEB_SOCKET_VERSION = { "Sec-WebSocket-Version" };

static const char *HEADER_OTHER = { "Undefined" };

static const char *content_type_header = { "Content-Type: application/json\r\n" };
static const char *content_length_header = { "Content-Length: 128\r\n" };

static void test_http_headers_get_string (void) {

	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT), HEADER_ACCEPT, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT_CHARSET), HEADER_ACCEPT_CHARSET, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT_ENCODING), HEADER_ACCEPT_ENCODING, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_ACCEPT_LANGUAGE), HEADER_ACCEPT_LANGUAGE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS), HEADER_ACCESS_CONTROL_REQUEST_HEADERS, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_AUTHORIZATION), HEADER_AUTHORIZATION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_CACHE_CONTROL), HEADER_CACHE_CONTROL, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_CONNECTION), HEADER_CONNECTION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_CONTENT_LENGTH), HEADER_CONTENT_LENGTH, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_CONTENT_TYPE), HEADER_CONTENT_TYPE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_COOKIE), HEADER_COOKIE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_DATE), HEADER_DATE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_EXPECT), HEADER_EXPECT, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_HOST), HEADER_HOST, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_ORIGIN), HEADER_ORIGIN, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_PROXY_AUTHORIZATION), HEADER_PROXY_AUTHORIZATION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_UPGRADE), HEADER_UPGRADE, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_USER_AGENT), HEADER_USER_AGENT, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_SEC_WEBSOCKET_KEY), HEADER_WEB_SOCKET_KEY, NULL);
	test_check_str_eq (http_header_string (HTTP_HEADER_SEC_WEBSOCKET_VERSION), HEADER_WEB_SOCKET_VERSION, NULL);

	test_check_str_eq (http_header_string (HTTP_HEADER_UNDEFINED), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 73), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 85), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 100), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 1024), HEADER_OTHER, NULL);

}

static void test_http_headers_get_type_by_string (void) {

	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT), HTTP_HEADER_ACCEPT, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT_CHARSET), HTTP_HEADER_ACCEPT_CHARSET, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT_ENCODING), HTTP_HEADER_ACCEPT_ENCODING, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCEPT_LANGUAGE), HTTP_HEADER_ACCEPT_LANGUAGE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_ACCESS_CONTROL_REQUEST_HEADERS), HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_AUTHORIZATION), HTTP_HEADER_AUTHORIZATION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_CACHE_CONTROL), HTTP_HEADER_CACHE_CONTROL, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_CONNECTION), HTTP_HEADER_CONNECTION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_CONTENT_LENGTH), HTTP_HEADER_CONTENT_LENGTH, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_CONTENT_TYPE), HTTP_HEADER_CONTENT_TYPE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_COOKIE), HTTP_HEADER_COOKIE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_DATE), HTTP_HEADER_DATE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_EXPECT), HTTP_HEADER_EXPECT, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_HOST), HTTP_HEADER_HOST, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_ORIGIN), HTTP_HEADER_ORIGIN, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_PROXY_AUTHORIZATION), HTTP_HEADER_PROXY_AUTHORIZATION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_UPGRADE), HTTP_HEADER_UPGRADE, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_USER_AGENT), HTTP_HEADER_USER_AGENT, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_WEB_SOCKET_KEY), HTTP_HEADER_SEC_WEBSOCKET_KEY, NULL);
	test_check_unsigned_eq (http_header_type_by_string (HEADER_WEB_SOCKET_VERSION), HTTP_HEADER_SEC_WEBSOCKET_VERSION, NULL);

	test_check_unsigned_eq (http_header_type_by_string (HEADER_OTHER), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("hola"), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("adios"), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("erick"), HTTP_HEADER_UNDEFINED, NULL);

}

static void test_http_header_type_by_string (void) {

	test_check_ptr (http_header_string (HTTP_HEADER_ACCEPT));
	test_check_ptr (http_header_string (HTTP_HEADER_ACCEPT_CHARSET));
	test_check_ptr (http_header_string (HTTP_HEADER_ACCEPT_ENCODING));
	test_check_ptr (http_header_string (HTTP_HEADER_ACCEPT_LANGUAGE));

	test_check_ptr (http_header_string (HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS));

	test_check_ptr (http_header_string (HTTP_HEADER_AUTHORIZATION));

	test_check_ptr (http_header_string (HTTP_HEADER_CACHE_CONTROL));

	test_check_ptr (http_header_string (HTTP_HEADER_CONNECTION));

	test_check_ptr (http_header_string (HTTP_HEADER_CONTENT_LENGTH));
	test_check_ptr (http_header_string (HTTP_HEADER_CONTENT_TYPE));

	test_check_ptr (http_header_string (HTTP_HEADER_COOKIE));

	test_check_ptr (http_header_string (HTTP_HEADER_DATE));

	test_check_ptr (http_header_string (HTTP_HEADER_EXPECT));

	test_check_ptr (http_header_string (HTTP_HEADER_HOST));

	test_check_ptr (http_header_string (HTTP_HEADER_ORIGIN));

	test_check_ptr (http_header_string (HTTP_HEADER_PROXY_AUTHORIZATION));

	test_check_ptr (http_header_string (HTTP_HEADER_UPGRADE));

	test_check_ptr (http_header_string (HTTP_HEADER_USER_AGENT));

	test_check_ptr (http_header_string (HTTP_HEADER_SEC_WEBSOCKET_KEY));
	test_check_ptr (http_header_string (HTTP_HEADER_SEC_WEBSOCKET_VERSION));

	test_check_str_eq (http_header_string (HTTP_HEADER_UNDEFINED), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 73), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 85), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 100), HEADER_OTHER, NULL);
	test_check_str_eq (http_header_string ((const http_header) 1024), HEADER_OTHER, NULL);

}

// "Content-Length: 128\r\n"
static void test_http_header_set (void) {

	const size_t content_length = 128;

	HttpHeader header;
	http_header_set (&header, "Content-Length: %lu\r\n", content_length);

	test_check_str_eq (header.value, content_length_header, NULL);
	test_check_str_len (header.value, strlen (content_length_header), NULL);
	test_check_int_eq (header.len, (int) strlen (content_length_header), NULL);

}

static void test_http_header_print (void) {

	HttpHeader header;

	// "Content-Type: application/json\r\n"
	http_header_set (&header, "%s", content_type_header);

	test_check_str_eq (header.value, content_type_header, NULL);
	test_check_int_eq (header.len, (int) strlen (content_type_header), NULL);

	http_header_print (&header);

}

#define TEST_HEADERS_NAME_SIZE				64

static void test_http_headers_swap_case (char *name) {

	for (char *ch = name; *ch; ch++) {
		*ch = islower ((unsigned char) *ch) ? (char) toupper ((unsigned char) *ch) : (char) tolower ((unsigned char) *ch);
	}

}

// the hash lookup finds every header of the map with any case, like strcasecmp ()
static void test_http_header_type_by_string_hash (void) {

	#define XX(num, name, string, description)													\
		if (HTTP_HEADER_##name != HTTP_HEADER_UNDEFINED) {										\
			char swapped[TEST_HEADERS_NAME_SIZE] = { 0 };										\
			(void) snprintf (swapped, TEST_HEADERS_NAME_SIZE, "%s", #string);					\
			test_http_headers_swap_case (swapped);												\
			test_check_unsigned_eq (http_header_type_by_string (#string), HTTP_HEADER_##name, NULL);	\
			test_check_unsigned_eq (http_header_type_by_string (swapped), HTTP_HEADER_##name, NULL);	\
		}
	HTTP_HEADERS_MAP(XX)
	#undef XX

	// the new headers of the map
	test_check_unsigned_eq (http_header_type_by_string ("x-request-id"), HTTP_HEADER_REQUEST_ID, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("TRANSFER-ENCODING"), HTTP_HEADER_TRANSFER_ENCODING, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("etag"), HTTP_HEADER_ETAG, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("x-real-ip"), HTTP_HEADER_REAL_IP, NULL);

	// a prefix, a longer name or an unknown one is not a header of the map
	test_check_unsigned_eq (http_header_type_by_string ("Content"), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("Content-Types"), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string ("X-Custom"), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string (""), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (http_header_type_by_string (NULL), HTTP_HEADER_UNDEFINED, NULL);

}

#pragma region list

#define LIST_LONG_VALUE_SIZE		300
#define LIST_MANY_FIELDS			40
#define LIST_FILL_VALUE_SIZE		1000
#define LIST_NUMBER_SIZE			16

static const char *list_json = { "application/json" };
static const char *list_length = { "128" };
static const char *list_first_cookie = { "session=first; Path=/" };
static const char *list_second_cookie = { "theme=dark; Path=/" };
static const char *list_trace_name = { "X-Trace" };
static const char *list_trace_value = { "abc-123" };

static const char *list_expected_lines = {
	"Content-Type: application/json\r\n"
	"Set-Cookie: session=first; Path=/\r\n"
	"X-Trace: abc-123\r\n"
	"Set-Cookie: theme=dark; Path=/\r\n"
	"Content-Length: 128\r\n"
};

static void list_check_lines (const HttpHeaders *headers, const char *expected) {

	size_t length = 0;
	const char *lines = http_headers_lines (headers, &length);

	test_check_unsigned_eq (length, strlen (expected), NULL);
	test_check (!memcmp (lines, expected, length), NULL);

}

static void list_check_field (
	const HttpHeaders *headers, const HttpHeaderField *field,
	const http_header type, const char *name, const char *value
) {

	test_check_ptr (field);
	test_check_unsigned_eq (field->type, type, NULL);

	size_t name_length = 0;
	const char *field_name = http_headers_field_name (headers, field, &name_length);
	test_check_unsigned_eq (name_length, strlen (name), NULL);
	test_check (!memcmp (field_name, name, name_length), NULL);

	size_t value_length = 0;
	const char *field_value = http_headers_field_value (headers, field, &value_length);
	test_check_unsigned_eq (value_length, strlen (value), NULL);
	test_check (!memcmp (field_value, value, value_length), NULL);

	size_t line_length = 0;
	const char *line = http_headers_field_line (headers, field, &line_length);
	test_check (line == field_name, NULL);
	test_check_unsigned_eq (line_length, name_length + value_length + strlen (": \r\n"), NULL);
	test_check (!memcmp (line + line_length - strlen ("\r\n"), "\r\n", strlen ("\r\n")), NULL);

}

static void list_fill (HttpHeaders *headers) {

	test_check_unsigned_eq (http_headers_add (headers, HTTP_HEADER_CONTENT_TYPE, list_json, strlen (list_json)), 0, NULL);
	test_check_unsigned_eq (http_headers_add (headers, HTTP_HEADER_SET_COOKIE, list_first_cookie, strlen (list_first_cookie)), 0, NULL);
	test_check_unsigned_eq (http_headers_add_by_name (headers, list_trace_name, strlen (list_trace_name), list_trace_value, strlen (list_trace_value)), 0, NULL);
	test_check_unsigned_eq (http_headers_add (headers, HTTP_HEADER_SET_COOKIE, list_second_cookie, strlen (list_second_cookie)), 0, NULL);
	test_check_unsigned_eq (http_headers_add (headers, HTTP_HEADER_CONTENT_LENGTH, list_length, strlen (list_length)), 0, NULL);

}

static void test_http_headers_list_init (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	test_check_unsigned_eq (headers.count, 0, NULL);
	test_check_null_ptr (headers.fields);
	test_check_unsigned_eq (headers.fields_size, HTTP_HEADERS_FIELDS_SIZE, NULL);
	test_check_unsigned_eq (headers.length, 0, NULL);
	test_check_null_ptr (headers.buffer);
	test_check_unsigned_eq (headers.buffer_size, HTTP_HEADERS_BUFFER_SIZE, NULL);

	test_check_null_ptr (http_headers_get (&headers, 0));
	test_check_null_ptr (http_headers_find (&headers, HTTP_HEADER_CONTENT_TYPE, 0));
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_CONTENT_TYPE), 0, NULL);
	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_CONTENT_TYPE), 0, NULL);

	list_check_lines (&headers, "");

	http_headers_reset (&headers);

}

// the headers stay in the order they were added, a name can be repeated
static void test_http_headers_list_add (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	list_fill (&headers);

	test_check_unsigned_eq (headers.count, 5, NULL);
	list_check_lines (&headers, list_expected_lines);

	list_check_field (&headers, http_headers_get (&headers, 0), HTTP_HEADER_CONTENT_TYPE, "Content-Type", list_json);
	list_check_field (&headers, http_headers_get (&headers, 1), HTTP_HEADER_SET_COOKIE, "Set-Cookie", list_first_cookie);
	list_check_field (&headers, http_headers_get (&headers, 2), HTTP_HEADER_UNDEFINED, list_trace_name, list_trace_value);
	list_check_field (&headers, http_headers_get (&headers, 3), HTTP_HEADER_SET_COOKIE, "Set-Cookie", list_second_cookie);
	list_check_field (&headers, http_headers_get (&headers, 4), HTTP_HEADER_CONTENT_LENGTH, "Content-Length", list_length);
	test_check_null_ptr (http_headers_get (&headers, 5));

	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_SET_COOKIE), 2, NULL);
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_CONTENT_TYPE), 1, NULL);
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_ETAG), 0, NULL);

	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_SET_COOKIE, 0), HTTP_HEADER_SET_COOKIE, "Set-Cookie", list_first_cookie);
	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_SET_COOKIE, 1), HTTP_HEADER_SET_COOKIE, "Set-Cookie", list_second_cookie);
	test_check_null_ptr (http_headers_find (&headers, HTTP_HEADER_SET_COOKIE, 2));
	test_check_null_ptr (http_headers_find (&headers, HTTP_HEADER_ETAG, 0));

	// an empty value is a header
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_ETAG, "", 0), 0, NULL);
	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_ETAG, 0), HTTP_HEADER_ETAG, "ETag", "");

	// the fields and the lines fit inside the list
	test_check_null_ptr (headers.fields);
	test_check_null_ptr (headers.buffer);

	http_headers_reset (&headers);

	test_check_unsigned_eq (headers.count, 0, NULL);
	test_check_unsigned_eq (headers.length, 0, NULL);

}

// a set removes every header with the type and adds the new one at the end
static void test_http_headers_list_set (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	list_fill (&headers);

	test_check_unsigned_eq (http_headers_set (&headers, HTTP_HEADER_SET_COOKIE, "only=one", strlen ("only=one")), 0, NULL);

	test_check_unsigned_eq (headers.count, 4, NULL);
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_SET_COOKIE), 1, NULL);

	list_check_lines (
		&headers,
		"Content-Type: application/json\r\n"
		"X-Trace: abc-123\r\n"
		"Content-Length: 128\r\n"
		"Set-Cookie: only=one\r\n"
	);

	// a set of a header that the list does not have adds it
	test_check_unsigned_eq (http_headers_set (&headers, HTTP_HEADER_ETAG, "\"tag\"", strlen ("\"tag\"")), 0, NULL);
	test_check_unsigned_eq (headers.count, 5, NULL);
	list_check_field (&headers, http_headers_get (&headers, 4), HTTP_HEADER_ETAG, "ETag", "\"tag\"");

	// a value that is not valid keeps the header that the list has
	test_check_unsigned_eq (http_headers_set (&headers, HTTP_HEADER_ETAG, "bad\r\nvalue", strlen ("bad\r\nvalue")), 1, NULL);
	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_ETAG, 0), HTTP_HEADER_ETAG, "ETag", "\"tag\"");

	http_headers_reset (&headers);

}

// the lines and the fields after a removed header take its place
static void test_http_headers_list_remove (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	list_fill (&headers);

	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_SET_COOKIE), 2, NULL);
	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_SET_COOKIE), 0, NULL);

	test_check_unsigned_eq (headers.count, 3, NULL);
	list_check_lines (
		&headers,
		"Content-Type: application/json\r\n"
		"X-Trace: abc-123\r\n"
		"Content-Length: 128\r\n"
	);

	list_check_field (&headers, http_headers_get (&headers, 0), HTTP_HEADER_CONTENT_TYPE, "Content-Type", list_json);
	list_check_field (&headers, http_headers_get (&headers, 1), HTTP_HEADER_UNDEFINED, list_trace_name, list_trace_value);
	list_check_field (&headers, http_headers_get (&headers, 2), HTTP_HEADER_CONTENT_LENGTH, "Content-Length", list_length);

	// the first and the last ones
	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_CONTENT_TYPE), 1, NULL);
	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_CONTENT_LENGTH), 1, NULL);
	list_check_lines (&headers, "X-Trace: abc-123\r\n");

	test_check_unsigned_eq (http_headers_remove_by_name (&headers, "x-trace", strlen ("x-trace")), 1, NULL);
	test_check_unsigned_eq (headers.count, 0, NULL);
	list_check_lines (&headers, "");

	// a list that was emptied is used again
	list_fill (&headers);
	list_check_lines (&headers, list_expected_lines);

	http_headers_reset (&headers);

}

// any name is a header, it is compared without case
// and the name of a known header is the header with its type
static void test_http_headers_list_names (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	test_check_unsigned_eq (http_headers_add_by_name (&headers, "X-Trace", strlen ("X-Trace"), "one", strlen ("one")), 0, NULL);
	test_check_unsigned_eq (http_headers_add_by_name (&headers, "x-trace", strlen ("x-trace"), "two", strlen ("two")), 0, NULL);
	test_check_unsigned_eq (http_headers_add_by_name (&headers, "X-Tracer", strlen ("X-Tracer"), "other", strlen ("other")), 0, NULL);

	test_check_unsigned_eq (http_headers_count_by_name (&headers, "X-TRACE", strlen ("X-TRACE")), 2, NULL);
	test_check_unsigned_eq (http_headers_count_by_name (&headers, "X-Tracer", strlen ("X-Tracer")), 1, NULL);
	test_check_unsigned_eq (http_headers_count_by_name (&headers, "X-Trac", strlen ("X-Trac")), 0, NULL);

	// each one keeps the name it was added with
	list_check_field (&headers, http_headers_find_by_name (&headers, "x-TRACE", strlen ("x-TRACE"), 0), HTTP_HEADER_UNDEFINED, "X-Trace", "one");
	list_check_field (&headers, http_headers_find_by_name (&headers, "x-TRACE", strlen ("x-TRACE"), 1), HTTP_HEADER_UNDEFINED, "x-trace", "two");
	test_check_null_ptr (http_headers_find_by_name (&headers, "x-TRACE", strlen ("x-TRACE"), 2));

	// a name that is not ended with '\0'
	test_check_unsigned_eq (http_headers_count_by_name (&headers, "X-Tracer", strlen ("X-Trace")), 2, NULL);

	// a known name gets its type and its name as cerver writes it
	test_check_unsigned_eq (http_headers_add_by_name (&headers, "content-type", strlen ("content-type"), list_json, strlen (list_json)), 0, NULL);
	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_CONTENT_TYPE, 0), HTTP_HEADER_CONTENT_TYPE, "Content-Type", list_json);
	list_check_field (&headers, http_headers_find_by_name (&headers, "CONTENT-TYPE", strlen ("CONTENT-TYPE"), 0), HTTP_HEADER_CONTENT_TYPE, "Content-Type", list_json);
	test_check_unsigned_eq (http_headers_count_by_name (&headers, "Content-Type", strlen ("Content-Type")), 1, NULL);

	// a set by name replaces the ones with the name, known or not
	test_check_unsigned_eq (http_headers_set_by_name (&headers, "X-TRACE", strlen ("X-TRACE"), "three", strlen ("three")), 0, NULL);
	test_check_unsigned_eq (http_headers_count_by_name (&headers, "x-trace", strlen ("x-trace")), 1, NULL);
	list_check_field (&headers, http_headers_find_by_name (&headers, "x-trace", strlen ("x-trace"), 0), HTTP_HEADER_UNDEFINED, "X-TRACE", "three");

	test_check_unsigned_eq (http_headers_set_by_name (&headers, "Content-type", strlen ("Content-type"), "text/plain", strlen ("text/plain")), 0, NULL);
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_CONTENT_TYPE), 1, NULL);
	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_CONTENT_TYPE, 0), HTTP_HEADER_CONTENT_TYPE, "Content-Type", "text/plain");

	list_check_lines (
		&headers,
		"X-Tracer: other\r\n"
		"X-TRACE: three\r\n"
		"Content-Type: text/plain\r\n"
	);

	// the name of the undefined header is like any other name
	test_check_unsigned_eq (http_headers_add_by_name (&headers, "Undefined", strlen ("Undefined"), "value", strlen ("value")), 0, NULL);
	list_check_field (&headers, http_headers_find_by_name (&headers, "undefined", strlen ("undefined"), 0), HTTP_HEADER_UNDEFINED, "Undefined", "value");

	test_check_unsigned_eq (http_headers_remove_by_name (&headers, "X-Tracer", strlen ("X-Tracer")), 1, NULL);
	test_check_unsigned_eq (http_headers_remove_by_name (&headers, "content-TYPE", strlen ("content-TYPE")), 1, NULL);
	test_check_unsigned_eq (headers.count, 2, NULL);

	http_headers_reset (&headers);

}

// a name that is not a token and a value that ends its line are rejected
static void test_http_headers_list_invalid (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	static const char *bad_names[] = {
		"", "X Trace", "X-Trace:", "X-Trace\r\n", "X\tTrace", "X-Trace ", "(trace)", "X/Trace", "X=Trace", "\"X\""
	};

	for (size_t idx = 0; idx < (sizeof (bad_names) / sizeof (bad_names[0])); idx++) {
		test_check (!http_header_name_is_valid (bad_names[idx], strlen (bad_names[idx])), bad_names[idx]);
		test_check_unsigned_eq (http_headers_add_by_name (&headers, bad_names[idx], strlen (bad_names[idx]), "value", strlen ("value")), 1, NULL);
		test_check_unsigned_eq (http_headers_set_by_name (&headers, bad_names[idx], strlen (bad_names[idx]), "value", strlen ("value")), 1, NULL);
	}

	// a NUL inside the name
	test_check (!http_header_name_is_valid ("X\0Trace", sizeof ("X\0Trace") - 1), NULL);

	test_check (http_header_name_is_valid ("X-Trace_1.2~!#$%&'*+^`|", strlen ("X-Trace_1.2~!#$%&'*+^`|")), NULL);

	// the longest name
	char long_name[HTTP_HEADER_NAME_MAX + 2];
	(void) memset (long_name, 'a', sizeof (long_name));
	test_check (http_header_name_is_valid (long_name, HTTP_HEADER_NAME_MAX), NULL);
	test_check (!http_header_name_is_valid (long_name, HTTP_HEADER_NAME_MAX + 1), NULL);
	test_check_unsigned_eq (http_headers_add_by_name (&headers, long_name, HTTP_HEADER_NAME_MAX + 1, "value", strlen ("value")), 1, NULL);

	static const char *bad_values[] = {
		"value\r\nSet-Cookie: stolen=1", "value\nother", "value\rother", "\r\n", "value\r\n"
	};

	for (size_t idx = 0; idx < (sizeof (bad_values) / sizeof (bad_values[0])); idx++) {
		test_check (!http_header_value_is_valid (bad_values[idx], strlen (bad_values[idx])), NULL);
		test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_LOCATION, bad_values[idx], strlen (bad_values[idx])), 1, NULL);
		test_check_unsigned_eq (http_headers_set (&headers, HTTP_HEADER_LOCATION, bad_values[idx], strlen (bad_values[idx])), 1, NULL);
		test_check_unsigned_eq (http_headers_add_by_name (&headers, "X-Trace", strlen ("X-Trace"), bad_values[idx], strlen (bad_values[idx])), 1, NULL);
	}

	// a NUL inside the value
	test_check (!http_header_value_is_valid ("va\0lue", sizeof ("va\0lue") - 1), NULL);
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_LOCATION, "va\0lue", sizeof ("va\0lue") - 1), 1, NULL);

	test_check (http_header_value_is_valid ("text/html; charset=utf-8", strlen ("text/html; charset=utf-8")), NULL);
	test_check (http_header_value_is_valid ("", 0), NULL);
	test_check (!http_header_value_is_valid (NULL, 0), NULL);

	// the undefined header and the types out of the map are not headers
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_UNDEFINED, "value", strlen ("value")), 1, NULL);
	test_check_unsigned_eq (http_headers_set (&headers, HTTP_HEADER_UNDEFINED, "value", strlen ("value")), 1, NULL);
	test_check_unsigned_eq (http_headers_add (&headers, (http_header) HTTP_HEADERS_SIZE, "value", strlen ("value")), 1, NULL);
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_UNDEFINED), 0, NULL);
	test_check_null_ptr (http_headers_find (&headers, HTTP_HEADER_UNDEFINED, 0));

	test_check_unsigned_eq (http_headers_add (NULL, HTTP_HEADER_LOCATION, "value", strlen ("value")), 1, NULL);
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_LOCATION, NULL, 0), 1, NULL);
	test_check_unsigned_eq (http_headers_add_by_name (&headers, NULL, 0, "value", strlen ("value")), 1, NULL);
	test_check_unsigned_eq (http_headers_count (NULL, HTTP_HEADER_LOCATION), 0, NULL);
	test_check_null_ptr (http_headers_get (NULL, 0));

	// nothing was added
	test_check_unsigned_eq (headers.count, 0, NULL);
	test_check_unsigned_eq (headers.length, 0, NULL);

	http_headers_reset (&headers);
	http_headers_reset (NULL);

}

// the lines that do not fit inside the list are allocated, with the ones it had
static void test_http_headers_list_grow_lines (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	list_fill (&headers);

	// longer than the 256 bytes that a header had
	char long_value[LIST_LONG_VALUE_SIZE + 1];
	(void) memset (long_value, 'v', LIST_LONG_VALUE_SIZE);
	long_value[LIST_LONG_VALUE_SIZE] = '\0';

	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_SET_COOKIE, long_value, LIST_LONG_VALUE_SIZE), 0, NULL);
	test_check_null_ptr (headers.buffer);

	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_LOCATION, long_value, LIST_LONG_VALUE_SIZE), 0, NULL);
	test_check_ptr (headers.buffer);
	test_check (headers.buffer_size > HTTP_HEADERS_BUFFER_SIZE, NULL);
	test_check (headers.length <= headers.buffer_size, NULL);
	test_check_null_ptr (headers.fields);

	test_check_unsigned_eq (headers.count, 7, NULL);

	// the ones that were inside the list are in the allocated lines
	size_t length = 0;
	const char *lines = http_headers_lines (&headers, &length);
	test_check (!memcmp (lines, list_expected_lines, strlen (list_expected_lines)), NULL);

	list_check_field (&headers, http_headers_get (&headers, 0), HTTP_HEADER_CONTENT_TYPE, "Content-Type", list_json);
	list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_SET_COOKIE, 2), HTTP_HEADER_SET_COOKIE, "Set-Cookie", long_value);
	list_check_field (&headers, http_headers_get (&headers, 6), HTTP_HEADER_LOCATION, "Location", long_value);

	// a removed header in the allocated lines
	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_SET_COOKIE), 3, NULL);
	list_check_field (&headers, http_headers_get (&headers, 3), HTTP_HEADER_LOCATION, "Location", long_value);

	// the reset releases the allocated lines and the list is used again
	http_headers_reset (&headers);
	test_check_null_ptr (headers.buffer);
	test_check_unsigned_eq (headers.buffer_size, HTTP_HEADERS_BUFFER_SIZE, NULL);

	list_fill (&headers);
	list_check_lines (&headers, list_expected_lines);

	http_headers_reset (&headers);

}

// the fields that do not fit inside the list are allocated, with the ones it had
static void test_http_headers_list_grow_fields (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	char number[LIST_NUMBER_SIZE];
	for (unsigned int idx = 0; idx < LIST_MANY_FIELDS; idx++) {
		const int number_length = snprintf (number, LIST_NUMBER_SIZE, "%u", idx);
		test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_SET_COOKIE, number, (size_t) number_length), 0, NULL);

		if (idx < HTTP_HEADERS_FIELDS_SIZE) test_check_null_ptr (headers.fields);
	}

	test_check_ptr (headers.fields);
	test_check (headers.fields_size > HTTP_HEADERS_FIELDS_SIZE, NULL);
	test_check_unsigned_eq (headers.count, LIST_MANY_FIELDS, NULL);
	test_check_unsigned_eq (http_headers_count (&headers, HTTP_HEADER_SET_COOKIE), LIST_MANY_FIELDS, NULL);

	for (unsigned int idx = 0; idx < LIST_MANY_FIELDS; idx++) {
		(void) snprintf (number, LIST_NUMBER_SIZE, "%u", idx);
		list_check_field (&headers, http_headers_find (&headers, HTTP_HEADER_SET_COOKIE, idx), HTTP_HEADER_SET_COOKIE, "Set-Cookie", number);
	}

	http_headers_reset (&headers);
	test_check_null_ptr (headers.fields);
	test_check_unsigned_eq (headers.fields_size, HTTP_HEADERS_FIELDS_SIZE, NULL);

}

// a list does not grow over its maximum of lines and of fields
static void test_http_headers_list_limits (void) {

	HttpHeaders headers;
	http_headers_init (&headers);

	char fill_value[LIST_FILL_VALUE_SIZE];
	(void) memset (fill_value, 'f', LIST_FILL_VALUE_SIZE);

	unsigned int added = 0;
	while (!http_headers_add (&headers, HTTP_HEADER_SET_COOKIE, fill_value, LIST_FILL_VALUE_SIZE)) {
		added += 1;
		test_check (added <= HTTP_HEADERS_BUFFER_MAX / LIST_FILL_VALUE_SIZE, NULL);
	}

	test_check (added > 0, NULL);
	test_check_unsigned_eq (headers.count, added, NULL);
	test_check (headers.length <= HTTP_HEADERS_BUFFER_MAX, NULL);
	test_check (headers.buffer_size <= HTTP_HEADERS_BUFFER_MAX, NULL);
	test_check (headers.length + LIST_FILL_VALUE_SIZE > HTTP_HEADERS_BUFFER_MAX, NULL);

	// a small one still fits, and the ones it had are complete
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_ETAG, "t", strlen ("t")), 0, NULL);
	for (unsigned int idx = 0; idx < added; idx++) {
		size_t value_length = 0;
		const char *value = http_headers_field_value (&headers, http_headers_find (&headers, HTTP_HEADER_SET_COOKIE, idx), &value_length);
		test_check_unsigned_eq (value_length, LIST_FILL_VALUE_SIZE, NULL);
		test_check (!memcmp (value, fill_value, LIST_FILL_VALUE_SIZE), NULL);
	}

	http_headers_reset (&headers);

	// a value bigger than all the lines of a list
	char *huge_value = (char *) malloc (HTTP_HEADERS_BUFFER_MAX + 1);
	test_check_ptr (huge_value);
	(void) memset (huge_value, 'h', HTTP_HEADERS_BUFFER_MAX + 1);
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_LOCATION, huge_value, HTTP_HEADERS_BUFFER_MAX + 1), 1, NULL);
	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_LOCATION, huge_value, HTTP_HEADERS_BUFFER_MAX), 1, NULL);
	test_check_unsigned_eq (headers.count, 0, NULL);
	free (huge_value);

	// the most fields
	for (unsigned int idx = 0; idx < HTTP_HEADERS_FIELDS_MAX; idx++) {
		test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_VIA, "v", strlen ("v")), 0, NULL);
	}

	test_check_unsigned_eq (http_headers_add (&headers, HTTP_HEADER_VIA, "v", strlen ("v")), 1, NULL);
	test_check_unsigned_eq (headers.count, HTTP_HEADERS_FIELDS_MAX, NULL);
	test_check_unsigned_eq (headers.fields_size, HTTP_HEADERS_FIELDS_MAX, NULL);

	test_check_unsigned_eq (http_headers_remove (&headers, HTTP_HEADER_VIA), HTTP_HEADERS_FIELDS_MAX, NULL);
	test_check_unsigned_eq (headers.count, 0, NULL);
	test_check_unsigned_eq (headers.length, 0, NULL);

	http_headers_reset (&headers);

}

#pragma endregion

void http_tests_headers (void) {

	(void) printf ("Testing HTTP headers...\n");

	test_http_headers_list_init ();

	test_http_headers_list_add ();

	test_http_headers_list_set ();

	test_http_headers_list_remove ();

	test_http_headers_list_names ();

	test_http_headers_list_invalid ();

	test_http_headers_list_grow_lines ();

	test_http_headers_list_grow_fields ();

	test_http_headers_list_limits ();

	test_http_header_type_by_string_hash ();

	test_http_headers_get_string ();

	test_http_headers_get_type_by_string ();

	test_http_header_type_by_string ();

	test_http_header_set ();

	test_http_header_print ();

	(void) printf ("Done!\n");

}
