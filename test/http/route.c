#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>

#include <cerver/http/json/json.h>

#include "../test.h"

// GET /test
static void test_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "Test route works!"
	);
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

static unsigned int custom_authentication_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	unsigned int retval = 1;

	http_request_multi_parts_print (request);

	const char *key = http_request_multi_parts_get_value (request, "key");
	if (!strcmp (key, "okay")) {
		retval = 0;
	}

	return retval;

}

static void *test_http_decode_data_into_json (void *json_ptr) {

	return json_dumps ((json_t *) json_ptr, 0);

}

static HttpRoute *test_http_route_new (void) {

	HttpRoute *route = http_route_new ();

	test_check_ptr (route);

	test_check_null_ptr (route->base);
	test_check_null_ptr (route->actual);
	test_check_null_ptr (route->route);

	test_check_unsigned_eq (route->n_tokens, 0, NULL);
	test_check_null_ptr (route->tokens);

	test_check_null_ptr (route->routes_tokens);

	test_check_unsigned_eq (route->modifier, HTTP_ROUTE_MODIFIER_NONE, NULL);

	test_check_unsigned_eq (route->auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);

	test_check_null_ptr (route->decode_data);
	test_check_null_ptr (route->delete_decoded_data);

	test_check_null_ptr (route->authentication_handler);

	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		test_check_null_ptr (route->handlers[i]);
		test_check_null_ptr (route->stats[i]);
	}

	test_check_null_ptr (route->file_stats);

	return route;

}

static void test_http_route_create (void) {

	HttpRoute *route = http_route_create (
		REQUEST_METHOD_GET, "/test", test_handler
	);

	test_check_ptr (route);

	test_check_ptr (route->actual);
	test_check_ptr (route->base);
	test_check_ptr (route->route);

	test_check_ptr (route->children);

	test_check_ptr (route->handlers[REQUEST_METHOD_GET]);

	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		test_check_ptr (route->stats[i]);
	}

	test_check_ptr (route->file_stats);

	http_route_delete (route);

}

static void test_http_route_set_handler (void) {

	HttpRoute *route = test_http_route_new ();

	test_check_ptr (route);

	http_route_set_handler (route, REQUEST_METHOD_GET, test_handler);

	test_check_ptr (route->handlers[REQUEST_METHOD_GET]);

	http_route_delete (route);

}

static void test_http_route_set_modifier (void) {

	HttpRoute *route = test_http_route_new ();

	test_check_ptr (route);

	http_route_set_modifier (route, HTTP_ROUTE_MODIFIER_MULTI_PART);

	test_check_unsigned_eq (route->modifier, HTTP_ROUTE_MODIFIER_MULTI_PART, NULL);

	http_route_delete (route);

}

static void test_http_route_set_auth (void) {

	HttpRoute *route = test_http_route_new ();

	test_check_ptr (route);

	http_route_set_auth (route, HTTP_ROUTE_AUTH_TYPE_BEARER);

	test_check_unsigned_eq (route->auth_type, HTTP_ROUTE_AUTH_TYPE_BEARER, NULL);

	http_route_delete (route);

}

static void test_http_route_set_decode_data (void) {

	HttpRoute *route = test_http_route_new ();

	test_check_ptr (route);

	http_route_set_decode_data (route, test_http_decode_data_into_json, free);

	test_check_ptr (route->decode_data);
	test_check_ptr (route->delete_decoded_data);

	http_route_delete (route);

}

static void test_http_route_set_decode_data_into_json (void) {

	HttpRoute *route = test_http_route_new ();

	test_check_ptr (route);

	http_route_set_decode_data_into_json (route);

	test_check_ptr (route->decode_data);
	test_check_ptr (route->delete_decoded_data);

	http_route_delete (route);

}

static void test_http_route_set_authentication_handler (void) {

	HttpRoute *route = test_http_route_new ();

	test_check_ptr (route);

	http_route_set_authentication_handler (route, custom_authentication_handler);

	test_check_ptr (route->authentication_handler);

	http_route_delete (route);

}

void http_tests_routes (void) {

	(void) printf ("Testing HTTP routes...\n");

	test_http_route_create ();
	test_http_route_set_handler ();
	test_http_route_set_modifier ();
	test_http_route_set_auth ();
	test_http_route_set_decode_data ();
	test_http_route_set_decode_data_into_json ();
	test_http_route_set_authentication_handler ();

	(void) printf ("Done!\n");

}
