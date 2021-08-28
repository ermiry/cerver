#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/utils.h>

#include "../test.h"

static const char *range_none = { "None" };
static const char *range_empty = { "Empty" };
static const char *range_first = { "First" };
static const char *range_full = { "Full" };

static void test_http_range_type_string (void) {

	test_check_str_eq (http_range_type_string (HTTP_RANGE_TYPE_NONE), range_none, NULL);
	test_check_str_eq (http_range_type_string (HTTP_RANGE_TYPE_EMPTY), range_empty, NULL);
	test_check_str_eq (http_range_type_string (HTTP_RANGE_TYPE_FIRST), range_first, NULL);
	test_check_str_eq (http_range_type_string (HTTP_RANGE_TYPE_FULL), range_full, NULL);

}

static void test_http_bytes_range_init (void) {

	BytesRange bytes_range = { 0 };

	http_bytes_range_init (&bytes_range);

	test_check_long_int_eq (bytes_range.file_size, 0, NULL);
	test_check_int_eq (bytes_range.type, HTTP_RANGE_TYPE_NONE, NULL);
	test_check_long_int_eq (bytes_range.start, 0, NULL);
	test_check_long_int_eq (bytes_range.end, 0, NULL);
	test_check_long_int_eq (bytes_range.chunk_size, 0, NULL);

}

static void test_http_bytes_range_print (void) {

	BytesRange bytes_range = { 0 };

	http_bytes_range_init (&bytes_range);

	http_bytes_range_print (&bytes_range);

}

void http_tests_utils (void) {

	(void) printf ("Testing HTTP utils...\n");

	test_http_range_type_string ();

	test_http_bytes_range_init ();
	test_http_bytes_range_print ();

	(void) printf ("Done!\n");

}
