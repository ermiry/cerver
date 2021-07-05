#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/origin.h>
#include <cerver/http/response.h>

#include "../test.h"

#define TEST_HEADER_LEN		256

static char json_header[TEST_HEADER_LEN] = { 0 };
static char header_content_len[TEST_HEADER_LEN] = { 0 };
static char header_content_type[TEST_HEADER_LEN] = { 0 };

static const char *content_type_header = { "Content-Type: application/json\r\n" };
static const char *content_length_header = { "Content-Length: 128\r\n" };
static const char *json_data = { "{ \"oki\": \"doki\" }" };

// cors
static const char *domain = { "https://ermiry.com" };
static const char *allow_origin_header = { "Access-Control-Allow-Origin: https://ermiry.com\r\n" };
static const char *allow_credentials_header = { "Access-Control-Allow-Credentials: true\r\n" };
static const char *allow_single_method_header = { "Access-Control-Allow-Methods: GET\r\n" };
static const char *allow_multiple_methods_header = { "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n" };

static HttpCerver *http_cerver = NULL;

static void test_http_responses_init (void) {

	(void) snprintf (
		json_header, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nContent-Length: 128\r\nContent-Type: application/json\r\n\r\n",
		CERVER_VERSION
	);

	(void) snprintf (
		header_content_len, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nContent-Length: 128\r\n\r\n",
		CERVER_VERSION
	);

	(void) snprintf (
		header_content_type, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\n\r\n",
		CERVER_VERSION
	);

	http_cerver = http_cerver_new ();

	test_check_ptr (http_cerver);

	test_check_unsigned_eq (
		http_responses_init (http_cerver), 0, "Failed to init HTTP responses!"
	);

}

static void test_http_responses_end (void) {

	http_responses_end ();

	http_cerver_delete (http_cerver);

}

#pragma region main

static HttpResponse *test_http_response_new (void) {

	HttpResponse *response = (HttpResponse *) http_response_new ();

	test_check_ptr (response);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 0, NULL);

	for (unsigned int i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (response->headers[i]);
	
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	return response;

}

static void test_http_response_reset (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_reset (response);

	test_check_ptr (response);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 0, NULL);

	for (unsigned int i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (response->headers[i]);
	
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_set_status (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_status (response, HTTP_STATUS_BAD_REQUEST);
	test_check_unsigned_eq (response->status, HTTP_STATUS_BAD_REQUEST, NULL);

	http_response_set_status (response, HTTP_STATUS_INTERNAL_SERVER_ERROR);
	test_check_unsigned_eq (response->status, HTTP_STATUS_INTERNAL_SERVER_ERROR, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region headers

static void test_http_response_set_header (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_header (response, strdup (json_header), strlen (json_header));
	test_check_ptr (response->header);
	test_check_str_eq (response->header, json_header, NULL);
	test_check_str_len (response->header, strlen (json_header), NULL);

	http_response_delete (response);

}

static void test_http_response_add_header (void) {

	u8 result = 0;
	HttpResponse *response = test_http_response_new ();

	// add one header
	result = http_response_add_header (
		response, HTTP_HEADER_CONTENT_TYPE,
		http_content_type_mime (HTTP_CONTENT_TYPE_JSON)
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_TYPE]);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_TYPE]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_TYPE]->str, content_type_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_TYPE]->str, strlen (content_type_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// add another header
	result = http_response_add_header (
		response, HTTP_HEADER_CONTENT_LENGTH, "128"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 2, NULL);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_LENGTH]);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str, content_length_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str, strlen (content_length_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, json_header, NULL);
	test_check_unsigned_eq (response->header_len, strlen (json_header), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_content_type_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_content_type_header (
		response, HTTP_CONTENT_TYPE_JSON
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_TYPE]);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_TYPE]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_TYPE]->str, content_type_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_TYPE]->str, strlen (content_type_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, header_content_type, NULL);
	test_check_unsigned_eq (response->header_len, strlen (header_content_type), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_content_length_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_content_length_header (
		response, 128
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_LENGTH]);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str, content_length_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str, strlen (content_length_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, header_content_len, NULL);
	test_check_unsigned_eq (response->header_len, strlen (header_content_len), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_json_headers (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_add_json_headers (
		response, 128
	);

	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 2, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_TYPE]);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_TYPE]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_TYPE]->str, content_type_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_TYPE]->str, strlen (content_type_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_LENGTH]);
	test_check_ptr (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str, content_length_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_LENGTH]->str, strlen (content_length_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// compile header
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_str_eq (response->header, json_header, NULL);
	test_check_unsigned_eq (response->header_len, strlen (json_header), NULL);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_false (response->data_ref);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region cors

static void test_http_response_add_cors_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_header (
		response, domain
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]);
	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]->str, allow_origin_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]->str, strlen (allow_origin_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_header_from_origin (void) {

	HttpResponse *response = test_http_response_new ();

	HttpOrigin origin = { 0 };
	http_origin_init (&origin, domain);

	u8 result = http_response_add_cors_header_from_origin (
		response, &origin
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]);
	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]->str, allow_origin_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN]->str, strlen (allow_origin_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_allow_credentials_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_allow_credentials_header (
		response
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS]);
	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS]->str, allow_credentials_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS]->str, strlen (allow_credentials_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_allow_methods_header_single (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_allow_methods_header (
		response, "GET"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]);
	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]->str, allow_single_method_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]->str, strlen (allow_single_method_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_allow_methods_header_multiple (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_cors_allow_methods_header (
		response, "GET, HEAD, OPTIONS"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]);
	test_check_ptr (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]->str);
	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]->str, allow_multiple_methods_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS]->str, strlen (allow_multiple_methods_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

#pragma endregion

#pragma region data

static void test_http_response_set_data (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_data (response, strdup (json_data), strlen (json_data));
	test_check_ptr (response->data);
	test_check_unsigned_eq (response->data_len, strlen (json_data), NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	http_response_delete (response);

}

static void test_http_response_set_data_ref (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_data_ref (response, (void *) json_data, strlen (json_data));
	test_check_ptr (response->data);
	test_check_unsigned_eq (response->data_len, strlen (json_data), NULL);
	test_check_bool_eq (response->data_ref, true, NULL);

	http_response_delete (response);

}

#pragma endregion

void http_tests_responses (void) {

	(void) printf ("Testing HTTP responses...\n");

	test_http_responses_init ();

	// main
	test_http_response_reset ();
	test_http_response_set_status ();

	// headers
	test_http_response_set_header ();
	test_http_response_add_header ();
	test_http_response_add_content_type_header ();
	test_http_response_add_content_length_header ();
	test_http_response_add_json_headers ();

	// cors
	test_http_response_add_cors_header ();
	test_http_response_add_cors_header_from_origin ();
	test_http_response_add_cors_allow_credentials_header ();
	test_http_response_add_cors_allow_methods_header_single ();
	test_http_response_add_cors_allow_methods_header_multiple ();

	// data
	test_http_response_set_data ();
	test_http_response_set_data_ref ();

	test_http_responses_end ();

	(void) printf ("Done!\n");

}
