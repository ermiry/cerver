#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/method.h>

#include "../test.h"

// the values of the parser (llhttp) that are not methods of a request
#define TEST_METHODS_PRI				34
#define TEST_METHODS_FLUSH				45
#define TEST_METHODS_OUTSIDE			100

static const char *UNDEFINED = { "Undefined" };

// every method of the table, with its value and its name
static void test_http_methods_names (void) {

	test_check_str_eq (http_request_method_str (REQUEST_METHOD_DELETE), "DELETE", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_GET), "GET", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_HEAD), "HEAD", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_POST), "POST", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_PUT), "PUT", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_CONNECT), "CONNECT", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_OPTIONS), "OPTIONS", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_TRACE), "TRACE", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_PROPFIND), "PROPFIND", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_MSEARCH), "M-SEARCH", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_PATCH), "PATCH", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_SOURCE), "SOURCE", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_QUERY), "QUERY", NULL);

	// the values are the ones of llhttp
	test_check_unsigned_eq (REQUEST_METHOD_DELETE, 0, NULL);
	test_check_unsigned_eq (REQUEST_METHOD_PATCH, 28, NULL);
	test_check_unsigned_eq (REQUEST_METHOD_SOURCE, 33, NULL);
	test_check_unsigned_eq (REQUEST_METHOD_QUERY, 46, NULL);
	test_check_unsigned_eq (REQUEST_METHOD_SIZE, REQUEST_METHOD_QUERY + 1, NULL);
	test_check_unsigned_eq (REQUEST_METHOD_UNDEFINED, 255, NULL);

}

// the 35 methods of the table are valid, PRI, the RTSP methods and the rest are not
static void test_http_methods_valid (void) {

	unsigned int valid = 0;
	for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
		if (http_request_method_is_valid ((RequestMethod) method)) {
			valid += 1;
			test_check (strcmp (http_request_method_str ((RequestMethod) method), UNDEFINED), NULL);
		}

		else {
			test_check_str_eq (http_request_method_str ((RequestMethod) method), UNDEFINED, NULL);
		}
	}

	test_check_unsigned_eq (valid, 35, NULL);

	for (unsigned int method = TEST_METHODS_PRI; method <= TEST_METHODS_FLUSH; method++) {
		test_check_false (http_request_method_is_valid ((RequestMethod) method));
	}

	test_check_false (http_request_method_is_valid (REQUEST_METHOD_UNDEFINED));
	test_check_false (http_request_method_is_valid ((RequestMethod) TEST_METHODS_OUTSIDE));

	test_check_str_eq (http_request_method_str (REQUEST_METHOD_UNDEFINED), UNDEFINED, NULL);
	test_check_str_eq (http_request_method_str ((RequestMethod) TEST_METHODS_OUTSIDE), UNDEFINED, NULL);

}

void http_tests_methods (void) {

	(void) printf ("Testing HTTP methods...\n");

	test_http_methods_names ();
	test_http_methods_valid ();

	(void) printf ("Done!\n");

}
