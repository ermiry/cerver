#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

#include <cerver/http/core/http.h>
#include <cerver/http/core/pairs.h>
#include <cerver/http/core/public.h>

#include "../test.h"
#include "faults.h"
#include "http.h"

// the allocations of HTTP that fail, with a fault injected in one of its calls

// the key of the query is the only calloc () of its size, with its characters and the null byte
#define FAULTS_HTTP_QUERY_KEY			"name"
#define FAULTS_HTTP_QUERY				FAULTS_HTTP_QUERY_KEY "=cerver"

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

void faults_tests_http (void) {

	(void) printf ("Testing FAULTS http...\n");

	faults_http_query_pairs ();

	faults_http_static_path ();

	(void) printf ("Done!\n");

}
