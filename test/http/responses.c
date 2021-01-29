#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/response.h>

#include "../test.h"

static const char *header = { "HTTP/1.1 200 OK\r\nServer: Cerver/2.0\r\nContent-Type: application/json\r\nContent-Length: 128\r\n\r\n" };

static HttpResponse *test_http_response_new (void) {

	HttpResponse *response = (HttpResponse *) http_response_new ();

	test_check_ptr (response);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 0, NULL);

	for (unsigned int i = 0; i < HTTP_REQUEST_HEADERS_SIZE; i++)
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

	for (unsigned int i = 0; i < HTTP_REQUEST_HEADERS_SIZE; i++)
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

	HttpResponse *response = test_http_response_new ();

	// TODO:

	http_response_delete (response);

}

static void test_http_response_add_content_type_header (void) {

	HttpResponse *response = test_http_response_new ();

	// TODO:

	http_response_delete (response);

}

static void test_http_response_add_content_length_header (void) {

	HttpResponse *response = test_http_response_new ();

	// TODO:

	http_response_delete (response);

}

static void test_http_response_set_data (void) {

	HttpResponse *response = test_http_response_new ();

	// TODO:

	http_response_delete (response);

}

static void test_http_response_set_data_ref (void) {

	HttpResponse *response = test_http_response_new ();

	// TODO:

	http_response_delete (response);

}

void http_tests_responses (void) {

	(void) printf ("Testing HTTP responses...\n");

	test_http_response_reset ();

	test_http_response_set_status ();

	test_http_response_set_header ();

	test_http_response_add_header ();

	test_http_response_add_content_type_header ();

	test_http_response_add_content_length_header ();

	test_http_response_set_data ();

	test_http_response_set_data_ref ();

	(void) printf ("Done!\n");

}