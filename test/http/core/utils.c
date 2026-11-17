#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/dlist.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/pairs.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/core/utils.h>

#include "../../test.h"

// a generated uploads path that does not fit in the uploads path of the HTTP cerver
#define TEST_UPLOADS_PATH_LONG_SIZE			512

static const char *range_none = { "None" };
static const char *range_empty = { "Empty" };
static const char *range_first = { "First" };
static const char *range_full = { "Full" };

static void test_http_range_type_to_string (void) {

	test_check_str_eq (http_range_type_to_string (HTTP_RANGE_TYPE_NONE), range_none, NULL);
	test_check_str_eq (http_range_type_to_string (HTTP_RANGE_TYPE_EMPTY), range_empty, NULL);
	test_check_str_eq (http_range_type_to_string (HTTP_RANGE_TYPE_FIRST), range_first, NULL);
	test_check_str_eq (http_range_type_to_string (HTTP_RANGE_TYPE_FULL), range_full, NULL);

}

static void test_http_bytes_range_init (void) {

	BytesRange bytes_range = CERVER_ZERO_INITIALIZER;

	http_bytes_range_init (&bytes_range);

	test_check_long_int_eq (bytes_range.file_size, 0, NULL);
	test_check_int_eq (bytes_range.type, HTTP_RANGE_TYPE_NONE, NULL);
	test_check_long_int_eq (bytes_range.start, 0, NULL);
	test_check_long_int_eq (bytes_range.end, 0, NULL);
	test_check_long_int_eq (bytes_range.chunk_size, 0, NULL);

}

static void test_http_bytes_range_print (void) {

	BytesRange bytes_range = CERVER_ZERO_INITIALIZER;

	http_bytes_range_init (&bytes_range);

	http_bytes_range_print (&bytes_range);

}

// the pairs that key_value_pair_create () makes without a key or without a value
// are skipped by the lookups and printed without crashing
static void test_http_key_value_pairs_null (void) {

	DoubleList *pairs = dlist_init (key_value_pair_delete, NULL);

	(void) dlist_insert_at_end (pairs, key_value_pair_create (NULL, "value"));
	(void) dlist_insert_at_end (pairs, key_value_pair_create ("flag", NULL));
	(void) dlist_insert_at_end (pairs, key_value_pair_create ("key", "value"));

	const String *value = key_value_pairs_get_value (pairs, "key");
	test_check_ptr (value);
	test_check_str_eq (value->str, "value", NULL);

	test_check_null_ptr (key_value_pairs_get_value (pairs, "flag"));
	test_check_null_ptr (key_value_pairs_get_value (pairs, "missing"));

	key_value_pairs_print (pairs);

	dlist_delete (pairs);

}

// the custom data of a NULL HTTP cerver is not set nor read
static void test_http_cerver_custom_data_null (void) {

	http_cerver_set_custom_data (NULL, NULL);
	http_cerver_set_delete_custom_data (NULL, free);
	http_cerver_set_default_delete_custom_data (NULL);

	test_check_null_ptr (http_cerver_get_custom_data (NULL));

}

// the length of a generated uploads path that was truncated is the one of the path that was kept
static void test_http_cerver_generate_uploads_path_truncated (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	char long_path[TEST_UPLOADS_PATH_LONG_SIZE] = { 0 };
	(void) memset (long_path, 'a', TEST_UPLOADS_PATH_LONG_SIZE - 1);

	http_cerver_generate_uploads_path (http_cerver, "/var/%s", long_path);
	test_check_unsigned_eq (
		(size_t) http_cerver->uploads_path_len, strlen (http_cerver->uploads_path),
		"the uploads path length is not the one of the truncated path"
	);

	(void) cerver_teardown (test_cerver);

}

// the getters of a NULL receive return nothing instead of reading it
static void test_http_receive_getters_null (void) {

	test_check_null_ptr (http_receive_get_cerver_receive (NULL));
	test_check_null_ptr (http_receive_get_cerver (NULL));
	test_check_int_eq (http_receive_get_sock_fd (NULL), -1, "a NULL receive has a socket");

}

void http_tests_utils (void) {

	(void) printf ("Testing HTTP utils...\n");

	test_http_range_type_to_string ();

	test_http_bytes_range_init ();
	test_http_bytes_range_print ();

	test_http_key_value_pairs_null ();

	test_http_cerver_custom_data_null ();

	test_http_cerver_generate_uploads_path_truncated ();

	test_http_receive_getters_null ();

	(void) printf ("Done!\n");

}
