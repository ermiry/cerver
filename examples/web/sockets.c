#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include <users.h>
#include <web/users.h>

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

	if (web_cerver) {
		cerver_stats_print (web_cerver, false, false);
		cerver_log_msg ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) web_cerver->cerver_data);
		cerver_log_line_break ();
		cerver_teardown (web_cerver);
	}

	cerver_end ();

	users_end ();

	exit (0);

}

#pragma endregion

#pragma region routes

// GET /
static void main_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (http_response_render_file (http_receive, "./examples/web/public/echo.html")) {
		cerver_log_error ("Failed to send ./examples/web/public/echo.html");
	}

}

static void test_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		(http_status) 200, "Test route works!"
	);

	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

static void echo_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) printf ("\n\n");
	cerver_log_success ("echo_handler ()");
	(void) printf ("\n");

}

static void echo_handler_on_open (const HttpReceive *http_receive) {

	(void) printf ("echo_handler_on_open ()\n");

}

static void echo_handler_on_close (
	const HttpReceive *http_receive, const char *reason
) {

	(void) printf ("echo_handler_on_close ()\n");

}

static void echo_handler_on_message (
	const HttpReceive *http_receive,
	const char *msg, const size_t msg_len
) {

	(void) printf ("echo_handler_on_message ()\n");

	(void) printf ("message[%ld]: %.*s\n", msg_len, (int) msg_len, msg);

	http_web_sockets_send (
		http_receive,
		msg, msg_len
	);

}

static void auth_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) printf ("\n\n");
	cerver_log_success ("Auth route works!");
	(void) printf ("\n");

}

static void auth_handler_on_open (const HttpReceive *http_receive) {

	(void) printf ("auth_handler_on_open ()\n");

}

static void auth_handler_on_close (
	const HttpReceive *http_receive, const char *reason
) {

	(void) printf ("auth_handler_on_close ()\n");

}

static void auth_handler_on_message (
	const HttpReceive *http_receive,
	const char *msg, const size_t msg_len
) {

	(void) printf ("auth_handler_on_message ()\n");

}

static void chat_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		(http_status) 200, "Chat route works!"
	);
	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

#pragma endregion

#pragma region start

int main (int argc, char **argv) {

	srand (time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	cerver_log_debug ("Simple Web Cerver Example");
	printf ("\n");

	users_init ();

	web_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"web-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

		http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
		http_cerver_auth_set_jwt_priv_key_filename (http_cerver, "keys/key.key");
		http_cerver_auth_set_jwt_pub_key_filename (http_cerver, "keys/key.key.pub");

		http_cerver_static_path_add (http_cerver, "./examples/web/public");

		// GET /
		HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "/", main_handler);
		http_cerver_route_register (http_cerver, main_route);

		// GET /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// GET /echo
		HttpRoute *echo_route = http_route_create (REQUEST_METHOD_GET, "echo", echo_handler);
		http_route_set_modifier (echo_route, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
		http_route_set_execute_handler (echo_route, true);
		http_route_set_ws_on_open (echo_route, echo_handler_on_open);
		http_route_set_ws_on_message (echo_route, echo_handler_on_message);
		http_route_set_ws_on_close (echo_route, echo_handler_on_close);
		http_cerver_route_register (http_cerver, echo_route);

		// GET /auth
		HttpRoute *auth_route = http_route_create (REQUEST_METHOD_GET, "auth", auth_handler);
		http_route_set_auth (auth_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
		http_route_set_decode_data (auth_route, user_parse_from_json, user_delete);
		http_route_set_modifier (auth_route, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
		http_route_set_execute_handler (auth_route, true);
		http_route_set_ws_on_open (auth_route, auth_handler_on_open);
		http_route_set_ws_on_message (auth_route, auth_handler_on_message);
		http_route_set_ws_on_close (auth_route, auth_handler_on_close);
		http_cerver_route_register (http_cerver, auth_route);

		// GET /chat
		HttpRoute *chat_route = http_route_create (REQUEST_METHOD_GET, "chat", chat_handler);
		http_route_set_modifier (chat_route, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
		http_cerver_route_register (http_cerver, chat_route);

		/*** users ***/
		// GET /api/users
		HttpRoute *users_route = http_route_create (REQUEST_METHOD_GET, "api/users", main_users_handler);
		http_cerver_route_register (http_cerver, users_route);

		// register users child routes
		// POST /api/users/login
		HttpRoute *users_login_route = http_route_create (REQUEST_METHOD_POST, "login", users_login_handler);
		http_route_child_add (users_route, users_login_route);

		// POST /api/users/register
		HttpRoute *users_register_route = http_route_create (REQUEST_METHOD_POST, "register", users_register_handler);
		http_route_child_add (users_route, users_register_route);

		// GET /api/users/profile
		HttpRoute *users_profile_route = http_route_create (REQUEST_METHOD_GET, "profile", users_profile_handler);
		http_route_set_auth (users_profile_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
		http_route_set_decode_data (users_profile_route, user_parse_from_json, user_delete);
		http_route_child_add (users_route, users_profile_route);

		if (cerver_start (web_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				web_cerver->info->name->str
			);

			cerver_delete (web_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (web_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion