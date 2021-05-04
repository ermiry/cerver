#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/response.h>

#include "../test.h"

#define TEST_HEADER_LEN		256

static char header[TEST_HEADER_LEN] = { 0 };
static char header_content_len[TEST_HEADER_LEN] = { 0 };
static char header_content_type[TEST_HEADER_LEN] = { 0 };

static const char *content_type_header = { "Content-Type: application/json\r\n" };
static const char *content_length_header = { "Content-Length: 128\r\n" };
static const char *json_data = { "{ \"oki\": \"doki\" }" };

static HttpCerver *http_cerver = NULL;

static void test_http_responses_init (void) {

	(void) snprintf (
		header, TEST_HEADER_LEN,
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

static void test_http_response_set_header (void) {

	HttpResponse *response = test_http_response_new ();

	http_response_set_header (response, strdup (header), strlen (header));
	test_check_ptr (response->header);
	test_check_str_eq (response->header, header, NULL);
	test_check_str_len (response->header, strlen (header), NULL);

	http_response_delete (response);

}

static void test_http_response_add_header (void) {

	u8 result = 0;
	HttpResponse *response = test_http_response_new ();

	// add one header
	result = http_response_add_header (
		response, HTTP_HEADER_CONTENT_TYPE,
		http_content_type_description (HTTP_CONTENT_TYPE_JSON)
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
	test_check_str_eq (response->header, header, NULL);
	test_check_unsigned_eq (response->header_len, strlen (header), NULL);

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

void http_tests_responses (void) {

	(void) printf ("Testing HTTP responses...\n");

	test_http_responses_init ();

	test_http_response_reset ();

	test_http_response_set_status ();

	test_http_response_set_header ();

	test_http_response_add_header ();

	test_http_response_add_content_type_header ();

	test_http_response_add_content_length_header ();

	test_http_response_set_data ();

	test_http_response_set_data_ref ();

	test_http_responses_end ();

	(void) printf ("Done!\n");

}