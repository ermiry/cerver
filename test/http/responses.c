#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <math.h>

#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/origin.h>
#include <cerver/http/response.h>

#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>

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
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nConnection: close\r\nContent-Length: 128\r\nContent-Type: application/json\r\n\r\n",
		CERVER_VERSION
	);

	(void) snprintf (
		header_content_len, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nConnection: close\r\nContent-Length: 128\r\n\r\n",
		CERVER_VERSION
	);

	(void) snprintf (
		header_content_type, TEST_HEADER_LEN,
		"HTTP/1.1 200 OK\r\nServer: Cerver/%s\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n",
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

	HttpHeader *header = NULL;
	for (unsigned int i = 0; i < HTTP_HEADERS_SIZE; i++) {
		header = &response->headers[i];

		test_check_int_eq (header->len, 0, NULL);
		test_check_str_empty (header->value);
	}

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

	HttpHeader *header = NULL;
	for (unsigned int i = 0; i < HTTP_HEADERS_SIZE; i++) {
		header = &response->headers[i];

		test_check_int_eq (header->len, 0, NULL);
		test_check_str_empty (header->value);
	}

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
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_TYPE].value, content_type_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_TYPE].value, strlen (content_type_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_CONTENT_TYPE].len, (int) strlen (content_type_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	// add another header
	result = http_response_add_header (
		response, HTTP_HEADER_CONTENT_LENGTH, "128"
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 2, NULL);
	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH].value, content_length_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_LENGTH].value, strlen (content_length_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH].len, (int) strlen (content_length_header), NULL);
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

#define TEST_HEADER_LONG_VALUE_SIZE			300
#define TEST_HEADER_FORMATTED_VALUE_SIZE	200

static const char *location_header = { "Location: /short\r\n" };

// a header that does not fit in its 256 bytes is rejected and the one that the response had is kept:
// its length was the one of the whole value, so the bytes after it were sent to the client
static void test_http_response_add_header_too_long (void) {

	char long_value[TEST_HEADER_LONG_VALUE_SIZE] = { 0 };
	(void) memset (long_value, 'v', TEST_HEADER_LONG_VALUE_SIZE - 1);

	char formatted_value[TEST_HEADER_FORMATTED_VALUE_SIZE] = { 0 };
	(void) memset (formatted_value, 'f', TEST_HEADER_FORMATTED_VALUE_SIZE - 1);

	HttpResponse *response = test_http_response_new ();

	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, long_value), 1, NULL);
	test_check_unsigned_eq (response->n_headers, 0, NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_LOCATION].len, 0, NULL);
	test_check_str_empty (response->headers[HTTP_HEADER_LOCATION].value);

	// the header that the response had is kept
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, "/short"), 0, NULL);
	test_check_unsigned_eq (http_response_add_header (response, HTTP_HEADER_LOCATION, long_value), 1, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);
	test_check_str_eq (response->headers[HTTP_HEADER_LOCATION].value, location_header, NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_LOCATION].len, (int) strlen (location_header), NULL);

	// a formatted value that would be cut is not a header
	test_check_unsigned_eq (
		http_response_add_custom_header (response, HTTP_HEADER_ETAG, "%s", formatted_value), 1, NULL
	);

	test_check_unsigned_eq (response->n_headers, 1, NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_ETAG].len, 0, NULL);
	test_check_str_empty (response->headers[HTTP_HEADER_ETAG].value);

	// the compiled header is text until its end, with the header that was kept
	http_response_compile_header (response);

	test_check_ptr (response->header);
	test_check_unsigned_eq (response->header_len, strlen ((const char *) response->header), NULL);
	test_check_ptr (strstr ((const char *) response->header, location_header));

	http_response_delete (response);

	// the headers of the cerver
	const u8 n_response_headers = http_cerver->n_response_headers;

	test_check_unsigned_eq (
		http_cerver_add_responses_header (http_cerver, HTTP_HEADER_LOCATION, long_value), 1, NULL
	);

	test_check_unsigned_eq (http_cerver->n_response_headers, n_response_headers, NULL);
	test_check_int_eq (http_cerver->response_headers[HTTP_HEADER_LOCATION].len, 0, NULL);

}

static void test_http_response_add_content_type_header (void) {

	HttpResponse *response = test_http_response_new ();

	u8 result = http_response_add_content_type_header (
		response, HTTP_CONTENT_TYPE_JSON
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_TYPE].value, content_type_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_TYPE].value, strlen (content_type_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_CONTENT_TYPE].len, (int) strlen (content_type_header), NULL);
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

	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH].value, content_length_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_LENGTH].value, strlen (content_length_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH].len, (int) strlen (content_length_header), NULL);
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

	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_TYPE].value, content_type_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_TYPE].value, strlen (content_type_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_CONTENT_TYPE].len, (int) strlen (content_type_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	test_check_str_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH].value, content_length_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_CONTENT_LENGTH].value, strlen (content_length_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_CONTENT_LENGTH].len, (int) strlen (content_length_header), NULL);
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

	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN].value, allow_origin_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN].value, strlen (allow_origin_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN].len, (int) strlen (allow_origin_header), NULL);
	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);

	http_response_delete (response);

}

static void test_http_response_add_cors_header_from_origin (void) {

	HttpResponse *response = test_http_response_new ();

	HttpOrigin origin = CERVER_ZERO_INITIALIZER;
	http_origin_init (&origin, domain);

	u8 result = http_response_add_cors_header_from_origin (
		response, &origin
	);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 1, NULL);

	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN].value, allow_origin_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN].value, strlen (allow_origin_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN].len, (int) strlen (allow_origin_header), NULL);
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

	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS].value, allow_credentials_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS].value, strlen (allow_credentials_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS].len, (int) strlen (allow_credentials_header), NULL);
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

	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS].value, allow_single_method_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS].value, strlen (allow_single_method_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS].len, (int) strlen (allow_single_method_header), NULL);
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

	test_check_str_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS].value, allow_multiple_methods_header, NULL);
	test_check_str_len (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS].value, strlen (allow_multiple_methods_header), NULL);
	test_check_int_eq (response->headers[HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS].len, (int) strlen (allow_multiple_methods_header), NULL);
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

#pragma region json

// the body of a compiled response, after its header
static const char *test_http_response_json_body (const HttpResponse *response) {

	test_check_ptr (response);
	test_check_ptr (response->data);
	test_check_unsigned_eq (response->data_len, strlen ((const char *) response->data), NULL);

	return (const char *) response->data;

}

// a message with quotes, a backslash and a control character is still valid json
static void test_http_response_json_escaped (void) {

	const char *message = "near '\"admin\": true' \\ \n";

	HttpResponse *response = http_response_json_error (HTTP_STATUS_BAD_REQUEST, message);
	const char *body = test_http_response_json_body (response);

	json_error_t error;
	json_t *json = json_loads (body, 0, &error);
	test_check_ptr (json);
	test_check_unsigned_eq (json_object_size (json), 1, "the message added keys to the json");
	test_check_str_eq (json_string_value (json_object_get (json, "error")), message, NULL);
	json_decref (json);

	http_response_delete (response);

	response = http_response_create_json_key_value (HTTP_STATUS_OK, "key\"", "value\"");
	json = json_loads (test_http_response_json_body (response), 0, &error);
	test_check_ptr (json);
	test_check_str_eq (json_string_value (json_object_get (json, "key\"")), "value\"", NULL);
	json_decref (json);
	http_response_delete (response);

	// a message that is not UTF-8 can not be json
	test_check_null_ptr (http_response_json_msg (HTTP_STATUS_OK, "\xff"));

}

// the bodies of the values keep the separators that they had
static void test_http_response_json_values (void) {

	HttpResponse *response = http_response_json_int_value (HTTP_STATUS_OK, "value", 7);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": 7}", NULL);
	http_response_delete (response);

	response = http_response_json_large_int_value (HTTP_STATUS_OK, "value", 4102444800L);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": 4102444800}", NULL);
	http_response_delete (response);

	// a real is not written with %f nor with the locale
	response = http_response_json_real_value (HTTP_STATUS_OK, "value", 1.5);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": 1.5}", NULL);
	http_response_delete (response);

	response = http_response_json_bool_value (HTTP_STATUS_OK, "value", true);
	test_check_str_eq (test_http_response_json_body (response), "{\"value\": true}", NULL);
	http_response_delete (response);

	response = http_response_json_msg (HTTP_STATUS_OK, "oki doki");
	test_check_str_eq (test_http_response_json_body (response), "{\"msg\": \"oki doki\"}", NULL);
	http_response_delete (response);

	// NaN is not json
	test_check_null_ptr (http_response_json_real_value (HTTP_STATUS_OK, "value", (double) NAN));

}

// the helpers freed the body that the response still had, a second compile read it
static void test_http_response_json_owned (void) {

	HttpResponse *responses[] = {
		http_response_json_int_value (HTTP_STATUS_OK, "value", 7),
		http_response_json_large_int_value (HTTP_STATUS_OK, "value", 7),
		http_response_json_real_value (HTTP_STATUS_OK, "value", 7.5),
		http_response_json_bool_value (HTTP_STATUS_OK, "value", false)
	};

	for (size_t i = 0; i < (sizeof (responses) / sizeof (responses[0])); i++) {
		test_check_ptr (responses[i]);
		test_check_bool_eq (responses[i]->data_ref, false, NULL);

		const char *body = test_http_response_json_body (responses[i]);
		test_check (strstr (body, "\"value\"") != NULL, body);

		test_check_unsigned_eq (http_response_compile (responses[i]), 0, NULL);
		test_check (strstr ((const char *) responses[i]->res, "\"value\"") != NULL, NULL);

		http_response_delete (responses[i]);
	}

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
	test_http_response_add_header_too_long ();
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

	// json
	test_http_response_json_escaped ();
	test_http_response_json_values ();
	test_http_response_json_owned ();

	test_http_responses_end ();

	(void) printf ("Done!\n");

}
