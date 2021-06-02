#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>
#include <cerver/http/multipart.h>

#include "../test.h"

static MultiPart *test_http_multi_part_new (void) {

	MultiPart *mpart = http_multi_part_new ();

	test_check_ptr (mpart);

	test_check_unsigned_eq (mpart->type, MULTI_PART_TYPE_NONE, NULL);

	test_check_unsigned_eq (mpart->next_header, MULTI_PART_HEADER_INVALID, NULL);
	for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++) {
		test_check_int_eq (mpart->headers[i].len, 0, NULL);
		test_check_str_empty (mpart->headers[i].value);
	}

	test_check_ptr (mpart->params);

	test_check_null_ptr (mpart->name);

	test_check_int_eq (mpart->filename_len, 0, NULL);
	test_check_str_empty (mpart->filename);

	test_check_int_eq (mpart->generated_filename_len, 0, NULL);
	test_check_str_empty (mpart->generated_filename);

	test_check_int_eq (mpart->fd, -1, NULL);
	test_check_int_eq (mpart->saved_filename_len, 0, NULL);
	test_check_str_empty (mpart->saved_filename);
	test_check_unsigned_eq (mpart->n_reads, 0, NULL);
	test_check_unsigned_eq (mpart->total_wrote, 0, NULL);

	test_check_int_eq (mpart->value_len, 0, NULL);
	test_check_str_empty (mpart->value);

	return mpart;

}

static void test_http_multi_part_create (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	test_check_unsigned_eq (http_multi_part_get_type (mpart), MULTI_PART_TYPE_NONE, NULL);

	test_check_false (http_multi_part_is_file (mpart));
	test_check_false (http_multi_part_is_value (mpart));

	test_check_null_ptr (http_multi_part_get_name (mpart));

	test_check_ptr (http_multi_part_get_filename (mpart));
	test_check_str_empty (http_multi_part_get_filename (mpart));
	test_check_int_eq (http_multi_part_get_filename_len (mpart), 0, NULL);

	test_check_ptr (http_multi_part_get_generated_filename (mpart));
	test_check_str_empty (http_multi_part_get_generated_filename (mpart));
	test_check_int_eq (http_multi_part_get_generated_filename_len (mpart), 0, NULL);

	test_check_ptr (http_multi_part_get_saved_filename (mpart));
	test_check_str_empty (http_multi_part_get_saved_filename (mpart));
	test_check_int_eq (http_multi_part_get_saved_filename_len (mpart), 0, NULL);

	test_check_int_eq (http_multi_part_get_n_reads (mpart), 0, NULL);
	test_check_int_eq (http_multi_part_get_total_wrote (mpart), 0, NULL);

	test_check_ptr (http_multi_part_get_value (mpart));
	test_check_str_empty (http_multi_part_get_value (mpart));
	test_check_int_eq (http_multi_part_get_value_len (mpart), 0, NULL);

	http_multi_part_delete (mpart);

}

void http_tests_multiparts (void) {

	(void) printf ("Testing HTTP multi-parts...\n");

	test_http_multi_part_create ();

	(void) printf ("Done!\n");

}
