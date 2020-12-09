#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/json/json.h>
#include <cerver/http/jwt/alg.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include <users.h>

static Cerver *api_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	if (api_cerver) {
		cerver_stats_print (api_cerver, false, false);
		cerver_log_msg ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) api_cerver->cerver_data);
		cerver_log_line_break ();
		cerver_teardown (api_cerver);
	}

	cerver_end ();

	users_end ();

	exit (0);

}

#pragma endregion

#pragma region users

// GET api/users
static void main_users_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg ((http_status) 200, "Users route works!");
	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

// POST api/users/login
static void users_login_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	// get the user values from the request
	const String *username = http_query_pairs_get_value (request->body_values, "username");
	const String *password = http_query_pairs_get_value (request->body_values, "password");

	if (username && password) {
		// get user by username
		User *user = user_get_by_username (username->str);
		if (user) {
			if (!strcmp (user->password->str, password->str)) {
				// printf ("\nPasswords match!\n");

				DoubleList *payload = dlist_init (key_value_pair_delete, NULL);
				dlist_insert_after (payload, dlist_end (payload), key_value_pair_create ("id", user->id->str));
				dlist_insert_after (payload, dlist_end (payload), key_value_pair_create ("name", user->name->str));
				dlist_insert_after (payload, dlist_end (payload), key_value_pair_create ("username", user->username->str));
				dlist_insert_after (payload, dlist_end (payload), key_value_pair_create ("role", user->role->str));

				// generate & send back auth token
				char *token = http_cerver_auth_generate_jwt ((HttpCerver *) http_receive->cr->cerver->cerver_data, payload);
				if (token) {
					char *bearer = c_string_create ("Bearer %s", token);
					json_t *json = json_pack ("{s:s}", "token", bearer);
					char *json_str = json_dumps (json, 0);

					HttpResponse *res = http_response_create (200, json_str, strlen (json_str));
					if (res) {
						http_response_compile (res);
						http_response_print (res);
						http_response_send (res, http_receive);
						http_respponse_delete (res);
					}

					free (json_str);
					free (bearer);
					free (token);
				}

				else {
					HttpResponse *res = http_response_json_error ((http_status) 500, "Internal error!");
					if (res) {
						http_response_print (res);
						http_response_send (res, http_receive);
						http_respponse_delete (res);
					}
				}

				dlist_delete (payload);
			}

			else {
				HttpResponse *res = http_response_json_error ((http_status) 400, "Password is incorrect!");
				if (res) {
					http_response_print (res);
					http_response_send (res, http_receive);
					http_respponse_delete (res);
				}
			}
		}

		else {
			HttpResponse *res = http_response_json_error ((http_status) 404, "User not found!");
			if (res) {
				http_response_print (res);
				http_response_send (res, http_receive);
				http_respponse_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error ((http_status) 400, "Missing user values!");
		if (res) {
			http_response_print (res);
			http_response_send (res, http_receive);
			http_respponse_delete (res);
		}
	}

}

// POST api/users/register
static void users_register_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	// get the user values from the request
	const String *name = http_query_pairs_get_value (request->body_values, "name");
	const String *username = http_query_pairs_get_value (request->body_values, "username");
	const String *password = http_query_pairs_get_value (request->body_values, "password");

	if (name && username && password) {
		// register a new user
		User *user = user_new ();
		if (user) {
			user->id = str_create ("%ld", time (NULL));
			user->name = str_new (name->str);
			user->username = str_new (username->str);
			user->password = str_new (password->str);
			user->role = str_new ("common");

			user_add (user);

			printf ("\n");
			user_print (user);
			cerver_log_success ("Created a new user!");
			printf ("\n");

			HttpResponse *res = http_response_json_msg ((http_status) 200, "Created a new user!");
			if (res) {
				http_response_print (res);
				http_response_send (res, http_receive);
				http_respponse_delete (res);
			}
		}

		else {
			HttpResponse *res = http_response_json_error ((http_status) 500, "Internal error!");
			if (res) {
				http_response_print (res);
				http_response_send (res, http_receive);
				http_respponse_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error ((http_status) 400, "Missing user values!");
		if (res) {
			http_response_print (res);
			http_response_send (res, http_receive);
			http_respponse_delete (res);
		}
	}

}

// GET api/users/profile
static void users_profile_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	User *user = (User *) request->decoded_data;

	char *message = c_string_create ("%s profile!", user->name->str);

	HttpResponse *res = http_response_json_msg ((http_status) 200, message);
	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

	free (message);

}

// *
static void catch_all_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg ((http_status) 200, "Cerver API implementation!");
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

	cerver_log_debug ("Cerver Web API Example");
	printf ("\n");

	users_init ();

	api_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"api-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);
	
	if (api_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (api_cerver, 4096);
		cerver_set_thpool_n_threads (api_cerver, 4);
		cerver_set_handler_type (api_cerver, CERVER_HANDLER_TYPE_THREADS);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) api_cerver->cerver_data;

		http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
		http_cerver_auth_set_jwt_priv_key_filename (http_cerver, "keys/key.key");
		http_cerver_auth_set_jwt_pub_key_filename (http_cerver, "keys/key.key.pub");

		// register top level routes
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

		// GET api/users/profile
		HttpRoute *users_profile_route = http_route_create (REQUEST_METHOD_GET, "profile", users_profile_handler);
		http_route_set_auth (users_profile_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
		http_route_set_decode_data (users_profile_route, user_parse_from_json, user_delete);
		http_route_child_add (users_route, users_profile_route);

		// add a catch all route
		http_cerver_set_catch_all_route (http_cerver, catch_all_handler);

		if (cerver_start (api_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				api_cerver->info->name->str
			);

			cerver_delete (api_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (api_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion