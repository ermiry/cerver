#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include <app/users.h>

static Cerver *auth_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	if (auth_cerver) {
		cerver_stats_print (auth_cerver, false, false);
		cerver_log_msg ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) auth_cerver->cerver_data);
		cerver_log_line_break ();
		cerver_teardown (auth_cerver);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region routes

// GET /test
void test_handler (
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

// GET /auth/token
void auth_token_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "Token auth route works!"
	);
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// POST /auth/custom
void auth_custom_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "Custom auth route works!"
	);
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

#pragma endregion

#pragma region start

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

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Web Cerver Authentication Example");
	cerver_log_line_break ();

	auth_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"web-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	if (auth_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (auth_cerver, 4096);
		cerver_set_thpool_n_threads (auth_cerver, 4);
		cerver_set_handler_type (auth_cerver, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (auth_cerver, true);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) auth_cerver->cerver_data;

		// bearer token auth configuration
		http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
		http_cerver_auth_set_jwt_priv_key_filename (http_cerver, "keys/key.key");
		http_cerver_auth_set_jwt_pub_key_filename (http_cerver, "keys/key.pub");

		// GET /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// GET /auth/token
		HttpRoute *auth_token_route = http_route_create (REQUEST_METHOD_GET, "auth/token", auth_token_handler);
		http_route_set_auth (auth_token_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
		http_route_set_decode_data (auth_token_route, user_parse_from_json, user_delete);
		http_cerver_route_register (http_cerver, auth_token_route);

		// GET /auth/custom
		HttpRoute *auth_custom_route = http_route_create (REQUEST_METHOD_POST, "auth/custom", auth_custom_handler);
		http_route_set_auth (auth_custom_route, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
		http_route_set_authentication_handler (auth_custom_route, custom_authentication_handler);
		http_cerver_route_register (http_cerver, auth_custom_route);

		if (cerver_start (auth_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				auth_cerver->info->name
			);

			cerver_delete (auth_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (auth_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion
