#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/method.h>
#include <cerver/http/request.h>

#include <cerver/http/json/json.h>

#include "data.h"

#include "../test.h"

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
	test_check_null_ptr (request->params);
	test_check_null_ptr (request->params_names);

	test_check_unsigned_eq (request->next_header, HTTP_HEADER_UNDEFINED, NULL);
	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (request->headers[i]);

	test_check_unsigned_eq (request->n_custom_headers, 0, NULL);
	test_check_null_ptr (request->current_custom_header);
	test_check_null_ptr (request->custom_headers);

	test_check_null_ptr (request->own_arena);

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
	for (u8 i = 0; i < HTTP_TREE_DEFAULT_MAX_PARAMS; i++)
		test_check_null_ptr (http_request_get_param_at_idx (request, i));

	test_check_null_ptr (http_request_get_param (request, "id"));
	test_check_null_ptr (http_request_get_param_name_at_idx (request, 0));

	// a header outside the table is never read
	test_check_null_ptr (http_request_get_header (request, (const http_header) HTTP_HEADERS_SIZE));

	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (http_request_get_header (request, (const http_header) i));
	
	test_check_null_ptr (request->current_custom_header);
	test_check_null_ptr (request->custom_headers);

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

static void test_http_request_set_current_custom_header (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_current_custom_header (request, "Custom-Header");

	http_request_delete (request);

}

static void test_http_request_set_current_custom_header_value (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_current_custom_header (request, "Custom-Header");
	http_request_set_current_custom_header_value (request, "Hola");

	http_request_delete (request);

}

static void test_http_request_get_custom_headers_count (void) {

	HttpRequest *request = test_http_request_new ();

	http_request_set_current_custom_header (request, "Custom-Header");
	http_request_set_current_custom_header_value (request, "Hola");

	http_request_set_current_custom_header (request, "Another-Header");
	http_request_set_current_custom_header_value (request, "Adios");

	test_check_unsigned_eq (
		http_request_get_custom_headers_count (request), 2, NULL
	);

	http_request_delete (request);

}

static void test_http_request_get_custom_header (void) {

	const char *custom_header = "Custom-Header";
	const char *custom_header_value = "Hola";

	const char *another_header = "Another-Header";
	const char *another_header_value = "Adios";

	HttpRequest *request = test_http_request_new ();

	http_request_set_current_custom_header (request, custom_header);
	http_request_set_current_custom_header_value (request, custom_header_value);

	test_check_str_eq (
		http_request_get_custom_header (request, custom_header), custom_header_value, NULL
	);

	test_check_null_ptr (http_request_get_custom_header (request, another_header));

	http_request_set_current_custom_header (request, another_header);
	http_request_set_current_custom_header_value (request, another_header_value);

	test_check_str_eq (
		http_request_get_custom_header (request, another_header), another_header_value, NULL
	);

	// the name is compared without case, like the parser does when it joins repeated headers
	test_check_str_eq (
		http_request_get_custom_header (request, "custom-header"), custom_header_value, NULL
	);

	test_check_str_eq (
		http_request_get_custom_header (request, "ANOTHER-HEADER"), another_header_value, NULL
	);

	http_request_delete (request);

}

// a header that became part of the HTTP headers map is still found by its name
static void test_http_request_get_custom_header_known (void) {

	HttpRequest *request = test_http_request_new ();

	request->headers[HTTP_HEADER_REQUEST_ID] = str_new ("request-id");
	request->headers[HTTP_HEADER_FORWARDED_FOR] = str_new ("10.0.0.1");

	test_check_str_eq (http_request_get_custom_header (request, "X-Request-Id"), "request-id", NULL);
	test_check_str_eq (http_request_get_custom_header (request, "x-forwarded-for"), "10.0.0.1", NULL);
	test_check_null_ptr (http_request_get_custom_header (request, "Transfer-Encoding"));

	http_request_delete (request);

}

// a param is a slice of the url, percent-decoded when it is requested, and found by its name
static void test_http_request_params (void) {

	HttpRequest *request = test_http_request_new ();

	static const char *url = "/users/john%20doe/items/a+b%2Fc/%zz";
	static char *names[] = { (char *) "user", (char *) "item", NULL };

	HttpRequestParam params[3];
	params[0].value = url + 7;
	params[0].len = 10;
	params[0].decoded = NULL;
	params[1].value = url + 24;
	params[1].len = 7;
	params[1].decoded = NULL;
	params[2].value = url + 32;
	params[2].len = 3;
	params[2].decoded = NULL;

	request->params = params;
	request->n_params = 3;
	request->params_names = names;

	test_check_str_eq (http_request_get_param_at_idx (request, 0)->str, "john doe", NULL);
	test_check_str_eq (http_request_get_param (request, "item")->str, "a+b/c", NULL);

	// an invalid escape is kept as it is
	test_check_str_eq (http_request_get_param_at_idx (request, 2)->str, "%zz", NULL);

	// the decoded value is created once
	test_check_ptr_eq (http_request_get_param_at_idx (request, 0), http_request_get_param (request, "user"));

	test_check_null_ptr (http_request_get_param (request, "missing"));
	test_check_null_ptr (http_request_get_param_at_idx (request, 3));
	test_check_str_eq (http_request_get_param_name_at_idx (request, 1), "item", NULL);
	test_check_null_ptr (http_request_get_param_name_at_idx (request, 2));

	// the request does not own the params array
	request->params = NULL;
	request->n_params = 0;

	http_request_delete (request);

}

// the methods have the values of the HTTP parser (llhttp): PATCH is 28, 34 is PRI
static void test_http_request_methods (void) {

	test_check_str_eq (http_request_method_str (REQUEST_METHOD_PATCH), "PATCH", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_GET), "GET", NULL);
	test_check_str_eq (http_request_method_str (REQUEST_METHOD_COPY), "COPY", NULL);
	test_check_str_eq (http_request_method_str ((RequestMethod) 34), "Undefined", NULL);

	test_check_true (http_request_method_is_valid (REQUEST_METHOD_PATCH));
	test_check_true (http_request_method_is_valid (REQUEST_METHOD_TRACE));
	test_check_true (http_request_method_is_valid (REQUEST_METHOD_QUERY));
	test_check_false (http_request_method_is_valid ((RequestMethod) 34));
	test_check_false (http_request_method_is_valid (REQUEST_METHOD_UNDEFINED));

	test_check_int_eq ((int) REQUEST_METHOD_PATCH, 28, NULL);

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

static bool test_http_request_id_valid (const char *id) {

	return http_request_id_is_valid (id, strlen (id));

}

// "xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx"
static void test_http_request_id_check_uuid (const char *id) {

	test_check_unsigned_eq (strlen (id), HTTP_REQUEST_ID_UUID_LENGTH, NULL);

	for (size_t i = 0; i < HTTP_REQUEST_ID_UUID_LENGTH; i++) {
		if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) test_check (id[i] == '-', NULL);
		else test_check (strchr ("0123456789abcdef", id[i]) != NULL, NULL);
	}

	test_check (id[14] == '4', "the UUID is not a version 4");
	test_check (strchr ("89ab", id[19]) != NULL, "the UUID does not have the RFC 4122 variant");

}

static void test_http_request_id (void) {

	test_check_true (test_http_request_id_valid ("abc-123_DEF.456"));
	test_check_true (test_http_request_id_valid ("7f9c2ba4e88f827d616045507605853e"));

	char longest[HTTP_REQUEST_ID_SIZE + 1] = { 0 };
	(void) memset (longest, 'a', HTTP_REQUEST_ID_LENGTH);
	test_check_true (test_http_request_id_valid (longest));
	longest[HTTP_REQUEST_ID_LENGTH] = 'a';
	test_check_false (test_http_request_id_valid (longest));

	test_check_false (test_http_request_id_valid (""));
	test_check_false (test_http_request_id_valid ("with space"));
	test_check_false (test_http_request_id_valid ("new\r\nline"));
	test_check_false (test_http_request_id_valid ("quote\""));
	test_check_false (http_request_id_is_valid ("ab\0cd", 5));
	test_check_false (http_request_id_is_valid (NULL, 4));

	// a new id is a random UUID v4
	char first[HTTP_REQUEST_ID_SIZE] = { 0 };
	char second[HTTP_REQUEST_ID_SIZE] = { 0 };
	test_check_unsigned_eq (http_request_id_generate (first), 0, NULL);
	test_check_unsigned_eq (http_request_id_generate (second), 0, NULL);
	test_http_request_id_check_uuid (first);
	test_http_request_id_check_uuid (second);
	test_check (strcmp (first, second), "two ids are the same");
	test_check_unsigned_eq (http_request_id_generate (NULL), 1, NULL);

	// the request keeps a valid X-Request-Id
	HttpRequest *request = http_request_new ();
	test_check_str_eq (http_request_get_id (request), "", NULL);

	request->headers[HTTP_HEADER_REQUEST_ID] = str_new ("nginx-0123456789abcdef");
	http_request_resolve_id (request);
	test_check_str_eq (http_request_get_id (request), "nginx-0123456789abcdef", NULL);

	// and replaces an invalid one
	str_delete (request->headers[HTTP_HEADER_REQUEST_ID]);
	request->headers[HTTP_HEADER_REQUEST_ID] = str_new ("bad id");
	http_request_resolve_id (request);
	test_http_request_id_check_uuid (http_request_get_id (request));

	str_delete (request->headers[HTTP_HEADER_REQUEST_ID]);
	request->headers[HTTP_HEADER_REQUEST_ID] = NULL;
	http_request_resolve_id (request);
	test_http_request_id_check_uuid (http_request_get_id (request));

	test_check_str_eq (http_request_get_client_ip (request), "", NULL);

	http_request_delete (request);

}

void http_tests_requests (void) {

	(void) printf ("Testing HTTP requests...\n");

	test_http_request_id ();

	// main
	test_http_request_create ();

	test_http_request_set_current_custom_header ();
	test_http_request_set_current_custom_header_value ();
	test_http_request_get_custom_headers_count ();
	test_http_request_get_custom_header ();
	test_http_request_get_custom_header_known ();
	test_http_request_params ();
	test_http_request_methods ();

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
