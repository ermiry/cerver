#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/cerver.h>

#include <cerver/http/admin.h>
#include <cerver/http/http.h>
#include <cerver/http/response.h>

#include "../test.h"

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

#pragma endregion

#pragma region generate

// GET /api/users
void main_users_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_send (oki_doki, http_receive);

}

// POST /api/users/login
void users_login_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_send (oki_doki, http_receive);

}

// POST /api/users/register
void users_register_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_send (oki_doki, http_receive);

}

// POST /api/users/upload
void users_upload_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

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

	cerver_teardown (test_cerver);

}

#pragma endregion

void http_tests_admin (void) {

	(void) printf ("Testing HTTP admin integration...\n");

	// systems
	test_http_admin_file_system_stats_new ();
	test_http_admin_file_system_stats_create ();

	// generate
	test_http_admin_generate_jsons ();

	(void) printf ("Done!\n");

}
