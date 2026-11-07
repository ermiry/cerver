#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#include <cerver/types/string.h>

#include <cerver/http/conditional.h>
#include <cerver/http/headers.h>
#include <cerver/http/request.h>

#include "../test.h"

static const char *test_conditional_body = { "{\"oki\": \"doki\"}" };

// the FNV-1a with 64 bits of the body, computed once
static char test_conditional_etag[HTTP_ETAG_SIZE] = { 0 };
static size_t test_conditional_etag_length = 0;

static const char *test_conditional_last_modified = { "Sun, 06 Nov 1994 08:49:37 GMT" };
#define TEST_CONDITIONAL_LAST_MODIFIED		784111777

static void test_etag_from_data (void) {

	test_conditional_etag_length = http_etag_from_data (
		test_conditional_body, strlen (test_conditional_body), false, test_conditional_etag
	);

	// "<16 hex>"
	test_check_unsigned_eq (test_conditional_etag_length, 18, NULL);
	test_check (test_conditional_etag[0] == '"', NULL);
	test_check (test_conditional_etag[17] == '"', NULL);

	// the same body always has the same entity tag
	char again[HTTP_ETAG_SIZE] = { 0 };
	(void) http_etag_from_data (test_conditional_body, strlen (test_conditional_body), false, again);
	test_check_str_eq (again, test_conditional_etag, NULL);

	// a different body has another one
	char other[HTTP_ETAG_SIZE] = { 0 };
	(void) http_etag_from_data ("{}", 2, false, other);
	test_check (strcmp (other, test_conditional_etag), NULL);

	// the compressed representation has the suffix
	char compressed[HTTP_ETAG_SIZE] = { 0 };
	const size_t compressed_length = http_etag_from_data (
		test_conditional_body, strlen (test_conditional_body), true, compressed
	);

	test_check_unsigned_eq (compressed_length, 18 + strlen (HTTP_ETAG_COMPRESSED_SUFFIX), NULL);
	test_check (strstr (compressed, HTTP_ETAG_COMPRESSED_SUFFIX "\"") != NULL, NULL);

	// an empty body has an entity tag too
	test_check_unsigned_eq (http_etag_from_data (NULL, 0, false, other), 18, NULL);
	test_check_unsigned_eq (http_etag_from_data (NULL, 1, false, other), 0, NULL);

}

static void test_etag_from_file (void) {

	struct stat file_status;
	(void) memset (&file_status, 0, sizeof (struct stat));
	file_status.st_size = 255;
	file_status.st_mtime = 16;

	char etag[HTTP_ETAG_SIZE] = { 0 };
	test_check_unsigned_eq (http_etag_from_file (&file_status, etag), 9, NULL);
	test_check_str_eq (etag, "W/\"ff-10\"", NULL);

	test_check_unsigned_eq (http_etag_from_file (NULL, etag), 0, NULL);

}

static bool test_none_match (const char *if_none_match) {

	return http_etag_none_match (
		if_none_match, strlen (if_none_match),
		test_conditional_etag, test_conditional_etag_length
	);

}

static void test_etag_none_match (void) {

	char value[HTTP_ETAG_SIZE * 4] = { 0 };

	test_check_true (test_none_match (test_conditional_etag));
	test_check_true (test_none_match ("*"));
	test_check_true (test_none_match (" * "));

	// the weak comparison ignores W/
	(void) snprintf (value, sizeof (value), "W/%s", test_conditional_etag);
	test_check_true (test_none_match (value));

	// a list with the entity tag in any position
	(void) snprintf (value, sizeof (value), "\"other\", %s", test_conditional_etag);
	test_check_true (test_none_match (value));

	(void) snprintf (value, sizeof (value), "%s,\"other\"", test_conditional_etag);
	test_check_true (test_none_match (value));

	(void) snprintf (value, sizeof (value), " , W/\"a\" ,  W/%s  ", test_conditional_etag);
	test_check_true (test_none_match (value));

	test_check_false (test_none_match ("\"other\""));
	test_check_false (test_none_match ("\"other\", W/\"another\""));
	test_check_false (test_none_match (""));
	test_check_false (test_none_match (","));
	test_check_false (test_none_match ("W/"));

	// an entity tag without its quotes is not the same one
	(void) snprintf (value, sizeof (value), "%.*s", (int) (test_conditional_etag_length - 2), test_conditional_etag + 1);
	test_check_false (test_none_match (value));

	// an unterminated quote does not read past the value
	test_check_false (test_none_match ("\"abc"));

	test_check_false (http_etag_none_match (NULL, 0, test_conditional_etag, test_conditional_etag_length));
	test_check_false (http_etag_none_match ("*", 1, NULL, 0));

}

static HttpRequest *test_conditional_request (
	const RequestMethod method, const char *if_none_match, const char *if_modified_since
) {

	HttpRequest *request = http_request_new ();
	test_check_ptr (request);

	request->method = method;
	if (if_none_match) request->headers[HTTP_HEADER_IF_NONE_MATCH] = str_new (if_none_match);
	if (if_modified_since) request->headers[HTTP_HEADER_IF_MODIFIED_SINCE] = str_new (if_modified_since);

	return request;

}

static bool test_conditional (
	const RequestMethod method, const char *if_none_match, const char *if_modified_since,
	const time_t last_modified
) {

	HttpRequest *request = test_conditional_request (method, if_none_match, if_modified_since);

	const bool not_modified = http_conditional_not_modified (
		request, test_conditional_etag, test_conditional_etag_length, last_modified
	);

	http_request_delete (request);

	return not_modified;

}

static void test_conditional_not_modified (void) {

	const time_t modified = TEST_CONDITIONAL_LAST_MODIFIED;

	test_check_true (test_conditional (REQUEST_METHOD_GET, test_conditional_etag, NULL, 0));
	test_check_true (test_conditional (REQUEST_METHOD_HEAD, test_conditional_etag, NULL, 0));
	test_check_false (test_conditional (REQUEST_METHOD_GET, "\"other\"", NULL, 0));

	// only GET and HEAD are answered with 304
	test_check_false (test_conditional (REQUEST_METHOD_POST, test_conditional_etag, NULL, 0));
	test_check_false (test_conditional (REQUEST_METHOD_PUT, "*", NULL, 0));

	// If-Modified-Since with the same date or a later one
	test_check_true (test_conditional (REQUEST_METHOD_GET, NULL, test_conditional_last_modified, modified));
	test_check_true (test_conditional (REQUEST_METHOD_GET, NULL, test_conditional_last_modified, modified - 1));
	test_check_false (test_conditional (REQUEST_METHOD_GET, NULL, test_conditional_last_modified, modified + 1));

	// without a last modification, or with an invalid date
	test_check_false (test_conditional (REQUEST_METHOD_GET, NULL, test_conditional_last_modified, 0));
	test_check_false (test_conditional (REQUEST_METHOD_GET, NULL, "yesterday", modified));

	// If-None-Match takes precedence over If-Modified-Since
	test_check_false (test_conditional (REQUEST_METHOD_GET, "\"other\"", test_conditional_last_modified, modified));
	test_check_true (test_conditional (REQUEST_METHOD_GET, test_conditional_etag, test_conditional_last_modified, modified + 1));

	// a request without conditions
	test_check_false (test_conditional (REQUEST_METHOD_GET, NULL, NULL, modified));
	test_check_false (http_conditional_not_modified (NULL, test_conditional_etag, test_conditional_etag_length, 0));

}

void http_tests_conditional (void) {

	(void) printf ("Testing HTTP conditional requests...\n");

	test_etag_from_data ();

	test_etag_from_file ();

	test_etag_none_match ();

	test_conditional_not_modified ();

	(void) printf ("Done!\n");

}
