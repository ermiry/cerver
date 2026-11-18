#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

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
#include <web/users.h>

static Cerver *api_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (api_cerver) (void) cerver_shutdown (api_cerver);

}

// the signals only stop the cerver, it is released when cerver_start () returns
static void finish (void) {

	cerver_stats_print (api_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (api_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (api_cerver);

	users_end ();

}

#pragma endregion

#pragma region routes

// *
static void catch_all_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Cerver API implementation!"
	);

}

#pragma endregion

#pragma region start

int main (void) {

	srand (time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	cerver_log_debug ("Cerver Web API Example");
	printf ("\n");

	users_init ();

	// a web cerver handles its connections with event loops, one for each processor
	api_cerver = cerver_create_web ("api-cerver", 8080, 2);

	if (api_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (api_cerver, 4096);
		cerver_set_thpool_n_threads (api_cerver, 4);

		cerver_set_reusable_address_flags (api_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http = cerver_http_get (api_cerver);

		cerver_http_auth_set_jwt_algorithm (http, CERVER_JWT_ALGORITHM_RS256);
		cerver_http_auth_set_jwt_priv_key_filename (http, "keys/key.key");
		cerver_http_auth_set_jwt_pub_key_filename (http, "keys/key.key.pub");

		// the users routes are the endpoints of a group
		CerverHttpGroup *users = cerver_http_group_create (http, "/api/users");

		// GET /api/users
		(void) cerver_http_group_root_add (users, REQUEST_METHOD_GET, main_users_handler);

		// POST /api/users/login
		(void) cerver_http_group_post (users, "login", users_login_handler);

		// POST /api/users/register
		(void) cerver_http_group_post (users, "register", users_register_handler);

		// GET /api/users/profile
		CerverHttpEndpoint *profile = cerver_http_group_get (users, "profile", users_profile_handler);
		cerver_http_endpoint_set_auth (profile, HTTP_ROUTE_AUTH_TYPE_BEARER);
		cerver_http_endpoint_set_decode_data (profile, user_parse_from_json, user_delete);

		// add a catch all route
		cerver_http_set_catch_all_route (http, catch_all_handler);

		if (!cerver_start (api_cerver)) {
			finish ();
		}

		else {
			cerver_log_error (
				"Failed to start %s!",
				api_cerver->info->name
			);

			(void) cerver_teardown (api_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion
