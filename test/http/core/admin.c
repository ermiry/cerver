#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/info.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

#include <cerver/threads/worker.h>

#include "../../test.h"

static const char *uploads_file_system = { "/var/uploads" };

#pragma region systems

static void test_http_admin_file_system_stats_new (void) {

	HttpAdminFileSystemStats *stats = http_admin_file_system_stats_new ();

	test_check_unsigned_eq (stats->path_len, 0, NULL);

	http_admin_file_system_stats_delete (stats);

}

static void test_http_admin_file_system_stats_create (void) {

	HttpAdminFileSystemStats *stats = http_admin_file_system_stats_create (
		uploads_file_system
	);

	test_check_unsigned_eq (stats->path_len, strlen (uploads_file_system), NULL);
	test_check_str_eq (stats->path, uploads_file_system, NULL);

	http_admin_file_system_stats_delete (stats);

}

// a file system without a path is not registered
static void test_http_admin_register_file_system_null (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	http_cerver_register_admin_file_system (http_cerver, NULL);
	test_check_unsigned_eq (dlist_size (http_cerver->admin_file_systems_stats), 0, "registered a file system without a path");

	http_cerver_register_admin_file_system (http_cerver, uploads_file_system);
	test_check_unsigned_eq (dlist_size (http_cerver->admin_file_systems_stats), 1, NULL);

	(void) cerver_teardown (test_cerver);

}

#pragma endregion

#pragma region generate

// GET /api/users
void main_users_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// POST /api/users/login
void users_login_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// POST /api/users/register
void users_register_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// POST /api/users/upload
void users_upload_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

static void test_http_admin_generate_jsons (void) {

	Cerver *test_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"test-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	test_check_ptr (test_cerver);

	/*** web cerver configuration ***/
	test_check_ptr (test_cerver->cerver_data);
	HttpCerver *http_cerver = (HttpCerver *) test_cerver->cerver_data;

	http_cerver_enable_admin_routes (http_cerver, true);

	// GET /api/users
	HttpRoute *users_route = http_route_create (REQUEST_METHOD_GET, "api/users", main_users_handler);
	http_cerver_route_register (http_cerver, users_route);

	// register users child routes
	// POST api/users/login
	HttpRoute *users_login_route = http_route_create (REQUEST_METHOD_POST, "login", users_login_handler);
	http_route_child_add (users_route, users_login_route);

	// POST api/users/register
	HttpRoute *users_register_route = http_route_create (REQUEST_METHOD_POST, "register", users_register_handler);
	http_route_child_add (users_route, users_register_route);

	// POST api/users/upload
	HttpRoute *upload_route = http_route_create (REQUEST_METHOD_POST, "upload", users_upload_handler);
	http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
	http_route_child_add (users_route, upload_route);

	u8 result = http_cerver_init (http_cerver);
	test_check_unsigned_eq (result, 0, NULL);

	/*** test ***/
	char *json = http_cerver_admin_generate_info_json (http_cerver);
	test_check_ptr (json);
	free (json);

	json = http_cerver_admin_generate_routes_stats_json (http_cerver);
	test_check_ptr (json);
	free (json);

	json = http_cerver_admin_generate_file_systems_stats_json (http_cerver);
	test_check_ptr (json);
	free (json);

	json = http_cerver_admin_generate_workers_json (http_cerver);
	test_check_ptr (json);
	free (json);

	cerver_teardown (test_cerver);

}

#pragma endregion

#pragma region bytes

#define TEST_ADMIN_JSON_SIZE			4096

static const char *test_admin_alias = { "the \"admin\" cerver" };
static const char *test_admin_alias_invalid = { "\xff\xfe" };

static const char *test_admin_missing_file_system = { "/missing/file/system" };

static const double test_admin_first_time = 0.5;
static const double test_admin_second_time = 1.5;
static const double test_admin_endpoint_time = 0.25;

static const size_t test_admin_first_request = 100;
static const size_t test_admin_second_request = 300;
static const size_t test_admin_first_response = 200;
static const size_t test_admin_second_response = 400;

static const size_t test_admin_uploaded_files = 2;
static const size_t test_admin_min_file_size = 10;
static const size_t test_admin_max_file_size = 30;
static const size_t test_admin_sum_file_size = 40;

static const size_t test_admin_ok_responses = 3;
static const size_t test_admin_not_found_responses = 1;

static const double test_admin_file_system_total = 2.5;
static const double test_admin_file_system_available = 1.5;
static const double test_admin_file_system_used = 1.0;
static const double test_admin_file_system_used_percentage = 40.0;

static const unsigned int test_admin_first_worker = 1;
static const unsigned int test_admin_second_worker = 2;

// the json of a generator is the expected one, byte by byte
static void test_http_admin_json_check (char *json, const char *expected) {

	test_check_ptr (json);

	test_check_str_eq (json, expected, NULL);

	free (json);

}

// the info of the cerver: its strings are escaped, and one that is not UTF-8 is left out
static void test_http_admin_json_info (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	cerver_set_alias (test_cerver, test_admin_alias);

	char expected[TEST_ADMIN_JSON_SIZE] = { 0 };

	// the uptime is the one that the generator left in the info of the cerver
	char *json = http_cerver_admin_generate_info_json (http_cerver);

	(void) snprintf (
		expected, TEST_ADMIN_JSON_SIZE,
		"{\"name\": \"test-cerver\", \"alias\": \"the \\\"admin\\\" cerver\", "
		"\"time_started\": %lld, \"uptime\": %lld, "
		"\"version\": {\"name\": \"%s\", \"date\": \"%s\", \"time\": \"%s\", \"author\": \"%s\"}}",
		(long long) test_cerver->info->time_started, (long long) test_cerver->info->uptime,
		CERVER_VERSION_NAME, CERVER_VERSION_DATE, CERVER_VERSION_TIME, CERVER_VERSION_AUTHOR
	);

	test_http_admin_json_check (json, expected);

	cerver_set_alias (test_cerver, test_admin_alias_invalid);

	json = http_cerver_admin_generate_info_json (http_cerver);

	(void) snprintf (
		expected, TEST_ADMIN_JSON_SIZE,
		"{\"name\": \"test-cerver\", "
		"\"time_started\": %lld, \"uptime\": %lld, "
		"\"version\": {\"name\": \"%s\", \"date\": \"%s\", \"time\": \"%s\", \"author\": \"%s\"}}",
		(long long) test_cerver->info->time_started, (long long) test_cerver->info->uptime,
		CERVER_VERSION_NAME, CERVER_VERSION_DATE, CERVER_VERSION_TIME, CERVER_VERSION_AUTHOR
	);

	test_http_admin_json_check (json, expected);

	(void) cerver_teardown (test_cerver);

}

// the stats of the routes of the first API, of the endpoints of the groups and of the cerver
static void test_http_admin_json_stats (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	HttpRoute *users_route = http_route_create (REQUEST_METHOD_GET, "api/users", main_users_handler);
	http_cerver_route_register (http_cerver, users_route);

	HttpRoute *login_route = http_route_create (REQUEST_METHOD_POST, "login", users_login_handler);
	http_route_child_add (users_route, login_route);

	// a method whose handler was taken away keeps its stats, and it is not one of the handlers of the route
	http_route_set_handler (login_route, REQUEST_METHOD_PUT, users_login_handler);
	http_route_set_handler (login_route, REQUEST_METHOD_PUT, NULL);
	test_check_ptr (login_route->stats[REQUEST_METHOD_PUT]);

	HttpRoute *upload_route = http_route_create (REQUEST_METHOD_POST, "upload", users_upload_handler);
	http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
	http_route_child_add (users_route, upload_route);

	// only the POST of a multi-part route has the stats of its files
	http_route_set_handler (upload_route, REQUEST_METHOD_GET, main_users_handler);

	HttpGroup *group = http_cerver_group_create (http_cerver, "api/items");
	test_check_ptr (group);

	HttpEndpoint *list = http_group_get (group, "list", main_users_handler);
	test_check_ptr (list);

	HttpEndpoint *files = http_group_post (group, "files", users_upload_handler);
	test_check_ptr (files);
	http_endpoint_set_modifier (files, HTTP_ROUTE_MODIFIER_MULTI_PART);

	test_check_unsigned_eq (http_cerver_init (http_cerver), 0, NULL);

	http_route_stats_update (
		users_route->stats[REQUEST_METHOD_GET],
		test_admin_first_time, test_admin_first_request, test_admin_first_response
	);

	http_route_stats_update (
		users_route->stats[REQUEST_METHOD_GET],
		test_admin_second_time, test_admin_second_request, test_admin_second_response
	);

	HttpRouteFileStats uploaded;
	(void) memset (&uploaded, 0, sizeof (HttpRouteFileStats));
	uploaded.n_uploaded_files = test_admin_uploaded_files;
	uploaded.min_file_size = test_admin_min_file_size;
	uploaded.max_file_size = test_admin_max_file_size;
	uploaded.sum_file_size = test_admin_sum_file_size;

	http_route_file_stats_update (upload_route->file_stats, &uploaded);

	http_route_stats_update (
		list->stats, test_admin_endpoint_time, test_admin_first_request, test_admin_first_response
	);

	http_route_file_stats_update (files->file_stats, &uploaded);

	http_cerver->n_incompleted_requests = 1;
	http_cerver->n_failed_auth_requests = 2;
	http_cerver->n_responses_by_status[HTTP_STATUS_OK] = test_admin_ok_responses;
	http_cerver->n_responses_by_status[HTTP_STATUS_NOT_FOUND] = test_admin_not_found_responses;
	http_cerver->n_aborted_requests[HTTP_STAGE_BODY][HTTP_ABORT_REASON_RESET] = 1;
	http_cerver->n_vanished_requests[HTTP_STAGE_HEADERS] = 2;
	http_cerver->n_health_requests = 1;

	test_http_admin_json_check (
		http_cerver_admin_generate_routes_stats_json (http_cerver),
		"{\"main_routes\": 1, \"children_routes\": 2, \"total_handlers\": 4, "
		"\"routes\": [{\"route\": \"/api/users\", \"handlers\": [{\"method\": \"GET\", \"requests\": 2, "
		"\"min_process_time\": 0.5, \"max_process_time\": 1.5, \"mean_process_time\": 1.0, "
		"\"min_request_size\": 100, \"max_request_size\": 300, \"mean_request_size\": 200, "
		"\"min_response_size\": 200, \"max_response_size\": 400, \"mean_response_size\": 300}], "
		"\"children\": [{\"route\": \"/api/users/login\", "
		"\"handlers\": [{\"method\": \"POST\", \"requests\": 0, "
		"\"min_process_time\": 0.0, \"max_process_time\": 0.0, \"mean_process_time\": 0.0, "
		"\"min_request_size\": 0, \"max_request_size\": 0, \"mean_request_size\": 0, "
		"\"min_response_size\": 0, \"max_response_size\": 0, \"mean_response_size\": 0}]}, "
		"{\"route\": \"/api/users/upload\", \"modifier\": \"Multi-Part\", "
		"\"handlers\": [{\"method\": \"GET\", \"requests\": 0, "
		"\"min_process_time\": 0.0, \"max_process_time\": 0.0, \"mean_process_time\": 0.0, "
		"\"min_request_size\": 0, \"max_request_size\": 0, \"mean_request_size\": 0, "
		"\"min_response_size\": 0, \"max_response_size\": 0, \"mean_response_size\": 0}, "
		"{\"method\": \"POST\", \"requests\": 0, "
		"\"min_process_time\": 0.0, \"max_process_time\": 0.0, \"mean_process_time\": 0.0, "
		"\"min_request_size\": 0, \"max_request_size\": 0, \"mean_request_size\": 0, "
		"\"min_response_size\": 0, \"max_response_size\": 0, \"mean_response_size\": 0, "
		"\"files\": {\"n_uploaded_files\": 2, \"min_n_files\": 2, \"max_n_files\": 2, \"mean_n_files\": 2.0, "
		"\"min_file_size\": 10, \"max_file_size\": 30, \"mean_file_size\": 20.0}}]}]}], "
		"\"endpoints\": [{\"method\": \"GET\", \"requests\": 1, "
		"\"min_process_time\": 0.25, \"max_process_time\": 0.25, \"mean_process_time\": 0.25, "
		"\"min_request_size\": 100, \"max_request_size\": 100, \"mean_request_size\": 100, "
		"\"min_response_size\": 200, \"max_response_size\": 200, \"mean_response_size\": 200, "
		"\"route\": \"/api/items/list\"}, "
		"{\"method\": \"POST\", \"requests\": 0, "
		"\"min_process_time\": 0.0, \"max_process_time\": 0.0, \"mean_process_time\": 0.0, "
		"\"min_request_size\": 0, \"max_request_size\": 0, \"mean_request_size\": 0, "
		"\"min_response_size\": 0, \"max_response_size\": 0, \"mean_response_size\": 0, "
		"\"route\": \"/api/items/files\", "
		"\"files\": {\"n_uploaded_files\": 2, \"min_n_files\": 2, \"max_n_files\": 2, \"mean_n_files\": 2.0, "
		"\"min_file_size\": 10, \"max_file_size\": 30, \"mean_file_size\": 20.0}}], "
		"\"n_incompleted_requests\": 1, \"n_unhandled_requests\": 0, \"n_catch_all_requests\": 0, "
		"\"n_failed_auth_requests\": 2, \"n_limited_requests\": 0, \"n_timeout_requests\": 0, "
		"\"n_unanswered_requests\": 0, \"n_failed_responses\": 0, "
		"\"n_responses_by_status\": {\"200\": 3, \"404\": 1}, "
		"\"n_aborted_requests\": {\"queued\": {\"closed\": 0, \"reset\": 0}, "
		"\"request_line\": {\"closed\": 0, \"reset\": 0}, \"headers\": {\"closed\": 0, \"reset\": 0}, "
		"\"body\": {\"closed\": 0, \"reset\": 1}, \"upload\": {\"closed\": 0, \"reset\": 0}, "
		"\"handler\": {\"closed\": 0, \"reset\": 0}, \"response\": {\"closed\": 0, \"reset\": 0}}, "
		"\"n_vanished_requests\": {\"queued\": 0, \"request_line\": 0, \"headers\": 2, \"body\": 0, "
		"\"upload\": 0, \"handler\": 0, \"response\": 0}, "
		"\"websockets\": {\"connections\": 0, \"n_opened\": 0, \"n_closed\": 0, "
		"\"n_messages_received\": 0, \"n_messages_sent\": 0, \"n_bytes_received\": 0, \"n_bytes_sent\": 0, "
		"\"n_failed_sends\": 0, \"n_errors\": 0, "
		"\"n_rejected\": {\"Version\": 0, \"Bad Request\": 0, \"Upgrade\": 0, \"Origin\": 0, \"Limit\": 0, "
		"\"Application\": 0}}, "
		"\"pools\": [], "
		"\"connections\": {\"n_reused\": 0, \"n_followed\": 0, \"n_pool_read\": 0, \"n_ended\": 0, \"n_requests\": 0}, "
		"\"n_health_requests\": 1, \"n_ready_requests\": 0, \"n_not_ready_requests\": 0, "
		"\"uploads_write_microseconds\": 0, \"uploads_write_bytes\": 0}"
	);

	(void) cerver_teardown (test_cerver);

}

// the file systems: a real that is not a number is left out
static void test_http_admin_json_file_systems (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	test_http_admin_json_check (
		http_cerver_admin_generate_file_systems_stats_json (http_cerver),
		"{\"filesystems\": []}"
	);

	// the stats of a path that does not exist stay as they are
	http_cerver_register_admin_file_system (http_cerver, test_admin_missing_file_system);
	http_cerver_register_admin_file_system (http_cerver, test_admin_missing_file_system);

	HttpAdminFileSystemStats *first = (HttpAdminFileSystemStats *) dlist_start (
		http_cerver->admin_file_systems_stats
	)->data;

	first->stats.total = test_admin_file_system_total;
	first->stats.available = test_admin_file_system_available;
	first->stats.used = test_admin_file_system_used;
	first->stats.used_percentage = test_admin_file_system_used_percentage;

	HttpAdminFileSystemStats *second = (HttpAdminFileSystemStats *) dlist_end (
		http_cerver->admin_file_systems_stats
	)->data;

	second->stats.used_percentage = (double) NAN;

	test_http_admin_json_check (
		http_cerver_admin_generate_file_systems_stats_json (http_cerver),
		"{\"filesystems\": ["
		"{\"path\": \"/missing/file/system\", \"total\": 2.5, \"available\": 1.5, \"used\": 1.0, \"used_percentage\": 40.0}, "
		"{\"path\": \"/missing/file/system\", \"total\": 0.0, \"available\": 0.0, \"used\": 0.0}"
		"]}"
	);

	(void) cerver_teardown (test_cerver);

}

// the workers, in the order that they were registered
static void test_http_admin_json_workers (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	test_http_admin_json_check (
		http_cerver_admin_generate_workers_json (http_cerver),
		"{\"workers\": []}"
	);

	Worker *first = worker_create_with_id (test_admin_first_worker);
	test_check_ptr (first);
	worker_set_name (first, "first");

	Worker *second = worker_create_with_id (test_admin_second_worker);
	test_check_ptr (second);
	worker_set_name (second, "the \\ second");

	http_cerver_register_admin_worker (http_cerver, first);
	http_cerver_register_admin_worker (http_cerver, second);

	test_http_admin_json_check (
		http_cerver_admin_generate_workers_json (http_cerver),
		"{\"workers\": ["
		"{\"id\": 1, \"name\": \"first\", \"state\": \"None\"}, "
		"{\"id\": 2, \"name\": \"the \\\\ second\", \"state\": \"None\"}"
		"]}"
	);

	(void) cerver_teardown (test_cerver);

	worker_delete (first);
	worker_delete (second);

}

#pragma endregion

void http_tests_admin (void) {

	(void) printf ("Testing HTTP admin integration...\n");

	// systems
	test_http_admin_file_system_stats_new ();
	test_http_admin_file_system_stats_create ();
	test_http_admin_register_file_system_null ();

	// generate
	test_http_admin_generate_jsons ();

	// bytes
	test_http_admin_json_info ();
	test_http_admin_json_stats ();
	test_http_admin_json_file_systems ();
	test_http_admin_json_workers ();

	(void) printf ("Done!\n");

}
