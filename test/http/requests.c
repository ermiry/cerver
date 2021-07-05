#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/request.h>

#include <cerver/http/json/json.h>

#include "../test.h"

#define DECODED_DATA_VALUE_SIZE			256
#define CUSTOM_DATA_VALUE_SIZE			256

typedef struct DecodedData {

	int len;
	char value[DECODED_DATA_VALUE_SIZE];

} DecodedData;

typedef struct CustomData {

	int len;
	char value[CUSTOM_DATA_VALUE_SIZE];

} CustomData;

static const char *base_dir = { "/var/uploads" };
static const char *basic_dir = { "hola" };
static const char *full_dir = { "/var/uploads/hola" };

static HttpRequest *test_http_request_new (void) {

	HttpRequest *request = (HttpRequest *) http_request_new ();

	test_check_unsigned_eq (request->method, REQUEST_METHOD_UNDEFINED, NULL);

	test_check_null_ptr (request->url);
	test_check_null_ptr (request->query);

	test_check_null_ptr (request->query_params);

	test_check_unsigned_eq (request->n_params, 0, NULL);
	for (u8 i = 0; i < REQUEST_PARAMS_SIZE; i++)
		test_check_null_ptr (request->params[i]);

	test_check_unsigned_eq (request->next_header, HTTP_HEADER_INVALID, NULL);
	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (request->headers[i]);

	test_check_null_ptr (request->decoded_data);
	test_check_null_ptr (request->delete_decoded_data);

	test_check_null_ptr (request->custom_data);
	test_check_null_ptr (request->delete_custom_data);

	test_check_null_ptr (request->body);

	test_check_null_ptr (request->multi_parts);
	test_check_null_ptr (request->current_part);
	test_check_null_ptr (request->next_part);

	test_check_unsigned_eq (request->n_files, 0, NULL);
	test_check_unsigned_eq (request->n_values, 0, NULL);

	test_check_int_eq (request->dirname_len, 0, NULL);
	test_check_str_empty (request->dirname);

	test_check_null_ptr (request->body_values);

	test_check_false (request->keep_files);

	return request;

}

static void test_http_request_create (void) {

	HttpRequest *request = test_http_request_new ();

	test_check_unsigned_eq (http_request_get_method (request), REQUEST_METHOD_UNDEFINED, NULL);
	
	test_check_null_ptr (http_request_get_url (request));
	test_check_null_ptr (http_request_get_query (request));

	test_check_null_ptr (http_request_get_query_params (request));

	test_check_unsigned_eq (http_request_get_n_params (request), 0, NULL);
	for (u8 i = 0; i < REQUEST_PARAMS_SIZE; i++)
		test_check_null_ptr (http_request_get_param_at_idx (request, i));

	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (http_request_get_header (request, (const http_header) i));
	
	test_check_unsigned_eq (http_request_get_content_type (request), HTTP_CONTENT_TYPE_NONE, NULL);

	test_check_null_ptr (http_request_get_content_type_string (request));

	test_check_false (http_request_content_type_is_json (request));

	test_check_null_ptr (http_request_get_decoded_data (request));

	test_check_null_ptr (http_request_get_custom_data (request));

	test_check_null_ptr (http_request_get_body (request));

	test_check_null_ptr (http_request_get_current_mpart (request));

	test_check_unsigned_eq (http_request_get_n_files (request), 0, NULL);
	test_check_unsigned_eq (http_request_get_n_values (request), 0, NULL);

	test_check_unsigned_eq (http_request_get_dirname_len (request), 0, NULL);
	test_check_str_empty (http_request_get_dirname (request));

	test_check_null_ptr (http_request_get_body_values (request));

	http_request_delete (request);

}

static void delete_decoded_data (void *data_ptr) {

	if (data_ptr) free (data_ptr);

}

static void test_http_request_set_decoded_data (void) {

	HttpRequest *request = test_http_request_new ();

	DecodedData *decoded_data = (DecodedData *) malloc (sizeof (DecodedData));

	http_request_set_decoded_data (request, decoded_data);

	test_check_ptr (http_request_get_decoded_data (request));

	http_request_set_delete_decoded_data (request, delete_decoded_data);

	test_check_ptr_eq (request->delete_decoded_data, delete_decoded_data);

	http_request_delete (request);

}

static void test_http_request_set_default_delete_decoded_data (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_default_delete_decoded_data (request);

	test_check_ptr_eq (request->delete_decoded_data, free);

	http_request_delete (request);

}

static void delete_custom_data (void *data_ptr) {

	if (data_ptr) free (data_ptr);

}

static void test_http_request_set_custom_data (void) {

	HttpRequest *request = test_http_request_new ();

	CustomData *custom_data = (CustomData *) malloc (sizeof (CustomData));

	http_request_set_custom_data (request, custom_data);

	test_check_ptr (http_request_get_custom_data (request));

	http_request_set_delete_custom_data (request, delete_custom_data);

	test_check_ptr_eq (request->delete_custom_data, delete_custom_data);

	http_request_delete (request);

}

static void test_http_request_set_delete_custom_data (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_default_delete_custom_data (request);

	test_check_ptr_eq (request->delete_custom_data, free);

	http_request_delete (request);

}

static void test_http_request_set_base_dirname (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_dirname (request, "%s", base_dir);
	
	test_check_str_eq (http_request_get_dirname (request), base_dir, NULL);
	test_check_str_len (http_request_get_dirname (request), strlen (base_dir), NULL);

	http_request_delete (request);

}

static void test_http_request_set_basic_dirname (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_dirname (request, "%s", basic_dir);
	
	test_check_str_eq (http_request_get_dirname (request), basic_dir, NULL);
	test_check_str_len (http_request_get_dirname (request), strlen (basic_dir), NULL);

	http_request_delete (request);

}

static void test_http_request_set_full_dirname (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_dirname (request, "%s/%s", base_dir, basic_dir);
	
	test_check_str_eq (http_request_get_dirname (request), full_dir, NULL);
	test_check_str_len (http_request_get_dirname (request), strlen (full_dir), NULL);

	http_request_delete (request);

}

void http_tests_requests (void) {

	(void) printf ("Testing HTTP requests...\n");

	// main
	test_http_request_create ();
	test_http_request_set_decoded_data ();
	test_http_request_set_default_delete_decoded_data ();
	test_http_request_set_custom_data ();
	test_http_request_set_delete_custom_data ();

	// dirname
	test_http_request_set_base_dirname ();
	test_http_request_set_basic_dirname ();
	test_http_request_set_full_dirname ();

	(void) printf ("Done!\n");

}
