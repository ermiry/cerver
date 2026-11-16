#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/arena.h>
#include <cerver/collections/dlist.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>

#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/pairs.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
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

void faults_tests_http (void) {

	(void) printf ("Testing FAULTS http...\n");

	faults_http_query_pairs ();

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

	(void) printf ("Done!\n");

}
