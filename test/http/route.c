#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>

#include <cerver/http/json/json.h>

#include "../test.h"

#define TEST_ROUTE_DOUBLE_EPSILON			1e-9

static bool test_route_double_equals (const double value, const double expected) {

	const double difference = value - expected;

	return (difference < TEST_ROUTE_DOUBLE_EPSILON) && (difference > -TEST_ROUTE_DOUBLE_EPSILON);

}

#define test_check_double_eq(value, expected, msg) test_check_true (test_route_double_equals (value, expected))

// GET /test
static void test_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) request;

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

	(void) http_receive;

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

	test_check_null_ptr (route->children);

	test_check_unsigned_eq (route->modifier, HTTP_ROUTE_MODIFIER_NONE, NULL);

	test_check_unsigned_eq (route->auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);

	test_check_null_ptr (route->decode_data);
	test_check_null_ptr (route->delete_decoded_data);

	test_check_null_ptr (route->authentication_handler);

	test_check_null_ptr (route->custom_data);
	test_check_null_ptr (route->delete_custom_data);

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

	// only the methods with a handler have stats
	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		if (i == REQUEST_METHOD_GET) test_check_ptr (route->stats[i]);
		else test_check_null_ptr (route->stats[i]);
	}

	test_check_ptr (route->file_stats);

	http_route_delete (route);

}

// a method outside REQUEST_METHOD_MAP is never written in the handlers table
static void test_http_route_invalid_method (void) {

	HttpRoute *route = http_route_create (
		(RequestMethod) REQUEST_METHOD_UNDEFINED, "test", test_handler
	);

	test_check_ptr (route);

	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		test_check_null_ptr (route->handlers[i]);
	}

	http_route_set_handler (route, (RequestMethod) (HTTP_HANDLERS_COUNT + 1), test_handler);
	http_route_set_handler (route, REQUEST_METHOD_PATCH, test_handler);

	test_check_ptr (route->handlers[REQUEST_METHOD_PATCH]);
	test_check_ptr (route->stats[REQUEST_METHOD_PATCH]);

	http_route_delete (route);

}

// the methods string has every method with a handler, separated with ", "
static void test_http_route_methods_string (void) {

	HttpRoute *route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
	http_route_set_handler (route, REQUEST_METHOD_POST, test_handler);
	http_route_set_handler (route, REQUEST_METHOD_PATCH, test_handler);

	char methods[HTTP_ROUTE_METHODS_STRING_SIZE] = { 0 };
	const size_t length = http_route_methods_string (route->handlers, methods, HTTP_ROUTE_METHODS_STRING_SIZE);

	test_check_str_eq (methods, "GET, POST, PATCH", NULL);
	test_check_unsigned_eq (length, strlen ("GET, POST, PATCH"), NULL);

	// a small buffer is never overflowed
	char small[4] = { 0 };
	(void) http_route_methods_string (route->handlers, small, sizeof (small));
	test_check_unsigned_eq (strlen (small), 3, NULL);

	http_route_delete (route);

}

// the stats are updated with atomic operations and read with a snapshot
static void test_http_route_stats_snapshot (void) {

	HttpRouteStats *stats = http_route_stats_new ();

	HttpRouteStats snapshot;
	http_route_stats_snapshot (stats, &snapshot);
	test_check_unsigned_eq (snapshot.n_requests, 0, NULL);
	test_check_unsigned_eq (snapshot.min_request_size, 0, NULL);
	test_check_bool_eq (snapshot.first, true, NULL);

	http_route_stats_update (stats, 2.0, 100, 1000);
	http_route_stats_update (stats, 4.0, 300, 3000);

	http_route_stats_snapshot (stats, &snapshot);
	test_check_unsigned_eq (snapshot.n_requests, 2, NULL);
	test_check_bool_eq (snapshot.first, false, NULL);
	test_check_double_eq (snapshot.min_process_time, 2.0, NULL);
	test_check_double_eq (snapshot.max_process_time, 4.0, NULL);
	test_check_double_eq (snapshot.mean_process_time, 3.0, NULL);
	test_check_unsigned_eq (snapshot.min_request_size, 100, NULL);
	test_check_unsigned_eq (snapshot.max_request_size, 300, NULL);
	test_check_unsigned_eq (snapshot.mean_request_size, 200, NULL);
	test_check_unsigned_eq (snapshot.min_response_size, 1000, NULL);
	test_check_unsigned_eq (snapshot.max_response_size, 3000, NULL);
	test_check_unsigned_eq (snapshot.mean_response_size, 2000, NULL);

	http_route_stats_delete (stats);

}

// the mean file size was computed with the number of files instead of their sizes
static void test_http_route_file_stats_mean (void) {

	HttpRouteFileStats *file_stats = http_route_file_stats_create ();

	// a request without files does not divide by zero
	HttpRouteFileStats *empty = http_route_file_stats_new ();
	http_route_file_stats_update (file_stats, empty);

	HttpRouteFileStats *uploaded = http_route_file_stats_new ();
	uploaded->n_uploaded_files = 2;
	uploaded->min_file_size = 100;
	uploaded->max_file_size = 300;
	uploaded->sum_file_size = 400;
	http_route_file_stats_update (file_stats, uploaded);

	HttpRouteFileStats snapshot;
	http_route_file_stats_snapshot (file_stats, &snapshot);
	test_check_unsigned_eq (snapshot.n_uploaded_files, 2, NULL);
	test_check_double_eq (snapshot.mean_file_size, 200.0, NULL);
	test_check_null_ptr (snapshot.mutex);

	http_route_file_stats_delete (uploaded);
	http_route_file_stats_delete (empty);
	http_route_file_stats_delete (file_stats);

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
	test_http_route_invalid_method ();
	test_http_route_methods_string ();
	test_http_route_stats_snapshot ();
	test_http_route_file_stats_mean ();
	test_http_route_set_handler ();
	test_http_route_set_modifier ();
	test_http_route_set_auth ();
	test_http_route_set_decode_data ();
	test_http_route_set_decode_data_into_json ();
	test_http_route_set_authentication_handler ();

	(void) printf ("Done!\n");

}
