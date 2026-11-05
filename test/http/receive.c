#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <cerver/cerver.h>
#include <cerver/connection.h>
#include <cerver/handler.h>

#include <cerver/http/headers.h>
#include <cerver/http/http.h>
#include <cerver/http/multipart.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>

#include "../test.h"

#include "http.h"

// feeds http_receive_handle () with requests split between recv () calls
// at every byte offset, one byte at a time and whole, through a socket pair,
// and checks what the route handlers received and what the client got back

// bugs this suite fixed:
// - a body received in many pieces was not terminated with '\0'
// - a header name, a header value or the url received in pieces kept only the last piece
// - a binary body was truncated at the first '\0'
// - urlencoded values and the query used their length before decoding
// - multi-part files were written before the authentication
// - two files with the same filename in a request overwrote each other
// - a pipelined request was parsed after the connection was closed
// - a static file path with ".." was served from outside the static path
// coverage gaps: the bearer JWT validation with real keys (see jwt_decode.c),
// the receive loop of handler.c (see the web integration tests)

#define TEST_RECEIVE_RESPONSE_SIZE			(1 << 16)
#define TEST_RECEIVE_VALUE_SIZE				(1 << 12)
#define TEST_RECEIVE_FILES_SIZE				4
#define TEST_RECEIVE_PATH_SIZE				(1 << 9)

#define TEST_RECEIVE_SERVER					0
#define TEST_RECEIVE_CLIENT					1

#define TEST_RECEIVE_ROOT_PATH				"/tmp/cerver-test-receive"
#define TEST_RECEIVE_UPLOADS_PATH			TEST_RECEIVE_ROOT_PATH "/uploads"
#define TEST_RECEIVE_MISSING_PATH			TEST_RECEIVE_ROOT_PATH "/missing"
#define TEST_RECEIVE_STATIC_PATH			TEST_RECEIVE_ROOT_PATH "/public"
#define TEST_RECEIVE_PUBLIC_FILE			TEST_RECEIVE_STATIC_PATH "/index.html"
#define TEST_RECEIVE_PUBLIC_DIRECTORY		TEST_RECEIVE_STATIC_PATH "/folder.d"
#define TEST_RECEIVE_SECRET_FILE			TEST_RECEIVE_ROOT_PATH "/secret.txt"

#define TEST_RECEIVE_DIRECTORY_MODE			0755

#define TEST_RECEIVE_ROUTE_TOKENS			(DEFAULT_ROUTES_TOKENS_SIZE + 4)

static const char *public_file_content = { "<html>public</html>" };
static const char *secret_file_content = { "secret-content" };

static const char *valid_authorization = { "Bearer test-token" };

static const char *catch_all_message = { "HTTP Cerver!" };
static const char *continue_response = { "HTTP/1.1 100 Continue\r\n\r\n" };
static const char *response_status_prefix = { "HTTP/1.1 " };

#pragma region observed

typedef struct TestReceiveFile {

	size_t size;
	char content[TEST_RECEIVE_VALUE_SIZE];
	char saved_filename[TEST_RECEIVE_PATH_SIZE];

} TestReceiveFile;

// what the route handlers received
typedef struct TestReceiveObserved {

	unsigned int n_handled;

	unsigned int n_authentications;
	bool body_at_authentication;

	bool has_body;
	size_t body_len;
	char body[TEST_RECEIVE_VALUE_SIZE];

	char content_type[TEST_RECEIVE_VALUE_SIZE];
	char accept[TEST_RECEIVE_VALUE_SIZE];
	char custom[TEST_RECEIVE_VALUE_SIZE];

	char query[TEST_RECEIVE_VALUE_SIZE];
	char query_key[TEST_RECEIVE_VALUE_SIZE];
	char body_value[TEST_RECEIVE_VALUE_SIZE];

	char param[TEST_RECEIVE_VALUE_SIZE];

	unsigned int n_parts;
	char part_value[TEST_RECEIVE_VALUE_SIZE];

	unsigned int n_files;
	TestReceiveFile files[TEST_RECEIVE_FILES_SIZE];

} TestReceiveObserved;

static TestReceiveObserved observed;

// the client side of a request
typedef struct TestReceiveResult {

	HttpReceiveStatus status;

	size_t response_len;
	char response[TEST_RECEIVE_RESPONSE_SIZE];

} TestReceiveResult;

static TestReceiveResult result;

// the number of requests that were fed, printed at the end
static size_t n_fed_requests = 0;

// the case that is running, printed when a check fails
static const char *current_case = NULL;
static const char *current_mode = NULL;
static size_t current_offset = 0;

#define test_receive_expect(condition)														\
	({																						\
		if (!(condition)) {																	\
			(void) fprintf (																\
				stderr, "%s:%d -> case \"%s\" - %s %zu - failed: %s\n",					\
				__FILE__, __LINE__, current_case, current_mode, current_offset, #condition	\
			);																				\
			(void) fprintf (stderr, "response:\n%s\n", result.response);					\
			exit (1);																		\
		}																					\
	})

static void test_receive_copy (char *destination, const String *value) {

	(void) snprintf (
		destination, TEST_RECEIVE_VALUE_SIZE, "%s", (value && value->str) ? value->str : ""
	);

}

static void test_receive_copy_c_string (char *destination, const char *value) {

	(void) snprintf (destination, TEST_RECEIVE_VALUE_SIZE, "%s", value ? value : "");

}

#pragma endregion

#pragma region handlers

static void test_receive_observe_body (const HttpRequest *request) {

	const String *body = http_request_get_body (request);
	if (body) {
		observed.has_body = true;
		observed.body_len = body->len;
		(void) memcpy (
			observed.body, body->str,
			(body->len < TEST_RECEIVE_VALUE_SIZE) ? body->len : TEST_RECEIVE_VALUE_SIZE
		);
	}

}

// POST /test/echo, POST /test/private, POST /test/bearer
static void test_receive_echo_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_handled += 1;

	test_receive_observe_body (request);

	test_receive_copy (observed.content_type, http_request_get_header (request, HTTP_HEADER_CONTENT_TYPE));
	test_receive_copy (observed.accept, http_request_get_header (request, HTTP_HEADER_ACCEPT));
	test_receive_copy_c_string (observed.custom, http_request_get_custom_header (request, "X-Custom"));

	test_receive_copy (observed.query, http_request_get_query (request));
	test_receive_copy (
		observed.query_key,
		http_query_pairs_get_value (http_request_get_query_params (request), "key")
	);

	test_receive_copy (observed.body_value, http_request_body_get_value (request, "b"));

	(void) http_response_send (oki_doki, http_receive);

}

// POST /test/items/:id/update
static void test_receive_param_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_handled += 1;

	test_receive_observe_body (request);

	test_receive_copy (observed.param, http_request_get_param_at_idx (request, 0));

	(void) http_response_send (oki_doki, http_receive);

}

// reads the saved file while the request exists (the files are deleted when it ends)
static void test_receive_observe_file (const MultiPart *multi_part) {

	if (observed.n_files < TEST_RECEIVE_FILES_SIZE) {
		TestReceiveFile *file = &observed.files[observed.n_files];
		observed.n_files += 1;

		test_receive_copy_c_string (file->saved_filename, http_multi_part_get_saved_filename (multi_part));

		FILE *saved = fopen (file->saved_filename, "rb");
		if (saved) {
			file->size = fread (file->content, 1, TEST_RECEIVE_VALUE_SIZE - 1, saved);
			(void) fclose (saved);
		}
	}

}

// POST /test/upload
static void test_receive_upload_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_handled += 1;

	if (http_request_multi_parts_iter_start (request)) {
		const MultiPart *multi_part = http_request_multi_parts_iter_get_next (request);
		while (multi_part) {
			observed.n_parts += 1;

			if (http_multi_part_is_file (multi_part)) test_receive_observe_file (multi_part);
			else test_receive_copy_c_string (observed.part_value, http_multi_part_get_value (multi_part));

			multi_part = http_request_multi_parts_iter_get_next (request);
		}
	}

	(void) http_response_send (oki_doki, http_receive);

}

// the authentication runs before the body is received
static unsigned int test_receive_authentication_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_authentications += 1;
	observed.body_at_authentication = (request->body != NULL) || (request->multi_parts != NULL);

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);

	return (authorization && !strcmp (authorization->str, valid_authorization)) ? 0 : 1;

}

// a route that authenticates with a value of the body
static unsigned int test_receive_body_authentication_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_authentications += 1;
	observed.body_at_authentication = (request->multi_parts != NULL);

	const char *key = http_request_multi_parts_get_value (request, "field");

	return (key && !strcmp (key, "hello")) ? 0 : 1;

}

static const char *fixed_generated_filename = { "fixed.jpg" };
static const char *numbered_filename_suffix = { "fixed-1.jpg" };

// every file gets the same name, so the second file of a request already exists
static void test_receive_fixed_filename_generator (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	http_multi_part_set_generated_filename (
		http_request_get_current_mpart (request), "%s", fixed_generated_filename
	);

}

#pragma endregion

#pragma region setup

static void test_receive_write_file (const char *filename, const char *content) {

	FILE *file = fopen (filename, "wb");
	test_check_ptr (file);

	(void) fwrite (content, 1, strlen (content), file);
	(void) fclose (file);

}

static void test_receive_files_create (void) {

	(void) mkdir (TEST_RECEIVE_ROOT_PATH, TEST_RECEIVE_DIRECTORY_MODE);
	(void) mkdir (TEST_RECEIVE_UPLOADS_PATH, TEST_RECEIVE_DIRECTORY_MODE);
	(void) mkdir (TEST_RECEIVE_STATIC_PATH, TEST_RECEIVE_DIRECTORY_MODE);
	(void) mkdir (TEST_RECEIVE_PUBLIC_DIRECTORY, TEST_RECEIVE_DIRECTORY_MODE);

	test_receive_write_file (TEST_RECEIVE_PUBLIC_FILE, public_file_content);
	test_receive_write_file (TEST_RECEIVE_SECRET_FILE, secret_file_content);

}

static void test_receive_files_delete (void) {

	(void) remove (TEST_RECEIVE_PUBLIC_FILE);
	(void) remove (TEST_RECEIVE_SECRET_FILE);
	(void) rmdir (TEST_RECEIVE_PUBLIC_DIRECTORY);
	(void) rmdir (TEST_RECEIVE_STATIC_PATH);
	(void) rmdir (TEST_RECEIVE_UPLOADS_PATH);
	(void) rmdir (TEST_RECEIVE_ROOT_PATH);

}

// returns the number of files left in the uploads directory
static unsigned int test_receive_uploads_count (void) {

	unsigned int count = 0;

	DIR *directory = opendir (TEST_RECEIVE_UPLOADS_PATH);
	if (directory) {
		const struct dirent *entry = NULL;
		while ((entry = readdir (directory))) {
			if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, "..")) count += 1;
		}

		(void) closedir (directory);
	}

	return count;

}

static HttpRoute *test_receive_child_route (
	HttpRoute *parent, const char *path, const HttpHandler handler
) {

	HttpRoute *route = http_route_create (REQUEST_METHOD_POST, path, handler);
	http_route_child_add (parent, route);

	return route;

}

static Cerver *test_receive_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_WEB, "test-receive", 8080, PROTOCOL_TCP, false, 2
	);

	test_check_ptr (cerver);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_ptr (http_cerver);

	HttpRoute *main_route = http_route_create (REQUEST_METHOD_POST, "test", test_receive_echo_handler);

	(void) test_receive_child_route (main_route, "echo", test_receive_echo_handler);
	(void) test_receive_child_route (main_route, "items/:id/update", test_receive_param_handler);

	HttpRoute *upload_route = test_receive_child_route (main_route, "upload", test_receive_upload_handler);
	http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);

	HttpRoute *private_route = test_receive_child_route (main_route, "private", test_receive_upload_handler);
	http_route_set_auth (private_route, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_route_set_authentication_handler (private_route, test_receive_authentication_handler);

	HttpRoute *bearer_route = test_receive_child_route (main_route, "bearer", test_receive_echo_handler);
	http_route_set_auth (bearer_route, HTTP_ROUTE_AUTH_TYPE_BEARER);

	HttpRoute *body_route = test_receive_child_route (main_route, "body", test_receive_upload_handler);
	http_route_set_auth (body_route, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_route_set_authentication_handler (body_route, test_receive_body_authentication_handler);
	http_route_set_authentication_with_body (body_route, true);

	http_cerver_route_register (http_cerver, main_route);

	http_cerver_set_uploads_path (http_cerver, TEST_RECEIVE_UPLOADS_PATH);
	http_cerver_set_default_uploads_filename_generator (http_cerver);
	http_cerver_set_uploads_delete_when_done (http_cerver, true);

	(void) http_cerver_static_path_add (http_cerver, TEST_RECEIVE_STATIC_PATH);

	test_check_unsigned_eq (http_cerver_init (http_cerver), 0, NULL);

	return cerver;

}

static void test_receive_limits_reset (HttpCerver *http_cerver) {

	http_cerver_set_max_body_size (http_cerver, HTTP_CERVER_DEFAULT_MAX_BODY_SIZE);
	http_cerver_set_max_url_size (http_cerver, HTTP_CERVER_DEFAULT_MAX_URL_SIZE);
	http_cerver_set_max_header_size (http_cerver, HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE);
	http_cerver_set_max_headers (http_cerver, HTTP_CERVER_DEFAULT_MAX_HEADERS);
	http_cerver_set_max_multipart_parts (http_cerver, HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS);
	http_cerver_set_max_file_size (http_cerver, HTTP_CERVER_DEFAULT_MAX_FILE_SIZE);

	http_cerver_set_uploads_path (http_cerver, TEST_RECEIVE_UPLOADS_PATH);

}

#pragma endregion

#pragma region feed

// a piece is copied to a buffer of its exact size, so the sanitizers catch any read past it
static void test_receive_feed_piece (
	HttpReceive *http_receive, const char *piece, const size_t piece_len
) {

	char *buffer = (char *) malloc (piece_len);
	test_check_ptr (buffer);

	(void) memcpy (buffer, piece, piece_len);

	http_receive->handler (http_receive, (ssize_t) piece_len, buffer);

	free (buffer);

}

static void test_receive_read_response (const int client_fd) {

	ssize_t received = 0;
	while (
		(result.response_len < (TEST_RECEIVE_RESPONSE_SIZE - 1))
		&& ((received = read (
			client_fd,
			result.response + result.response_len,
			TEST_RECEIVE_RESPONSE_SIZE - 1 - result.response_len
		)) > 0)
	) {
		result.response_len += (size_t) received;
	}

	result.response[result.response_len] = '\0';

}

// feeds the request in pieces that end at each cut
static void test_receive_feed (
	Cerver *cerver,
	const char *request, const size_t request_len,
	const size_t *cuts, const size_t n_cuts
) {

	(void) memset (&observed, 0, sizeof (TestReceiveObserved));
	(void) memset (&result, 0, sizeof (TestReceiveResult));

	n_fed_requests += 1;

	int fds[2] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);

	// the request is never sent through the socket, so the client is done writing
	(void) shutdown (fds[TEST_RECEIVE_CLIENT], SHUT_WR);

	Connection *connection = connection_create_empty ();
	test_check_ptr (connection);
	connection->socket->sock_fd = fds[TEST_RECEIVE_SERVER];
	connection->active = true;

	CerverReceive cerver_receive;
	(void) memset (&cerver_receive, 0, sizeof (CerverReceive));
	cerver_receive.cerver = cerver;
	cerver_receive.socket = connection->socket;
	cerver_receive.connection = connection;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);
	test_check_ptr (http_receive);

	size_t start = 0;
	for (size_t i = 0; (i <= n_cuts) && (connection->socket->sock_fd != -1); i++) {
		const size_t end = (i < n_cuts) ? cuts[i] : request_len;
		if (end > start) test_receive_feed_piece (http_receive, request + start, end - start);
		start = end;
	}

	result.status = http_receive->receive_status;

	// a request that was not completed is closed like the receive loop does
	if (connection->socket->sock_fd != -1) connection_end (connection);

	test_receive_read_response (fds[TEST_RECEIVE_CLIENT]);

	http_receive_delete (http_receive);
	connection_delete (connection);

	(void) close (fds[TEST_RECEIVE_CLIENT]);

}

static void test_receive_feed_whole (
	Cerver *cerver, const char *request, const size_t request_len
) {

	current_mode = "whole";
	current_offset = 0;

	test_receive_feed (cerver, request, request_len, NULL, 0);

}

static void test_receive_feed_split (
	Cerver *cerver, const char *request, const size_t request_len, const size_t offset
) {

	current_mode = "split at";
	current_offset = offset;

	test_receive_feed (cerver, request, request_len, &offset, 1);

}

static void test_receive_feed_bytes (
	Cerver *cerver, const char *request, const size_t request_len
) {

	current_mode = "one byte per recv";
	current_offset = 0;

	size_t *cuts = (size_t *) calloc (request_len, sizeof (size_t));
	test_check_ptr (cuts);

	for (size_t i = 0; i < request_len; i++) cuts[i] = i + 1;

	test_receive_feed (cerver, request, request_len, cuts, request_len);

	free (cuts);

}

// runs the request whole, split at every byte and one byte at a time
static void test_receive_every_split (
	Cerver *cerver, const char *name,
	const char *request, const size_t request_len,
	void (*check)(void)
) {

	current_case = name;

	test_receive_feed_whole (cerver, request, request_len);
	check ();

	for (size_t offset = 1; offset < request_len; offset++) {
		test_receive_feed_split (cerver, request, request_len, offset);
		check ();
	}

	test_receive_feed_bytes (cerver, request, request_len);
	check ();

}

static bool test_receive_status_is (const char *status) {

	const size_t prefix_len = strlen (response_status_prefix);

	return !strncmp (result.response, response_status_prefix, prefix_len)
		&& !strncmp (result.response + prefix_len, status, strlen (status));

}

static unsigned int test_receive_count_responses (void) {

	unsigned int count = 0;

	const char *found = result.response;
	while ((found = strstr (found, response_status_prefix))) {
		count += 1;
		found += strlen (response_status_prefix);
	}

	return count;

}

#pragma endregion

#pragma region requests

static const char *echo_json_body = { "{\"name\": \"cerver\", \"values\": [1, 2, 3]}" };

static const char *echo_json_request = {
	"POST /test/echo?key=value%20x&other=1 HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: application/json\r\n"
	"X-Custom: custom-value\r\n"
	"Content-Length: 39\r\n"
	"\r\n"
	"{\"name\": \"cerver\", \"values\": [1, 2, 3]}"
};

static void test_receive_check_echo_json (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (observed.has_body);
	test_receive_expect (observed.body_len == strlen (echo_json_body));
	test_receive_expect (!strcmp (observed.body, echo_json_body));
	test_receive_expect (!strcmp (observed.content_type, "application/json"));
	test_receive_expect (!strcmp (observed.custom, "custom-value"));
	test_receive_expect (!strcmp (observed.query, "key=value%20x&other=1"));
	test_receive_expect (!strcmp (observed.query_key, "value x"));

}

// a binary body with '\0' bytes
static const char binary_request[] = {
	"POST /test/items/abc123/update HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: application/octet-stream\r\n"
	"Content-Length: 8\r\n"
	"\r\n"
	"a\0b\0c\0d\0"
};

static const char binary_body[] = { 'a', '\0', 'b', '\0', 'c', '\0', 'd', '\0' };

static void test_receive_check_binary (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.param, "abc123"));
	test_receive_expect (observed.body_len == sizeof (binary_body));
	test_receive_expect (!memcmp (observed.body, binary_body, sizeof (binary_body)));

}

static const char *urlencoded_request = {
	"POST /test/echo HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: application/x-www-form-urlencoded\r\n"
	"Content-Length: 19\r\n"
	"\r\n"
	"a=1&b=hello%20world"
};

static void test_receive_check_urlencoded (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body_value, "hello world"));

}

// two files with the same filename and a value
static const char *multipart_request = {
	"POST /test/upload HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
	"Content-Length: 411\r\n"
	"\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"field\"\r\n"
	"\r\n"
	"hello\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"first\"; filename=\"photo.jpg\"\r\n"
	"Content-Type: image/jpeg\r\n"
	"\r\n"
	"first-file-content\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"second\"; filename=\"photo.jpg\"\r\n"
	"Content-Type: image/jpeg\r\n"
	"\r\n"
	"second-file-content\r\n"
	"------CerverBoundary7MA4YWxk--\r\n"
};

static void test_receive_check_multipart (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (observed.n_parts == 3);
	test_receive_expect (!strcmp (observed.part_value, "hello"));
	test_receive_expect (observed.n_files == 2);
	test_receive_expect (!strcmp (observed.files[0].content, "first-file-content"));
	test_receive_expect (!strcmp (observed.files[1].content, "second-file-content"));
	test_receive_expect (strcmp (observed.files[0].saved_filename, observed.files[1].saved_filename));
	test_receive_expect (test_receive_uploads_count () == 0);

}

static const char *private_request = {
	"POST /test/private HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Authorization: Bearer test-token\r\n"
	"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
	"Content-Length: 117\r\n"
	"\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"field\"\r\n"
	"\r\n"
	"hello\r\n"
	"------CerverBoundary7MA4YWxk--\r\n"
};

static void test_receive_check_private (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_authentications == 1);
	test_receive_expect (!observed.body_at_authentication);
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.part_value, "hello"));

}

static const char *repeated_headers_request = {
	"POST /test/echo HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Accept: text/html\r\n"
	"X-Custom: one\r\n"
	"Accept: application/json\r\n"
	"x-custom: two\r\n"
	"Content-Length: 0\r\n"
	"\r\n"
};

static void test_receive_check_repeated_headers (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.accept, "text/html, application/json"));
	test_receive_expect (!strcmp (observed.custom, "one, two"));

}

static const char *expect_continue_request = {
	"POST /test/echo HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Expect: 100-continue\r\n"
	"Content-Length: 4\r\n"
	"\r\n"
	"data"
};

static void test_receive_check_expect_continue (void) {

	test_receive_expect (!strncmp (result.response, continue_response, strlen (continue_response)));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body, "data"));

}

#pragma endregion

#pragma region cases

static void test_receive_splits (Cerver *cerver) {

	test_receive_every_split (
		cerver, "json body, query and custom header",
		echo_json_request, strlen (echo_json_request), test_receive_check_echo_json
	);

	test_receive_every_split (
		cerver, "binary body and route param",
		binary_request, sizeof (binary_request) - 1, test_receive_check_binary
	);

	test_receive_every_split (
		cerver, "urlencoded body",
		urlencoded_request, strlen (urlencoded_request), test_receive_check_urlencoded
	);

	test_receive_every_split (
		cerver, "multi-part with two files with the same filename",
		multipart_request, strlen (multipart_request), test_receive_check_multipart
	);

	test_receive_every_split (
		cerver, "authenticated multi-part",
		private_request, strlen (private_request), test_receive_check_private
	);

	test_receive_every_split (
		cerver, "repeated headers",
		repeated_headers_request, strlen (repeated_headers_request), test_receive_check_repeated_headers
	);

	test_receive_every_split (
		cerver, "expect 100-continue",
		expect_continue_request, strlen (expect_continue_request), test_receive_check_expect_continue
	);

}

static void test_receive_whole_case (
	Cerver *cerver, const char *name, const char *request
) {

	current_case = name;
	test_receive_feed_whole (cerver, request, strlen (request));

}

static void test_receive_rejected_authentication (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "failed authentication before the body",
		"POST /test/private HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Authorization: Bearer wrong-token\r\n"
		"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
		"Content-Length: 146\r\n"
		"\r\n"
		"------CerverBoundary7MA4YWxk\r\n"
		"Content-Disposition: form-data; name=\"first\"; filename=\"photo.jpg\"\r\n"
		"\r\n"
		"file-content\r\n"
		"------CerverBoundary7MA4YWxk--\r\n"
	);

	test_receive_expect (test_receive_status_is ("401"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_REJECTED);
	test_receive_expect (observed.n_authentications == 1);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (test_receive_uploads_count () == 0);

	test_receive_whole_case (
		cerver, "short bearer authorization",
		"POST /test/bearer HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Authorization: Bearer\r\n"
		"Content-Length: 0\r\n"
		"\r\n"
	);

	test_receive_expect (test_receive_status_is ("401"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (
		cerver, "bearer authorization without prefix",
		"POST /test/bearer HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Authorization: Basic dXNlcjpwYXNzd29yZA==\r\n"
		"Content-Length: 0\r\n"
		"\r\n"
	);

	test_receive_expect (test_receive_status_is ("401"));
	test_receive_expect (observed.n_handled == 0);

}

static void test_receive_bad_requests (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "repeated unique header",
		"POST /test/echo HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Host: example.com\r\n"
		"Content-Length: 0\r\n"
		"\r\n"
	);

	test_receive_expect (test_receive_status_is ("400"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (cerver, "malformed request", "NOT AN HTTP REQUEST\r\n\r\n");

	test_receive_expect (test_receive_status_is ("400"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_PARSER_ERROR);

	test_receive_whole_case (
		cerver, "multi-part without boundary",
		"POST /test/upload HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Content-Type: multipart/form-data; charset=utf-8\r\n"
		"Content-Length: 4\r\n"
		"\r\n"
		"data"
	);

	test_receive_expect (test_receive_status_is ("400"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (
		cerver, "pipelined requests",
		"POST /test/echo HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"first"
		"POST /test/echo HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Content-Length: 6\r\n"
		"\r\n"
		"second"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body, "first"));
	test_receive_expect (test_receive_count_responses () == 1);

}

static void test_receive_too_many_route_tokens (Cerver *cerver) {

	char request[TEST_RECEIVE_VALUE_SIZE] = { 0 };
	int written = snprintf (request, TEST_RECEIVE_VALUE_SIZE, "POST /test");
	for (unsigned int i = 0; i < TEST_RECEIVE_ROUTE_TOKENS; i++) {
		written += snprintf (request + written, TEST_RECEIVE_VALUE_SIZE - written, "/token");
	}

	(void) snprintf (
		request + written, TEST_RECEIVE_VALUE_SIZE - written,
		" HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_whole_case (cerver, "url with more tokens than any route", request);

	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

}

static void test_receive_static_files (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "static file",
		"GET /index.html HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (strstr (result.response, public_file_content) != NULL);

	test_receive_whole_case (
		cerver, "static file outside the static path",
		"GET /../secret.txt HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (strstr (result.response, secret_file_content) == NULL);
	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

	test_receive_whole_case (
		cerver, "static directory",
		"GET /folder.d HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

}

static void test_receive_limit_case (
	Cerver *cerver, const char *name, const char *request, const char *status
) {

	test_receive_whole_case (cerver, name, request);

	test_receive_expect (test_receive_status_is (status));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_LIMIT);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (test_receive_uploads_count () == 0);

	test_receive_limits_reset ((HttpCerver *) cerver->cerver_data);

}

static void test_receive_limits (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_max_url_size (http_cerver, 1 << 4);
	test_receive_limit_case (
		cerver, "url limit",
		"POST /test/echo?key=a-long-query-value HTTP/1.1\r\nHost: localhost:8080\r\n\r\n",
		"414"
	);

	http_cerver_set_max_headers (http_cerver, 1 << 1);
	test_receive_limit_case (
		cerver, "headers count limit",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nAccept: */*\r\nX-One: 1\r\n\r\n",
		"431"
	);

	http_cerver_set_max_header_size (http_cerver, 1 << 6);
	test_receive_limit_case (
		cerver, "headers size limit",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\n"
		"X-Padding: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n",
		"431"
	);

	http_cerver_set_max_body_size (http_cerver, 1 << 4);
	test_receive_limit_case (
		cerver, "body limit by content length",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 32\r\n\r\n"
		"01234567890123456789012345678901",
		"413"
	);

	http_cerver_set_max_body_size (http_cerver, 1 << 4);
	test_receive_limit_case (
		cerver, "chunked body limit",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nTransfer-Encoding: chunked\r\n\r\n"
		"20\r\n01234567890123456789012345678901\r\n0\r\n\r\n",
		"413"
	);

	http_cerver_set_max_multipart_parts (http_cerver, 1);
	test_receive_limit_case (cerver, "multi-part parts limit", multipart_request, "413");

	http_cerver_set_max_file_size (http_cerver, 1 << 3);
	test_receive_limit_case (cerver, "multi-part file size limit", multipart_request, "413");

}

static void test_receive_custom_header_too_large (Cerver *cerver) {

	char request[TEST_RECEIVE_VALUE_SIZE] = { 0 };
	char value[HTTP_CUSTOM_HEADER_VALUE_SIZE + 1] = { 0 };
	(void) memset (value, 'v', HTTP_CUSTOM_HEADER_VALUE_SIZE);

	(void) snprintf (
		request, TEST_RECEIVE_VALUE_SIZE,
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nX-Custom: %s\r\n\r\n", value
	);

	test_receive_whole_case (cerver, "custom header value that does not fit", request);

	test_receive_expect (test_receive_status_is ("431"));
	test_receive_expect (observed.n_handled == 0);

}

static void test_receive_authentication_with_body (Cerver *cerver) {

	// the multi-part body of the upload route, sent to the route that authenticates with it
	const char *body = strstr (multipart_request, "\r\n\r\n") + strlen ("\r\n\r\n");

	char request[TEST_RECEIVE_VALUE_SIZE] = { 0 };
	(void) snprintf (
		request, TEST_RECEIVE_VALUE_SIZE,
		"POST /test/body HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
		"Content-Length: %zu\r\n"
		"\r\n"
		"%s",
		strlen (body), body
	);

	test_receive_whole_case (cerver, "authentication with the body", request);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_authentications == 1);
	test_receive_expect (observed.body_at_authentication);
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (observed.n_files == 2);

	(void) memcpy (strstr (request, "hello"), "wrong", strlen ("hello"));

	test_receive_whole_case (cerver, "failed authentication with the body", request);

	test_receive_expect (test_receive_status_is ("401"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_REJECTED);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (test_receive_uploads_count () == 0);

}

static void test_receive_numbered_filenames (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_uploads_filename_generator (http_cerver, test_receive_fixed_filename_generator);

	test_receive_whole_case (cerver, "files with the same generated filename", multipart_request);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_files == 2);
	test_receive_expect (!strcmp (observed.files[0].content, "first-file-content"));
	test_receive_expect (!strcmp (observed.files[1].content, "second-file-content"));
	test_receive_expect (strstr (observed.files[1].saved_filename, numbered_filename_suffix) != NULL);
	test_receive_expect (test_receive_uploads_count () == 0);

	http_cerver_set_default_uploads_filename_generator (http_cerver);

}

static void test_receive_upload_error (Cerver *cerver) {

	http_cerver_set_uploads_path ((HttpCerver *) cerver->cerver_data, TEST_RECEIVE_MISSING_PATH);

	test_receive_whole_case (cerver, "file that can not be saved", multipart_request);

	test_receive_expect (test_receive_status_is ("500"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_UPLOAD_ERROR);
	test_receive_expect (observed.n_handled == 0);

	test_receive_limits_reset ((HttpCerver *) cerver->cerver_data);

}

#pragma endregion

void http_tests_receive (void) {

	(void) printf ("Testing HTTP receive...\n");

	test_receive_files_create ();

	Cerver *cerver = test_receive_cerver_create ();

	test_receive_splits (cerver);

	test_receive_rejected_authentication (cerver);
	test_receive_bad_requests (cerver);
	test_receive_too_many_route_tokens (cerver);
	test_receive_static_files (cerver);
	test_receive_limits (cerver);
	test_receive_custom_header_too_large (cerver);
	test_receive_authentication_with_body (cerver);
	test_receive_numbered_filenames (cerver);
	test_receive_upload_error (cerver);

	(void) cerver_teardown (cerver);

	test_receive_files_delete ();

	(void) printf ("Done! %zu requests were fed\n", n_fed_requests);

}
