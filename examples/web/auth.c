#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/security/auth.h>

#include <cerver/jwt/jwt.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include <users.h>

static Cerver *auth_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (auth_cerver) (void) cerver_shutdown (auth_cerver);

}

// the signals only stop the cerver, it is released when cerver_start () returns
static void finish (void) {

	cerver_stats_print (auth_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (auth_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (auth_cerver);

}

#pragma endregion

#pragma region routes

// GET /test
void test_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Test route works!"
	);

}

// GET /auth/token
void auth_token_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Token auth route works!"
	);

}

// POST /auth/custom
void auth_custom_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Custom auth route works!"
	);

}

#pragma endregion

#pragma region start

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

int main (void) {

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

		cerver_set_reusable_address_flags (auth_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (auth_cerver);

		// bearer token auth configuration
		cerver_http_auth_set_jwt_algorithm (http_cerver, CERVER_JWT_ALGORITHM_RS256);
		cerver_http_auth_set_jwt_priv_key_filename (http_cerver, "keys/key.key");
		cerver_http_auth_set_jwt_pub_key_filename (http_cerver, "keys/key.key.pub");

		// GET /test
		CerverHttpGroup *test_group = cerver_http_group_create (http_cerver, "/test");
		(void) cerver_http_group_root_add (test_group, REQUEST_METHOD_GET, test_handler);

		CerverHttpGroup *auth_group = cerver_http_group_create (http_cerver, "/auth");

		// GET /auth/token
		CerverHttpEndpoint *auth_token = cerver_http_group_get (auth_group, "token", auth_token_handler);
		cerver_http_endpoint_set_auth (auth_token, HTTP_ROUTE_AUTH_TYPE_BEARER);
		cerver_http_endpoint_set_decode_data (auth_token, user_parse_from_json, user_delete);

		// POST /auth/custom
		CerverHttpEndpoint *auth_custom = cerver_http_group_post (auth_group, "custom", auth_custom_handler);
		cerver_http_endpoint_set_auth (auth_custom, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
		cerver_http_endpoint_set_authentication_handler (auth_custom, custom_authentication_handler);

		// the handler authenticates with a value of the multi-part body
		cerver_http_endpoint_set_authentication_with_body (auth_custom, true);

		if (!cerver_start (auth_cerver)) {
			finish ();
		}

		else {
			cerver_log_error (
				"Failed to start %s!",
				auth_cerver->info->name
			);

			(void) cerver_teardown (auth_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion
