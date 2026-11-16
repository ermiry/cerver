#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/dlist.h>

#include <cerver/http/core/http.h>
#include <cerver/http/core/pairs.h>
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

void faults_tests_http (void) {

	(void) printf ("Testing FAULTS http...\n");

	faults_http_query_pairs ();

	faults_http_response_compile ();

	(void) printf ("Done!\n");

}
