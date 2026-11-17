#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/collections/arena.h>
#include <cerver/collections/dlist.h>

#include <cerver/connection/connection.h>
#include <cerver/connection/end.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/pairs.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/parser/parser.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/security/auth.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>

#include "../test.h"
#include "faults.h"
#include "http.h"

// the allocations of HTTP that fail, with a fault injected in one of its calls

// the key of the query is the only calloc () of its size, with its characters and the null byte
#define FAULTS_HTTP_QUERY_KEY			"name"
#define FAULTS_HTTP_QUERY				FAULTS_HTTP_QUERY_KEY "=cerver"

// the characters of the path are the only calloc () of its size, and its String the only malloc () of its size
#define FAULTS_HTTP_STATIC_PATH			"/tmp/cerver-faults-static-path"

// a json body with one value, the only malloc () of its size with its null byte
#define FAULTS_HTTP_JSON_KEY			"value"
#define FAULTS_HTTP_JSON_VALUE			7
#define FAULTS_HTTP_JSON_BODY			"{\"value\": 7}"

// a message longer than the json bodies that are written in the stack
#define FAULTS_HTTP_JSON_LONG_VALUE_SIZE	512
#define FAULTS_HTTP_JSON_LONG_BODY_SIZE		(FAULTS_HTTP_JSON_LONG_VALUE_SIZE + 64)

// the workers of a cerver that has none
#define FAULTS_HTTP_WORKERS_JSON		"{\"workers\": []}"

// the routes whose stats are a text that does not fit in the chunks of its arena
#define FAULTS_HTTP_ROUTES				64
#define FAULTS_HTTP_ROUTE_SIZE			32

// that text gets a block of the arena of its own: the header of the block and the buffer of the writer,
// that doubles its size until the text fits
#define FAULTS_HTTP_LARGE_TEXT_SIZE		(1 << 15)
#define FAULTS_HTTP_LARGE_BLOCK_SIZE	(ARENA_ALIGNMENT + FAULTS_HTTP_LARGE_TEXT_SIZE)

// the claims of a token as they are written
#define FAULTS_HTTP_CLAIMS				"{\"id\": \"faults\", \"exp\": 4102444800}"

#define FAULTS_HTTP_BEARER				"Bearer header.claims.signature"

#define FAULTS_HTTP_CERVER_NAME			"faults-http"
#define FAULTS_HTTP_CERVER_PORT			7016

// a pair without memory for its key is not added (it copied the key into NULL)
static void faults_http_query_pairs (void) {

	const char *query = FAULTS_HTTP_QUERY;

	faults_arm (FAULTS_FUNCTION_CALLOC, true, sizeof (FAULTS_HTTP_QUERY_KEY));
	DoubleList *pairs = http_parse_query_into_pairs (query, query + strlen (query));
	faults_disarm (FAULTS_FUNCTION_CALLOC);

	test_check_ptr (pairs);
	test_check_unsigned_eq ((unsigned int) dlist_size (pairs), 0, "a pair without its key was added");

	dlist_delete (pairs);

}

// adds the static path with a fault armed in one of the allocations of its path
static void faults_http_static_path_add_failed (
	HttpCerver *http_cerver, const FaultsFunction function, const size_t size
) {

	const size_t paths = dlist_size (http_cerver->static_paths);

	const unsigned int triggered = faults_triggered (function);
	faults_arm (function, true, size);
	HttpStaticPath *static_path = http_cerver_static_path_add (http_cerver, FAULTS_HTTP_STATIC_PATH);
	faults_disarm (function);

	test_check_unsigned_eq (faults_triggered (function), triggered + 1, "the path did not fail");
	test_check_null_ptr (static_path);
	test_check_unsigned_eq ((unsigned int) dlist_size (http_cerver->static_paths), (unsigned int) paths, NULL);

}

// a static path without memory for its path is not added (it was resolved with a NULL path)
static void faults_http_static_path (void) {

	// the lists of the HttpCerver are created with it, without a cerver
	HttpCerver *http_cerver = http_cerver_create (NULL);
	test_check_ptr (http_cerver);

	faults_http_static_path_add_failed (http_cerver, FAULTS_FUNCTION_CALLOC, sizeof (FAULTS_HTTP_STATIC_PATH));
	faults_http_static_path_add_failed (http_cerver, FAULTS_FUNCTION_MALLOC, sizeof (String));

	test_check_ptr (http_cerver_static_path_add (http_cerver, FAULTS_HTTP_STATIC_PATH));
	test_check_unsigned_eq ((unsigned int) dlist_size (http_cerver->static_paths), 1, NULL);

	http_cerver_delete (http_cerver);

}

// a json body that fits in the stack is copied into memory of its size: without it there is no response
static void faults_http_json_body (void) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (FAULTS_HTTP_JSON_BODY));
	HttpResponse *response = http_response_json_int_value (
		HTTP_STATUS_OK, FAULTS_HTTP_JSON_KEY, FAULTS_HTTP_JSON_VALUE
	);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (response);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the body was not copied");

	// the same call with memory
	response = http_response_json_int_value (HTTP_STATUS_OK, FAULTS_HTTP_JSON_KEY, FAULTS_HTTP_JSON_VALUE);
	test_check_ptr (response);
	test_check_str_eq ((const char *) response->data, FAULTS_HTTP_JSON_BODY, NULL);
	http_response_delete (response);

}

// a json body that does not fit in the stack is written again in memory of its size
static void faults_http_json_body_long (void) {

	char value[FAULTS_HTTP_JSON_LONG_VALUE_SIZE] = { 0 };
	(void) memset (value, 'v', FAULTS_HTTP_JSON_LONG_VALUE_SIZE - 1);

	char expected[FAULTS_HTTP_JSON_LONG_BODY_SIZE] = { 0 };
	const int length = snprintf (expected, FAULTS_HTTP_JSON_LONG_BODY_SIZE, "{\"msg\": \"%s\"}", value);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, (size_t) length + 1);
	HttpResponse *response = http_response_json_msg (HTTP_STATUS_OK, value);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (response);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the body was not written again");

	response = http_response_json_msg (HTTP_STATUS_OK, value);
	test_check_ptr (response);
	test_check_str_eq ((const char *) response->data, expected, NULL);
	http_response_delete (response);

}

// a json of the admin routes is written in an arena of its own and copied: without one of them there is no json
static void faults_http_json_generate (HttpCerver *http_cerver) {

	unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	// the arena
	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	char *json = http_cerver_admin_generate_workers_json (http_cerver);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (json);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the arena was not created");

	// the copy of the text
	triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (FAULTS_HTTP_WORKERS_JSON));
	json = http_cerver_admin_generate_workers_json (http_cerver);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (json);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the text was not copied");

	json = http_cerver_admin_generate_workers_json (http_cerver);
	test_check_ptr (json);
	test_check_str_eq (json, FAULTS_HTTP_WORKERS_JSON, NULL);
	free (json);

}

static void faults_http_handler (
	const struct _HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

// a json that grows more than the chunks of its arena needs a block of its own: without it there is no json,
// and the part that was written is never returned
static void faults_http_json_generate_large (HttpCerver *http_cerver) {

	for (unsigned int i = 0; i < FAULTS_HTTP_ROUTES; i++) {
		char name[FAULTS_HTTP_ROUTE_SIZE] = { 0 };
		(void) snprintf (name, FAULTS_HTTP_ROUTE_SIZE, "faults/%u", i);

		HttpRoute *route = http_route_create (REQUEST_METHOD_GET, name, faults_http_handler);
		test_check_ptr (route);
		http_cerver_route_register (http_cerver, route);
	}

	// the text is one that only fits in the block
	char *json = http_cerver_admin_generate_routes_stats_json (http_cerver);
	test_check_ptr (json);
	test_check (strlen (json) >= (FAULTS_HTTP_LARGE_TEXT_SIZE / 2), "the stats of the routes fit in a chunk");
	test_check (strlen (json) < FAULTS_HTTP_LARGE_TEXT_SIZE, "the stats of the routes do not fit in the block");
	free (json);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_HTTP_LARGE_BLOCK_SIZE);
	json = http_cerver_admin_generate_routes_stats_json (http_cerver);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (json);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the text did not get its block");

}

// the claims of a token as json are written in memory of their size: without it there is no json
static void faults_http_claims_json (void) {

	CerverJsonDocument *document = cerver_json_parse (
		NULL, FAULTS_HTTP_CLAIMS, sizeof (FAULTS_HTTP_CLAIMS) - 1, CERVER_JSON_FLAGS_NONE, NULL, NULL
	);

	test_check_ptr (document);

	const CerverJsonValue *claims = cerver_json_document_root (document);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (FAULTS_HTTP_CLAIMS));
	char *json = (char *) http_decode_data_into_json (claims);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (json);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the claims were not written");

	json = (char *) http_decode_data_into_json (claims);
	test_check_ptr (json);
	test_check_str_eq (json, FAULTS_HTTP_CLAIMS, NULL);
	free (json);

	cerver_json_document_delete (document);

}

// the claims of a bearer that is validated without a request are read in an arena of the call:
// without it the bearer is not valid
static void faults_http_bearer_arena (HttpCerver *http_cerver) {

	void *decoded = NULL;

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	const bool valid = http_cerver_auth_validate_jwt (
		http_cerver, FAULTS_HTTP_BEARER, http_decode_data_into_json, &decoded
	);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_bool_eq (valid, false, NULL);
	test_check_null_ptr (decoded);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the arena was not created");

}

#pragma region parser

#define FAULTS_HTTP_CONNECTIONS			2
#define FAULTS_HTTP_SERVER				0
#define FAULTS_HTTP_CLIENT				1

// a request with one field more than the array of the receive takes, that doubles it
#define FAULTS_HTTP_FIELDS				(HTTP_RECEIVE_FIELDS_INLINE + 1)
#define FAULTS_HTTP_FIELDS_GROWN		(HTTP_RECEIVE_FIELDS_INLINE * 2)

#define FAULTS_HTTP_REQUEST_SIZE		(1 << 12)
#define FAULTS_HTTP_RESPONSE_SIZE		(1 << 12)

static const char *faults_http_ok = { "HTTP/1.1 200" };
static const char *faults_http_server_error = { "HTTP/1.1 500" };

static void faults_http_fields_handler (const HttpReceive *http_receive, const HttpRequest *request) {

	(void) request;

	(void) http_response_json_msg_send (http_receive, HTTP_STATUS_OK, "fields");

}

static Cerver *faults_http_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_WEB, FAULTS_HTTP_CERVER_NAME, FAULTS_HTTP_CERVER_PORT, PROTOCOL_TCP, false, FAULTS_HTTP_CONNECTIONS
	);

	test_check_ptr (cerver);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_ptr (http_cerver);

	http_cerver_route_register (
		http_cerver, http_route_create (REQUEST_METHOD_GET, "fields", faults_http_fields_handler)
	);

	test_check_unsigned_eq (http_cerver_init (http_cerver), 0, NULL);

	return cerver;

}

// the request with its fields, X-Field-<n>: <n>
static size_t faults_http_fields_request (char *request) {

	size_t length = (size_t) snprintf (request, FAULTS_HTTP_REQUEST_SIZE, "GET /fields HTTP/1.1\r\nHost: cerver\r\n");

	for (unsigned int i = 1; i < FAULTS_HTTP_FIELDS; i++) {
		length += (size_t) snprintf (request + length, FAULTS_HTTP_REQUEST_SIZE - length, "X-Field-%u: %u\r\n", i, i);
	}

	length += (size_t) snprintf (request + length, FAULTS_HTTP_REQUEST_SIZE - length, "\r\n");

	return length;

}

// the request through a receive of its own, the start of the response that it got
static void faults_http_receive (Cerver *cerver, const char *request, const size_t length, char *response) {

	int fds[2] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);
	(void) shutdown (fds[FAULTS_HTTP_CLIENT], SHUT_WR);

	Connection *connection = connection_create_empty ();
	test_check_ptr (connection);
	connection->socket->sock_fd = fds[FAULTS_HTTP_SERVER];
	connection->active = true;

	CerverReceive cerver_receive;
	(void) memset (&cerver_receive, 0, sizeof (CerverReceive));
	cerver_receive.cerver = cerver;
	cerver_receive.socket = connection->socket;
	cerver_receive.connection = connection;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);
	test_check_ptr (http_receive);

	char *buffer = (char *) malloc (length);
	test_check_ptr (buffer);
	(void) memcpy (buffer, request, length);

	http_receive->handler (http_receive, (ssize_t) length, buffer);
	free (buffer);

	if (connection->socket->sock_fd != -1) connection_end (connection);

	const ssize_t received = read (fds[FAULTS_HTTP_CLIENT], response, FAULTS_HTTP_RESPONSE_SIZE - 1);
	response[(received > 0) ? received : 0] = '\0';

	http_receive_delete (http_receive);
	connection_delete (connection);

	(void) close (fds[FAULTS_HTTP_CLIENT]);

}

// a request with more fields than the receive takes doubles its array; without memory for it, the request is
// answered with 500 instead of reading its head with the fields that fit
static void faults_http_fields_grow (void) {

	Cerver *cerver = faults_http_cerver_create ();

	char request[FAULTS_HTTP_REQUEST_SIZE];
	const size_t length = faults_http_fields_request (request);

	char buffer[FAULTS_HTTP_RESPONSE_SIZE];
	const char *response = buffer;

	faults_http_receive (cerver, request, length, buffer);
	test_check (!strncmp (response, faults_http_ok, strlen (faults_http_ok)), response);

	const unsigned int before = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_HTTP_FIELDS_GROWN * sizeof (CerverHttpField));
	faults_http_receive (cerver, request, length, buffer);
	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC) - before;
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_unsigned_eq (triggered, 1, "the array of the fields did not grow");
	test_check (!strncmp (response, faults_http_server_error, strlen (faults_http_server_error)), response);

	(void) cerver_teardown (cerver);

}

#pragma endregion

void faults_tests_http (void) {

	(void) printf ("Testing FAULTS http...\n");

	faults_http_query_pairs ();

	faults_http_static_path ();

	// the responses need their producer
	HttpCerver *responses_cerver = http_cerver_new ();
	test_check_ptr (responses_cerver);
	test_check_unsigned_eq (http_responses_init (responses_cerver), 0, NULL);

	faults_http_json_body ();
	faults_http_json_body_long ();

	http_responses_end ();
	http_cerver_delete (responses_cerver);

	// the json of the admin routes and a bearer need a whole cerver, that never starts
	Cerver *cerver = cerver_create_web (FAULTS_HTTP_CERVER_NAME, FAULTS_HTTP_CERVER_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	faults_http_json_generate (http_cerver_get (cerver));
	faults_http_json_generate_large (http_cerver_get (cerver));
	faults_http_claims_json ();
	faults_http_bearer_arena (http_cerver_get (cerver));

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	faults_http_fields_grow ();

	(void) printf ("Done!\n");

}
