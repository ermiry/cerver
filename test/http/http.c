#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>

#include "http.h"

#include "../test.h"

const char *private_key = { "./test/http/keys/key.key" };
const char *public_key = { "./test/http/keys/key.pub" };

const char *base_token = { "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MDc1NTU2ODYsImlkIjoiMTYwNzU1NTY4NiIsIm5hbWUiOiJFcmljayBTYWxhcyIsInJvbGUiOiJjb21tb24iLCJ1c2VybmFtZSI6ImVybWlyeSJ9.QIEZXo8vkthUPQzjozQNJ8P5ZTxbA6w2OWjeVplWVB2cs1ZySTRMvZ6Dh8dDe-CAHH4P9zcJumJIR0LWxZQ63O61c-cKZuupaPiI9qQaqql30oBnywznWCDkkXgkoTh683fSxoFR71Gzqp5e41mX-KxopH-Kh4Bz3eM3d27p-8tKOUGUlVk1AgWfGU2LFfTu1ZLkVYwo4ceGxHdntS1C_6IiutG8TLFjuKixgujIhK0ireG1cfAs7uvtGhWLXokLpvTbIrrjKSKLEjCcPh_yPpWeay0Y4yV1e5zSQKHiG7ry0D3qDWJcfHtjNLjAFb93TnpXtoXhftq_uL4d7BV16Y9ssQ5MpNoEh3bxHBgaCHOCGuCAVJfdDsRBT5C60_jme7S1utQ4qkpA8YbAoYKi58yGkHxnnYYbaYFQYSykbsYPFZ-4dieFlaIc5-m-vymwG--AMjfhu8Sdm0V0xA3Vq_9FzUIfXvo32-k0eH4QGPX6W8-FinR_Lj-Zj29mXfgALpTlF-m7Sbo3hVebnpwAFKZR495UBLL8B8u3MHB9XQxX6z4fHAxv5Nkftr4ShGRWwI_yVD73sLq44V9FEBQksoXLEBm5RqRC8qvhoxOIzbEYokd6MWLak1sZRUfIIqu65fERA5sMjs9JRnlYu7twP3Fk_VPy-tZBXXA7KTc3lkE" };
const char *bearer_token = { "Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MDc1NTU2ODYsImlkIjoiMTYwNzU1NTY4NiIsIm5hbWUiOiJFcmljayBTYWxhcyIsInJvbGUiOiJjb21tb24iLCJ1c2VybmFtZSI6ImVybWlyeSJ9.QIEZXo8vkthUPQzjozQNJ8P5ZTxbA6w2OWjeVplWVB2cs1ZySTRMvZ6Dh8dDe-CAHH4P9zcJumJIR0LWxZQ63O61c-cKZuupaPiI9qQaqql30oBnywznWCDkkXgkoTh683fSxoFR71Gzqp5e41mX-KxopH-Kh4Bz3eM3d27p-8tKOUGUlVk1AgWfGU2LFfTu1ZLkVYwo4ceGxHdntS1C_6IiutG8TLFjuKixgujIhK0ireG1cfAs7uvtGhWLXokLpvTbIrrjKSKLEjCcPh_yPpWeay0Y4yV1e5zSQKHiG7ry0D3qDWJcfHtjNLjAFb93TnpXtoXhftq_uL4d7BV16Y9ssQ5MpNoEh3bxHBgaCHOCGuCAVJfdDsRBT5C60_jme7S1utQ4qkpA8YbAoYKi58yGkHxnnYYbaYFQYSykbsYPFZ-4dieFlaIc5-m-vymwG--AMjfhu8Sdm0V0xA3Vq_9FzUIfXvo32-k0eH4QGPX6W8-FinR_Lj-Zj29mXfgALpTlF-m7Sbo3hVebnpwAFKZR495UBLL8B8u3MHB9XQxX6z4fHAxv5Nkftr4ShGRWwI_yVD73sLq44V9FEBQksoXLEBm5RqRC8qvhoxOIzbEYokd6MWLak1sZRUfIIqu65fERA5sMjs9JRnlYu7twP3Fk_VPy-tZBXXA7KTc3lkE" };

static const char *ermiry_com = { "https://ermiry.com" };

#pragma region main

static void test_http_cerver_new (void) {

	HttpCerver *http_cerver = http_cerver_new ();

	test_check_ptr (http_cerver);

	test_check_null_ptr (http_cerver->static_paths);

	test_check_null_ptr (http_cerver->routes);

	test_check_null_ptr (http_cerver->main_route);

	test_check_null_ptr (http_cerver->default_handler);

	test_check_false (http_cerver->not_found_handler);
	test_check_null_ptr (http_cerver->not_found);

	test_check_int_eq (http_cerver->uploads_path_len, 0, NULL);

	test_check_unsigned_eq (http_cerver->uploads_file_mode, HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_filename_generator);

	test_check_unsigned_eq (http_cerver->uploads_dir_mode, HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_dirname_generator);

	test_check (http_cerver->uploads_delete_when_done == HTTP_CERVER_DEFAULT_UPLOADS_DELETE, NULL);

	test_check (http_cerver->jwt_alg == JWT_ALG_NONE, NULL);

	test_check_null_ptr (http_cerver->jwt_opt_key_name);
	test_check_null_ptr (http_cerver->jwt_private_key);

	test_check_null_ptr (http_cerver->jwt_opt_pub_key_name);
	test_check_null_ptr (http_cerver->jwt_public_key);

	HttpOrigin *origin = NULL;
	test_check_unsigned_eq (http_cerver->n_origins, 0, NULL);
	for (u8 i = 0; i < HTTP_ORIGINS_SIZE; i++) {
		origin = &http_cerver->origins_whitelist[i];
		test_check_int_eq (origin->len, 0, NULL);
		test_check_str_empty (origin->value);
	}

	test_check_unsigned_eq (http_cerver->n_response_headers, 0, NULL);
	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (http_cerver->response_headers[i]);

	test_check_unsigned_eq (http_cerver->n_incompleted_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_unhandled_requests, 0, NULL);

	test_check_unsigned_eq (http_cerver->n_catch_all_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_failed_auth_requests, 0, NULL);

	test_check (http_cerver->enable_admin_routes == HTTP_CERVER_DEFAULT_ENABLE_ADMIN, NULL);
	
	test_check (http_cerver->enable_admin_routes_auth == HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH, NULL);
	test_check (http_cerver->enable_admin_head_handlers == HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS, NULL);
	test_check (http_cerver->enable_admin_options_handlers == HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS, NULL);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);
	test_check_null_ptr (http_cerver->admin_decode_data);
	test_check_null_ptr (http_cerver->admin_delete_decoded_data);
	test_check_null_ptr (http_cerver->admin_auth_handler);

	test_check (http_cerver->enable_admin_cors_headers == HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS, NULL);
	test_check_int_eq (http_cerver->admin_origin.len, 0, NULL);
	test_check_str_empty (http_cerver->admin_origin.value);

	test_check_null_ptr (http_cerver->admin_file_systems_stats);
	test_check_null_ptr (http_cerver->admin_mutex);

	test_check_null_ptr (http_cerver->mutex);

	http_cerver_delete (http_cerver);

}

static HttpCerver *test_http_cerver_create (void) {

	HttpCerver *http_cerver = http_cerver_create (NULL);

	test_check_ptr (http_cerver);

	test_check_ptr (http_cerver->static_paths);

	test_check_ptr (http_cerver->routes);

	test_check_null_ptr (http_cerver->main_route);

	test_check_ptr (http_cerver->default_handler);

	test_check_false (http_cerver->not_found_handler);
	test_check_ptr (http_cerver->not_found);

	test_check_int_eq (http_cerver->uploads_path_len, 0, NULL);

	test_check_unsigned_eq (http_cerver->uploads_file_mode, HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_filename_generator);

	test_check_unsigned_eq (http_cerver->uploads_dir_mode, HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_dirname_generator);

	test_check (http_cerver->uploads_delete_when_done == HTTP_CERVER_DEFAULT_UPLOADS_DELETE, NULL);

	test_check (http_cerver->jwt_alg == JWT_DEFAULT_ALG, NULL);

	test_check_null_ptr (http_cerver->jwt_opt_key_name);
	test_check_null_ptr (http_cerver->jwt_private_key);

	test_check_null_ptr (http_cerver->jwt_opt_pub_key_name);
	test_check_null_ptr (http_cerver->jwt_public_key);

	test_check_unsigned_eq (http_cerver->n_response_headers, 0, NULL);
	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
		test_check_null_ptr (http_cerver->response_headers[i]);

	test_check_unsigned_eq (http_cerver->n_incompleted_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_unhandled_requests, 0, NULL);

	test_check_unsigned_eq (http_cerver->n_catch_all_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_failed_auth_requests, 0, NULL);

	test_check (http_cerver->enable_admin_routes == HTTP_CERVER_DEFAULT_ENABLE_ADMIN, NULL);
	
	test_check (http_cerver->enable_admin_routes_auth == HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH, NULL);
	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);
	test_check_null_ptr (http_cerver->admin_decode_data);
	test_check_null_ptr (http_cerver->admin_delete_decoded_data);
	test_check_null_ptr (http_cerver->admin_auth_handler);

	test_check (http_cerver->enable_admin_cors_headers == HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS, NULL);
	test_check_int_eq (http_cerver->admin_origin.len, 0, NULL);
	test_check_str_empty (http_cerver->admin_origin.value);

	test_check_ptr (http_cerver->admin_file_systems_stats);
	test_check_ptr (http_cerver->admin_mutex);

	test_check_ptr (http_cerver->mutex);

	return http_cerver;

}

#pragma endregion

#pragma region admin

static void test_http_cerver_enable_admin_routes (void) {

	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_routes, true, NULL);

	http_cerver_delete (http_cerver);

}

static void test_http_cerver_enable_admin_head_handlers (void) {

	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);

	http_cerver_enable_admin_head_handlers (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_head_handlers, true, NULL);

	http_cerver_delete (http_cerver);

}

static void test_http_cerver_enable_admin_options_handlers (void) {

	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);

	http_cerver_enable_admin_options_handlers (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_options_handlers, true, NULL);

	http_cerver_delete (http_cerver);

}

static void *user_parse_from_json (void *user_json_ptr) {

	if (user_json_ptr) (void) printf ("user_json_ptr!");

	return NULL;

}

static void test_http_cerver_admin_bearer_auth_set_decode (void) {

	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_BEARER);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_BEARER, NULL);

	http_cerver_admin_routes_auth_set_decode_data (
		http_cerver,
		user_parse_from_json, free
	);

	test_check_ptr_eq (http_cerver->admin_decode_data, user_parse_from_json);
	test_check_ptr_eq (http_cerver->admin_delete_decoded_data, free);

	http_cerver_delete (http_cerver);

}

static void test_http_cerver_admin_bearer_auth_to_json (void) {

	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_BEARER);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_BEARER, NULL);

	http_cerver_admin_routes_auth_decode_to_json (http_cerver);

	test_check_ptr_eq (http_cerver->admin_decode_data, http_decode_data_into_json);
	test_check_ptr_eq (http_cerver->admin_delete_decoded_data, free);

	http_cerver_delete (http_cerver);

}

static unsigned int custom_authentication_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	unsigned int retval = 1;

	http_request_multi_parts_print (request);

	const char *key = http_request_multi_parts_get_value (request, "key");
	if (!strcmp (key, "okay")) {
		cerver_log_success ("Success auth!");

		retval = 0;
	}

	else {
		cerver_log_error ("Failed auth!");
	}

	return retval;

}

static void test_http_cerver_admin_custom_auth (void) {
	
	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_CUSTOM);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);

	http_cerver_admin_routes_set_authentication_handler (http_cerver, custom_authentication_handler);

	test_check_ptr_eq (http_cerver->admin_auth_handler, custom_authentication_handler);

	http_cerver_delete (http_cerver);

}

static void test_http_cerver_admin_cors_headers (void) {
	
	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_cors_headers (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_cors_headers, true, NULL);

	http_cerver_admin_set_origin (http_cerver, ermiry_com);
	test_check_str_eq (http_cerver->admin_origin.value, ermiry_com, NULL);
	test_check_str_len (http_cerver->admin_origin.value, strlen (ermiry_com), NULL);
	test_check_int_eq (http_cerver->admin_origin.len, (int) strlen (ermiry_com), NULL);

	http_cerver_delete (http_cerver);

}

static void test_http_cerver_admin_file_systems (void) {
	
	HttpCerver *http_cerver = test_http_cerver_create ();

	http_cerver_enable_admin_routes (http_cerver, true);

	http_cerver_register_admin_file_system (http_cerver, "/");

	http_cerver_delete (http_cerver);

}

#pragma endregion

#pragma region receive

static void test_http_receive_new (void) {

	HttpReceive *http_receive = http_receive_new ();

	test_check (http_receive->receive_status == HTTP_RECEIVE_STATUS_NONE, NULL);

	test_check_null_ptr (http_receive->cr);

	test_check_false (http_receive->keep_alive);

	test_check_null_ptr (http_receive->handler);

	test_check_null_ptr (http_receive->http_cerver);

	test_check_null_ptr (http_receive->parser);

	test_check_null_ptr (http_receive->mpart_parser);

	test_check_null_ptr (http_receive->request);

	test_check (http_receive->type == HTTP_RECEIVE_TYPE_NONE, NULL);
	test_check_null_ptr (http_receive->route);
	test_check_null_ptr (http_receive->served_file);

	test_check (http_receive->status == HTTP_STATUS_NONE, NULL);
	test_check_unsigned_eq (http_receive->sent, 0, NULL);

	test_check_null_ptr (http_receive->file_stats);

	http_receive_delete (http_receive);

}

static void test_http_receive_create (void) {

	HttpCerver http_cerver;

	Cerver cerver;
	cerver.cerver_data = &http_cerver;

	CerverReceive cerver_receive;
	cerver_receive.cerver = &cerver;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);

	test_check (http_receive->receive_status == HTTP_RECEIVE_STATUS_NONE, NULL);

	test_check_ptr (http_receive->cr);
	test_check_ptr (http_receive_get_cerver_receive (http_receive));
	test_check_ptr (http_receive_get_cerver (http_receive));

	test_check_false (http_receive->keep_alive);

	test_check_ptr (http_receive->handler);

	test_check_ptr (http_receive->http_cerver);

	test_check_ptr (http_receive->parser);

	http_receive->parser->data = http_receive;

	test_check_null_ptr (http_receive->settings.on_message_begin);
	test_check_ptr (http_receive->settings.on_url);
	test_check_null_ptr (http_receive->settings.on_status);
	test_check_ptr (http_receive->settings.on_header_field);
	test_check_ptr (http_receive->settings.on_header_value);
	test_check_ptr (http_receive->settings.on_headers_complete);
	test_check_ptr (http_receive->settings.on_body);
	test_check_ptr (http_receive->settings.on_message_complete);
	test_check_null_ptr (http_receive->settings.on_chunk_header);
	test_check_null_ptr (http_receive->settings.on_chunk_complete);

	test_check_null_ptr (http_receive->mpart_parser);

	test_check_ptr (http_receive->request);

	test_check (http_receive->type == HTTP_RECEIVE_TYPE_NONE, NULL);
	test_check_null_ptr (http_receive->route);
	test_check_null_ptr (http_receive->served_file);

	test_check (http_receive->status == HTTP_STATUS_NONE, NULL);
	test_check_unsigned_eq (http_receive->sent, 0, NULL);

	test_check_ptr (http_receive->file_stats);

	http_receive_delete (http_receive);

}

#pragma endregion

static void http_tests_main (void) {

	(void) printf ("Testing HTTP main...\n");

	// main
	test_http_cerver_new ();

	// admin
	test_http_cerver_enable_admin_routes ();
	test_http_cerver_enable_admin_head_handlers ();
	test_http_cerver_enable_admin_options_handlers ();
	test_http_cerver_admin_bearer_auth_set_decode ();
	test_http_cerver_admin_bearer_auth_to_json ();
	test_http_cerver_admin_custom_auth ();
	test_http_cerver_admin_cors_headers ();
	test_http_cerver_admin_file_systems ();

	// receive
	test_http_receive_new ();
	test_http_receive_create ();

	(void) printf ("Done!\n");

}

int main (int argc, char **argv) {

	(void) printf ("Testing HTTP...\n");

	http_tests_main ();

	// unit tests
	http_tests_headers ();
	http_tests_methods ();
	http_tests_contents ();
	http_tests_origins ();
	http_tests_routes ();
	http_tests_requests ();
	http_tests_responses ();
	http_tests_multiparts ();
	http_tests_jwt ();

	// integration tests
	http_tests_origins_whitelist ();
	http_tests_jwt_encode ();
	http_tests_jwt_decode ();

	http_tests_admin ();

	(void) printf ("\nDone with HTTP tests!\n\n");

	return 0;

}
