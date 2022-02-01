#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>
#include <cerver/http/multipart.h>

#include "../test.h"

static void test_http_multi_part_fields (MultiPart *mpart) {

	test_check_unsigned_eq (mpart->type, MULTI_PART_TYPE_NONE, NULL);

	test_check_unsigned_eq (mpart->next_header, MULTI_PART_HEADER_INVALID, NULL);
	for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++) {
		test_check_int_eq (mpart->headers[i].len, 0, NULL);
		test_check_str_empty (mpart->headers[i].value);
	}

	test_check_int_eq (mpart->temp_header.len, 0, NULL);
	test_check_str_empty (mpart->temp_header.value);

	test_check_ptr (mpart->params);

	test_check_null_ptr (mpart->name);

	test_check_int_eq (mpart->filename_len, 0, NULL);
	test_check_str_empty (mpart->filename);

	test_check_int_eq (mpart->generated_filename_len, 0, NULL);
	test_check_str_empty (mpart->generated_filename);

	test_check_int_eq (mpart->fd, -1, NULL);
	test_check_unsigned_eq (mpart->n_reads, 0, NULL);
	test_check_unsigned_eq (mpart->total_wrote, 0, NULL);

	test_check_int_eq (mpart->saved_filename_len, 0, NULL);
	test_check_str_empty (mpart->saved_filename);

	test_check_false (mpart->moved_file);

	test_check_int_eq (mpart->value_len, 0, NULL);
	test_check_str_empty (mpart->value);

}

static MultiPart *test_http_multi_part_new (void) {

	MultiPart *mpart = (MultiPart *) http_multi_part_new ();

	test_check_ptr (mpart);

	test_http_multi_part_fields (mpart);

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

	test_check_int_eq (http_multi_part_get_n_reads (mpart), 0, NULL);
	test_check_int_eq (http_multi_part_get_total_wrote (mpart), 0, NULL);

	test_check_ptr (http_multi_part_get_saved_filename (mpart));
	test_check_str_empty (http_multi_part_get_saved_filename (mpart));
	test_check_int_eq (http_multi_part_get_saved_filename_len (mpart), 0, NULL);

	test_check_false (http_multi_part_get_moved_file (mpart));

	test_check_ptr (http_multi_part_get_value (mpart));
	test_check_str_empty (http_multi_part_get_value (mpart));
	test_check_int_eq (http_multi_part_get_value_len (mpart), 0, NULL);

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_is_not_empty (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	test_check_false (http_multi_part_is_not_empty (mpart));

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_get_type (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	test_check_int_eq (http_multi_part_get_type (mpart), MULTI_PART_TYPE_NONE, NULL);

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_is_file (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	mpart->type = MULTI_PART_TYPE_FILE;

	test_check_true (http_multi_part_is_file (mpart));

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_is_value (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	mpart->type = MULTI_PART_TYPE_VALUE;

	test_check_true (http_multi_part_is_value (mpart));

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_get_filename (void) {

	const char *filename = "file";

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	mpart->type = MULTI_PART_TYPE_FILE;

	// manualy set mpart filename
	(void) strncpy (mpart->filename, filename, HTTP_MULTI_PART_FILENAME_SIZE - 1);
	mpart->filename_len = strlen (mpart->filename);

	test_check_str_eq (http_multi_part_get_filename (mpart), filename, NULL);

	test_check_int_eq (http_multi_part_get_filename_len (mpart), strlen (filename), NULL);

	test_check_true (http_multi_part_is_not_empty (mpart));

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_set_generated_filename (void) {

	static const char *path = "/var/uploads";
	static const char *filename = "test-file.png";

	char buffer[HTTP_MULTI_PART_GENERATED_FILENAME_SIZE] = { 0 };
	int len = snprintf (buffer, HTTP_MULTI_PART_GENERATED_FILENAME_SIZE, "%s/%s", path, filename);

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	mpart->type = MULTI_PART_TYPE_FILE;

	http_multi_part_set_generated_filename (mpart, "%s/%s", path, filename);

	test_check_str_eq (http_multi_part_get_generated_filename (mpart), buffer, NULL);

	test_check_int_eq (http_multi_part_get_generated_filename_len (mpart), len, NULL);

	// generated filename is not taken into account for this check
	// test_check_true (http_multi_part_is_not_empty (mpart));

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_get_n_reads (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	test_check_int_eq (http_multi_part_get_n_reads (mpart), 0, NULL);

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_get_total_wrote (void) {

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	test_check_int_eq (http_multi_part_get_total_wrote (mpart), 0, NULL);

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_get_value (void) {

	static const char *value = "hola";

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	mpart->type = MULTI_PART_TYPE_VALUE;

	// manualy set mpart value
	(void) strncpy (mpart->value, value, HTTP_MULTI_PART_VALUE_SIZE - 1);
	mpart->value_len = strlen (mpart->value);

	test_check_str_eq (http_multi_part_get_value (mpart), value, NULL);

	test_check_int_eq (http_multi_part_get_value_len (mpart), strlen (value), NULL);

	test_check_true (http_multi_part_is_not_empty (mpart));

	http_multi_part_delete (mpart);

}

static void test_http_multi_part_reset (void) {

	static const char *path = "/var/uploads";
	static const char *filename = "test-file.png";

	char buffer[HTTP_MULTI_PART_GENERATED_FILENAME_SIZE] = { 0 };
	(void) snprintf (buffer, HTTP_MULTI_PART_GENERATED_FILENAME_SIZE, "%s/%s", path, filename);

	static const char *value = "hola";

	MultiPart *mpart = test_http_multi_part_new ();

	test_check_ptr (mpart);

	// manualy set mpart filename
	(void) strncpy (mpart->filename, filename, HTTP_MULTI_PART_FILENAME_SIZE - 1);
	mpart->filename_len = strlen (mpart->filename);

	http_multi_part_set_generated_filename (mpart, "%s/%s", path, filename);

	// manualy set mpart value
	(void) strncpy (mpart->value, value, HTTP_MULTI_PART_VALUE_SIZE - 1);
	mpart->value_len = strlen (mpart->value);

	http_multi_part_reset (mpart);

	test_check_false (http_multi_part_is_not_empty (mpart));

	// check individual fields
	test_http_multi_part_fields (mpart);

	http_multi_part_delete (mpart);

}

void http_tests_multiparts (void) {

	(void) printf ("Testing HTTP multi-parts...\n");

	test_http_multi_part_create ();

	test_http_multi_part_is_not_empty ();

	test_http_multi_part_get_type ();
	test_http_multi_part_is_file ();
	test_http_multi_part_is_value ();

	test_http_multi_part_get_filename ();
	test_http_multi_part_set_generated_filename ();
	test_http_multi_part_get_n_reads ();
	test_http_multi_part_get_total_wrote ();
	// test_http_multi_part_get_saved_filename ();
	test_http_multi_part_get_value ();

	test_http_multi_part_reset ();

	(void) printf ("Done!\n");

}
