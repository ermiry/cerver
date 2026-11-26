#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

#include <cerver/config.h>

#include <cerver/core/cerver.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/pairs.h>
#include <cerver/http/core/public.h>
#include <cerver/http/parser/http_parser.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>

#include "../test.h"
#include "faults.h"
#include "http.h"

// the allocations of HTTP that fail, with a fault injected in one of its calls

// the key of the query is the only calloc () of its size, with its characters and the null byte
#define FAULTS_HTTP_QUERY_KEY			"name"
#define FAULTS_HTTP_QUERY				FAULTS_HTTP_QUERY_KEY "=cerver"

// two headers of this length do not fit in the buffer of the response,
// so its header is the only calloc () of its size
#define FAULTS_HTTP_HEADER_VALUE_SIZE	160

#define FAULTS_HTTP_BODY				"{\"msg\": \"body\"}"

// the characters of the path are the only calloc () of its size, and its String the only malloc () of its size
#define FAULTS_HTTP_STATIC_PATH			"/tmp/cerver-faults-static-path"

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

// a response with two headers that do not fit in its buffer and a body
static HttpResponse *faults_http_response_create (const char *value) {

	HttpResponse *response = (HttpResponse *) http_response_new ();
	test_check_ptr (response);

	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, value), 0, NULL);
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_CONTENT_LOCATION, value), 0, NULL);
	test_check_unsigned_eq (
		http_response_set_data_ref (response, FAULTS_HTTP_BODY, strlen (FAULTS_HTTP_BODY)), 0, NULL
	);

	return response;

}

// the size of the calloc () of the header of the response, measured with a twin response
static size_t faults_http_response_header_size (const char *value) {

	HttpResponse *twin = faults_http_response_create (value);
	http_response_compile_header (twin);
	const size_t header_size = twin->header_len + 1;
	http_response_delete (twin);

	return header_size;

}

// a response whose header could not be allocated is compiled with only its body
// (it copied from the NULL header, that memcpy () does not take)
static void faults_http_response_compile (void) {

	HttpCerver *http_cerver = http_cerver_new ();
	test_check_ptr (http_cerver);
	test_check_unsigned_eq (http_responses_init (http_cerver), 0, NULL);

	char value[FAULTS_HTTP_HEADER_VALUE_SIZE] = { 0 };
	(void) memset (value, 'v', FAULTS_HTTP_HEADER_VALUE_SIZE - 1);

	const size_t header_size = faults_http_response_header_size (value);
	test_check (header_size > HTTP_RESPONSE_HEADER_BUFFER_SIZE, "the header fits in the buffer of the response");

	HttpResponse *response = faults_http_response_create (value);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_CALLOC);
	faults_arm (FAULTS_FUNCTION_CALLOC, true, header_size);
	const u8 result = http_response_compile (response);
	faults_disarm (FAULTS_FUNCTION_CALLOC);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_CALLOC), triggered + 1, "the header did not fail");
	test_check_unsigned_eq (result, 0, NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq ((unsigned int) response->res_len, (unsigned int) strlen (FAULTS_HTTP_BODY), NULL);
	test_check (!memcmp (response->res, FAULTS_HTTP_BODY, response->res_len), "the response is not its body");

	http_response_delete (response);

	http_responses_end ();
	http_cerver_delete (http_cerver);

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

static unsigned int faults_http_triggered (void) {

	return faults_triggered (FAULTS_FUNCTION_MALLOC) + faults_triggered (FAULTS_FUNCTION_CALLOC);

}

// a receive of the size of the part is created with the allocation of that part failing
static void faults_http_receive_create_failed (CerverReceive *cerver_receive, const size_t size) {

	const unsigned int triggered = faults_http_triggered ();

	// the compiler can merge a malloc () and its memset () in a calloc ()
	faults_arm (FAULTS_FUNCTION_MALLOC, true, size);
	faults_arm (FAULTS_FUNCTION_CALLOC, true, size);
	HttpReceive *http_receive = http_receive_create (cerver_receive);
	faults_disarm (FAULTS_FUNCTION_CALLOC);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_unsigned_eq (faults_http_triggered (), triggered + 1, "the part of the receive did not fail");
	test_check_null_ptr (http_receive);

}

// a receive without memory for its parser or its request is not returned
// (the parser was initialized in NULL, and the request was used without a check)
static void faults_http_receive_create (void) {

	HttpCerver *http_cerver = http_cerver_create (NULL);
	test_check_ptr (http_cerver);

	Cerver cerver = CERVER_ZERO_INITIALIZER;
	cerver.cerver_data = http_cerver;

	CerverReceive cerver_receive = CERVER_ZERO_INITIALIZER;
	cerver_receive.cerver = &cerver;

	faults_http_receive_create_failed (&cerver_receive, sizeof (http_parser));
	faults_http_receive_create_failed (&cerver_receive, sizeof (HttpRequest));

	HttpReceive *http_receive = http_receive_create (&cerver_receive);
	test_check_ptr (http_receive);
	http_receive_delete (http_receive);

	http_cerver_delete (http_cerver);

}

void faults_tests_http (void) {

	(void) printf ("Testing FAULTS http...\n");

	faults_http_query_pairs ();

	faults_http_response_compile ();

	faults_http_static_path ();

	faults_http_receive_create ();

	(void) printf ("Done!\n");

}
