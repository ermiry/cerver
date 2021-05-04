#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/method.h>

#include "../test.h"

static const char *DELETE = { "DELETE" };
static const char *GET = { "GET" };
static const char *HEAD = { "HEAD" };
static const char *POST = { "POST" };
static const char *PUT = { "PUT" };

static const char *CONNECT = { "CONNECT" };
static const char *OPTIONS = { "OPTIONS" };
static const char *TRACE = { "TRACE" };

static const char *PATCH = { "PATCH" };

static const char *SOURCE = { "SOURCE" };

static const char *OTHER = { "NONE" };

static void test_http_method_get (void) {

	test_check_str_eq (http_method_str (HTTP_METHOD_DELETE), DELETE, NULL);
	test_check_str_eq (http_method_str (HTTP_METHOD_GET), GET, NULL);
	test_check_str_eq (http_method_str (HTTP_METHOD_HEAD), HEAD, NULL);
	test_check_str_eq (http_method_str (HTTP_METHOD_POST), POST, NULL);
	test_check_str_eq (http_method_str (HTTP_METHOD_PUT), PUT, NULL);

	test_check_str_eq (http_method_str (HTTP_METHOD_CONNECT), CONNECT, NULL);
	test_check_str_eq (http_method_str (HTTP_METHOD_OPTIONS), OPTIONS, NULL);
	test_check_str_eq (http_method_str (HTTP_METHOD_TRACE), TRACE, NULL);

	test_check_str_eq (http_method_str (HTTP_METHOD_PATCH), PATCH, NULL);

	test_check_str_eq (http_method_str (HTTP_METHOD_SOURCE), SOURCE, NULL);

	test_check_str_eq (http_method_str ((const enum http_method) 34), OTHER, NULL);
	test_check_str_eq (http_method_str ((const enum http_method) 62), OTHER, NULL);
	test_check_str_eq (http_method_str ((const enum http_method) 100), OTHER, NULL);

}

void http_tests_methods (void) {

	(void) printf ("Testing HTTP methods...\n");

	test_http_method_get ();

	(void) printf ("Done!\n");

}