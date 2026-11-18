#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/routes/route.h>

#include "../../test.h"

#define TEST_ROUTE_DOUBLE_EPSILON			1e-9

// the size of a buffer that does not fit the first method with its end
#define TEST_ROUTE_SMALL_METHODS_SIZE		4

static bool test_route_double_equals (const double value, const double expected) {

	const double difference = value - expected;

	return (difference < TEST_ROUTE_DOUBLE_EPSILON) && (difference > -TEST_ROUTE_DOUBLE_EPSILON);

}

#define test_check_double_eq(value, expected, msg) test_check_true (test_route_double_equals (value, expected))

// a handler that only marks the methods of a table, it is never called
static void test_handler (CerverHttpContext *context) {

	(void) context;

}

// the methods string has every method with a handler, separated with ", "
static void test_http_route_methods_string (void) {

	HttpHandler handlers[HTTP_HANDLERS_COUNT] = { 0 };
	handlers[REQUEST_METHOD_GET] = test_handler;
	handlers[REQUEST_METHOD_POST] = test_handler;
	handlers[REQUEST_METHOD_PATCH] = test_handler;

	char methods[HTTP_ROUTE_METHODS_STRING_SIZE] = { 0 };
	const size_t length = http_route_methods_string (handlers, methods, HTTP_ROUTE_METHODS_STRING_SIZE);

	test_check_str_eq (methods, "GET, POST, PATCH", NULL);
	test_check_unsigned_eq (length, strlen ("GET, POST, PATCH"), NULL);

	// a small buffer is never overflowed
	char small[TEST_ROUTE_SMALL_METHODS_SIZE] = { 0 };
	(void) http_route_methods_string (handlers, small, sizeof (small));
	test_check_unsigned_eq (strlen (small), TEST_ROUTE_SMALL_METHODS_SIZE - 1, NULL);

	// a table without handlers has no methods
	HttpHandler none[HTTP_HANDLERS_COUNT] = { 0 };
	test_check_unsigned_eq (http_route_methods_string (none, methods, HTTP_ROUTE_METHODS_STRING_SIZE), 0, NULL);
	test_check_str_eq (methods, "", NULL);

}

// the stats are updated with atomic operations and read with a snapshot
static void test_cerver_http_route_stats_snapshot (void) {

	HttpRouteStats *stats = cerver_http_route_stats_new ();

	HttpRouteStats snapshot;
	cerver_http_route_stats_snapshot (stats, &snapshot);
	test_check_unsigned_eq (snapshot.n_requests, 0, NULL);
	test_check_unsigned_eq (snapshot.min_request_size, 0, NULL);
	test_check_bool_eq (snapshot.first, true, NULL);

	cerver_http_route_stats_update (stats, 2.0, 100, 1000);
	cerver_http_route_stats_update (stats, 4.0, 300, 3000);

	cerver_http_route_stats_snapshot (stats, &snapshot);
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

	cerver_http_route_stats_delete (stats);

}

// the mean file size was computed with the number of files instead of their sizes
static void test_cerver_http_route_file_stats_mean (void) {

	HttpRouteFileStats *file_stats = cerver_http_route_file_stats_create ();

	// a request without files does not divide by zero
	HttpRouteFileStats *empty = cerver_http_route_file_stats_new ();
	cerver_http_route_file_stats_update (file_stats, empty);

	HttpRouteFileStats *uploaded = cerver_http_route_file_stats_new ();
	uploaded->n_uploaded_files = 2;
	uploaded->min_file_size = 100;
	uploaded->max_file_size = 300;
	uploaded->sum_file_size = 400;
	cerver_http_route_file_stats_update (file_stats, uploaded);

	HttpRouteFileStats snapshot;
	cerver_http_route_file_stats_snapshot (file_stats, &snapshot);
	test_check_unsigned_eq (snapshot.n_uploaded_files, 2, NULL);
	test_check_double_eq (snapshot.mean_file_size, 200.0, NULL);
	test_check_null_ptr (snapshot.mutex);

	cerver_http_route_file_stats_delete (uploaded);
	cerver_http_route_file_stats_delete (empty);
	cerver_http_route_file_stats_delete (file_stats);

}

void http_tests_routes (void) {

	(void) printf ("Testing HTTP routes...\n");

	test_http_route_methods_string ();
	test_cerver_http_route_stats_snapshot ();
	test_cerver_http_route_file_stats_mean ();

	(void) printf ("Done!\n");

}
