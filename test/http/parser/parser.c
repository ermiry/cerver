#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/headers/types.h>
#include <cerver/http/parser/body.h>
#include <cerver/http/parser/head.h>
#include <cerver/http/parser/multipart.h>
#include <cerver/http/parser/parser.h>
#include <cerver/http/requests/method.h>

#include "../../test.h"
#include "../http.h"

// the parser of cerver by its API: the head whole and in pieces, its spans, its limits and its array,
// the bodies, the responses, the lenient flags, the errors and the names of the known headers
// (test/conformance/http.c compares it with llhttp, and test/http/receive.c reads every request through it)

#define TEST_PARSER_FIELDS				(1 << 6)
#define TEST_PARSER_FEW_FIELDS			2
#define TEST_PARSER_BODY_SIZE			(1 << 12)
#define TEST_PARSER_TEXT_SIZE			(1 << 14)

// the values of a byte, for the tests that put each one in a place
#define TEST_PARSER_BYTE_VALUES			256

static CerverHttpField test_parser_fields[TEST_PARSER_FIELDS];
static CerverHttpParser test_parser;

// what the body of a message gave
static char test_parser_body[TEST_PARSER_BODY_SIZE];
static size_t test_parser_body_length = 0;

static const char *test_parser_simple = {
	"GET /path/to?key=value HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"User-Agent: cerver-test\r\n"
	"X-Custom:   spaced value \t \r\n"
	"\r\n"
};

#pragma region helpers

static void test_parser_start (const CerverHttpParserType type, const unsigned int lenient) {

	cerver_http_parser_init (&test_parser, type, lenient, NULL, test_parser_fields, TEST_PARSER_FIELDS);

}

static bool test_parser_span_is (const CerverHttpSpan span, const char *expected) {

	const size_t length = strlen (expected);

	return (span.length == length)
		&& !memcmp (cerver_http_head_at (&test_parser.head, span), expected, length);

}

static CerverHttpParserResult test_parser_head (const char *text, const size_t length, size_t *consumed) {

	return cerver_http_parser_head (&test_parser, text, length, consumed);

}

// the body after the head, all its pieces joined, until the message is complete or an error
static CerverHttpParserResult test_parser_read_body (const char *text, const size_t length, size_t *used) {

	test_parser_body_length = 0;

	size_t offset = 0;
	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_BODY;
	while (result == CERVER_HTTP_PARSER_RESULT_BODY) {
		size_t consumed = 0;
		const char *data = NULL;
		size_t data_length = 0;

		result = cerver_http_parser_body (&test_parser, text + offset, length - offset, &consumed, &data, &data_length);
		offset += consumed;

		if (result == CERVER_HTTP_PARSER_RESULT_BODY) {
			test_check ((test_parser_body_length + data_length) < TEST_PARSER_BODY_SIZE, NULL);
			(void) memcpy (test_parser_body + test_parser_body_length, data, data_length);
			test_parser_body_length += data_length;
		}
	}

	test_parser_body[test_parser_body_length] = '\0';
	*used = offset;

	return result;

}

// a whole message: its head and its body, the result of the last step
static CerverHttpParserResult test_parser_message (const char *text, size_t *used) {

	const size_t length = strlen (text);

	size_t consumed = 0;
	CerverHttpParserResult result = test_parser_head (text, length, &consumed);

	if (result == CERVER_HTTP_PARSER_RESULT_HEAD) {
		size_t body_used = 0;
		result = test_parser_read_body (text + consumed, length - consumed, &body_used);
		consumed += body_used;
	}

	*used = consumed;

	return result;

}

static CerverHttpParserResult test_parser_request (const char *text, const unsigned int lenient) {

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, lenient);

	size_t used = 0;

	return test_parser_message (text, &used);

}

static void test_parser_rejected (const char *text, const unsigned int lenient, const CerverHttpParserError error) {

	test_check_unsigned_eq (test_parser_request (text, lenient), CERVER_HTTP_PARSER_RESULT_ERROR, text);
	test_check_unsigned_eq (cerver_http_parser_get_error (&test_parser), error, text);

}

static void test_parser_accepted (const char *text, const unsigned int lenient) {

	test_check_unsigned_eq (test_parser_request (text, lenient), CERVER_HTTP_PARSER_RESULT_COMPLETE, text);

}

#pragma endregion

#pragma region head

static void test_parser_head_simple (void) {

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

	size_t consumed = 0;
	test_check_unsigned_eq (
		test_parser_head (test_parser_simple, strlen (test_parser_simple), &consumed),
		CERVER_HTTP_PARSER_RESULT_HEAD, NULL
	);

	const CerverHttpHead *head = &test_parser.head;
	test_check_unsigned_eq (consumed, strlen (test_parser_simple), NULL);
	test_check_unsigned_eq (head->length, consumed, NULL);
	test_check_unsigned_eq (head->method, REQUEST_METHOD_GET, NULL);
	test_check_true (test_parser_span_is (head->target, "/path/to?key=value"));
	test_check_unsigned_eq (head->major, 1, NULL);
	test_check_unsigned_eq (head->minor, 1, NULL);
	test_check_unsigned_eq (head->fields_count, 3, NULL);

	// the spaces and the tabs around a value are not part of it
	test_check_true (test_parser_span_is (head->fields[0].name, "Host"));
	test_check_true (test_parser_span_is (head->fields[0].value, "localhost:8080"));
	test_check_unsigned_eq (head->fields[0].type, HTTP_HEADER_HOST, NULL);
	test_check_unsigned_eq (head->fields[1].type, HTTP_HEADER_USER_AGENT, NULL);
	test_check_true (test_parser_span_is (head->fields[2].value, "spaced value"));
	test_check_unsigned_eq (head->fields[2].type, HTTP_HEADER_UNDEFINED, NULL);

	// the names and the values of the fields count for the limit, with the spaces after a value
	test_check_unsigned_eq (head->fields_counted, 3, NULL);
	test_check_unsigned_eq (head->fields_size, strlen ("Host") + strlen ("localhost:8080") + strlen ("User-Agent")
		+ strlen ("cerver-test") + strlen ("X-Custom") + strlen ("spaced value \t "), NULL);

	test_check_ptr_eq (cerver_http_head_field_with_type (head, HTTP_HEADER_HOST), &head->fields[0]);
	test_check_null_ptr (cerver_http_head_field_with_type (head, HTTP_HEADER_COOKIE));
	test_check_ptr_eq (cerver_http_head_field_with_name (head, "x-custom", strlen ("x-custom")), &head->fields[2]);
	test_check_null_ptr (cerver_http_head_field_with_name (head, "x-other", strlen ("x-other")));

	test_check_true (cerver_http_parser_started (&test_parser));
	test_check_true (cerver_http_parser_head_read (&test_parser));
	test_check_true (cerver_http_parser_keep_alive (&test_parser));
	test_check_false (cerver_http_parser_has_body (&test_parser));
	test_check_false (test_parser.upgrade);

	// the same head again, until its body is read
	consumed = 0;
	test_check_unsigned_eq (
		test_parser_head (test_parser_simple, strlen (test_parser_simple), &consumed),
		CERVER_HTTP_PARSER_RESULT_HEAD, NULL
	);
	test_check_unsigned_eq (consumed, strlen (test_parser_simple), NULL);

	size_t used = 0;
	test_check_unsigned_eq (test_parser_read_body ("", 0, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (used, 0, NULL);
	test_check_unsigned_eq (test_parser.messages, 1, NULL);
	test_check_false (cerver_http_parser_head_read (&test_parser));

}

// the head arrives one byte at a time: every call has the bytes from its start, and it is read once
static void test_parser_head_pieces (void) {

	const size_t length = strlen (test_parser_simple);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;
	size_t consumed = 0;
	size_t given = 0;
	while ((result == CERVER_HTTP_PARSER_RESULT_MORE) && (given < length)) {
		given += 1;
		result = test_parser_head (test_parser_simple, given, &consumed);
		if (result == CERVER_HTTP_PARSER_RESULT_MORE) test_check_unsigned_eq (consumed, 0, NULL);
	}

	test_check_unsigned_eq (result, CERVER_HTTP_PARSER_RESULT_HEAD, NULL);
	test_check_unsigned_eq (given, length, NULL);
	test_check_unsigned_eq (consumed, length, NULL);
	test_check_unsigned_eq (test_parser.head.fields_count, 3, NULL);
	test_check_true (test_parser_span_is (test_parser.head.fields[2].value, "spaced value"));

	// the bytes after the head are not part of it
	static char text[TEST_PARSER_TEXT_SIZE];
	(void) snprintf (text, sizeof (text), "%sGET / HTTP/1.1\r\n", test_parser_simple);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);
	test_check_unsigned_eq (consumed, length, NULL);

}

// a full array of fields asks for a bigger one, and the parser goes on with it
static void test_parser_head_fields (void) {

	static CerverHttpField few[TEST_PARSER_FEW_FIELDS];
	cerver_http_parser_init (
		&test_parser, CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE, NULL, few, TEST_PARSER_FEW_FIELDS
	);

	const char *text = { "GET / HTTP/1.1\r\nA: 1\r\nB: 2\r\nC: 3\r\nD: 4\r\nE: 5\r\n\r\n" };

	size_t consumed = 0;
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_FIELDS, NULL);
	test_check_unsigned_eq (test_parser.head.fields_count, TEST_PARSER_FEW_FIELDS, NULL);

	cerver_http_parser_fields_set (&test_parser, test_parser_fields, TEST_PARSER_FIELDS);
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	const CerverHttpHead *head = &test_parser.head;
	test_check_unsigned_eq (head->fields_count, 5, NULL);
	test_check_unsigned_eq (head->fields_counted, 5, NULL);
	test_check_true (test_parser_span_is (head->fields[0].name, "A"));
	test_check_true (test_parser_span_is (head->fields[4].value, "5"));

	// an array smaller than the fields that were read is not taken
	cerver_http_parser_fields_set (&test_parser, few, TEST_PARSER_FEW_FIELDS);
	test_check_ptr_eq (test_parser.head.fields, test_parser_fields);

}

// the head moved to another buffer is the same head, its spans are offsets
static void test_parser_head_rebase (void) {

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

	static char original[TEST_PARSER_TEXT_SIZE];
	static char copy[TEST_PARSER_TEXT_SIZE];
	(void) snprintf (original, sizeof (original), "%s", test_parser_simple);

	size_t consumed = 0;
	test_check_unsigned_eq (test_parser_head (original, strlen (original), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	(void) memcpy (copy, original, consumed);
	(void) memset (original, 0, sizeof (original));
	cerver_http_parser_rebase (&test_parser, copy);

	test_check_ptr_eq (test_parser.head.base, copy);
	test_check_true (test_parser_span_is (test_parser.head.target, "/path/to?key=value"));
	test_check_true (test_parser_span_is (test_parser.head.fields[0].value, "localhost:8080"));

}

// the empty lines before a request line are part of its head
static void test_parser_head_empty_lines (void) {

	const char *text = { "\r\n\r\nGET / HTTP/1.1\r\n\r\n" };

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

	size_t consumed = 0;
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);
	test_check_unsigned_eq (consumed, strlen (text), NULL);
	test_check_true (test_parser_span_is (test_parser.head.target, "/"));

	// only empty lines is no message yet, and the end of the connection there is not an error
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_head ("\r\n", strlen ("\r\n"), &consumed), CERVER_HTTP_PARSER_RESULT_MORE, NULL);
	test_check_false (cerver_http_parser_started (&test_parser));
	test_check_unsigned_eq (cerver_http_parser_finish (&test_parser), CERVER_HTTP_PARSER_RESULT_MORE, NULL);

}

#pragma endregion

#pragma region limits

static void test_parser_limits_start (const size_t target_size, const size_t fields_size, const unsigned int fields) {

	const CerverHttpParserLimits limits = { target_size, fields_size, fields };

	cerver_http_parser_init (
		&test_parser, CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE, &limits,
		test_parser_fields, TEST_PARSER_FIELDS
	);

}

static CerverHttpParserResult test_parser_limited (const char *text) {

	size_t consumed = 0;

	return test_parser_head (text, strlen (text), &consumed);

}

static void test_parser_limits_target (void) {

	// the target at its limit, one byte more, and a line that is not complete yet
	test_parser_limits_start (strlen ("/abcd"), 0, 0);
	test_check_unsigned_eq (test_parser_limited ("GET /abcd HTTP/1.1\r\n\r\n"), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	test_parser_limits_start (strlen ("/abcd"), 0, 0);
	test_check_unsigned_eq (test_parser_limited ("GET /abcde HTTP/1.1\r\n\r\n"), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_TARGET_SIZE, NULL);

	test_parser_limits_start (strlen ("/abcd"), 0, 0);
	test_check_unsigned_eq (test_parser_limited ("GET /abcd"), CERVER_HTTP_PARSER_RESULT_MORE, NULL);

	test_parser_limits_start (strlen ("/abcd"), 0, 0);
	test_check_unsigned_eq (test_parser_limited ("GET /abcde"), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_TARGET_SIZE, NULL);

	// a line without a space after a method that can be one is not a request
	test_parser_limits_start (0, 0, 0);
	test_check_unsigned_eq (test_parser_limited ("ABCDEFGHIJKLMNOP"), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_METHOD, NULL);

}

static void test_parser_limits_fields (void) {

	// the names and the values of the fields: "A" "1" "B" "22" are 5 bytes
	const char *text = { "GET / HTTP/1.1\r\nA: 1\r\nB: 22\r\n\r\n" };

	test_parser_limits_start (0, 5, 0);
	test_check_unsigned_eq (test_parser_limited (text), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	test_parser_limits_start (0, 4, 0);
	test_check_unsigned_eq (test_parser_limited (text), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE, NULL);

	// a field that is not complete yet is already too large
	test_parser_limits_start (0, 4, 0);
	test_check_unsigned_eq (test_parser_limited ("GET / HTTP/1.1\r\nA: 1\r\nB: 22"), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE, NULL);

	test_parser_limits_start (0, 5, 0);
	test_check_unsigned_eq (test_parser_limited ("GET / HTTP/1.1\r\nA: 1\r\nB: 22"), CERVER_HTTP_PARSER_RESULT_MORE, NULL);

	// the number of fields
	test_parser_limits_start (0, 0, 2);
	test_check_unsigned_eq (test_parser_limited (text), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	test_parser_limits_start (0, 0, 1);
	test_check_unsigned_eq (test_parser_limited (text), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE, NULL);

	// the trailers and the extensions of the chunks count as fields
	const char *chunked = { "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1;e=f\r\nx\r\n0\r\nT: v\r\n\r\n" };

	test_parser_limits_start (0, 0, 1);
	size_t used = 0;
	test_check_unsigned_eq (test_parser_message (chunked, &used), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE, NULL);

	test_parser_limits_start (0, strlen ("Transfer-Encoding") + strlen ("chunked") + strlen ("e=f"), 0);
	test_check_unsigned_eq (test_parser_message (chunked, &used), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE, NULL);

	test_parser_limits_start (0, 0, 2);
	test_check_unsigned_eq (test_parser_message (chunked, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser.head.fields_counted, 2, NULL);

}

static void test_parser_limits_default (void) {

	CerverHttpParserLimits limits;
	cerver_http_parser_limits_default (&limits);

	test_check_unsigned_eq (limits.target_size, CERVER_HTTP_PARSER_DEFAULT_TARGET_SIZE, NULL);
	test_check_unsigned_eq (limits.fields_size, CERVER_HTTP_PARSER_DEFAULT_FIELDS_SIZE, NULL);
	test_check_unsigned_eq (limits.fields, CERVER_HTTP_PARSER_DEFAULT_FIELDS, NULL);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser.limits.target_size, CERVER_HTTP_PARSER_DEFAULT_TARGET_SIZE, NULL);

	// the bits outside the lenient flags are not kept
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, ~0u);
	test_check_unsigned_eq (test_parser.lenient, HTTP_PARSER_LENIENT_ALL, NULL);

}

#pragma endregion

#pragma region start

static void test_parser_start_versions (void) {

	test_parser_accepted ("GET / HTTP/1.0\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_false (cerver_http_parser_keep_alive (&test_parser));

	test_parser_accepted ("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (cerver_http_parser_keep_alive (&test_parser));

	test_parser_accepted ("GET / HTTP/1.1\r\nConnection: Close\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_false (cerver_http_parser_keep_alive (&test_parser));

	// the versions that are valid but not served, and the ones that are not valid
	test_parser_rejected ("GET / HTTP/0.9\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED);
	test_parser_rejected ("GET / HTTP/2.0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED);
	test_parser_rejected ("GET /\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED);
	test_parser_rejected ("GET / HTTP/3.0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION);
	test_parser_rejected ("GET / HTTP/1.\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION);
	test_parser_rejected ("GET / HTTP/11\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION);
	test_parser_rejected ("GET / RTSP/1.0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_PROTOCOL);
	test_parser_rejected ("GET / http/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_PROTOCOL);

	// a minor above 1 is read as HTTP/1.1 (RFC 9112 section 2.3), with its keep-alive, also with the lenient flag
	test_parser_accepted ("GET / HTTP/1.2\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser.head.major, 1, NULL);
	test_check_unsigned_eq (test_parser.head.minor, 1, NULL);
	test_check_true (cerver_http_parser_keep_alive (&test_parser));
	test_parser_accepted ("GET / HTTP/1.9\r\nConnection: close\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser.head.minor, 1, NULL);
	test_check_false (cerver_http_parser_keep_alive (&test_parser));
	test_parser_accepted ("GET / HTTP/1.5\r\n\r\n", HTTP_PARSER_LENIENT_VERSION);
	test_check_unsigned_eq (test_parser.head.minor, 1, NULL);

	// the lenient flag of the version takes any one, and the request line without a version
	test_parser_accepted ("GET / HTTP/3.4\r\n\r\n", HTTP_PARSER_LENIENT_VERSION);
	test_check_unsigned_eq (test_parser.head.major, 3, NULL);
	test_check_unsigned_eq (test_parser.head.minor, 4, NULL);
	test_parser_accepted ("GET / HTTP/2.0\r\n\r\n", HTTP_PARSER_LENIENT_VERSION);
	test_parser_accepted ("GET /\r\n\r\n", HTTP_PARSER_LENIENT_VERSION);
	test_check_unsigned_eq (test_parser.head.major, 0, NULL);
	test_check_unsigned_eq (test_parser.head.minor, 9, NULL);

}

static void test_parser_start_methods (void) {

	static char text[TEST_PARSER_TEXT_SIZE];
	const char *request = text;

	#define XX(num, name, string)																\
		(void) snprintf (text, sizeof (text), "%s / HTTP/1.1\r\n\r\n", #string);				\
		test_parser_accepted (request, HTTP_PARSER_LENIENT_NONE);								\
		test_check_unsigned_eq (test_parser.head.method, REQUEST_METHOD_##name, request);
	REQUEST_METHOD_MAP(XX)
	#undef XX

	// a token that is not a method of the table is one that is not implemented (RFC 9110 section 9.1),
	// the methods are case-sensitive
	test_parser_rejected ("get / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN);
	test_parser_rejected ("DESCRIBE / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN);
	test_parser_rejected ("FOO / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN);
	test_parser_rejected ("ABCDEFGHIJK / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN);

	// what is not a token, a token longer than the longest method, and a line without a space after its method
	test_parser_rejected ("G@T / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD);
	test_parser_rejected ("ABCDEFGHIJKL / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD);
	test_parser_rejected ("GET\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD);

	// a line that starts with a space has an empty method, that is not a token
	test_parser_rejected (" GET / HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_METHOD);

	// the preface of HTTP/2 is a version that is not served, and never a request
	test_parser_rejected ("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED);
	test_parser_rejected ("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", HTTP_PARSER_LENIENT_VERSION, CERVER_HTTP_PARSER_ERROR_METHOD);
	test_parser_rejected ("PRI *\r\n\r\n", HTTP_PARSER_LENIENT_VERSION, CERVER_HTTP_PARSER_ERROR_METHOD);

}

static void test_parser_start_targets (void) {

	test_parser_accepted ("OPTIONS * HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_parser_accepted ("GET http://user:pass@host:8080/path?query HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_parser_accepted ("GET http://[::1]:8080/ HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_parser_accepted ("CONNECT host:443 HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (test_parser.upgrade);
	test_parser_accepted ("GET /a?b=c?d#fragment HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_parser_accepted ("GET  /two-spaces  HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (test_parser_span_is (test_parser.head.target, "/two-spaces"));

	test_parser_rejected ("GET a/b HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("GET http:/host/ HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("GET http://a@@b/ HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("GET http://a#b/ HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("GET /\x7f HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("GET /\xc3\xa9 HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("GET /a\x01" "b HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);
	test_parser_rejected ("CONNECT host/\x7f HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);

	// a target longer than a word is read 8 bytes at a time
	test_parser_accepted ("GET /a-path-longer-than-two-words/and-more HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_parser_rejected ("GET /a-path-longer-than-two-words/and\x7fmore HTTP/1.1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TARGET);

}

// the bytes that a request line can have between its target and its version
#define TEST_PARSER_TARGET_PREFIX		"GET /target-of-a-request-line-longer-than-a-vector"

// only a space ends the target: any other byte is in it (and only visible ASCII is a target), also the ones that
// are not visible in the place of the space, where the fast path looks for its version
static void test_parser_start_separators (void) {

	static const char *version = { "HTTP/1.1\r\n\r\n" };

	char text[TEST_PARSER_TEXT_SIZE];

	for (unsigned int value = 0; value < TEST_PARSER_BYTE_VALUES; value++) {
		size_t length = strlen (TEST_PARSER_TARGET_PREFIX);
		(void) memcpy (text, TEST_PARSER_TARGET_PREFIX, length);
		text[length++] = (char) value;
		(void) memcpy (text + length, version, strlen (version));
		length += strlen (version);

		test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

		size_t consumed = 0;
		const CerverHttpParserResult result = test_parser_head (text, length, &consumed);

		test_check_int_eq (
			result, (value == ' ') ? CERVER_HTTP_PARSER_RESULT_HEAD : CERVER_HTTP_PARSER_RESULT_ERROR, NULL
		);
	}

}

#pragma endregion

#pragma region fields

static void test_parser_fields_errors (void) {

	test_parser_rejected ("GET / HTTP/1.1\r\nA : b\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	test_parser_rejected ("GET / HTTP/1.1\r\n: b\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	test_parser_rejected ("GET / HTTP/1.1\r\nA\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	test_parser_rejected ("GET / HTTP/1.1\r\n A: b\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	test_parser_rejected ("GET / HTTP/1.1\r\nA: b\r\n c\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);
	test_parser_rejected ("GET / HTTP/1.1\r\nA: b\x01" "c\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);
	test_parser_rejected ("GET / HTTP/1.1\r\nA: b\x7f" "c\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);
	test_parser_rejected ("GET / HTTP/1.1\r\nA: b\nB: c\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_LINE);
	test_parser_rejected ("GET / HTTP/1.1\r\nA: b\rB: c\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_LINE);
	test_parser_rejected ("GET / HTTP/1.1\nA: b\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_LINE);

	// a CR followed by another byte is an error as soon as it arrives
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	size_t consumed = 0;
	test_check_unsigned_eq (test_parser_head ("GET / HTTP/1.1\r\n\rx", strlen ("GET / HTTP/1.1\r\n\rx"), &consumed), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_LINE, NULL);

	// a value with HTAB, obs-text and every visible byte, longer than a word
	test_parser_accepted ("GET / HTTP/1.1\r\nA: \t!~\x80\xff a\tvalue longer than a word\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (test_parser_span_is (test_parser.head.fields[0].value, "!~\x80\xff a\tvalue longer than a word"));

}

static void test_parser_fields_specials (void) {

	test_parser_accepted ("POST / HTTP/1.1\r\nContent-Length: 0003\r\n\r\nabc", HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser.head.content_length, 3, NULL);
	test_check_str_eq (test_parser_body, "abc", NULL);

	test_parser_rejected ("POST / HTTP/1.1\r\nContent-Length: 18446744073709551616\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);
	// the biggest one fits, its body is never complete here
	test_check_unsigned_eq (
		test_parser_request ("POST / HTTP/1.1\r\nContent-Length: 18446744073709551615\r\n\r\n", HTTP_PARSER_LENIENT_NONE),
		CERVER_HTTP_PARSER_RESULT_MORE, NULL
	);
	test_check_true (test_parser.head.content_length == UINT64_MAX);
	test_parser_rejected ("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);
	test_parser_rejected ("POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\nx", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);
	test_parser_rejected ("POST / HTTP/1.1\r\nContent-Length:\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);

	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);
	test_parser_rejected ("POST / HTTP/1.1\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked, gzip\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding:\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);

	test_parser_accepted ("POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\nTransfer-Encoding: chunked   \r\n\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (test_parser.head.flags & CERVER_HTTP_HEAD_CHUNKED);

	test_parser_accepted ("GET / HTTP/1.1\r\nConnection: keep-alive , Upgrade\r\nUpgrade: websocket\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (test_parser.head.flags & CERVER_HTTP_HEAD_CONNECTION_KEEP_ALIVE);
	test_check_true (test_parser.head.flags & CERVER_HTTP_HEAD_CONNECTION_UPGRADE);
	test_check_true (test_parser.upgrade);

	test_parser_accepted ("GET / HTTP/1.1\r\nProxy-Connection: close\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_true (test_parser.head.flags & CERVER_HTTP_HEAD_CONNECTION_CLOSE);
	test_parser_accepted ("GET / HTTP/1.1\r\nConnection: closed\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_false (test_parser.head.flags & CERVER_HTTP_HEAD_CONNECTION_CLOSE);

	// an Upgrade without Connection: upgrade is not an upgrade
	test_parser_accepted ("GET / HTTP/1.1\r\nUpgrade: websocket\r\n\r\n", HTTP_PARSER_LENIENT_NONE);
	test_check_false (test_parser.upgrade);

}

static void test_parser_fields_types (void) {

	static char lower[TEST_PARSER_TEXT_SIZE];
	static char upper[TEST_PARSER_TEXT_SIZE];

	for (unsigned int type = 0; type < HTTP_HEADER_UNDEFINED; type++) {
		const char *name = http_header_string ((http_header) type);
		const size_t length = strlen (name);

		for (size_t i = 0; i <= length; i++) {
			lower[i] = (char) ((name[i] >= 'A' && name[i] <= 'Z') ? (name[i] + ('a' - 'A')) : name[i]);
			upper[i] = (char) ((name[i] >= 'a' && name[i] <= 'z') ? (name[i] - ('a' - 'A')) : name[i]);
		}

		test_check_unsigned_eq (cerver_http_parser_field_type (name, length), type, name);
		test_check_unsigned_eq (cerver_http_parser_field_type (lower, length), type, name);
		test_check_unsigned_eq (cerver_http_parser_field_type (upper, length), type, name);

		// one byte less is not a known name, no known name starts another one
		test_check_unsigned_eq (cerver_http_parser_field_type (name, length - 1), HTTP_HEADER_UNDEFINED, name);
	}

	test_check_unsigned_eq (cerver_http_parser_field_type ("X-Unknown", strlen ("X-Unknown")), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (cerver_http_parser_field_type ("", 0), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (cerver_http_parser_field_type (NULL, 0), HTTP_HEADER_UNDEFINED, NULL);

	// a byte that is not a token never becomes one of a known name
	test_check_unsigned_eq (cerver_http_parser_field_type ("Hos\x14", strlen ("Hos\x14")), HTTP_HEADER_UNDEFINED, NULL);
	test_check_unsigned_eq (cerver_http_parser_field_type ("Content\rLength", strlen ("Content\rLength")), HTTP_HEADER_UNDEFINED, NULL);

}

#pragma endregion

#pragma region body

static void test_parser_body_content_length (void) {

	const char *text = { "POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\n0123456789GET / HTTP/1.1\r\n\r\n" };

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

	size_t used = 0;
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_str_eq (test_parser_body, "0123456789", NULL);
	test_check_true (cerver_http_parser_has_body (&test_parser));

	// the next message of the connection starts at the bytes after the body
	const size_t next = used;
	test_check_unsigned_eq (test_parser_message (text + next, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser.messages, 2, NULL);
	test_check_unsigned_eq (test_parser.head.method, REQUEST_METHOD_GET, NULL);

	// the body one byte at a time
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	size_t consumed = 0;
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	size_t offset = consumed;
	size_t body = 0;
	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;
	while (result != CERVER_HTTP_PARSER_RESULT_COMPLETE) {
		const char *data = NULL;
		size_t data_length = 0;
		result = cerver_http_parser_body (&test_parser, text + offset, 1, &consumed, &data, &data_length);
		test_check (result != CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
		offset += consumed;
		body += data_length;
	}

	test_check_unsigned_eq (body, 10, NULL);

	// the connection ended in the middle of the body
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);
	test_check_unsigned_eq (test_parser_read_body (text + consumed, 3, &used), CERVER_HTTP_PARSER_RESULT_MORE, NULL);
	test_check_unsigned_eq (cerver_http_parser_finish (&test_parser), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_INCOMPLETE, NULL);

}

static void test_parser_body_chunked (void) {

	const char *text = {
		"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
		"5;name=value;quoted=\"a \\\" b\"\r\nfirst\r\n"
		"A\r\n0123456789\r\n"
		"0\r\nX-Trailer: value\r\nConnection: close\r\n\r\n"
	};

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

	size_t used = 0;
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_str_eq (test_parser_body, "first0123456789", NULL);
	test_check_unsigned_eq (used, strlen (text), NULL);

	// the trailers count as fields, and a Connection there still closes the connection
	test_check_unsigned_eq (test_parser.head.fields_counted, 3, NULL);
	test_check_false (cerver_http_parser_keep_alive (&test_parser));

	// every byte apart
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	size_t consumed = 0;
	test_check_unsigned_eq (test_parser_head (text, strlen (text), &consumed), CERVER_HTTP_PARSER_RESULT_HEAD, NULL);

	size_t offset = consumed;
	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;
	while ((result != CERVER_HTTP_PARSER_RESULT_COMPLETE) && (offset < strlen (text))) {
		const char *data = NULL;
		size_t data_length = 0;
		result = cerver_http_parser_body (&test_parser, text + offset, 1, &consumed, &data, &data_length);
		test_check (result != CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
		offset += consumed;
	}

	test_check_unsigned_eq (result, CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (offset, strlen (text), NULL);

	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n10000000000000000\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nG\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1 \r\nx\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1; a\r\nx\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1;a=\"\x01\"\r\nx\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxy\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CHUNK_DATA);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 1\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nTransfer-Encoding: chunked\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nA\x01: b\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nA: b\x01\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);
	test_parser_rejected ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nA: b\r\n c\r\n\r\n", HTTP_PARSER_LENIENT_NONE, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);

}

#pragma endregion

#pragma region responses

static CerverHttpParserResult test_parser_response (const char *text, const bool skip_body, const bool finish) {

	test_parser_start (CERVER_HTTP_PARSER_TYPE_RESPONSE, HTTP_PARSER_LENIENT_NONE);
	test_parser.skip_body = skip_body;

	size_t used = 0;
	CerverHttpParserResult result = test_parser_message (text, &used);
	if (finish && (result == CERVER_HTTP_PARSER_RESULT_MORE)) result = cerver_http_parser_finish (&test_parser);

	return result;

}

static void test_parser_responses (void) {

	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", false, false), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser.head.status, 200, NULL);
	test_check_true (test_parser_span_is (test_parser.head.reason, "OK"));
	test_check_str_eq (test_parser_body, "ok", NULL);

	// a response without a length ends with its connection
	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 200 OK\r\n\r\nuntil the end", false, true), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_str_eq (test_parser_body, "until the end", NULL);
	test_check_false (cerver_http_parser_keep_alive (&test_parser));

	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\nframes", false, false), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_true (test_parser.upgrade);
	test_check_unsigned_eq (test_parser_body_length, 0, NULL);

	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 204 No Content\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 304 Not Modified\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n", true, false), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 200\r\nContent-Length: 0\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser.head.reason.length, 0, NULL);

	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 20 OK\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_STATUS, NULL);
	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 200OK\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser_response ("HTTP/1.1 200 O\rK\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_LINE, NULL);
	test_check_unsigned_eq (test_parser_response ("ICE/1.0 200 OK\r\n\r\n", false, false), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_PROTOCOL, NULL);

	// a 100 is complete without a body, and the response that follows it is the next message
	test_parser_start (CERVER_HTTP_PARSER_TYPE_RESPONSE, HTTP_PARSER_LENIENT_NONE);
	const char *continued = { "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" };
	size_t used = 0;
	test_check_unsigned_eq (test_parser_message (continued, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser.head.status, 100, NULL);
	test_check_unsigned_eq (test_parser_message (continued + used, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser.head.status, 200, NULL);

}

#pragma endregion

#pragma region messages

// the bytes after a message that closes its connection
static void test_parser_messages_closed (void) {

	const char *text = { "GET / HTTP/1.1\r\nConnection: close\r\n\r\n\r\nGET / HTTP/1.1\r\n\r\n" };
	const size_t first = strlen ("GET / HTTP/1.1\r\nConnection: close\r\n\r\n");

	size_t consumed = 0;
	size_t used = 0;

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (used, first, NULL);
	test_check_unsigned_eq (test_parser_head (text + first, strlen (text + first), &consumed), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_CLOSED, NULL);

	// only empty lines after it are not an error
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_head ("\r\n", strlen ("\r\n"), &consumed), CERVER_HTTP_PARSER_RESULT_MORE, NULL);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_DATA_AFTER_CLOSE);
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_head (text + first, strlen (text + first), &consumed), CERVER_HTTP_PARSER_RESULT_MORE, NULL);
	test_check_unsigned_eq (consumed, strlen (text + first), NULL);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_KEEP_ALIVE);
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_message (text + first, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);

	// a reset starts a new message whatever the one before was
	cerver_http_parser_reset (&test_parser);
	test_check_false (cerver_http_parser_started (&test_parser));
	test_check_unsigned_eq (test_parser_message ("GET / HTTP/1.1\r\n\r\n", &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);

}

static void test_parser_messages_upgrade (void) {

	// an upgrade with a body ends after its body, the bytes after it are of its new protocol
	const char *text = { "POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: other\r\nContent-Length: 2\r\n\r\nokframes" };

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	size_t used = 0;
	test_check_unsigned_eq (test_parser_message (text, &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_true (test_parser.upgrade);
	test_check_str_eq (test_parser_body, "ok", NULL);
	test_check_unsigned_eq (used, strlen (text) - strlen ("frames"), NULL);

	// a CONNECT ends with its head, also with a Content-Length
	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_message ("CONNECT host:443 HTTP/1.1\r\nContent-Length: 2\r\n\r\nok", &used), CERVER_HTTP_PARSER_RESULT_COMPLETE, NULL);
	test_check_unsigned_eq (test_parser_body_length, 0, NULL);

}

static void test_parser_messages_finish (void) {

	size_t consumed = 0;

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (cerver_http_parser_finish (&test_parser), CERVER_HTTP_PARSER_RESULT_MORE, NULL);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_head ("GE", strlen ("GE"), &consumed), CERVER_HTTP_PARSER_RESULT_MORE, NULL);
	test_check_unsigned_eq (cerver_http_parser_finish (&test_parser), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (test_parser.error, CERVER_HTTP_PARSER_ERROR_INCOMPLETE, NULL);

	test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);
	test_check_unsigned_eq (test_parser_head ("GET / HTTP/1.1\r\nA: b", strlen ("GET / HTTP/1.1\r\nA: b"), &consumed), CERVER_HTTP_PARSER_RESULT_MORE, NULL);
	test_check_unsigned_eq (cerver_http_parser_finish (&test_parser), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);

	// a parser that failed stays failed
	test_check_unsigned_eq (test_parser_head ("GET / HTTP/1.1\r\n\r\n", strlen ("GET / HTTP/1.1\r\n\r\n"), &consumed), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
	test_check_unsigned_eq (cerver_http_parser_finish (&test_parser), CERVER_HTTP_PARSER_RESULT_ERROR, NULL);

}

#pragma endregion

#pragma region lenient

typedef struct TestParserLenientCase {

	const char *request;
	unsigned int lenient;

} TestParserLenientCase;

// each request is rejected in the strict mode and accepted with its flag
static const TestParserLenientCase test_parser_lenient_cases[] = {
	{ "GET / HTTP/1.1\r\nA: b\x01" "c\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS },
	{ "GET / HTTP/1.1\r\nA: b\r\n folded\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS },
	{ "GET / HTTP/1.1\r\nContent-Length : 0\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS },
	{ "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_CHUNKED_LENGTH },
	{ "POST / HTTP/1.1\r\nTransfer-Encoding: chunked, gzip\r\n\r\n", HTTP_PARSER_LENIENT_TRANSFER_ENCODING },
	{ "GET / HTTP/4.1\r\n\r\n", HTTP_PARSER_LENIENT_VERSION },
	{ "GET / HTTP/1.1\rA: b\r\n\r\n", HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR },
	{ "GET / HTTP/1.1\r\nA: b\n\n", HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF },
	{ "GET / HTTP/1.1\nA: b\n\n", HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF | HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR },
	{ "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx1\r\ny\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_OPTIONAL_CRLF_AFTER_CHUNK },
	{ "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1  \r\nx\r\n0\r\n\r\n", HTTP_PARSER_LENIENT_SPACES_AFTER_CHUNK_SIZE }
};

#define TEST_PARSER_LENIENT_CASES		(sizeof (test_parser_lenient_cases) / sizeof (test_parser_lenient_cases[0]))

static void test_parser_lenient (void) {

	for (size_t i = 0; i < TEST_PARSER_LENIENT_CASES; i++) {
		const TestParserLenientCase *lenient = &test_parser_lenient_cases[i];

		test_check_unsigned_eq (test_parser_request (lenient->request, HTTP_PARSER_LENIENT_NONE), CERVER_HTTP_PARSER_RESULT_ERROR, lenient->request);

		// a request with a Transfer-Encoding that does not end with chunked reads its body until the end
		CerverHttpParserResult result = test_parser_request (lenient->request, lenient->lenient);
		if (result == CERVER_HTTP_PARSER_RESULT_MORE) result = cerver_http_parser_finish (&test_parser);
		test_check_unsigned_eq (result, CERVER_HTTP_PARSER_RESULT_COMPLETE, lenient->request);
	}

	// a folded value keeps its line breaks in its span
	test_parser_accepted ("GET / HTTP/1.1\r\nA: first\r\n  second\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS);
	test_check_true (test_parser.head.fields[0].flags & CERVER_HTTP_FIELD_FOLDED);
	test_check_true (test_parser_span_is (test_parser.head.fields[0].value, "first\r\n  second"));

}

// the symbols of a token (RFC 9110 section 5.6.2), its letters and its digits are checked by range
static const char *test_parser_token_symbols = { "!#$%&'*+-.^_`|~" };

#define TEST_PARSER_NAME_POSITIONS		40

static bool test_parser_is_token (const unsigned char byte) {

	return ((byte >= 'a') && (byte <= 'z')) || ((byte >= 'A') && (byte <= 'Z'))
		|| ((byte >= '0') && (byte <= '9')) || (byte && strchr (test_parser_token_symbols, byte));

}

// every byte in every position of a name, in the lanes of the kernels of 16 and 8 bytes and after them:
// only the bytes of a token are a name (the colon ends it)
static void test_parser_fields_bytes (void) {

	static const char *start_line = { "GET / HTTP/1.1\r\n" };
	static const char *end = { ": v\r\n\r\n" };

	char text[TEST_PARSER_TEXT_SIZE];

	for (unsigned int value = 0; value < TEST_PARSER_BYTE_VALUES; value++) {
		if (value == ':') continue;

		for (size_t position = 0; position < TEST_PARSER_NAME_POSITIONS; position++) {
			const size_t name_length = TEST_PARSER_NAME_POSITIONS;

			size_t length = strlen (start_line);
			(void) memcpy (text, start_line, length);
			(void) memset (text + length, 'n', name_length);
			text[length + position] = (char) value;
			length += name_length;
			(void) memcpy (text + length, end, strlen (end));
			length += strlen (end);

			test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

			size_t consumed = 0;
			const CerverHttpParserResult result = test_parser_head (text, length, &consumed);

			if (test_parser_is_token ((unsigned char) value)) {
				test_check_int_eq (result, CERVER_HTTP_PARSER_RESULT_HEAD, NULL);
				test_check_unsigned_eq (test_parser.head.fields[0].name.length, name_length, NULL);
			}

			else {
				test_check_int_eq (result, CERVER_HTTP_PARSER_RESULT_ERROR, NULL);
			}
		}
	}

}

// a byte of a value: HTAB, SP, VCHAR and obs-text (RFC 9110 section 5.5)
static bool test_parser_is_value (const unsigned char byte) {

	return (byte == '\t') || ((byte >= ' ') && (byte != 0x7F));

}

// every byte in every position of a value, in the lanes of the kernels of 16 and 8 bytes and after them:
// a control, DEL, a CR or a LF alone is an error
static void test_parser_fields_value_bytes (void) {

	static const char *start = { "GET / HTTP/1.1\r\nA: " };
	static const char *end = { "\r\n\r\n" };

	char text[TEST_PARSER_TEXT_SIZE];

	for (unsigned int value = 0; value < TEST_PARSER_BYTE_VALUES; value++) {
		for (size_t position = 0; position < TEST_PARSER_NAME_POSITIONS; position++) {
			const size_t value_length = TEST_PARSER_NAME_POSITIONS;

			size_t length = strlen (start);
			(void) memcpy (text, start, length);
			(void) memset (text + length, 'v', value_length);
			text[length + position] = (char) value;
			length += value_length;
			(void) memcpy (text + length, end, strlen (end));
			length += strlen (end);

			test_parser_start (CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE);

			size_t consumed = 0;
			const CerverHttpParserResult result = test_parser_head (text, length, &consumed);

			test_check_int_eq (
				result, test_parser_is_value ((unsigned char) value) ? CERVER_HTTP_PARSER_RESULT_HEAD : CERVER_HTTP_PARSER_RESULT_ERROR,
				NULL
			);
		}
	}

}

// a CR alone in a field only ends an empty value, with the lenient flag of the headers: the one of a CR without a LF
// ends the request line and the empty line, not a field
static void test_parser_fields_carriage (void) {

	test_parser_rejected ("GET / HTTP/1.1\r\nA: 1\rB: 2\r\n\r\n", HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR, CERVER_HTTP_PARSER_ERROR_LINE);
	test_parser_rejected ("GET / HTTP/1.1\r\nA:\rB: 2\r\n\r\n", HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR, CERVER_HTTP_PARSER_ERROR_LINE);
	test_parser_accepted ("GET / HTTP/1.1\rA: 1\r\n\r\n", HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR);

	// with the lenient flag of the headers, an empty value that ends at a CR alone is a line of its own (its end is
	// that CR) and the next one folds it, also when that one ends with a LF alone after a byte that only the flag takes
	// (llhttp 9.4.3; the fuzzer llhttp found it)
	test_parser_accepted ("GET / HTTP/1.1\r\nA:\r \x01\nB: c\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS);
	test_parser_accepted ("GET / HTTP/1.1\r\nA:\r b\x01\nB: c\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS);
	test_parser_rejected ("GET / HTTP/1.1\r\nA:\r b\nB: c\r\n\r\n", HTTP_PARSER_LENIENT_HEADERS, CERVER_HTTP_PARSER_ERROR_LINE);

}

#pragma region multipart

#define TEST_PARSER_MULTIPART_PIECES	3

// what the callbacks of a body saw: the data of its parts one after another, and the parts
static char test_parser_multipart_data[TEST_PARSER_BODY_SIZE];
static size_t test_parser_multipart_length = 0;
static unsigned int test_parser_multipart_parts = 0;

static int test_parser_multipart_on_data (CerverHttpMultipart *parser, const char *at, size_t length) {

	(void) parser;

	test_check ((test_parser_multipart_length + length) < TEST_PARSER_BODY_SIZE, NULL);
	(void) memcpy (test_parser_multipart_data + test_parser_multipart_length, at, length);
	test_parser_multipart_length += length;

	return 0;

}

static int test_parser_multipart_on_header (CerverHttpMultipart *parser, const char *at, size_t length) {

	(void) parser;
	(void) at;
	(void) length;

	return 0;

}

static int test_parser_multipart_on_begin (CerverHttpMultipart *parser) {

	(void) parser;

	test_parser_multipart_parts += 1;

	return 0;

}

static int test_parser_multipart_on_event (CerverHttpMultipart *parser) {

	(void) parser;

	return 0;

}

static const CerverHttpMultipartCallbacks test_parser_multipart_callbacks = {
	test_parser_multipart_on_header, test_parser_multipart_on_header, test_parser_multipart_on_data,
	test_parser_multipart_on_begin, test_parser_multipart_on_event, test_parser_multipart_on_event,
	test_parser_multipart_on_event
};

// the data of a part has what looks like the start of a boundary and is not one: a CRLF and its dashes,
// the boundary without one of its bytes, the boundary without its CRLF; each byte is data
static void test_parser_multipart_lookalikes (void) {

	static const char *boundary = { "--boundary" };
	static const char *data = { "a\r\nb\r\n-c\r\n--d\r\n--boundar\r\n--boundarX--boundary" };

	char body[TEST_PARSER_BODY_SIZE];
	const int length = snprintf (
		body, TEST_PARSER_BODY_SIZE,
		"%s\r\nContent-Disposition: form-data; name=\"a\"\r\n\r\n%s\r\n%s--\r\n", boundary, data, boundary
	);

	// the body whole, and in pieces of 1, 2 and 3 bytes
	for (size_t piece = 0; piece <= TEST_PARSER_MULTIPART_PIECES; piece++) {
		test_parser_multipart_length = 0;
		test_parser_multipart_parts = 0;

		CerverHttpMultipart multipart;
		test_check_true (cerver_http_multipart_init (
			&multipart, boundary, strlen (boundary), &test_parser_multipart_callbacks, NULL
		));

		const size_t size = piece ? piece : (size_t) length;
		for (size_t offset = 0; offset < (size_t) length; offset += size) {
			const size_t bytes = (((size_t) length - offset) < size) ? ((size_t) length - offset) : size;
			test_check_unsigned_eq (cerver_http_multipart_execute (&multipart, body + offset, bytes), bytes, NULL);
		}

		test_check_unsigned_eq (test_parser_multipart_parts, 1, NULL);
		test_check_unsigned_eq (test_parser_multipart_length, strlen (data), NULL);
		test_check (!memcmp (test_parser_multipart_data, data, strlen (data)), NULL);
	}

}

#pragma endregion

static void test_parser_strings (void) {

	#define XX(num, name, string, reason)																	\
		test_check_str_eq (cerver_http_parser_error_string (CERVER_HTTP_PARSER_ERROR_##name), #string, NULL);	\
		test_check_str_eq (cerver_http_parser_error_reason (CERVER_HTTP_PARSER_ERROR_##name), #reason, NULL);
	CERVER_HTTP_PARSER_ERROR_MAP (XX)
	#undef XX

	test_check_str_eq (cerver_http_parser_error_string ((CerverHttpParserError) 1000), "none", NULL);
	test_check_str_eq (cerver_http_parser_error_reason ((CerverHttpParserError) 1000), "No error", NULL);

	#define XX(num, name, string, description)																\
		test_check_str_eq (http_parser_lenient_string (HTTP_PARSER_LENIENT_##name), #string, NULL);			\
		test_check_str_eq (http_parser_lenient_description (HTTP_PARSER_LENIENT_##name), #description, NULL);
	HTTP_PARSER_LENIENT_MAP(XX)
	#undef XX

	test_check_str_eq (http_parser_lenient_string (HTTP_PARSER_LENIENT_NONE), "none", NULL);
	test_check_str_eq (http_parser_lenient_description ((HttpParserLenient) 0x400), "none", NULL);

}

#pragma endregion

void http_tests_parser (void) {

	(void) printf ("Testing HTTP parser...\n");

	test_parser_head_simple ();
	test_parser_head_pieces ();
	test_parser_head_fields ();
	test_parser_head_rebase ();
	test_parser_head_empty_lines ();

	test_parser_limits_target ();
	test_parser_limits_fields ();
	test_parser_limits_default ();

	test_parser_start_versions ();
	test_parser_start_methods ();
	test_parser_start_targets ();
	test_parser_start_separators ();

	test_parser_fields_errors ();
	test_parser_fields_specials ();
	test_parser_fields_types ();
	test_parser_fields_bytes ();
	test_parser_fields_value_bytes ();
	test_parser_fields_carriage ();

	test_parser_body_content_length ();
	test_parser_body_chunked ();

	test_parser_responses ();

	test_parser_messages_closed ();
	test_parser_messages_upgrade ();
	test_parser_messages_finish ();

	test_parser_lenient ();
	test_parser_multipart_lookalikes ();
	test_parser_strings ();

	(void) printf ("Done!\n");

}
