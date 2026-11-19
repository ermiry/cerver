#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/context.h>
#include "cerver/http/core/handler.h"
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/security/auth.h>

#include <cerver/jwt/jwt.h>

#include <cerver/threads/worker.h>

#include "../test.h"
#include "data.h"
#include "http.h"

const char *private_key = { "./test/http/keys/key.key" };
const char *public_key = { "./test/http/keys/key.pub" };

const char *base_token = { "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MDc1NTU2ODYsImlkIjoiMTYwNzU1NTY4NiIsIm5hbWUiOiJFcmljayBTYWxhcyIsInJvbGUiOiJjb21tb24iLCJ1c2VybmFtZSI6ImVybWlyeSJ9.QIEZXo8vkthUPQzjozQNJ8P5ZTxbA6w2OWjeVplWVB2cs1ZySTRMvZ6Dh8dDe-CAHH4P9zcJumJIR0LWxZQ63O61c-cKZuupaPiI9qQaqql30oBnywznWCDkkXgkoTh683fSxoFR71Gzqp5e41mX-KxopH-Kh4Bz3eM3d27p-8tKOUGUlVk1AgWfGU2LFfTu1ZLkVYwo4ceGxHdntS1C_6IiutG8TLFjuKixgujIhK0ireG1cfAs7uvtGhWLXokLpvTbIrrjKSKLEjCcPh_yPpWeay0Y4yV1e5zSQKHiG7ry0D3qDWJcfHtjNLjAFb93TnpXtoXhftq_uL4d7BV16Y9ssQ5MpNoEh3bxHBgaCHOCGuCAVJfdDsRBT5C60_jme7S1utQ4qkpA8YbAoYKi58yGkHxnnYYbaYFQYSykbsYPFZ-4dieFlaIc5-m-vymwG--AMjfhu8Sdm0V0xA3Vq_9FzUIfXvo32-k0eH4QGPX6W8-FinR_Lj-Zj29mXfgALpTlF-m7Sbo3hVebnpwAFKZR495UBLL8B8u3MHB9XQxX6z4fHAxv5Nkftr4ShGRWwI_yVD73sLq44V9FEBQksoXLEBm5RqRC8qvhoxOIzbEYokd6MWLak1sZRUfIIqu65fERA5sMjs9JRnlYu7twP3Fk_VPy-tZBXXA7KTc3lkE" };
const char *bearer_token = { "Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MDc1NTU2ODYsImlkIjoiMTYwNzU1NTY4NiIsIm5hbWUiOiJFcmljayBTYWxhcyIsInJvbGUiOiJjb21tb24iLCJ1c2VybmFtZSI6ImVybWlyeSJ9.QIEZXo8vkthUPQzjozQNJ8P5ZTxbA6w2OWjeVplWVB2cs1ZySTRMvZ6Dh8dDe-CAHH4P9zcJumJIR0LWxZQ63O61c-cKZuupaPiI9qQaqql30oBnywznWCDkkXgkoTh683fSxoFR71Gzqp5e41mX-KxopH-Kh4Bz3eM3d27p-8tKOUGUlVk1AgWfGU2LFfTu1ZLkVYwo4ceGxHdntS1C_6IiutG8TLFjuKixgujIhK0ireG1cfAs7uvtGhWLXokLpvTbIrrjKSKLEjCcPh_yPpWeay0Y4yV1e5zSQKHiG7ry0D3qDWJcfHtjNLjAFb93TnpXtoXhftq_uL4d7BV16Y9ssQ5MpNoEh3bxHBgaCHOCGuCAVJfdDsRBT5C60_jme7S1utQ4qkpA8YbAoYKi58yGkHxnnYYbaYFQYSykbsYPFZ-4dieFlaIc5-m-vymwG--AMjfhu8Sdm0V0xA3Vq_9FzUIfXvo32-k0eH4QGPX6W8-FinR_Lj-Zj29mXfgALpTlF-m7Sbo3hVebnpwAFKZR495UBLL8B8u3MHB9XQxX6z4fHAxv5Nkftr4ShGRWwI_yVD73sLq44V9FEBQksoXLEBm5RqRC8qvhoxOIzbEYokd6MWLak1sZRUfIIqu65fERA5sMjs9JRnlYu7twP3Fk_VPy-tZBXXA7KTc3lkE" };

static const char *ermiry_com = { "https://ermiry.com" };

#pragma region main

static void test_cerver_http_new (void) {

	HttpCerver *http_cerver = cerver_http_new ();

	test_check_ptr (http_cerver);

	test_check_null_ptr (http_cerver->static_paths);

	test_check_null_ptr (http_cerver->tree);

	test_check_null_ptr (http_cerver->default_handler);

	test_check_false (http_cerver->not_found_handler);
	test_check_null_ptr (http_cerver->not_found);

	test_check_int_eq (http_cerver->uploads_path_len, 0, NULL);

	test_check_unsigned_eq (http_cerver->uploads_file_mode, CERVER_HTTP_DEFAULT_UPLOADS_FILE_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_filename_generator);

	test_check_unsigned_eq (http_cerver->uploads_dir_mode, CERVER_HTTP_DEFAULT_UPLOADS_DIR_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_dirname_generator);

	test_check (http_cerver->uploads_delete_when_done == CERVER_HTTP_DEFAULT_UPLOADS_DELETE, NULL);

	test_check (http_cerver->jwt_alg == CERVER_JWT_ALGORITHM_NONE, NULL);

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

	test_check_null_ptr (http_cerver->custom_data);
	test_check_null_ptr (http_cerver->delete_custom_data);

	test_check_unsigned_eq (http_cerver->response_headers.count, 0, NULL);
	test_check_unsigned_eq (http_cerver->response_headers.length, 0, NULL);
	test_check_null_ptr (http_cerver->response_headers.fields);
	test_check_null_ptr (http_cerver->response_headers.buffer);

	test_check_unsigned_eq (http_cerver->n_incompleted_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_unhandled_requests, 0, NULL);

	test_check_unsigned_eq (http_cerver->n_catch_all_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_failed_auth_requests, 0, NULL);

	test_check (http_cerver->enable_admin_routes == CERVER_HTTP_DEFAULT_ENABLE_ADMIN, NULL);
	
	test_check (http_cerver->enable_admin_routes_auth == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_AUTH, NULL);
	test_check (http_cerver->enable_admin_info_route == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_INFO, NULL);
	test_check (http_cerver->enable_admin_head_handlers == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_HEADS, NULL);
	test_check (http_cerver->enable_admin_options_handlers == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_OPTIONS, NULL);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);
	test_check_null_ptr (http_cerver->admin_decode_data);
	test_check_null_ptr (http_cerver->admin_delete_decoded_data);
	test_check_null_ptr (http_cerver->admin_auth_handler);

	test_check_null_ptr (http_cerver->admin_routes_custom_data);
	test_check_null_ptr (http_cerver->delete_admin_routes_custom_data);

	test_check (http_cerver->enable_admin_cors_headers == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_CORS, NULL);
	test_check_int_eq (http_cerver->admin_origin.len, 0, NULL);
	test_check_str_empty (http_cerver->admin_origin.value);

	test_check_null_ptr (http_cerver->admin_file_systems_stats);
	test_check_null_ptr (http_cerver->admin_workers);
	test_check_null_ptr (http_cerver->admin_mutex);

	test_check_null_ptr (http_cerver->mutex);

	cerver_http_delete (http_cerver);

}

static HttpCerver *test_cerver_http_create (void) {

	HttpCerver *http_cerver = cerver_http_create (NULL);

	test_check_ptr (http_cerver);

	test_check_ptr (http_cerver->static_paths);

	test_check_ptr (http_cerver->tree);

	test_check_ptr (http_cerver->default_handler);

	test_check_false (http_cerver->not_found_handler);
	test_check_ptr (http_cerver->not_found);

	test_check_int_eq (http_cerver->uploads_path_len, 0, NULL);

	test_check_unsigned_eq (http_cerver->uploads_file_mode, CERVER_HTTP_DEFAULT_UPLOADS_FILE_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_filename_generator);

	test_check_unsigned_eq (http_cerver->uploads_dir_mode, CERVER_HTTP_DEFAULT_UPLOADS_DIR_MODE, NULL);
	test_check_null_ptr (http_cerver->uploads_dirname_generator);

	test_check (http_cerver->uploads_delete_when_done == CERVER_HTTP_DEFAULT_UPLOADS_DELETE, NULL);

	test_check (http_cerver->jwt_alg == CERVER_HTTP_DEFAULT_JWT_ALGORITHM, NULL);
	test_check (http_cerver->jwt_alg == CERVER_JWT_ALGORITHM_HS256, NULL);

	test_check_null_ptr (http_cerver->jwt_opt_key_name);
	test_check_null_ptr (http_cerver->jwt_private_key);

	test_check_null_ptr (http_cerver->jwt_opt_pub_key_name);
	test_check_null_ptr (http_cerver->jwt_public_key);

	test_check_null_ptr (http_cerver->custom_data);
	test_check_null_ptr (http_cerver->delete_custom_data);

	test_check_unsigned_eq (http_cerver->response_headers.count, 0, NULL);
	test_check_unsigned_eq (http_cerver->response_headers.length, 0, NULL);
	test_check_null_ptr (http_cerver->response_headers.fields);
	test_check_null_ptr (http_cerver->response_headers.buffer);

	test_check_unsigned_eq (http_cerver->n_incompleted_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_unhandled_requests, 0, NULL);

	test_check_unsigned_eq (http_cerver->n_catch_all_requests, 0, NULL);
	test_check_unsigned_eq (http_cerver->n_failed_auth_requests, 0, NULL);

	test_check (http_cerver->enable_admin_routes == CERVER_HTTP_DEFAULT_ENABLE_ADMIN, NULL);
	
	test_check (http_cerver->enable_admin_routes_auth == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_AUTH, NULL);
	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);
	test_check_null_ptr (http_cerver->admin_decode_data);
	test_check_null_ptr (http_cerver->admin_delete_decoded_data);
	test_check_null_ptr (http_cerver->admin_auth_handler);

	test_check (http_cerver->enable_admin_cors_headers == CERVER_HTTP_DEFAULT_ENABLE_ADMIN_CORS, NULL);
	test_check_int_eq (http_cerver->admin_origin.len, 0, NULL);
	test_check_str_empty (http_cerver->admin_origin.value);

	test_check_ptr (http_cerver->admin_file_systems_stats);
	test_check_ptr (http_cerver->admin_workers);
	test_check_ptr (http_cerver->admin_mutex);

	test_check_ptr (http_cerver->mutex);

	return http_cerver;

}

#pragma endregion

#pragma region uploads

static void custom_uploads_filename_generator (CerverHttpContext *context) {

	const CerverHttpPart *part = cerver_http_request_current_part (context);

	cerver_http_part_set_generated_filename (
		part,
		"%d-%ld-%s",
		context->cr->connection->socket->sock_fd,
		time (NULL),
		cerver_http_part_filename (part).data
	);

}

static void test_cerver_http_set_uploads_path (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	const char *uploads_path = { "/home/uploads" };

	cerver_http_set_uploads_path (http_cerver, uploads_path);

	test_check_str_eq (http_cerver->uploads_path, uploads_path, NULL);
	test_check_int_eq (http_cerver->uploads_path_len, (int) strlen (uploads_path), NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_generate_uploads_path (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	const char *base_path = { "/home/ermiry" };
	const char *uploads_path = { "uploads" };
	const char *complete_uploads_path = { "/home/ermiry/uploads" };

	cerver_http_generate_uploads_path (http_cerver, "%s/%s", base_path, uploads_path);

	test_check_str_eq (http_cerver->uploads_path, complete_uploads_path, NULL);
	test_check_int_eq (http_cerver->uploads_path_len, (int) strlen (complete_uploads_path), NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_set_uploads_file_mode (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	const unsigned int file_mode = 0600;

	cerver_http_set_uploads_file_mode (http_cerver, file_mode);

	test_check_unsigned_eq (http_cerver->uploads_file_mode, file_mode, NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_set_uploads_filename_generator (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_set_uploads_filename_generator (http_cerver, custom_uploads_filename_generator);

	test_check_ptr_eq (http_cerver->uploads_filename_generator, custom_uploads_filename_generator);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_set_default_uploads_filename_generator (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_set_default_uploads_filename_generator (http_cerver);

	test_check_ptr_eq (http_cerver->uploads_filename_generator, cerver_http_default_uploads_filename_generator);

	cerver_http_delete (http_cerver);

}

#pragma endregion

#pragma region data

static void http_delete_custom_data (void *data_ptr) {

	if (data_ptr) free (data_ptr);

}

static void test_cerver_http_get_custom_data (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	test_check_null_ptr (cerver_http_get_custom_data (http_cerver));

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_set_custom_data (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	CustomData *custom_data = (CustomData *) malloc (sizeof (CustomData));

	cerver_http_set_custom_data (http_cerver, custom_data);

	test_check_ptr (cerver_http_get_custom_data (http_cerver));

	cerver_http_set_delete_custom_data (http_cerver, http_delete_custom_data);

	test_check_ptr_eq (http_cerver->delete_custom_data, http_delete_custom_data);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_set_default_delete_custom_data (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	CustomData *custom_data = (CustomData *) malloc (sizeof (CustomData));

	cerver_http_set_custom_data (http_cerver, custom_data);

	test_check_ptr (cerver_http_get_custom_data (http_cerver));

	cerver_http_set_default_delete_custom_data (http_cerver);

	test_check_ptr_eq (http_cerver->delete_custom_data, free);

	cerver_http_delete (http_cerver);

}

#pragma endregion

#pragma region admin

static void test_cerver_http_enable_admin_routes (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_routes, true, NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_enable_admin_info_route (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_info_route (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_info_route, true, NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_enable_admin_head_handlers (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);

	cerver_http_enable_admin_head_handlers (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_head_handlers, true, NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_enable_admin_options_handlers (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);

	cerver_http_enable_admin_options_handlers (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_options_handlers, true, NULL);

	cerver_http_delete (http_cerver);

}

static void *user_parse_from_json (const CerverJsonValue *claims) {

	if (claims) (void) printf ("claims!");

	return NULL;

}

static void test_cerver_http_admin_bearer_auth_set_decode (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_BEARER);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_BEARER, NULL);

	cerver_http_admin_routes_auth_set_decode_data (
		http_cerver,
		user_parse_from_json, free
	);

	test_check_ptr_eq (http_cerver->admin_decode_data, user_parse_from_json);
	test_check_ptr_eq (http_cerver->admin_delete_decoded_data, free);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_admin_bearer_auth_to_json (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_BEARER);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_BEARER, NULL);

	cerver_http_admin_routes_auth_decode_to_json (http_cerver);

	test_check_ptr_eq (http_cerver->admin_decode_data, http_decode_data_into_json);
	test_check_ptr_eq (http_cerver->admin_delete_decoded_data, free);

	cerver_http_delete (http_cerver);

}

static unsigned int custom_authentication_handler (CerverHttpContext *context) {

	unsigned int retval = 1;

	const CerverHttpView key = cerver_http_request_part_value (context, "key");
	if (cerver_http_view_equals (key, "okay")) {
		cerver_log_success ("Success auth!");

		retval = 0;
	}

	else {
		cerver_log_error ("Failed auth!");
	}

	return retval;

}

static void test_cerver_http_admin_custom_auth (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_CUSTOM);

	test_check_unsigned_eq (http_cerver->admin_auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);

	cerver_http_admin_routes_set_authentication_handler (http_cerver, custom_authentication_handler);

	test_check_ptr_eq (http_cerver->admin_auth_handler, custom_authentication_handler);

	void *custom_data = (void *) malloc (sizeof (int));

	cerver_http_admin_routes_set_custom_data (http_cerver, custom_data);
	cerver_http_admin_routes_set_delete_custom_data (http_cerver, free);

	test_check_ptr_eq (http_cerver->admin_routes_custom_data, custom_data);
	test_check_ptr_eq (http_cerver->delete_admin_routes_custom_data, free);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_admin_cors_headers (void) {
	
	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_enable_admin_cors_headers (http_cerver, true);

	test_check_bool_eq (http_cerver->enable_admin_cors_headers, true, NULL);

	cerver_http_admin_set_origin (http_cerver, ermiry_com);
	test_check_str_eq (http_cerver->admin_origin.value, ermiry_com, NULL);
	test_check_str_len (http_cerver->admin_origin.value, strlen (ermiry_com), NULL);
	test_check_int_eq (http_cerver->admin_origin.len, (int) strlen (ermiry_com), NULL);

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_admin_file_systems (void) {
	
	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);

	cerver_http_register_admin_file_system (http_cerver, "/");

	cerver_http_delete (http_cerver);

}

static void test_cerver_http_register_admin_worker (void) {

	HttpCerver *http_cerver = test_cerver_http_create ();

	cerver_http_enable_admin_routes (http_cerver, true);

	Worker *worker = worker_create ();
	cerver_http_register_admin_worker (http_cerver, worker);

	// the http cerver only keeps a reference to its admin workers
	cerver_http_delete (http_cerver);

	worker_delete (worker);

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

	// the parser is embedded and empty until the receive is created for a connection
	test_check_null_ptr (http_receive->parser.head.fields);
	test_check_unsigned_eq (http_receive->parser.head.fields_capacity, 0, NULL);
	test_check_null_ptr (http_receive->fields);
	test_check_null_ptr (http_receive->head_buffer);
	test_check (http_receive->body_handler != NULL, NULL);
	test_check_false (http_receive->keep_alive_requested);
	test_check_unsigned_eq (http_receive->unparsed, 0, NULL);

	test_check_null_ptr (http_receive->mpart_parser);

	test_check_null_ptr (http_receive->request);

	test_check (http_receive->type == HTTP_RECEIVE_TYPE_NONE, NULL);
	test_check_null_ptr (http_receive->served_file);

	test_check (http_receive->status == HTTP_STATUS_NONE, NULL);
	test_check_unsigned_eq (http_receive->sent, 0, NULL);

	test_check_null_ptr (http_receive->file_stats);

	http_receive_delete (http_receive);

}

static void test_http_receive_create (void) {

	// http_receive_create () reads the http cerver's configuration
	HttpCerver http_cerver;
	(void) memset (&http_cerver, 0, sizeof (HttpCerver));

	Cerver cerver;
	(void) memset (&cerver, 0, sizeof (Cerver));
	cerver.cerver_data = &http_cerver;

	CerverReceive cerver_receive;
	(void) memset (&cerver_receive, 0, sizeof (CerverReceive));
	cerver_receive.cerver = &cerver;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);

	test_check (http_receive->receive_status == HTTP_RECEIVE_STATUS_NONE, NULL);

	test_check_ptr (http_receive->cr);
	test_check_ptr (http_receive_get_cerver_receive (http_receive));
	test_check_ptr (http_receive_get_cerver (http_receive));

	test_check_false (http_receive->keep_alive);

	test_check_ptr (http_receive->handler);

	test_check_ptr (http_receive->http_cerver);

	// the parser is embedded, it parses requests with the limits of the cerver and the fields of the receive
	test_check_unsigned_eq (http_receive->parser.type, CERVER_HTTP_PARSER_TYPE_REQUEST, NULL);
	test_check_unsigned_eq (http_receive->parser.lenient, HTTP_PARSER_LENIENT_NONE, NULL);
	test_check_unsigned_eq (http_receive->parser.limits.target_size, http_cerver.max_url_size, NULL);
	test_check_unsigned_eq (http_receive->parser.limits.fields_size, http_cerver.max_header_size, NULL);
	test_check_unsigned_eq (http_receive->parser.limits.fields, http_cerver.max_headers, NULL);
	test_check (http_receive->fields == http_receive->fields_inline, NULL);
	test_check (http_receive->parser.head.fields == http_receive->fields, NULL);
	test_check_unsigned_eq (http_receive->parser.head.fields_capacity, HTTP_RECEIVE_FIELDS_INLINE, NULL);
	test_check_false (cerver_http_parser_started (&http_receive->parser));

	test_check_null_ptr (http_receive->mpart_parser);

	test_check_ptr (http_receive->request);

	test_check (http_receive->type == HTTP_RECEIVE_TYPE_NONE, NULL);
	test_check_null_ptr (http_receive->served_file);

	test_check (http_receive->status == HTTP_STATUS_NONE, NULL);
	test_check_unsigned_eq (http_receive->sent, 0, NULL);

	test_check_ptr (http_receive->file_stats);

	http_receive_delete (http_receive);

}

// the lenient flags of the cerver are set in the parser of each receive
static void test_http_receive_create_lenient (void) {

	HttpCerver http_cerver;
	(void) memset (&http_cerver, 0, sizeof (HttpCerver));

	cerver_http_set_parser_lenient (&http_cerver, HTTP_PARSER_LENIENT_VERSION | HTTP_PARSER_LENIENT_HEADERS);
	test_check_unsigned_eq (http_cerver.parser_lenient, (HTTP_PARSER_LENIENT_VERSION | HTTP_PARSER_LENIENT_HEADERS), NULL);

	test_check_str_eq (http_parser_lenient_string (HTTP_PARSER_LENIENT_VERSION), "version", NULL);
	test_check_str_eq (http_parser_lenient_string (HTTP_PARSER_LENIENT_NONE), "none", NULL);
	test_check_str_eq (http_parser_lenient_description (HTTP_PARSER_LENIENT_VERSION), "any HTTP version", NULL);

	Cerver cerver;
	(void) memset (&cerver, 0, sizeof (Cerver));
	cerver.cerver_data = &http_cerver;

	CerverReceive cerver_receive;
	(void) memset (&cerver_receive, 0, sizeof (CerverReceive));
	cerver_receive.cerver = &cerver;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);
	test_check_ptr (http_receive);
	test_check_unsigned_eq (http_receive->parser.lenient, (HTTP_PARSER_LENIENT_VERSION | HTTP_PARSER_LENIENT_HEADERS), NULL);
	http_receive_delete (http_receive);

	// the strict mode again for the next receives
	cerver_http_set_parser_lenient (&http_cerver, HTTP_PARSER_LENIENT_NONE);

	http_receive = http_receive_create (&cerver_receive);
	test_check_ptr (http_receive);
	test_check_unsigned_eq (http_receive->parser.lenient, 0, NULL);
	http_receive_delete (http_receive);

}

#pragma endregion

static void http_tests_main (void) {

	(void) printf ("Testing HTTP main...\n");

	// main
	test_cerver_http_new ();

	// uploads
	test_cerver_http_set_uploads_path ();
	test_cerver_http_generate_uploads_path ();
	test_cerver_http_set_uploads_file_mode ();
	test_cerver_http_set_uploads_filename_generator ();
	test_cerver_http_set_default_uploads_filename_generator ();

	// data
	test_cerver_http_get_custom_data ();
	test_cerver_http_set_custom_data ();
	test_cerver_http_set_default_delete_custom_data ();

	// admin
	test_cerver_http_enable_admin_routes ();
	test_cerver_http_enable_admin_info_route ();
	test_cerver_http_enable_admin_head_handlers ();
	test_cerver_http_enable_admin_options_handlers ();
	test_cerver_http_admin_bearer_auth_set_decode ();
	test_cerver_http_admin_bearer_auth_to_json ();
	test_cerver_http_admin_custom_auth ();
	test_cerver_http_admin_cors_headers ();
	test_cerver_http_admin_file_systems ();
	test_cerver_http_register_admin_worker ();

	// receive
	test_http_receive_new ();
	test_http_receive_create ();
	test_http_receive_create_lenient ();

	(void) printf ("Done!\n");

}

// the full matrix of test/disconnect.sh runs alone, without the other suites
#define DISCONNECT_ONLY_VARIABLE			"DISCONNECT_ONLY"

// the connections that are kept run alone to be repeated, they hand each connection between threads
#define KEEPALIVE_ONLY_VARIABLE				"KEEPALIVE_ONLY"

// the pools of threads run alone to be repeated, their requests wait and move between threads
#define POOLS_ONLY_VARIABLE					"POOLS_ONLY"

// the handlers of the event loops run alone to be repeated, for each backend of the loops
#define LOOP_ONLY_VARIABLE					"LOOP_ONLY"

// the parser runs alone, its units take a second
#define PARSER_ONLY_VARIABLE				"PARSER_ONLY"

// the suites that drive event loops run alone, for each backend of the loops
#define RECEIVE_ONLY_VARIABLE				"RECEIVE_ONLY"
#define SERVER_ONLY_VARIABLE				"SERVER_ONLY"
#define FEATURES_ONLY_VARIABLE				"FEATURES_ONLY"

// the HTTP client runs alone to be repeated, for each backend of its event loop
#define CLIENT_ONLY_VARIABLE				"CLIENT_ONLY"

int main (void) {

	test_output_init ();

	if (getenv (CLIENT_ONLY_VARIABLE)) {
		http_tests_client ();
		return 0;
	}

	if (getenv (PARSER_ONLY_VARIABLE)) {
		http_tests_parser ();
		return 0;
	}

	if (getenv (RECEIVE_ONLY_VARIABLE)) {
		http_tests_receive ();
		return 0;
	}

	if (getenv (SERVER_ONLY_VARIABLE)) {
		http_tests_server ();
		return 0;
	}

	if (getenv (FEATURES_ONLY_VARIABLE)) {
		http_tests_features ();
		return 0;
	}

	if (getenv (DISCONNECT_ONLY_VARIABLE)) {
		http_tests_disconnect ();
		return 0;
	}

	if (getenv (KEEPALIVE_ONLY_VARIABLE)) {
		http_tests_keepalive ();
		return 0;
	}

	if (getenv (POOLS_ONLY_VARIABLE)) {
		http_tests_pools ();
		return 0;
	}

	if (getenv (LOOP_ONLY_VARIABLE)) {
		http_tests_loop ();
		return 0;
	}

	(void) printf ("Testing HTTP...\n");

	http_tests_main ();

	// unit tests
	http_tests_headers ();
	http_tests_methods ();
	http_tests_contents ();
	http_tests_origins ();
	http_tests_routes ();
	http_tests_tree ();
	http_tests_requests ();
	http_tests_responses ();
	http_tests_recycle ();
	http_tests_multiparts ();
	http_tests_urls ();
	http_tests_utils ();
	http_tests_date ();
	http_tests_conditional ();
	http_tests_compression ();
	http_tests_proxy ();
	http_tests_parser ();

	http_tests_jwt ();

	// integration tests
	http_tests_origins_whitelist ();
	http_tests_jwt_encode ();
	http_tests_jwt_decode ();

	// the keys, the verification and the claims of the tokens
	http_tests_tokens ();

	http_tests_admin ();

	// the parser fed with every request split at every byte
	http_tests_receive ();

	// a real cerver: deadlines, overload policies, drain and signals
	http_tests_server ();

	// the connections that are kept between requests
	http_tests_keepalive ();

	// the pools of threads of the routes, and the health and ready routes
	http_tests_pools ();

	// the handlers that run in the event loops, and the pipelines that a loop handles in a row
	http_tests_loop ();

	// the headers that cerver adds, entity tags, compression, streams and metrics
	http_tests_features ();

	// clients that leave in every part of the HTTP process
	http_tests_disconnect ();

	// the HTTP client, against a server of the test
	http_tests_client ();

	(void) printf ("\nDone with HTTP tests!\n\n");

	return 0;

}
