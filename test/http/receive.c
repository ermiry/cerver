#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <cerver/cerver.h>
#include <cerver/connection.h>
#include <cerver/handler.h>

#include <cerver/collections/arena.h>

#include <cerver/http/access.h>
#include <cerver/http/auth.h>
#include <cerver/http/headers.h>
#include <cerver/http/http.h>
#include <cerver/http/jwt/alg.h>
#include <cerver/http/multipart.h>
#include <cerver/http/recycle.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>
#include <cerver/http/tree.h>

#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

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
// - a multi-part filename that was empty or ".." after it was sanitized pointed to the uploads directory
// - a dirname generator without an uploads path created its directory at the filesystem root
// - two requests with the same fd in the same second shared the default uploads directory
// coverage gaps: the receive loop of handler.c (see the web integration tests)

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
#define TEST_RECEIVE_PRIVATE_PATH			TEST_RECEIVE_ROOT_PATH "/private"
#define TEST_RECEIVE_PRIVATE_FILE			TEST_RECEIVE_PRIVATE_PATH "/report.txt"

#define TEST_RECEIVE_DIRECTORY_MODE			0755

#define TEST_RECEIVE_ROUTE_TOKENS			(HTTP_TREE_DEFAULT_MAX_PARAMS + 4)

#define TEST_RECEIVE_ENDPOINT_MAX_BODY		(1 << 3)

#define TEST_RECEIVE_ALLOWED_ORIGIN			"http://allowed.com"

#define TEST_RECEIVE_URL_BUFFER				0
#define TEST_RECEIVE_FIELD_BUFFER			1
#define TEST_RECEIVE_VALUE_BUFFER			2
#define TEST_RECEIVE_BUFFERS				3

// when it is set, the requests are created like a worker thread does, in its arena
static Arena *receive_arena = NULL;
static String *receive_buffers[TEST_RECEIVE_BUFFERS] = { NULL, NULL, NULL };

// the receive that uses the arena and the buffers, it is not in the arena of its request
static HttpReceive receive_storage;

static const char *public_file_content = { "<html>public</html>" };
static const char *secret_file_content = { "secret-content" };
static const char *private_file_content = { "private-report" };

static const char *valid_authorization = { "Bearer test-token" };

// the request that follows another one in the same buffer
#define PIPELINED_SECOND_REQUEST			\
	"POST /test/echo HTTP/1.1\r\n"			\
	"Host: localhost:8080\r\n"				\
	"Content-Length: 6\r\n"					\
	"\r\n"									\
	"second"

static const char *pipelined_second_request = { PIPELINED_SECOND_REQUEST };

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
	size_t body_capacity;
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

	// what the parser found when the request was completed
	bool keep_alive_requested;
	size_t unparsed;

	// the client can find the end of the response without the close of the connection
	bool framed;

	// the 101 was sent, with the bytes after the request kept for its connection
	bool upgraded;
	size_t pending_size;

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
		observed.body_capacity = body->max_len;
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

static const char *group_custom_data = { "group-data" };
static const char *render_json_body = { "{\"render\": true}" };
static const char *forbidden_body = { "{\"error\": \"forbidden\"}" };
static const char *custom_not_allowed_body = { "{\"error\": \"custom 405\"}" };

// GET /v2/items/:id, GET /v2/users/:name
static void test_receive_named_param_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_handled += 1;

	test_receive_copy (observed.param, http_request_get_param (request, "id"));
	if (!observed.param[0]) test_receive_copy (observed.param, http_request_get_param (request, "name"));

	test_receive_copy_c_string (observed.custom, (const char *) http_receive_get_route_custom_data (http_receive));
	test_receive_copy (observed.accept, http_request_get_header (request, HTTP_HEADER_REQUEST_ID));

	(void) http_response_send (oki_doki, http_receive);

}

// PATCH /v2/items/:id, the response is sent from its header and its data
static void test_receive_patch_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_handled += 1;

	test_receive_observe_body (request);
	test_receive_copy (observed.param, http_request_get_param (request, "id"));

	(void) http_response_json_custom_reference_send (
		http_receive, HTTP_STATUS_OK, render_json_body, strlen (render_json_body)
	);

}

// GET /v2/render
static void test_receive_render_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	observed.n_handled += 1;

	(void) http_response_render_json (
		http_receive, HTTP_STATUS_OK, render_json_body, strlen (render_json_body)
	);

}

static const char *framed_body = { "framed-body" };
static const char *framed_raw_header = { "HTTP/1.1 200 OK\r\nX-Raw: incomplete\r\n" };
static const char *framed_bytes_header = { "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n" };

// GET /v2/framed/created, a body without its length ends when the connection is closed
static void test_receive_framed_created_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	observed.n_handled += 1;

	(void) http_response_create_and_send (
		HTTP_STATUS_CREATED, framed_body, strlen (framed_body), http_receive
	);

}

// GET /v2/framed/empty, a response without a body and without its length
static void test_receive_framed_empty_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	observed.n_handled += 1;

	(void) http_response_create_and_send (HTTP_STATUS_OK, NULL, 0, http_receive);

}

// GET /v2/framed/stream, the stream is ended by cerver after its handler
static void test_receive_framed_stream_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	observed.n_handled += 1;

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);

	if (!http_response_stream_begin (http_receive, response)) {
		(void) http_response_stream_write (http_receive, framed_body, strlen (framed_body));

		// the response has no end until its last chunk is sent
		test_check_bool_eq (http_receive->framed, false, NULL);
	}

	http_response_return (response);

}

static void test_receive_framed_send_header (
	const HttpReceive *http_receive, const char *header
) {

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	http_response_set_header (response, strdup (header), strlen (header));
	(void) http_response_set_data_ref (response, framed_body, strlen (framed_body));
	(void) http_response_send (response, http_receive);
	http_response_return (response);

}

// GET /v2/framed/raw, a header that cerver can not complete is sent as it is
static void test_receive_framed_raw_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	observed.n_handled += 1;

	test_receive_framed_send_header (http_receive, framed_raw_header);

}

// GET /v2/framed/bytes, a complete header that was set as bytes, with the length of its body
static void test_receive_framed_bytes_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	observed.n_handled += 1;

	test_receive_framed_send_header (http_receive, framed_bytes_header);

}

// GET /v2/framed/silent, the handler does not answer, cerver does
static void test_receive_framed_silent_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

	observed.n_handled += 1;

}

static unsigned int observed_custom_deleted = 0;
static unsigned int observed_decoded_deleted = 0;

static void test_receive_custom_data_delete (void *data) {

	observed_custom_deleted += 1;

	free (data);

}

static void test_receive_decoded_data_delete (void *data) {

	observed_decoded_deleted += 1;

	free (data);

}

// POST /v2/data, a request with data that is deleted when it ends
static void test_receive_data_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	observed.n_handled += 1;

	test_receive_observe_body (request);

	// a request never has the data of the one that the connection had before
	test_check_null_ptr (http_request_get_custom_data (request));
	test_check_null_ptr (http_request_get_decoded_data (request));

	HttpRequest *owner = (HttpRequest *) request;

	http_request_set_custom_data (owner, strdup ("custom"));
	http_request_set_delete_custom_data (owner, test_receive_custom_data_delete);

	http_request_set_decoded_data (owner, strdup ("decoded"));
	http_request_set_delete_decoded_data (owner, test_receive_decoded_data_delete);

	(void) http_response_send (oki_doki, http_receive);

}

static unsigned int observed_before = 0;
static unsigned int observed_after = 0;

static HttpMiddlewareResult test_receive_count_before (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

	observed_before += 1;

	return HTTP_MIDDLEWARE_RESULT_CONTINUE;

}

static void test_receive_count_after (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

	observed_after += 1;

}

// answers the request, so the handler never runs
static HttpMiddlewareResult test_receive_forbid_before (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_render_json (
		http_receive, HTTP_STATUS_FORBIDDEN, forbidden_body, strlen (forbidden_body)
	);

	return HTTP_MIDDLEWARE_RESULT_STOP;

}

static void test_receive_method_not_allowed_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	test_receive_copy_c_string (observed.query, http_receive_get_allow (http_receive));

	(void) http_response_render_json (
		http_receive, HTTP_STATUS_METHOD_NOT_ALLOWED,
		custom_not_allowed_body, strlen (custom_not_allowed_body)
	);

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

	(void) http_receive;

	observed.n_authentications += 1;
	observed.body_at_authentication = (request->body != NULL) || (request->multi_parts != NULL);

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);

	return (authorization && !strcmp (authorization->str, valid_authorization)) ? 0 : 1;

}

// a route that authenticates with a value of the body
static unsigned int test_receive_body_authentication_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

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

	(void) http_receive;

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
	(void) mkdir (TEST_RECEIVE_PRIVATE_PATH, TEST_RECEIVE_DIRECTORY_MODE);

	test_receive_write_file (TEST_RECEIVE_PUBLIC_FILE, public_file_content);
	test_receive_write_file (TEST_RECEIVE_SECRET_FILE, secret_file_content);
	test_receive_write_file (TEST_RECEIVE_PRIVATE_FILE, private_file_content);

}

static void test_receive_files_delete (void) {

	(void) remove (TEST_RECEIVE_PUBLIC_FILE);
	(void) remove (TEST_RECEIVE_SECRET_FILE);
	(void) remove (TEST_RECEIVE_PRIVATE_FILE);
	(void) rmdir (TEST_RECEIVE_PRIVATE_PATH);
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

// the methods of the WebSocket endpoints, a socket pair never starts their connections
static const HttpWebSocketHandlers test_receive_web_socket_handlers = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static HttpRoute *test_receive_child_route (
	HttpRoute *parent, const char *path, const HttpHandler handler
) {

	HttpRoute *route = http_route_create (REQUEST_METHOD_POST, path, handler);
	http_route_child_add (parent, route);

	return route;

}

// the routes of the groups API, next to the ones of the first API
static void test_receive_groups_create (HttpCerver *http_cerver) {

	HttpGroup *v2 = http_cerver_group_create (http_cerver, "v2");
	test_check_ptr (v2);

	http_group_set_custom_data (v2, (void *) group_custom_data, NULL);
	test_check_unsigned_eq (http_group_add_before (v2, test_receive_count_before), 0, NULL);
	test_check_unsigned_eq (http_group_add_after (v2, test_receive_count_after), 0, NULL);

	test_check_ptr (http_group_get (v2, "items/:id", test_receive_named_param_handler));
	test_check_ptr (http_group_patch (v2, "items/:id", test_receive_patch_handler));
	test_check_ptr (http_group_get (v2, "users/:name", test_receive_named_param_handler));
	test_check_ptr (http_group_get (v2, "render", test_receive_render_handler));

	HttpEndpoint *forbidden = http_group_post (v2, "items/:id", test_receive_echo_handler);
	test_check_unsigned_eq (http_endpoint_add_before (forbidden, test_receive_forbid_before), 0, NULL);

	HttpEndpoint *limited = http_group_post (v2, "limited", test_receive_echo_handler);
	http_endpoint_set_max_body_size (limited, TEST_RECEIVE_ENDPOINT_MAX_BODY);

	// a request without an upgrade goes to its handler
	HttpEndpoint *socket = http_group_get (v2, "socket", test_receive_echo_handler);
	http_endpoint_set_web_socket (socket, &test_receive_web_socket_handlers);

	// a request without an upgrade is answered with 426
	test_check_ptr (http_group_web_socket_add (v2, "only", &test_receive_web_socket_handlers));

	// the methods that the parser accepts since llhttp
	test_check_ptr (http_group_route_add (v2, REQUEST_METHOD_PROPFIND, "dav", test_receive_echo_handler));
	test_check_ptr (http_group_route_add (v2, REQUEST_METHOD_QUERY, "search", test_receive_echo_handler));

	HttpGroup *cors = http_group_create (v2, "cors");
	http_group_set_cors (cors, true);
	test_check_ptr (http_group_get (cors, "resource", test_receive_echo_handler));

	HttpGroup *framed = http_group_create (v2, "framed");
	test_check_ptr (http_group_get (framed, "created", test_receive_framed_created_handler));
	test_check_ptr (http_group_get (framed, "empty", test_receive_framed_empty_handler));
	test_check_ptr (http_group_get (framed, "stream", test_receive_framed_stream_handler));
	test_check_ptr (http_group_get (framed, "raw", test_receive_framed_raw_handler));
	test_check_ptr (http_group_get (framed, "bytes", test_receive_framed_bytes_handler));
	test_check_ptr (http_group_get (framed, "silent", test_receive_framed_silent_handler));

	test_check_ptr (http_group_post (v2, "data", test_receive_data_handler));

	test_check_unsigned_eq (http_cerver_add_origin_to_whitelist (http_cerver, TEST_RECEIVE_ALLOWED_ORIGIN), 0, NULL);

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

	// the first routes API has no WebSocket connections, they are answered with 501
	HttpRoute *socket_route = test_receive_child_route (main_route, "socket", test_receive_echo_handler);
	http_route_set_modifier (socket_route, HTTP_ROUTE_MODIFIER_WEB_SOCKET);

	HttpRoute *body_route = test_receive_child_route (main_route, "body", test_receive_upload_handler);
	http_route_set_auth (body_route, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_route_set_authentication_handler (body_route, test_receive_body_authentication_handler);
	http_route_set_authentication_with_body (body_route, true);

	http_cerver_route_register (http_cerver, main_route);

	test_receive_groups_create (http_cerver);

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

// the same receive that a worker thread prepares with its arena and its buffers,
// or one that allocates its own memory
static HttpReceive *test_receive_create (CerverReceive *cerver_receive) {

	HttpReceive *http_receive = NULL;

	if (receive_arena) {
		const HttpReceiveResources resources = {
			receive_arena,
			receive_buffers[TEST_RECEIVE_URL_BUFFER],
			receive_buffers[TEST_RECEIVE_FIELD_BUFFER],
			receive_buffers[TEST_RECEIVE_VALUE_BUFFER]
		};

		test_check_unsigned_eq (http_receive_prepare (&receive_storage, cerver_receive, &resources), 0, NULL);

		http_receive = &receive_storage;
	}

	else {
		http_receive = http_receive_create (cerver_receive);
	}

	test_check_ptr (http_receive);

	return http_receive;

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

	HttpReceive *http_receive = test_receive_create (&cerver_receive);

	// an upgraded connection is read by its WebSocket, never by the parser again
	size_t start = 0;
	for (size_t i = 0; (i <= n_cuts) && (connection->socket->sock_fd != -1) && !http_receive->upgraded; i++) {
		const size_t end = (i < n_cuts) ? cuts[i] : request_len;
		if (end > start) test_receive_feed_piece (http_receive, request + start, end - start);
		start = end;
	}

	result.status = http_receive->receive_status;
	result.keep_alive_requested = http_receive->keep_alive_requested;
	result.unparsed = http_receive->unparsed;
	result.framed = http_receive->framed;
	result.upgraded = http_receive->upgraded;
	result.pending_size = http_receive->web_socket ? http_receive->web_socket->pending_size : 0;

	// a request that was not completed is closed like the receive loop does
	if (connection->socket->sock_fd != -1) connection_end (connection);

	test_receive_read_response (fds[TEST_RECEIVE_CLIENT]);

	http_receive_delete (http_receive);
	if (receive_arena) arena_reset (receive_arena);

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

static void test_receive_every_split_actual (
	Cerver *cerver,
	const char *request, const size_t request_len,
	void (*check)(void)
) {

	test_receive_feed_whole (cerver, request, request_len);
	check ();

	for (size_t offset = 1; offset < request_len; offset++) {
		test_receive_feed_split (cerver, request, request_len, offset);
		check ();
	}

	test_receive_feed_bytes (cerver, request, request_len);
	check ();

}

// runs the request whole, split at every byte and one byte at a time,
// with its own memory and in the arena of the thread
static void test_receive_every_split (
	Cerver *cerver, const char *name,
	const char *request, const size_t request_len,
	void (*check)(void)
) {

	current_case = name;

	Arena *arena = receive_arena;

	receive_arena = NULL;
	test_receive_every_split_actual (cerver, request, request_len, check);

	receive_arena = arena;
	test_receive_every_split_actual (cerver, request, request_len, check);

}

static void test_receive_arena_create (void) {

	receive_arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
	test_check_ptr (receive_arena);

	for (unsigned int i = 0; i < TEST_RECEIVE_BUFFERS; i++) {
		receive_buffers[i] = str_new (NULL);
		test_check_ptr (receive_buffers[i]);
	}

}

static void test_receive_arena_delete (void) {

	arena_delete (receive_arena);
	receive_arena = NULL;

	for (unsigned int i = 0; i < TEST_RECEIVE_BUFFERS; i++) {
		str_delete (receive_buffers[i]);
		receive_buffers[i] = NULL;
	}

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

// a body is reserved once from its Content-Length (and its '\0'),
// in the smallest of the buffers that the threads reuse that fits it
static size_t test_receive_body_capacity (const size_t length) {

	size_t capacity = (size_t) 1 << HTTP_RECYCLE_BUFFER_MIN_EXPONENT;
	while (capacity < (length + 1)) capacity <<= 1;

	return capacity;

}

static void test_receive_check_echo_json (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (observed.has_body);
	test_receive_expect (observed.body_len == strlen (echo_json_body));
	test_receive_expect (!strcmp (observed.body, echo_json_body));
	// reserved once from its Content-Length, whatever the pieces it arrived in
	test_receive_expect (observed.body_capacity == test_receive_body_capacity (strlen (echo_json_body)));
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
	test_receive_expect (observed.body_capacity == test_receive_body_capacity (sizeof (binary_body)));

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

// a chunked body with a chunk extension and trailers,
// a trailer is not a header of the request, even with the name of one
static const char *chunked_request = {
	"POST /test/echo HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: application/json\r\n"
	"X-Custom: custom-value\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	"10\r\n"
	"{\"name\": \"cerver\r\n"
	"7;extension=value\r\n"
	"\", \"val\r\n"
	"10\r\n"
	"ues\": [1, 2, 3]}\r\n"
	"0\r\n"
	"X-Custom: trailer-value\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
};

static void test_receive_check_chunked (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (observed.has_body);
	test_receive_expect (observed.body_len == strlen (echo_json_body));
	test_receive_expect (!strcmp (observed.body, echo_json_body));
	test_receive_expect (!strcmp (observed.content_type, "application/json"));
	test_receive_expect (!strcmp (observed.custom, "custom-value"));

}

// the parts of a chunked multi-part body arrive in pieces of the chunks
static const char *chunked_multipart_request = {
	"POST /test/upload HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	"4E\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"field\"\r\n"
	"\r\n"
	"\r\n"
	"27\r\n"
	"hello\r\n"
	"------CerverBoundary7MA4YWxk--\r\n"
	"\r\n"
	"0\r\n"
	"\r\n"
};

static void test_receive_check_chunked_multipart (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.part_value, "hello"));

}

static const char *chunked_expect_continue_request = {
	"POST /test/echo HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Expect: 100-continue\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	"4\r\n"
	"data\r\n"
	"0\r\n"
	"\r\n"
};

#pragma endregion

#pragma region cases

static void test_receive_splits_chunked (Cerver *cerver) {

	test_receive_every_split (
		cerver, "chunked body with an extension and trailers",
		chunked_request, strlen (chunked_request), test_receive_check_chunked
	);

	test_receive_every_split (
		cerver, "chunked multi-part",
		chunked_multipart_request, strlen (chunked_multipart_request), test_receive_check_chunked_multipart
	);

	test_receive_every_split (
		cerver, "chunked body with expect 100-continue",
		chunked_expect_continue_request, strlen (chunked_expect_continue_request), test_receive_check_expect_continue
	);

}

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

	test_receive_splits_chunked (cerver);

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
		PIPELINED_SECOND_REQUEST
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body, "first"));
	test_receive_expect (test_receive_count_responses () == 1);

	// the parser stopped after the first request, the second one was not parsed
	test_receive_expect (result.keep_alive_requested);
	test_receive_expect (result.unparsed == strlen (pipelined_second_request));

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

#define TEST_RECEIVE_UNSAFE_REQUEST_SIZE	(1 << 11)
#define TEST_RECEIVE_ESCAPED_FILE			TEST_RECEIVE_ROOT_PATH "/escape.txt"

static const char *unsafe_filenames_body = {
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"empty\"; filename=\"\"\r\n"
	"Content-Type: application/octet-stream\r\n"
	"\r\n"
	"ignored-content\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"parent\"; filename=\"../escape.txt\"\r\n"
	"\r\n"
	"parent-content\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"up\"; filename=\"..\"\r\n"
	"\r\n"
	"up-content\r\n"
	"------CerverBoundary7MA4YWxk\r\n"
	"Content-Disposition: form-data; name=\"current\"; filename=\".\"\r\n"
	"\r\n"
	"current-content\r\n"
	"------CerverBoundary7MA4YWxk--\r\n"
};

static bool test_receive_saved_as (const unsigned int idx, const char *filename) {

	const char *saved = observed.files[idx].saved_filename;
	const char *name = strrchr (saved, '/');

	return !strncmp (saved, TEST_RECEIVE_UPLOADS_PATH "/", strlen (TEST_RECEIVE_UPLOADS_PATH "/"))
		&& name && !strcmp (name + 1, filename);

}

// an empty filename saves nothing, and every other name is saved inside the uploads path
static void test_receive_unsafe_filenames (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_uploads_filename_generator (http_cerver, NULL);

	char request[TEST_RECEIVE_UNSAFE_REQUEST_SIZE];
	(void) snprintf (
		request, TEST_RECEIVE_UNSAFE_REQUEST_SIZE,
		"POST /test/upload HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
		"Content-Length: %zu\r\n"
		"\r\n"
		"%s",
		strlen (unsafe_filenames_body), unsafe_filenames_body
	);

	test_receive_whole_case (cerver, "unsafe filenames", request);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_parts == 4);
	test_receive_expect (observed.n_files == 4);

	// the file input without a file
	test_receive_expect (!strlen (observed.files[0].saved_filename));
	test_receive_expect (observed.files[0].size == 0);

	test_receive_expect (test_receive_saved_as (1, "..escape.txt"));
	test_receive_expect (!strcmp (observed.files[1].content, "parent-content"));

	test_receive_expect (test_receive_saved_as (2, HTTP_MULTI_PART_DEFAULT_FILENAME));
	test_receive_expect (!strcmp (observed.files[2].content, "up-content"));

	test_receive_expect (test_receive_saved_as (3, HTTP_MULTI_PART_DEFAULT_FILENAME "-1"));
	test_receive_expect (!strcmp (observed.files[3].content, "current-content"));

	test_receive_expect (access (TEST_RECEIVE_ESCAPED_FILE, F_OK) != 0);
	test_receive_expect (test_receive_uploads_count () == 0);

	http_cerver_set_default_uploads_filename_generator (http_cerver);

}

#define TEST_RECEIVE_DIRNAME				"cerver-test-receive-dirname"
#define TEST_RECEIVE_ROOT_DIRNAME			"/" TEST_RECEIVE_DIRNAME
#define TEST_RECEIVE_UPLOADS_DIRNAME		TEST_RECEIVE_UPLOADS_PATH "/" TEST_RECEIVE_DIRNAME

static unsigned int n_generated_dirnames = 0;

static void test_receive_counting_dirname_generator (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	n_generated_dirnames += 1;

	http_request_set_dirname (request, "%s", TEST_RECEIVE_DIRNAME);

}

// without an uploads path the dirname was generated and its directory was created at the filesystem root
static void test_receive_dirname_without_uploads_path (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver->uploads_path_len = 0;
	(void) memset (http_cerver->uploads_path, 0, HTTP_CERVER_UPLOADS_PATH_SIZE);

	n_generated_dirnames = 0;
	http_cerver_set_uploads_dirname_generator (http_cerver, test_receive_counting_dirname_generator);

	test_receive_whole_case (cerver, "a dirname generator without an uploads path", multipart_request);

	const bool created_at_root = (access (TEST_RECEIVE_ROOT_DIRNAME, F_OK) == 0);
	if (created_at_root) (void) rmdir (TEST_RECEIVE_ROOT_DIRNAME);

	http_cerver_set_uploads_dirname_generator (http_cerver, NULL);
	http_cerver_set_uploads_path (http_cerver, TEST_RECEIVE_UPLOADS_PATH);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (n_generated_dirnames == 0);
	test_receive_expect (!created_at_root);

}

// with an uploads path the files of the request are saved inside its generated directory,
// the cerver deletes the files when the request is done, so the empty directory is removed too
static void test_receive_dirname_with_uploads_path (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	n_generated_dirnames = 0;
	http_cerver_set_uploads_dirname_generator (http_cerver, test_receive_counting_dirname_generator);

	test_receive_whole_case (cerver, "a dirname generator with an uploads path", multipart_request);

	const bool left_in_uploads = (access (TEST_RECEIVE_UPLOADS_DIRNAME, F_OK) == 0);
	if (left_in_uploads) (void) rmdir (TEST_RECEIVE_UPLOADS_DIRNAME);

	http_cerver_set_uploads_dirname_generator (http_cerver, NULL);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (n_generated_dirnames == 1);
	test_receive_expect (!left_in_uploads);
	test_receive_expect (observed.n_files == 2);
	test_receive_expect (!strncmp (
		observed.files[0].saved_filename,
		TEST_RECEIVE_UPLOADS_DIRNAME "/", strlen (TEST_RECEIVE_UPLOADS_DIRNAME "/")
	));
	test_receive_expect (!strcmp (observed.files[0].content, "first-file-content"));
	test_receive_expect (access (TEST_RECEIVE_ROOT_DIRNAME, F_OK) != 0);

}

// copies the directory of the first saved file of the request and removes it
static void test_receive_default_dirname_take (char *directory) {

	test_receive_copy_c_string (directory, observed.files[0].saved_filename);

	char *last_slash = strrchr (directory, '/');
	if (last_slash) *last_slash = '\0';

	(void) rmdir (directory);

}

// two requests with the same fd in the same second shared the directory of the default generator,
// so their files were mixed in it, now each request has its own sequence
static void test_receive_default_dirnames (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_default_uploads_dirname_generator (http_cerver);

	char first[TEST_RECEIVE_PATH_SIZE] = { 0 };
	char second[TEST_RECEIVE_PATH_SIZE] = { 0 };

	test_receive_whole_case (cerver, "the first request with the default dirname", multipart_request);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_files == 2);
	test_receive_default_dirname_take (first);

	test_receive_whole_case (cerver, "the second request with the default dirname", multipart_request);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_files == 2);
	test_receive_default_dirname_take (second);

	http_cerver_set_uploads_dirname_generator (http_cerver, NULL);

	test_receive_expect (!strncmp (first, TEST_RECEIVE_UPLOADS_PATH "/", strlen (TEST_RECEIVE_UPLOADS_PATH "/")));
	test_receive_expect (strcmp (first, second) != 0);
	test_receive_expect (test_receive_uploads_count () == 0);

}

// an attribute without a value dereferenced NULL (found by the multipart fuzzer)
static void test_receive_attribute_without_value (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "a multi-part attribute without a value",
		"POST /test/upload HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Content-Type: multipart/form-data; boundary=----CerverBoundary7MA4YWxk\r\n"
		"Content-Length: 109\r\n"
		"\r\n"
		"------CerverBoundary7MA4YWxk\r\n"
		"Content-Disposition: form-data; name\r\n"
		"\r\n"
		"hello\r\n"
		"------CerverBoundary7MA4YWxk--\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);

}

// a request with a bearer of up to HTTP_JWT_BEARER_SIZE
#define TEST_RECEIVE_BEARER_REQUEST_SIZE	(1 << 14)
#define TEST_RECEIVE_JWT_EXPIRED_SECONDS	3600
#define TEST_RECEIVE_JWT_VALID_SECONDS		3600

// a literal, so the compiler checks its arguments
#define TEST_RECEIVE_BEARER_KEYS_REQUEST_FORMAT		\
	"POST /bearer HTTP/1.1\r\n"						\
	"Host: localhost:8080\r\n"						\
	"%s"											\
	"Content-Length: 0\r\n"							\
	"\r\n"

static Cerver *test_receive_bearer_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_WEB, "test-receive-bearer", 8080, PROTOCOL_TCP, false, 2
	);

	test_check_ptr (cerver);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpRoute *bearer_route = http_route_create (REQUEST_METHOD_POST, "bearer", test_receive_echo_handler);
	http_route_set_auth (bearer_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
	http_cerver_route_register (http_cerver, bearer_route);

	http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
	http_cerver_auth_set_jwt_priv_key_filename (http_cerver, private_key);
	http_cerver_auth_set_jwt_pub_key_filename (http_cerver, public_key);

	test_check_unsigned_eq (http_cerver_init (http_cerver), 0, NULL);

	return cerver;

}

// a token signed with the cerver's private key that expires at the given time
static void test_receive_bearer_token (
	HttpCerver *http_cerver, const time_t expires, char *bearer
) {

	HttpJwt *http_jwt = http_cerver_auth_jwt_new ();
	test_check_ptr (http_jwt);

	http_cerver_auth_jwt_add_value (http_jwt, "id", "receive-test");
	http_cerver_auth_jwt_add_value_int (http_jwt, "exp", (int64_t) expires);

	test_check_unsigned_eq (http_cerver_auth_generate_bearer_jwt (http_cerver, http_jwt), 0, NULL);

	(void) snprintf (bearer, HTTP_JWT_BEARER_SIZE, "%s", http_jwt->bearer);

	http_cerver_auth_jwt_delete (http_jwt);

}

static void test_receive_bearer_case (
	Cerver *cerver, const char *name, const char *authorization, const char *status
) {

	char header[HTTP_JWT_BEARER_SIZE + TEST_RECEIVE_PATH_SIZE] = { 0 };
	if (authorization) {
		(void) snprintf (header, sizeof (header), "Authorization: %s\r\n", authorization);
	}

	char request[TEST_RECEIVE_BEARER_REQUEST_SIZE];
	(void) snprintf (request, TEST_RECEIVE_BEARER_REQUEST_SIZE, TEST_RECEIVE_BEARER_KEYS_REQUEST_FORMAT, header);

	test_receive_whole_case (cerver, name, request);

	test_receive_expect (test_receive_status_is (status));

}

// the bearer JWT validated with real RS256 keys through the receive
static void test_receive_bearer_keys (void) {

	Cerver *cerver = test_receive_bearer_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	const time_t now = time (NULL);

	char valid[HTTP_JWT_BEARER_SIZE];
	test_receive_bearer_token (http_cerver, now + TEST_RECEIVE_JWT_VALID_SECONDS, valid);

	char expired[HTTP_JWT_BEARER_SIZE];
	test_receive_bearer_token (http_cerver, now - TEST_RECEIVE_JWT_EXPIRED_SECONDS, expired);

	// the first character of the signature changes (the last one has bits that are not used)
	char tampered[HTTP_JWT_BEARER_SIZE];
	(void) snprintf (tampered, HTTP_JWT_BEARER_SIZE, "%s", valid);
	char *signature = strrchr (tampered, '.') + 1;
	signature[0] = (signature[0] == 'A') ? 'B' : 'A';

	test_receive_bearer_case (cerver, "a valid bearer", valid, "200");
	test_receive_bearer_case (cerver, "an expired bearer", expired, "401");
	test_receive_bearer_case (cerver, "a bearer with a bad signature", tampered, "401");
	test_receive_bearer_case (cerver, "a bearer that is not a jwt", "Bearer not-a-jwt", "401");
	test_receive_bearer_case (cerver, "a request without a bearer", NULL, "401");

	(void) cerver_teardown (cerver);

}

// a literal, so the compiler checks its arguments
#define TEST_RECEIVE_STATIC_REQUEST_FORMAT		\
	"GET %s HTTP/1.1\r\n"						\
	"Host: localhost:8080\r\n"					\
	"%s"										\
	"\r\n"

static const char *private_file_url = { "/report.txt" };
static const char *public_file_url = { "/index.html" };
static const char *missing_file_url = { "/missing.txt" };

static void test_receive_static_case (
	Cerver *cerver, const char *name, const char *url,
	const char *authorization, const char *status
) {

	char header[HTTP_JWT_BEARER_SIZE + TEST_RECEIVE_PATH_SIZE] = { 0 };
	if (authorization) {
		(void) snprintf (header, sizeof (header), "Authorization: %s\r\n", authorization);
	}

	char request[TEST_RECEIVE_BEARER_REQUEST_SIZE];
	(void) snprintf (request, TEST_RECEIVE_BEARER_REQUEST_SIZE, TEST_RECEIVE_STATIC_REQUEST_FORMAT, url, header);

	test_receive_whole_case (cerver, name, request);

	test_receive_expect (test_receive_status_is (status));

	// the file of a rejected request is never sent
	if (strcmp (status, "200")) {
		test_receive_expect (strstr (result.response, private_file_content) == NULL);
	}

}

// a public static path and a private one, with its authentication
static Cerver *test_receive_static_cerver_create (
	const HttpRouteAuthType auth_type, HttpStaticPath **private_path
) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_WEB, "test-receive-static", 8080, PROTOCOL_TCP, false, 2
	);

	test_check_ptr (cerver);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	test_check_ptr (http_cerver_static_path_add (http_cerver, TEST_RECEIVE_STATIC_PATH));

	*private_path = http_cerver_static_path_add (http_cerver, TEST_RECEIVE_PRIVATE_PATH);
	test_check_ptr (*private_path);
	http_static_path_set_auth (*private_path, auth_type);

	return cerver;

}

// the files of a static path with a custom authentication
static void test_receive_static_custom_authentication (void) {

	HttpStaticPath *private_path = NULL;
	Cerver *cerver = test_receive_static_cerver_create (HTTP_ROUTE_AUTH_TYPE_CUSTOM, &private_path);

	http_static_path_set_authentication_handler (private_path, test_receive_authentication_handler);
	test_check_unsigned_eq (http_cerver_init ((HttpCerver *) cerver->cerver_data), 0, NULL);

	test_receive_static_case (cerver, "a private file without authentication", private_file_url, NULL, "401");
	test_receive_static_case (cerver, "a private file with a bad authentication", private_file_url, "Bearer bad-token", "401");

	test_receive_static_case (cerver, "a private file with its authentication", private_file_url, valid_authorization, "200");
	test_receive_expect (strstr (result.response, private_file_content) != NULL);

	test_receive_static_case (cerver, "a public file next to a private path", public_file_url, NULL, "200");
	test_receive_expect (strstr (result.response, public_file_content) != NULL);

	// a missing file is answered like a protected one, its existence is not told
	test_receive_static_case (cerver, "a missing file without authentication", missing_file_url, NULL, "401");
	test_receive_static_case (cerver, "a missing file with authentication", missing_file_url, valid_authorization, "200");
	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

	(void) cerver_teardown (cerver);

}

// the files of a static path with the bearer JWT of the cerver
static void test_receive_static_bearer_authentication (void) {

	HttpStaticPath *private_path = NULL;
	Cerver *cerver = test_receive_static_cerver_create (HTTP_ROUTE_AUTH_TYPE_BEARER, &private_path);
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
	http_cerver_auth_set_jwt_priv_key_filename (http_cerver, private_key);
	http_cerver_auth_set_jwt_pub_key_filename (http_cerver, public_key);

	test_check_unsigned_eq (http_cerver_init (http_cerver), 0, NULL);

	const time_t now = time (NULL);

	char valid[HTTP_JWT_BEARER_SIZE];
	test_receive_bearer_token (http_cerver, now + TEST_RECEIVE_JWT_VALID_SECONDS, valid);

	char expired[HTTP_JWT_BEARER_SIZE];
	test_receive_bearer_token (http_cerver, now - TEST_RECEIVE_JWT_EXPIRED_SECONDS, expired);

	test_receive_static_case (cerver, "a private file without a bearer", private_file_url, NULL, "401");
	test_receive_static_case (cerver, "a private file with an expired bearer", private_file_url, expired, "401");

	test_receive_static_case (cerver, "a private file with a valid bearer", private_file_url, valid, "200");
	test_receive_expect (strstr (result.response, private_file_content) != NULL);

	(void) cerver_teardown (cerver);

}

static void test_receive_upload_error (Cerver *cerver) {

	http_cerver_set_uploads_path ((HttpCerver *) cerver->cerver_data, TEST_RECEIVE_MISSING_PATH);

	test_receive_whole_case (cerver, "file that can not be saved", multipart_request);

	test_receive_expect (test_receive_status_is ("500"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_UPLOAD_ERROR);
	test_receive_expect (observed.n_handled == 0);

	test_receive_limits_reset ((HttpCerver *) cerver->cerver_data);

}

static const char *foreign_content = { "foreign-content" };

// the name of a file that another request already saved: fixed.jpg or fixed-<number>.jpg
static void test_receive_foreign_filename (const unsigned int number, char *filename) {

	if (number) {
		(void) snprintf (filename, TEST_RECEIVE_PATH_SIZE, "%s/fixed-%u.jpg", TEST_RECEIVE_UPLOADS_PATH, number);
	}

	else {
		(void) snprintf (filename, TEST_RECEIVE_PATH_SIZE, "%s/%s", TEST_RECEIVE_UPLOADS_PATH, fixed_generated_filename);
	}

}

// a part that could not open its file because every numbered name was taken
// removed the file of another request with the last name that it tried
static void test_receive_foreign_files (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	char filename[TEST_RECEIVE_PATH_SIZE] = { 0 };
	for (unsigned int number = 0; number <= HTTP_MULTI_PART_FILENAME_NUMBERS; number++) {
		test_receive_foreign_filename (number, filename);
		test_receive_write_file (filename, foreign_content);
	}

	http_cerver_set_uploads_filename_generator (http_cerver, test_receive_fixed_filename_generator);

	test_receive_whole_case (cerver, "every numbered filename is taken", multipart_request);

	http_cerver_set_default_uploads_filename_generator (http_cerver);

	unsigned int kept = 0;
	for (unsigned int number = 0; number <= HTTP_MULTI_PART_FILENAME_NUMBERS; number++) {
		test_receive_foreign_filename (number, filename);
		if (!access (filename, F_OK)) kept += 1;
		(void) remove (filename);
	}

	test_receive_expect (test_receive_status_is ("500"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_UPLOAD_ERROR);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (kept == (HTTP_MULTI_PART_FILENAME_NUMBERS + 1));
	test_receive_expect (test_receive_uploads_count () == 0);

}

static void test_receive_empty_filename_generator (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

// a generator that did not set a name saved the files as the uploads path itself ("uploads/"),
// now they are saved with their sanitized filenames
static void test_receive_empty_generated_filename (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_uploads_filename_generator (http_cerver, test_receive_empty_filename_generator);

	test_receive_whole_case (cerver, "a generator that does not set a filename", multipart_request);

	http_cerver_set_default_uploads_filename_generator (http_cerver);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_files == 2);
	test_receive_expect (strstr (observed.files[0].saved_filename, "/photo.jpg") != NULL);
	test_receive_expect (!strcmp (observed.files[1].content, "second-file-content"));
	test_receive_expect (test_receive_uploads_count () == 0);

}

#define TEST_RECEIVE_BLOCKER_NAME			"blocker"
#define TEST_RECEIVE_BLOCKER_FILE			TEST_RECEIVE_UPLOADS_PATH "/" TEST_RECEIVE_BLOCKER_NAME

static void test_receive_blocked_dirname_generator (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	http_request_set_dirname (request, "%s/inside", TEST_RECEIVE_BLOCKER_NAME);

}

// a directory that can not be created rejects the request before its files
static void test_receive_directory_error (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	test_receive_write_file (TEST_RECEIVE_BLOCKER_FILE, foreign_content);

	http_cerver_set_uploads_dirname_generator (http_cerver, test_receive_blocked_dirname_generator);

	test_receive_whole_case (cerver, "a directory that can not be created", multipart_request);

	http_cerver_set_uploads_dirname_generator (http_cerver, NULL);

	const bool blocker_kept = !access (TEST_RECEIVE_BLOCKER_FILE, F_OK);
	(void) remove (TEST_RECEIVE_BLOCKER_FILE);

	test_receive_expect (test_receive_status_is ("500"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_UPLOAD_ERROR);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (blocker_kept);
	test_receive_expect (test_receive_uploads_count () == 0);

}

static const char *named_param_request = {
	"GET /v2/items/abc%20123?x=1 HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"X-Request-Id: request-7\r\n"
	"\r\n"
};

static void test_receive_check_named_param (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.param, "abc 123"));
	test_receive_expect (!strcmp (observed.custom, group_custom_data));
	test_receive_expect (!strcmp (observed.accept, "request-7"));

}

static bool test_receive_has_header (const char *header) {

	return strstr (result.response, header) != NULL;

}

// the routes of the groups API: params by name, HEAD, OPTIONS, PATCH and middlewares
static void test_receive_groups (Cerver *cerver) {

	test_receive_every_split (
		cerver, "param by name",
		named_param_request, strlen (named_param_request), test_receive_check_named_param
	);

	observed_before = observed_after = 0;
	test_receive_whole_case (
		cerver, "HEAD with the GET handler",
		"HEAD /v2/items/abc HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (test_receive_has_header ("Content-Length: 15"));
	test_receive_expect (!test_receive_has_header ("doki"));
	test_receive_expect ((observed_before == 1) && (observed_after == 1));

	test_receive_whole_case (
		cerver, "HEAD with a rendered response",
		"HEAD /v2/render HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (!test_receive_has_header (render_json_body));

	test_receive_whole_case (
		cerver, "OPTIONS answered by the cerver",
		"OPTIONS /v2/items/abc HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("204"));
	test_receive_expect (test_receive_has_header ("Allow: GET, HEAD, POST, OPTIONS, PATCH\r\n"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (
		cerver, "PATCH",
		"PATCH /v2/items/abc HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 2\r\n\r\nhi"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.param, "abc"));
	test_receive_expect (!strcmp (observed.body, "hi"));
	test_receive_expect (test_receive_has_header (render_json_body));

	observed_before = observed_after = 0;
	test_receive_whole_case (
		cerver, "a middleware that answers the request",
		"POST /v2/items/abc HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("403"));
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect ((observed_before == 1) && (observed_after == 1));

	test_receive_whole_case (
		cerver, "a path with a dot is a route before a static file",
		"GET /v2/users/john.doe HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (!strcmp (observed.param, "john.doe"));

	test_receive_whole_case (
		cerver, "the body limit of an endpoint",
		"POST /v2/limited HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 32\r\n\r\n"
		"0123456789abcdef0123456789abcdef"
	);

	test_receive_expect (test_receive_status_is ("413"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (
		cerver, "a websocket endpoint without an upgrade goes to its handler",
		"GET /v2/socket HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);

	test_receive_whole_case (
		cerver, "a websocket endpoint without a handler requires the upgrade",
		"GET /v2/only HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("426"));
	test_receive_expect (strstr (result.response, "Upgrade: websocket\r\n"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (
		cerver, "a route of the first API with the websocket modifier",
		"POST /test/socket HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("501"));
	test_receive_expect (observed.n_handled == 0);

	test_receive_whole_case (
		cerver, "a custom header without case",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nx-custom: lower\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_expect (!strcmp (observed.custom, "lower"));

}

// a path without a handler for the method: catch all by default, 405 when it is enabled
static void test_receive_method_not_allowed (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	const char *request = "DELETE /v2/items/abc HTTP/1.1\r\nHost: localhost:8080\r\n\r\n";

	test_receive_whole_case (cerver, "a method without handler goes to the catch all", request);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

	http_cerver_set_method_not_allowed (http_cerver, true);

	test_receive_whole_case (cerver, "405 with the Allow header", request);
	test_receive_expect (test_receive_status_is ("405"));
	test_receive_expect (test_receive_has_header ("Allow: GET, HEAD, POST, OPTIONS, PATCH\r\n"));
	test_receive_expect (test_receive_has_header ("Method not allowed!"));

	test_receive_whole_case (
		cerver, "an unknown path is still not found",
		"DELETE /v2/missing HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

	http_cerver_set_method_not_allowed_handler (http_cerver, test_receive_method_not_allowed_handler);

	test_receive_whole_case (cerver, "405 with a custom handler", request);
	test_receive_expect (test_receive_status_is ("405"));
	test_receive_expect (test_receive_has_header (custom_not_allowed_body));
	test_receive_expect (!strcmp (observed.query, "GET, HEAD, POST, OPTIONS, PATCH"));

	http_cerver_set_method_not_allowed_handler (http_cerver, NULL);
	http_cerver_set_method_not_allowed (http_cerver, false);

}

// tolerant (the default), strict and redirect
static void test_receive_trailing_slash (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	const char *request = "GET /v2/items/abc/?x=1 HTTP/1.1\r\nHost: localhost:8080\r\n\r\n";

	test_receive_whole_case (cerver, "a tolerated trailing slash", request);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (!strcmp (observed.param, "abc"));

	test_receive_whole_case (
		cerver, "a first API route with a trailing slash",
		"POST /test/echo/ HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_expect (observed.n_handled == 1);

	test_receive_whole_case (
		cerver, "an empty segment never matches",
		"GET /v2//items/abc HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
	);

	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

	http_cerver_set_trailing_slash (http_cerver, HTTP_TRAILING_SLASH_STRICT);

	test_receive_whole_case (cerver, "a strict trailing slash", request);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (strstr (result.response, catch_all_message) != NULL);

	http_cerver_set_trailing_slash (http_cerver, HTTP_TRAILING_SLASH_REDIRECT);

	test_receive_whole_case (cerver, "a redirected trailing slash", request);
	test_receive_expect (test_receive_status_is ("308"));
	test_receive_expect (test_receive_has_header ("Location: /v2/items/abc?x=1\r\n"));
	test_receive_expect (observed.n_handled == 0);

	// an invalid policy is ignored
	http_cerver_set_trailing_slash (http_cerver, HTTP_TRAILING_SLASH_MAX);
	test_receive_expect (http_cerver->tree->trailing_slash == HTTP_TRAILING_SLASH_REDIRECT);

	http_cerver_set_trailing_slash (http_cerver, HTTP_TRAILING_SLASH_TOLERANT);

}

// a preflight from a whitelisted origin gets the CORS headers
static void test_receive_cors_preflight (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "a preflight from an allowed origin",
		"OPTIONS /v2/cors/resource HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Origin: " TEST_RECEIVE_ALLOWED_ORIGIN "\r\n"
		"Access-Control-Request-Method: GET\r\n"
		"Access-Control-Request-Headers: authorization\r\n"
		"\r\n"
	);

	test_receive_expect (test_receive_status_is ("204"));
	test_receive_expect (test_receive_has_header ("Access-Control-Allow-Origin: " TEST_RECEIVE_ALLOWED_ORIGIN));
	test_receive_expect (test_receive_has_header ("Access-Control-Allow-Methods: GET, HEAD, OPTIONS"));
	test_receive_expect (test_receive_has_header ("Access-Control-Allow-Headers: authorization"));

	test_receive_whole_case (
		cerver, "a preflight from another origin",
		"OPTIONS /v2/cors/resource HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Origin: http://other.com\r\n"
		"Access-Control-Request-Method: GET\r\n"
		"\r\n"
	);

	test_receive_expect (test_receive_status_is ("204"));
	test_receive_expect (!test_receive_has_header ("Access-Control-Allow-Origin"));

	test_receive_whole_case (
		cerver, "a preflight to a route without CORS",
		"OPTIONS /v2/items/abc HTTP/1.1\r\n"
		"Host: localhost:8080\r\n"
		"Origin: " TEST_RECEIVE_ALLOWED_ORIGIN "\r\n"
		"Access-Control-Request-Method: GET\r\n"
		"\r\n"
	);

	test_receive_expect (!test_receive_has_header ("Access-Control-Allow-Origin"));

}

#pragma endregion

#pragma region parser

// a header value bigger than the default limit of the headers
#define TEST_RECEIVE_BIG_HEADER_VALUE		(HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE + (1 << 10))
#define TEST_RECEIVE_BIG_REQUEST_SIZE		(TEST_RECEIVE_BIG_HEADER_VALUE + (1 << 8))

static const char *query_request = {
	"QUERY /v2/search?key=value HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: application/json\r\n"
	"Content-Length: 26\r\n"
	"\r\n"
	"{\"select\": [\"name\", \"id\"]}"
};

static const char *query_body = { "{\"select\": [\"name\", \"id\"]}" };

static void test_receive_check_query (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body, query_body));
	test_receive_expect (!strcmp (observed.query_key, "value"));

}

// a request that only the lenient flag makes valid, it is answered by the echo route
typedef struct TestReceiveStrictCase {

	const char *name;
	const char *request;
	unsigned int lenient;
	const char *body;

} TestReceiveStrictCase;

// llhttp needs both flags to accept a line that ends with LF
#define TEST_RECEIVE_LENIENT_LF				(HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF | HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR)

static const TestReceiveStrictCase strict_cases[] = {
	{
		"LF without CR",
		"POST /test/echo HTTP/1.1\nHost: localhost:8080\nContent-Length: 5\n\nfirst",
		TEST_RECEIVE_LENIENT_LF, "first"
	},
	{
		"obsolete line folding",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nX-Custom: first\r\n second\r\nContent-Length: 5\r\n\r\nfirst",
		HTTP_PARSER_LENIENT_HEADERS, "first"
	},
	{
		"an unknown version",
		"POST /test/echo HTTP/1.5\r\nHost: localhost:8080\r\nContent-Length: 5\r\n\r\nfirst",
		HTTP_PARSER_LENIENT_VERSION, "first"
	},
	{
		"Transfer-Encoding with Content-Length",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n"
		"5\r\nfirst\r\n0\r\n\r\n",
		HTTP_PARSER_LENIENT_CHUNKED_LENGTH, "first"
	},
	{
		"spaces after a chunk size",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nTransfer-Encoding: chunked\r\n\r\n"
		"5  \r\nfirst\r\n0\r\n\r\n",
		HTTP_PARSER_LENIENT_SPACES_AFTER_CHUNK_SIZE, "first"
	},
	{
		"a chunk without CRLF after its data",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nTransfer-Encoding: chunked\r\n\r\n"
		"5\r\nfirst0\r\n\r\n",
		HTTP_PARSER_LENIENT_OPTIONAL_CRLF_AFTER_CHUNK, "first"
	},
	{
		"a control character in a header value",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nX-Custom: a\x01" "b\r\nContent-Length: 5\r\n\r\nfirst",
		HTTP_PARSER_LENIENT_HEADERS, "first"
	}
};

#define TEST_RECEIVE_STRICT_CASES		(sizeof (strict_cases) / sizeof (strict_cases[0]))

static const char nul_value_request[] = {
	"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nX-Custom: a\0b\r\nContent-Length: 5\r\n\r\nfirst"
};

static const char *lf_request = {
	"POST /test/echo?key=value HTTP/1.1\n"
	"Host: localhost:8080\n"
	"Content-Type: application/json\n"
	"X-Custom: custom-value\n"
	"Content-Length: 5\n"
	"\n"
	"first"
};

static void test_receive_check_lf (void) {

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body, "first"));
	test_receive_expect (!strcmp (observed.content_type, "application/json"));
	test_receive_expect (!strcmp (observed.custom, "custom-value"));
	test_receive_expect (!strcmp (observed.query_key, "value"));

}

static void test_receive_parser_rejected (void) {

	test_receive_expect (test_receive_status_is ("400"));
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_PARSER_ERROR);
	test_receive_expect (observed.n_handled == 0);

}

// the methods of llhttp: WebDAV and QUERY can be registered,
// the RTSP methods and the preface of HTTP/2 are rejected by the parser
static void test_receive_parser_methods (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	test_receive_every_split (
		cerver, "QUERY with a body", query_request, strlen (query_request), test_receive_check_query
	);

	test_receive_whole_case (cerver, "a WebDAV method", "PROPFIND /v2/dav HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);

	http_cerver_set_method_not_allowed (http_cerver, true);

	test_receive_whole_case (cerver, "a method that the path does not have", "MKCOL /v2/dav HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	test_receive_expect (test_receive_status_is ("405"));
	test_receive_expect (test_receive_has_header ("Allow: OPTIONS, PROPFIND\r\n"));
	test_receive_expect (observed.n_handled == 0);

	http_cerver_set_method_not_allowed (http_cerver, false);

	test_receive_whole_case (cerver, "an RTSP method", "DESCRIBE /v2/dav HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	test_receive_parser_rejected ();

	test_receive_whole_case (cerver, "the preface of HTTP/2", "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
	test_receive_parser_rejected ();

	test_receive_whole_case (cerver, "a lower case method", "post /test/echo HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	test_receive_parser_rejected ();

}

// each request is rejected in the strict mode and accepted with its lenient flag
static void test_receive_parser_strict (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	for (size_t i = 0; i < TEST_RECEIVE_STRICT_CASES; i++) {
		const TestReceiveStrictCase *strict = &strict_cases[i];

		test_receive_whole_case (cerver, strict->name, strict->request);
		test_receive_parser_rejected ();

		http_cerver_set_parser_lenient (http_cerver, strict->lenient);
		test_check_unsigned_eq (http_cerver->parser_lenient, strict->lenient, NULL);

		test_receive_whole_case (cerver, strict->name, strict->request);
		test_receive_expect (test_receive_status_is ("200"));
		test_receive_expect (observed.n_handled == 1);
		test_receive_expect (!strcmp (observed.body, strict->body));

		http_cerver_set_parser_lenient (http_cerver, HTTP_PARSER_LENIENT_NONE);
	}

	// the lenient flags are applied to the parser of every request, also in pieces
	http_cerver_set_parser_lenient (http_cerver, TEST_RECEIVE_LENIENT_LF);
	test_receive_every_split (cerver, "lenient LF without CR", lf_request, strlen (lf_request), test_receive_check_lf);

	// a value outside the flags is ignored, also the header value relaxed of llhttp (0x400),
	// that loops forever with a NUL in a header value
	http_cerver_set_parser_lenient (http_cerver, ~0u);
	test_check_unsigned_eq (http_cerver->parser_lenient, HTTP_PARSER_LENIENT_ALL, NULL);
	test_check_false (http_cerver->parser_lenient & LLHTTP_LENIENT_HEADER_VALUE_RELAXED);

	// the headers lenient flag accepts it, without looping
	current_case = "a NUL in a header value with every flag";
	test_receive_feed_whole (cerver, nul_value_request, sizeof (nul_value_request) - 1);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);

	http_cerver_set_parser_lenient (http_cerver, HTTP_PARSER_LENIENT_NONE);

	current_case = "a NUL in a header value";
	test_receive_feed_whole (cerver, nul_value_request, sizeof (nul_value_request) - 1);
	test_receive_parser_rejected ();

	// some requests are rejected even with every flag
	test_receive_whole_case (cerver, "a space before a colon", "POST /test/echo HTTP/1.1\r\nHost : localhost:8080\r\n\r\n");
	test_receive_parser_rejected ();

	test_receive_whole_case (
		cerver, "two Content-Length",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nfirst"
	);

	test_receive_parser_rejected ();

}

// the request of RFC 6455 section 1.3, with its accept
#define TEST_RECEIVE_WEB_SOCKET_REQUEST(headers)										\
	"GET /v2/socket HTTP/1.1\r\n"														\
	"Host: localhost:8080\r\n"															\
	"Upgrade: websocket\r\n"															\
	"Connection: Upgrade\r\n"															\
	"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"									\
	headers																				\
	"\r\n"

#define TEST_RECEIVE_WEB_SOCKET_VERSION			"Sec-WebSocket-Version: 13\r\n"

static const char *test_receive_web_socket_accept = { "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n" };

// a masked text frame "Hello" (RFC 6455 section 5.7)
static const char test_receive_web_socket_frame[] = {
	(char) 0x81, (char) 0x85, 0x37, (char) 0xfa, 0x21, 0x3d, 0x7f, (char) 0x9f, 0x4d, 0x51, 0x58
};

#define TEST_RECEIVE_WEB_SOCKET_FRAME_SIZE		sizeof (test_receive_web_socket_frame)

// the upgrade was answered with the 101 of RFC 6455, and its place was given back with the receive
static void test_receive_web_socket_switched (void) {

	test_receive_expect (test_receive_status_is ("101"));
	test_receive_expect (result.upgraded);
	test_receive_expect (strstr (result.response, test_receive_web_socket_accept));
	test_receive_expect (strstr (result.response, "Upgrade: websocket\r\n"));
	test_receive_expect (strstr (result.response, "Connection: Upgrade\r\n"));
	test_receive_expect (!strstr (result.response, "Content-Length"));
	test_receive_expect (!strstr (result.response, "close"));
	test_receive_expect (observed.n_handled == 0);

}

// a handshake that was not completed by its connection gives its place back
static void test_receive_web_socket_released (Cerver *cerver) {

	const HttpCerver *http_cerver = (const HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (http_cerver->web_sockets->reserved, 0, NULL);
	test_check_unsigned_eq (http_cerver->web_sockets->count, 0, NULL);

}

static void test_receive_web_socket_upgrade_rejected (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "an upgrade without a key",
		"GET /v2/socket HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" TEST_RECEIVE_WEB_SOCKET_VERSION "\r\n"
	);

	test_receive_expect (test_receive_status_is ("400"));
	test_receive_expect (!result.upgraded);

	test_receive_whole_case (cerver, "an upgrade without a version", TEST_RECEIVE_WEB_SOCKET_REQUEST (""));
	test_receive_expect (test_receive_status_is ("426"));
	test_receive_expect (strstr (result.response, "Sec-WebSocket-Version: 13\r\n"));

	test_receive_whole_case (
		cerver, "an upgrade with another version",
		TEST_RECEIVE_WEB_SOCKET_REQUEST ("Sec-WebSocket-Version: 8\r\n")
	);

	test_receive_expect (test_receive_status_is ("426"));

	test_receive_whole_case (
		cerver, "an upgrade with a short key",
		"GET /v2/socket HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: c2hvcnQ=\r\n" TEST_RECEIVE_WEB_SOCKET_VERSION "\r\n"
	);

	test_receive_expect (test_receive_status_is ("400"));

	test_receive_whole_case (
		cerver, "an upgrade with two keys",
		TEST_RECEIVE_WEB_SOCKET_REQUEST ("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" TEST_RECEIVE_WEB_SOCKET_VERSION)
	);

	test_receive_expect (test_receive_status_is ("400"));

	test_receive_whole_case (
		cerver, "an upgrade with a body",
		TEST_RECEIVE_WEB_SOCKET_REQUEST (TEST_RECEIVE_WEB_SOCKET_VERSION "Content-Length: 5\r\n") "first"
	);

	test_receive_expect (test_receive_status_is ("400"));

	test_receive_whole_case (
		cerver, "an upgrade with HTTP/1.0",
		"GET /v2/socket HTTP/1.0\r\nHost: localhost:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" TEST_RECEIVE_WEB_SOCKET_VERSION "\r\n"
	);

	test_receive_expect (test_receive_status_is ("400"));

	test_receive_whole_case (
		cerver, "an upgrade with POST",
		"POST /v2/socket HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" TEST_RECEIVE_WEB_SOCKET_VERSION "Content-Length: 0\r\n\r\n"
	);

	// the endpoint is a GET, a POST goes to the catch all (405 is opt-in)
	test_receive_expect (!test_receive_status_is ("101"));
	test_receive_expect (!result.upgraded);

	test_receive_web_socket_released (cerver);

}

// the length of the request before its first frame
static size_t test_receive_web_socket_header_len = 0;

// the bytes of the frame that came in the piece that completed the request,
// the pieces after the 101 are read by the thread of the connection, never by the parser
static size_t test_receive_web_socket_expected_pending (void) {

	size_t expected = TEST_RECEIVE_WEB_SOCKET_FRAME_SIZE;

	if (!strcmp (current_mode, "one byte per recv")) {
		expected = 0;
	}

	else if (!strcmp (current_mode, "split at") && (current_offset >= test_receive_web_socket_header_len)) {
		expected = current_offset - test_receive_web_socket_header_len;
	}

	return expected;

}

static void test_receive_web_socket_switched_split (void) {

	test_receive_web_socket_switched ();
	test_receive_expect (result.pending_size == test_receive_web_socket_expected_pending ());

}

// the upgrade of RFC 6455, with the tokens of Firefox, and with the first frame in the same packet
static void test_receive_web_socket_upgrade (Cerver *cerver) {

	test_receive_web_socket_upgrade_rejected (cerver);

	test_receive_whole_case (
		cerver, "an upgrade to a websocket endpoint",
		TEST_RECEIVE_WEB_SOCKET_REQUEST (TEST_RECEIVE_WEB_SOCKET_VERSION)
	);

	test_receive_web_socket_switched ();
	test_receive_expect (result.pending_size == 0);

	test_receive_whole_case (
		cerver, "an upgrade with the tokens in lists",
		"GET /v2/socket HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" TEST_RECEIVE_WEB_SOCKET_VERSION "\r\n"
	);

	test_receive_web_socket_switched ();

	char request[TEST_RECEIVE_RESPONSE_SIZE];
	const size_t header_len = strlen (TEST_RECEIVE_WEB_SOCKET_REQUEST (TEST_RECEIVE_WEB_SOCKET_VERSION));
	test_receive_web_socket_header_len = header_len;
	(void) memcpy (request, TEST_RECEIVE_WEB_SOCKET_REQUEST (TEST_RECEIVE_WEB_SOCKET_VERSION), header_len);
	(void) memcpy (request + header_len, test_receive_web_socket_frame, TEST_RECEIVE_WEB_SOCKET_FRAME_SIZE);

	test_receive_every_split (
		cerver, "an upgrade with its first frame",
		request, header_len + TEST_RECEIVE_WEB_SOCKET_FRAME_SIZE,
		test_receive_web_socket_switched_split
	);

	test_receive_web_socket_released (cerver);

}

// an Upgrade or a CONNECT is answered and closed without waiting for another protocol
static void test_receive_parser_upgrade (Cerver *cerver) {

	test_receive_whole_case (
		cerver, "an upgrade to a route without websocket",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: h2c\r\nConnection: Upgrade\r\nContent-Length: 5\r\n\r\nfirst"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (!strcmp (observed.body, "first"));

	test_receive_web_socket_upgrade (cerver);

	test_receive_whole_case (cerver, "a CONNECT", "CONNECT localhost:443 HTTP/1.1\r\nHost: localhost:443\r\n\r\n");
	test_receive_expect (test_receive_count_responses () == 1);
	test_receive_expect (result.status != HTTP_RECEIVE_STATUS_NONE);

}

// what the client asked is kept for the keep-alive, the connection is still closed
static void test_receive_parser_keep_alive (Cerver *cerver) {

	test_receive_whole_case (cerver, "HTTP/1.1", "POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n");
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (result.keep_alive_requested);
	test_receive_expect (result.unparsed == 0);

	test_receive_whole_case (
		cerver, "HTTP/1.1 with Connection close",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nConnection: close\r\nContent-Length: 5\r\n\r\nfirst"
		PIPELINED_SECOND_REQUEST
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (!result.keep_alive_requested);
	test_receive_expect (result.unparsed == strlen (pipelined_second_request));
	test_receive_expect (test_receive_count_responses () == 1);

	test_receive_whole_case (cerver, "HTTP/1.0", "POST /test/echo HTTP/1.0\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n");
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (!result.keep_alive_requested);

	test_receive_whole_case (
		cerver, "HTTP/1.0 with keep-alive",
		"POST /test/echo HTTP/1.0\r\nHost: localhost:8080\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (result.keep_alive_requested);

}

// llhttp does not limit the headers (http_parser stopped at 80 KiB),
// the limit of the cerver is always active, 0 sets its default value
static void test_receive_parser_header_limit (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_max_header_size (http_cerver, 0);
	test_check_unsigned_eq (http_cerver->max_header_size, HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE, NULL);

	http_cerver_set_max_url_size (http_cerver, 0);
	test_check_unsigned_eq (http_cerver->max_url_size, HTTP_CERVER_DEFAULT_MAX_URL_SIZE, NULL);

	char *request = (char *) malloc (TEST_RECEIVE_BIG_REQUEST_SIZE);
	test_check_ptr (request);

	size_t length = (size_t) snprintf (
		request, TEST_RECEIVE_BIG_REQUEST_SIZE, "POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nX-Big: "
	);

	(void) memset (request + length, 'a', TEST_RECEIVE_BIG_HEADER_VALUE);
	length += TEST_RECEIVE_BIG_HEADER_VALUE;
	(void) snprintf (request + length, TEST_RECEIVE_BIG_REQUEST_SIZE - length, "\r\nContent-Length: 0\r\n\r\n");

	test_receive_limit_case (cerver, "headers bigger than the default limit", request, "431");

	free (request);

}

#pragma endregion

#pragma region framed

static void test_receive_framed_case (
	Cerver *cerver, const char *name, const char *request,
	const char *status, const bool framed
) {

	test_receive_whole_case (cerver, name, request);

	test_receive_expect (test_receive_status_is (status));
	test_receive_expect (result.framed == framed);

}

// the client finds the end of a response without the close of the connection
// when it has its length, when its status has no body, when it answers a HEAD
// or when its stream sent its last chunk: a connection can only be kept after one of those
static void test_receive_framed (Cerver *cerver) {

	test_receive_framed_case (
		cerver, "framed: a compiled response of cerver",
		"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n", "200", true
	);

	test_receive_framed_case (
		cerver, "framed: a rendered body",
		"GET /v2/render HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "200", true
	);

	test_receive_framed_case (
		cerver, "framed: a json body sent from its header and its data",
		"PATCH /v2/items/16 HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 0\r\n\r\n", "200", true
	);

	test_receive_framed_case (
		cerver, "framed: a static file",
		"GET /index.html HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "200", true
	);

	test_receive_framed_case (
		cerver, "framed: the answer to a preflight has no body",
		"OPTIONS /v2/cors/resource HTTP/1.1\r\nHost: localhost:8080\r\n"
		"Origin: " TEST_RECEIVE_ALLOWED_ORIGIN "\r\nAccess-Control-Request-Method: GET\r\n\r\n",
		"204", true
	);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_trailing_slash (http_cerver, HTTP_TRAILING_SLASH_REDIRECT);

	test_receive_framed_case (
		cerver, "framed: a redirect with an empty body",
		"GET /v2/render/ HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "308", true
	);

	http_cerver_set_trailing_slash (http_cerver, HTTP_TRAILING_SLASH_TOLERANT);

	test_receive_framed_case (
		cerver, "framed: the response that cerver sends for a handler that did not answer",
		"GET /v2/framed/silent HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "500", true
	);

	test_receive_framed_case (
		cerver, "framed: a request that the parser rejects",
		"GET /v2/render HTTP/1.1\r\nBad Header\r\n\r\n", "400", true
	);

	test_receive_expect (result.status != HTTP_RECEIVE_STATUS_COMPLETED);

	test_receive_framed_case (
		cerver, "framed: a complete header that was set as bytes, with its length",
		"GET /v2/framed/bytes HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "200", true
	);

	test_receive_expect (strstr (result.response, framed_body) != NULL);

	// a stream whose handler did not end it is ended by cerver with its last chunk
	test_receive_framed_case (
		cerver, "framed: a chunked stream",
		"GET /v2/framed/stream HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "200", true
	);

	test_receive_expect (strstr (result.response, "\r\n0\r\n\r\n") != NULL);

	// the answer to a HEAD has no body, whatever its GET sends
	test_receive_framed_case (
		cerver, "framed: a HEAD of a body without its length",
		"HEAD /v2/framed/created HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "201", true
	);

	test_receive_expect (strstr (result.response, framed_body) == NULL);

	// the ones that only end when the connection is closed
	test_receive_framed_case (
		cerver, "not framed: a body without its length",
		"GET /v2/framed/created HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "201", false
	);

	test_receive_expect (!test_receive_has_header ("Content-Length"));
	test_receive_expect (strstr (result.response, framed_body) != NULL);

	test_receive_framed_case (
		cerver, "not framed: a response without a body and without its length",
		"GET /v2/framed/empty HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "200", false
	);

	test_receive_framed_case (
		cerver, "not framed: a stream to an HTTP/1.0 client",
		"GET /v2/framed/stream HTTP/1.0\r\n\r\n", "200", false
	);

	test_receive_expect (!test_receive_has_header ("Transfer-Encoding"));

	test_receive_framed_case (
		cerver, "not framed: a header that cerver can not complete",
		"GET /v2/framed/raw HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", "200", false
	);

	// an upgrade is not a response that a request can follow
	test_receive_whole_case (
		cerver, "not framed: an upgrade",
		"GET /v2/socket HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"
	);

	test_receive_expect (test_receive_status_is ("101"));
	test_receive_expect (result.upgraded);
	test_receive_expect (!result.framed);

}

#define TEST_RECEIVE_PREFLIGHT_REQUEST_SIZE		(1 << 15)
#define TEST_RECEIVE_PREFLIGHT_LONG_HEADERS		300
#define TEST_RECEIVE_PREFLIGHT_HUGE_HEADERS		(1 << 13)

static void test_receive_preflight_headers_case (
	Cerver *cerver, const char *name, const size_t headers_length
) {

	char *headers = (char *) malloc (headers_length + 1);
	char *request = (char *) malloc (TEST_RECEIVE_PREFLIGHT_REQUEST_SIZE);
	test_check_ptr (headers);
	test_check_ptr (request);

	(void) memset (headers, 'h', headers_length);
	headers[headers_length] = '\0';

	const int request_length = snprintf (
		request, TEST_RECEIVE_PREFLIGHT_REQUEST_SIZE,
		"OPTIONS /v2/cors/resource HTTP/1.1\r\nHost: localhost:8080\r\n"
		"Origin: " TEST_RECEIVE_ALLOWED_ORIGIN "\r\nAccess-Control-Request-Method: GET\r\n"
		"Access-Control-Request-Headers: %s\r\n\r\n",
		headers
	);

	test_check ((request_length > 0) && (request_length < TEST_RECEIVE_PREFLIGHT_REQUEST_SIZE), NULL);

	current_case = name;
	test_receive_feed_whole (cerver, request, (size_t) request_length);

	test_receive_expect (test_receive_status_is ("204"));

	// the whole value is sent back in its header, and nothing else:
	// the response is text until its end, and its header ends with its empty line
	test_receive_expect (result.response_len == strlen (result.response));

	const char *allowed = strstr (result.response, "\r\nAccess-Control-Allow-Headers: ");
	test_receive_expect (allowed != NULL);

	allowed += strlen ("\r\nAccess-Control-Allow-Headers: ");
	test_receive_expect (!strncmp (allowed, headers, headers_length));
	test_receive_expect (!strncmp (allowed + headers_length, "\r\n", strlen ("\r\n")));

	test_receive_expect (
		!strcmp (result.response + result.response_len - strlen ("\r\n\r\n"), "\r\n\r\n")
	);

	free (request);
	free (headers);

}

// the headers that a preflight asks for are sent back complete, whatever their length:
// a header of a response kept 256 bytes and its length was the one of the whole value,
// so the bytes after it were sent to the client
static void test_receive_preflight_long_headers (Cerver *cerver) {

	test_receive_preflight_headers_case (
		cerver, "a preflight that asks for headers longer than 256 bytes",
		TEST_RECEIVE_PREFLIGHT_LONG_HEADERS
	);

	test_receive_preflight_headers_case (
		cerver, "a preflight that asks for headers that are not kept inside a response",
		TEST_RECEIVE_PREFLIGHT_HUGE_HEADERS
	);

}

#pragma endregion

#pragma region reset

// a connection that handles more than one request with the same receive
typedef struct TestReceiveSession {

	int fds[2];

	Connection *connection;
	CerverReceive cerver_receive;

	HttpReceive *http_receive;

	// the callback of the body of a request that has not started
	llhttp_data_cb on_body;

	// what a request that has not started uses of the arena that was lent
	size_t arena_used;
	size_t arena_allocations;

} TestReceiveSession;

static void test_receive_session_start (TestReceiveSession *session, Cerver *cerver) {

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, session->fds), 0, NULL);

	session->connection = connection_create_empty ();
	test_check_ptr (session->connection);
	session->connection->socket->sock_fd = session->fds[TEST_RECEIVE_SERVER];
	session->connection->active = true;

	(void) memset (&session->cerver_receive, 0, sizeof (CerverReceive));
	session->cerver_receive.cerver = cerver;
	session->cerver_receive.socket = session->connection->socket;
	session->cerver_receive.connection = session->connection;

	session->http_receive = test_receive_create (&session->cerver_receive);

	session->on_body = session->http_receive->settings.on_body;

	session->arena_used = receive_arena ? receive_arena->used : 0;
	session->arena_allocations = receive_arena ? receive_arena->n_allocations : 0;

	// the connection is not closed after its response
	session->http_receive->keep_alive = true;

}

// reads the response that is already in the socket, the connection is still open
static void test_receive_session_read (const TestReceiveSession *session) {

	ssize_t received = 0;
	while (
		(result.response_len < (TEST_RECEIVE_RESPONSE_SIZE - 1))
		&& ((received = recv (
			session->fds[TEST_RECEIVE_CLIENT],
			result.response + result.response_len,
			TEST_RECEIVE_RESPONSE_SIZE - 1 - result.response_len,
			MSG_DONTWAIT
		)) > 0)
	) {
		result.response_len += (size_t) received;
	}

	result.response[result.response_len] = '\0';

}

// feeds a request of the connection in two pieces and keeps what it left
static void test_receive_session_request (
	TestReceiveSession *session,
	const char *request, const size_t request_len, const size_t cut
) {

	HttpReceive *http_receive = session->http_receive;

	(void) memset (&observed, 0, sizeof (TestReceiveObserved));
	(void) memset (&result, 0, sizeof (TestReceiveResult));

	n_fed_requests += 1;

	if (cut) test_receive_feed_piece (http_receive, request, cut);
	if (request_len > cut) test_receive_feed_piece (http_receive, request + cut, request_len - cut);

	result.status = http_receive->receive_status;
	result.keep_alive_requested = http_receive->keep_alive_requested;
	result.unparsed = http_receive->unparsed;
	result.framed = http_receive->framed;

	test_receive_session_read (session);

}

static void test_receive_session_end (TestReceiveSession *session) {

	http_receive_delete (session->http_receive);
	if (receive_arena) arena_reset (receive_arena);

	connection_delete (session->connection);

	(void) close (session->fds[TEST_RECEIVE_CLIENT]);

}

// what belongs to a request starts again, and what belongs to the connection is kept
static void test_receive_reset_check (
	const TestReceiveSession *session, const HttpReceive *before
) {

	const HttpReceive *http_receive = session->http_receive;

	test_receive_expect (http_receive->receive_status == HTTP_RECEIVE_STATUS_NONE);
	test_receive_expect (http_receive->stage == HTTP_STAGE_QUEUED);
	test_receive_expect (!http_receive->keep_alive_requested);
	test_receive_expect (http_receive->unparsed == 0);
	test_receive_expect (http_receive->mpart_parser == NULL);
	test_receive_expect (http_receive->type == HTTP_RECEIVE_TYPE_NONE);
	test_receive_expect (http_receive->dispatch == HTTP_RECEIVE_DISPATCH_NONE);
	test_receive_expect (http_receive->node == NULL);
	test_receive_expect (http_receive->endpoint == NULL);
	test_receive_expect (http_receive->route == NULL);
	test_receive_expect (http_receive->served_file == NULL);
	test_receive_expect (!http_receive->head);
	test_receive_expect (http_receive->max_body_size == 0);
	test_receive_expect (http_receive->status == HTTP_STATUS_NONE);
	test_receive_expect (http_receive->sent == 0);
	test_receive_expect (!http_receive->is_multi_part);
	test_receive_expect (http_receive->upload_sequence == 0);
	test_receive_expect (!http_receive->upload_directory_created);
	test_receive_expect (http_receive->header_size == 0);
	test_receive_expect (http_receive->n_headers == 0);
	test_receive_expect (http_receive->body_size == 0);
	test_receive_expect (http_receive->n_parts == 0);
	test_receive_expect (!http_receive->responded);
	test_receive_expect (!http_receive->framed);
	test_receive_expect (http_receive->stream.state == HTTP_STREAM_STATE_NONE);
	test_receive_expect (http_receive->received == 0);
	test_receive_expect (http_receive->diagnostics.n_recvs == 0);
	test_receive_expect (!http_receive->diagnostics.timed_out);
	test_receive_expect (!http_receive->response_failed);
	test_receive_expect (!http_receive->send_deadline_set);
	test_receive_expect (http_receive->web_socket == NULL);
	test_receive_expect (http_receive->start_time > 0);

	// the parser is ready for a new request with the callbacks of a new one
	test_receive_expect (http_receive->parser.error == HPE_OK);
	test_receive_expect (http_receive->parser.data == http_receive);
	test_receive_expect (http_receive->parser.settings == &http_receive->settings);
	test_receive_expect (http_receive->settings.on_body == session->on_body);

	// the request is a new one, without what the one before received
	const HttpRequest *request = http_receive->request;
	test_receive_expect (request != NULL);
	test_receive_expect (request->url == NULL);
	test_receive_expect (request->query == NULL);
	test_receive_expect (request->body == NULL);
	test_receive_expect (request->multi_parts == NULL);
	test_receive_expect (request->custom_data == NULL);
	test_receive_expect (request->decoded_data == NULL);
	test_receive_expect (request->n_params == 0);
	for (unsigned int idx = 0; idx < HTTP_HEADERS_SIZE; idx++) {
		test_receive_expect (request->headers[idx] == NULL);
	}

	test_receive_expect (http_receive->file_stats != NULL);

	// the arena only has the new request, the data of the one that ended was released
	if (receive_arena) {
		test_receive_expect (http_receive->arena == receive_arena);
		test_receive_expect (receive_arena->used == session->arena_used);
		test_receive_expect (receive_arena->n_allocations == session->arena_allocations);
	}

	// the connection
	test_receive_expect (http_receive->cr == before->cr);
	test_receive_expect (http_receive->http_cerver == before->http_cerver);
	test_receive_expect (http_receive->handler == before->handler);
	test_receive_expect (http_receive->keep_alive == before->keep_alive);
	test_receive_expect (http_receive->arena == before->arena);
	test_receive_expect (http_receive->borrowed_buffers == before->borrowed_buffers);
	test_receive_expect (http_receive->sock_fd == before->sock_fd);
	test_receive_expect (http_receive->client_state == before->client_state);
	test_receive_expect (http_receive->upgraded == before->upgraded);

	if (before->borrowed_buffers) {
		test_receive_expect (http_receive->url_buffer == before->url_buffer);
		test_receive_expect (http_receive->field_buffer == before->field_buffer);
		test_receive_expect (http_receive->value_buffer == before->value_buffer);
	}

}

static void test_receive_session_reset (TestReceiveSession *session) {

	const HttpReceive before = *session->http_receive;

	test_check_unsigned_eq (http_receive_reset (session->http_receive), 0, NULL);

	test_receive_reset_check (session, &before);

}

static const char *reset_param_request = {
	"POST /test/items/first-item/update?stale=query HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Type: text/plain\r\n"
	"Accept: text/stale\r\n"
	"X-Custom: stale-custom\r\n"
	"Content-Length: 10\r\n"
	"\r\n"
	"first-body"
};

static const char *reset_data_request = {
	"POST /v2/data HTTP/1.1\r\n"
	"Host: localhost:8080\r\n"
	"Content-Length: 4\r\n"
	"\r\n"
	"data"
};

static const char *reset_not_found_request = {
	"GET /v2/items HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"
};

static const char *reset_bad_request = {
	"GET /v2/render HTTP/1.1\r\nBad Header\r\n\r\n"
};

static const char *reset_incomplete_request = {
	"POST /test/echo HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 64\r\n\r\npartial"
};

// a connection that is kept does not say that it is closed
static void test_receive_reset_check_kept (void) {

	test_receive_expect (!test_receive_has_header ("Connection"));
	test_receive_expect (test_receive_count_responses () == 1);

}

// the echo request after any other request of its connection:
// it only has what it sent, and its response is the only one in the socket
static void test_receive_reset_echo (TestReceiveSession *session, const size_t cut) {

	test_receive_session_reset (session);

	test_receive_session_request (session, echo_json_request, strlen (echo_json_request), cut);

	test_receive_check_echo_json ();
	test_receive_expect (result.status == HTTP_RECEIVE_STATUS_COMPLETED);
	test_receive_expect (result.framed);
	test_receive_expect (observed.accept[0] == '\0');
	test_receive_expect (observed.param[0] == '\0');
	test_receive_expect (observed.n_files == 0);

	test_receive_reset_check_kept ();

}

// the second request of a connection, cut at every byte
static void test_receive_reset_every_split (Cerver *cerver) {

	const size_t request_len = strlen (echo_json_request);

	current_case = "reset: a second request cut at every byte";
	current_mode = "split";

	for (size_t cut = 0; cut <= request_len; cut++) {
		current_offset = cut;

		TestReceiveSession session;
		test_receive_session_start (&session, cerver);

		test_receive_session_request (&session, reset_param_request, strlen (reset_param_request), 0);

		test_receive_expect (test_receive_status_is ("200"));
		test_receive_expect (!strcmp (observed.param, "first-item"));
		test_receive_expect (observed.body_len == strlen ("first-body"));
		test_receive_reset_check_kept ();

		test_receive_reset_echo (&session, cut);

		test_receive_session_end (&session);
	}

	current_mode = "whole";
	current_offset = 0;

}

// many requests of every kind with the same receive
static void test_receive_reset_many (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	current_case = "reset: many requests in a connection";

	observed_custom_deleted = 0;
	observed_decoded_deleted = 0;

	TestReceiveSession session;
	test_receive_session_start (&session, cerver);

	// the data that a request keeps is deleted when it ends, once
	test_receive_session_request (&session, reset_data_request, strlen (reset_data_request), 0);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);
	test_receive_expect (observed_custom_deleted == 0);
	test_receive_expect (observed_decoded_deleted == 0);

	test_receive_session_reset (&session);
	test_receive_expect (observed_custom_deleted == 1);
	test_receive_expect (observed_decoded_deleted == 1);

	test_receive_session_request (&session, reset_data_request, strlen (reset_data_request), 0);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_handled == 1);

	test_receive_reset_echo (&session, 0);
	test_receive_expect (observed_custom_deleted == 2);
	test_receive_expect (observed_decoded_deleted == 2);

	// the uploads of each request have their own directory, and none is left
	http_cerver_set_default_uploads_dirname_generator (http_cerver);

	char first[TEST_RECEIVE_PATH_SIZE] = { 0 };
	char second[TEST_RECEIVE_PATH_SIZE] = { 0 };

	test_receive_session_reset (&session);
	test_receive_session_request (&session, multipart_request, strlen (multipart_request), 0);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_parts == 3);
	test_receive_expect (observed.n_files == 2);
	test_receive_expect (!strcmp (observed.files[0].content, "first-file-content"));
	test_receive_copy_c_string (first, observed.files[0].saved_filename);
	test_receive_reset_check_kept ();

	test_receive_session_reset (&session);
	test_receive_expect (test_receive_uploads_count () == 0);

	test_receive_session_request (&session, multipart_request, strlen (multipart_request), 0);
	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (observed.n_parts == 3);
	test_receive_expect (observed.n_files == 2);
	test_receive_expect (!strcmp (observed.files[1].content, "second-file-content"));
	test_receive_copy_c_string (second, observed.files[0].saved_filename);

	test_receive_expect (first[0] != '\0');
	test_receive_expect (strcmp (first, second) != 0);

	// a body after an upload is not handled as its parts
	test_receive_reset_echo (&session, 0);
	test_receive_expect (test_receive_uploads_count () == 0);
	test_receive_expect (observed.n_parts == 0);

	http_cerver_set_uploads_dirname_generator (http_cerver, NULL);

	// a request without a body after one with it, and a stream
	test_receive_session_reset (&session);
	test_receive_session_request (
		&session, "GET /v2/render HTTP/1.1\r\nHost: localhost:8080\r\n\r\n",
		strlen ("GET /v2/render HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"), 0
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (strstr (result.response, render_json_body) != NULL);
	test_receive_expect (!observed.has_body);
	test_receive_reset_check_kept ();

	test_receive_session_reset (&session);
	test_receive_session_request (
		&session, "GET /v2/framed/stream HTTP/1.1\r\nHost: localhost:8080\r\n\r\n",
		strlen ("GET /v2/framed/stream HTTP/1.1\r\nHost: localhost:8080\r\n\r\n"), 0
	);

	test_receive_expect (test_receive_status_is ("200"));
	test_receive_expect (result.framed);
	test_receive_expect (strstr (result.response, "\r\n0\r\n\r\n") != NULL);

	test_receive_reset_echo (&session, 0);

	// a request that the parser rejected and one that did not match
	test_receive_session_reset (&session);
	test_receive_session_request (&session, reset_bad_request, strlen (reset_bad_request), 0);
	test_receive_expect (test_receive_status_is ("400"));
	test_receive_expect (result.status != HTTP_RECEIVE_STATUS_COMPLETED);

	test_receive_reset_echo (&session, 0);

	test_receive_session_reset (&session);
	test_receive_session_request (&session, reset_not_found_request, strlen (reset_not_found_request), 0);
	test_receive_expect (observed.n_handled == 0);

	test_receive_reset_echo (&session, 0);

	// a request that was left in the middle of its body
	test_receive_session_reset (&session);
	test_receive_session_request (&session, reset_incomplete_request, strlen (reset_incomplete_request), 0);
	test_receive_expect (observed.n_handled == 0);
	test_receive_expect (result.response_len == 0);
	test_receive_expect (session.http_receive->body_size == strlen ("partial"));

	test_receive_reset_echo (&session, 0);

	// the last request also deletes its data, with the receive
	test_receive_session_reset (&session);
	test_receive_session_request (&session, reset_data_request, strlen (reset_data_request), 0);
	test_receive_expect (test_receive_status_is ("200"));

	test_receive_session_end (&session);

	test_receive_expect (observed_custom_deleted == 3);
	test_receive_expect (observed_decoded_deleted == 3);
	test_receive_expect (test_receive_uploads_count () == 0);

}

// the counters of a cerver that the end of a request can change
typedef struct TestReceiveCounters {

	size_t n_unhandled;
	size_t n_incompleted;
	size_t n_aborted;
	size_t n_vanished;
	size_t n_failed_responses;

} TestReceiveCounters;

static void test_receive_counters_take (
	const HttpCerver *http_cerver, TestReceiveCounters *counters
) {

	counters->n_unhandled = http_cerver->n_unhandled_requests;
	counters->n_incompleted = http_cerver->n_incompleted_requests;
	counters->n_failed_responses = http_cerver->n_failed_responses;

	counters->n_aborted = 0;
	counters->n_vanished = 0;
	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		counters->n_vanished += http_cerver->n_vanished_requests[stage];

		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			counters->n_aborted += http_cerver->n_aborted_requests[stage][reason];
		}
	}

}

// the request of the session is logged and counted, and only the expected counters change
static void test_receive_access_check (
	const TestReceiveSession *session, const TestReceiveCounters *expected_changes
) {

	const HttpCerver *http_cerver = session->http_receive->http_cerver;

	TestReceiveCounters before;
	test_receive_counters_take (http_cerver, &before);

	http_access_request (session->http_receive);

	TestReceiveCounters after;
	test_receive_counters_take (http_cerver, &after);

	test_receive_expect (after.n_unhandled == before.n_unhandled + expected_changes->n_unhandled);
	test_receive_expect (after.n_incompleted == before.n_incompleted + expected_changes->n_incompleted);
	test_receive_expect (after.n_aborted == before.n_aborted + expected_changes->n_aborted);
	test_receive_expect (after.n_vanished == before.n_vanished + expected_changes->n_vanished);
	test_receive_expect (after.n_failed_responses == before.n_failed_responses + expected_changes->n_failed_responses);

}

// feeds a request in a new connection that is left open, so its end can be accounted
static void test_receive_access_feed (
	TestReceiveSession *session, Cerver *cerver, const char *request
) {

	test_receive_session_start (session, cerver);
	test_receive_session_request (session, request, strlen (request), 0);

	// the bytes that the connection received for the request
	session->http_receive->received = strlen (request);

}

// every request is logged and counted once when it ends, by what happened to it:
// the stats of its endpoint, or the counter of the requests that had no route,
// that could not be parsed, whose client left or that stopped arriving
static void test_receive_access (Cerver *cerver) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	TestReceiveSession session;
	TestReceiveCounters changes;

	// a request that its endpoint answered is counted in the stats of the endpoint
	current_case = "access: a request answered by its endpoint";
	test_receive_access_feed (&session, cerver, reset_data_request);
	test_receive_expect (session.http_receive->receive_status == HTTP_RECEIVE_STATUS_COMPLETED);

	const HttpEndpoint *endpoint = session.http_receive->endpoint;
	test_receive_expect (endpoint != NULL);

	HttpRouteStats stats_before;
	http_route_stats_snapshot (endpoint->stats, &stats_before);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	test_receive_access_check (&session, &changes);

	HttpRouteStats stats_after;
	http_route_stats_snapshot (endpoint->stats, &stats_after);
	test_receive_expect (stats_after.n_requests == stats_before.n_requests + 1);
	test_receive_expect (stats_after.sum_request_sizes == stats_before.sum_request_sizes + strlen (reset_data_request));
	test_receive_expect (stats_after.sum_response_sizes == stats_before.sum_response_sizes + session.http_receive->sent);
	test_receive_expect (session.http_receive->sent == result.response_len);

	// each request of a connection is counted with its own bytes and its own time
	test_receive_session_reset (&session);
	test_receive_session_request (&session, echo_json_request, strlen (echo_json_request), 0);
	session.http_receive->received = strlen (echo_json_request);
	test_receive_expect (session.http_receive->endpoint != endpoint);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	test_receive_access_check (&session, &changes);

	http_route_stats_snapshot (endpoint->stats, &stats_after);
	test_receive_expect (stats_after.n_requests == stats_before.n_requests + 1);

	test_receive_session_end (&session);

	// a file is only logged
	current_case = "access: a static file";
	test_receive_access_feed (&session, cerver, "GET /index.html HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	test_receive_expect (session.http_receive->type == HTTP_RECEIVE_TYPE_FILE);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	test_receive_access_check (&session, &changes);
	test_receive_session_end (&session);

	// a request without a route
	current_case = "access: a request without a route";
	test_receive_access_feed (&session, cerver, reset_not_found_request);
	test_receive_expect (session.http_receive->receive_status == HTTP_RECEIVE_STATUS_UNHANDLED);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	changes.n_unhandled = 1;
	test_receive_access_check (&session, &changes);
	test_receive_session_end (&session);

	// a request that was answered before its body is only logged
	current_case = "access: a request that was rejected";
	test_receive_access_feed (
		&session, cerver,
		"POST /test/private HTTP/1.1\r\nHost: localhost:8080\r\nAuthorization: Bearer wrong\r\nContent-Length: 0\r\n\r\n"
	);

	test_receive_expect (session.http_receive->receive_status == HTTP_RECEIVE_STATUS_REJECTED);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	test_receive_access_check (&session, &changes);
	test_receive_session_end (&session);

	// a request that the parser could not parse
	current_case = "access: a request that could not be parsed";
	test_receive_access_feed (&session, cerver, reset_bad_request);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	changes.n_incompleted = 1;
	test_receive_access_check (&session, &changes);
	test_receive_session_end (&session);

	// a request whose client left before it was complete
	current_case = "access: a request whose client left";
	test_receive_access_feed (&session, cerver, reset_incomplete_request);
	http_receive_client_left (session.http_receive, 0);
	test_receive_expect (session.http_receive->receive_status == HTTP_RECEIVE_STATUS_CLOSED);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	changes.n_aborted = 1;
	test_receive_access_check (&session, &changes);

	const size_t aborted_in_body = http_cerver->n_aborted_requests[HTTP_STAGE_BODY][HTTP_ABORT_REASON_CLOSED];
	test_receive_expect (aborted_in_body > 0);
	test_receive_session_end (&session);

	// a request that stopped arriving until its deadline
	current_case = "access: a request that stopped arriving";
	test_receive_access_feed (&session, cerver, reset_incomplete_request);
	http_receive_handle_timeout (session.http_receive, true);
	test_receive_expect (session.http_receive->receive_status == HTTP_RECEIVE_STATUS_TIMEOUT);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	changes.n_vanished = 1;
	test_receive_access_check (&session, &changes);
	test_receive_session_end (&session);

	// a request whose response failed was counted where its client left, it is not counted again
	current_case = "access: a request whose response failed";
	test_receive_access_feed (&session, cerver, reset_incomplete_request);

	(void) memset (&changes, 0, sizeof (TestReceiveCounters));
	TestReceiveCounters before_failed;
	test_receive_counters_take (http_cerver, &before_failed);

	http_receive_response_failed (session.http_receive, EPIPE);
	http_receive_client_left (session.http_receive, EPIPE);

	TestReceiveCounters after_failed;
	test_receive_counters_take (http_cerver, &after_failed);
	test_receive_expect (after_failed.n_failed_responses == before_failed.n_failed_responses + 1);

	test_receive_access_check (&session, &changes);
	test_receive_session_end (&session);

}

// a receive is used by all the requests of its connection, with its own memory
// and with the arena and the buffers that its owner lends to it
static void test_receive_reset (Cerver *cerver) {

	test_receive_reset_many (cerver);
	test_receive_reset_every_split (cerver);
	test_receive_access (cerver);

	Arena *arena = receive_arena;
	receive_arena = NULL;

	test_receive_reset_many (cerver);
	test_receive_reset_every_split (cerver);
	test_receive_access (cerver);

	receive_arena = arena;

	// a receive that does not exist
	test_check_unsigned_eq (http_receive_reset (NULL), 1, NULL);

}

#pragma endregion

void http_tests_receive (void) {

	(void) printf ("Testing HTTP receive...\n");

	test_receive_files_create ();
	test_receive_arena_create ();

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
	test_receive_unsafe_filenames (cerver);
	test_receive_dirname_without_uploads_path (cerver);
	test_receive_dirname_with_uploads_path (cerver);
	test_receive_default_dirnames (cerver);
	test_receive_attribute_without_value (cerver);
	test_receive_upload_error (cerver);
	test_receive_foreign_files (cerver);
	test_receive_empty_generated_filename (cerver);
	test_receive_directory_error (cerver);

	test_receive_groups (cerver);
	test_receive_method_not_allowed (cerver);
	test_receive_trailing_slash (cerver);
	test_receive_cors_preflight (cerver);

	test_receive_parser_methods (cerver);
	test_receive_parser_strict (cerver);
	test_receive_parser_upgrade (cerver);
	test_receive_parser_keep_alive (cerver);
	test_receive_parser_header_limit (cerver);

	test_receive_framed (cerver);
	test_receive_preflight_long_headers (cerver);
	test_receive_reset (cerver);

	(void) cerver_teardown (cerver);

	test_receive_bearer_keys ();

	test_receive_static_custom_authentication ();
	test_receive_static_bearer_authentication ();

	test_receive_arena_delete ();
	test_receive_files_delete ();

	(void) printf ("Done! %zu requests were fed\n", n_fed_requests);

}
