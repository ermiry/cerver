#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/responses/response.h>

#include "../../test.h"

static const char *uploads_file_system = { "/var/uploads" };

#define TEST_ADMIN_DECIMAL_BASE			10

static const char *test_admin_uptime_json_key = { "\"uptime\": " };
static const char *test_admin_uptime_metric = { "\ncerver_uptime_seconds " };

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

// the secs since the cerver of the info started, and the uptime that it keeps, that the generators do not touch
static const time_t test_admin_started_ago = 90;
static const u64 test_admin_kept_uptime = 7;

// the value after the key is the time since the cerver started, read in the second of the generation or the next
static void test_http_admin_uptime_check (const char *generated, const char *key) {

	const char *found = strstr (generated, key);
	test_check_ptr (found);

	const long long uptime = strtoll (found + strlen (key), NULL, TEST_ADMIN_DECIMAL_BASE);
	test_check (
		(uptime >= (long long) test_admin_started_ago) && (uptime <= (long long) test_admin_started_ago + 1),
		"the uptime is not the time since the cerver started"
	);

}

// the info and the metrics compute the uptime for each response, and do not write the one that the cerver keeps:
// the threads generate them at the same time (it was written without a lock)
static void test_http_admin_uptime (void) {

	Cerver *test_cerver = cerver_create_web ("test-cerver", 8080, 2);
	test_check_ptr (test_cerver);

	HttpCerver *http_cerver = http_cerver_get (test_cerver);

	test_cerver->info->time_started = time (NULL) - test_admin_started_ago;
	test_cerver->info->uptime = test_admin_kept_uptime;

	char *json = http_cerver_admin_generate_info_json (http_cerver);
	test_check_ptr (json);
	test_http_admin_uptime_check (json, test_admin_uptime_json_key);
	free (json);

	String *metrics = http_cerver_metrics_generate (http_cerver);
	test_check_ptr (metrics);
	test_http_admin_uptime_check (metrics->str, test_admin_uptime_metric);
	str_delete (metrics);

	test_check_unsigned_eq ((unsigned int) test_cerver->info->uptime, test_admin_kept_uptime, "the kept uptime was written");

	(void) cerver_teardown (test_cerver);

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
	test_http_admin_uptime ();

	(void) printf ("Done!\n");

}
