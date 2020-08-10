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

static DoubleList *users = NULL;

static Cerver *api_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {
	
	if (api_cerver) {
		cerver_stats_print (api_cerver, false, false);
		printf ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) api_cerver->cerver_data);
		printf ("\n");
		cerver_teardown (api_cerver);
	} 

	dlist_delete (users);

	exit (0);

}

#pragma endregion

#pragma region users

typedef struct User {

	String *id;
	String *name;
	String *username;
	String *password;
	String *role;
	time_t iat;

} User;

static User *user_new (void) {

	User *user = (User *) malloc (sizeof (User));
	if (user) {
		user->id = NULL;
		user->name = NULL;
		user->username = NULL;
		user->password = NULL;
		user->role = NULL;
		user->iat = 0;
	}

	return user;

}

static void user_delete (void *user_ptr) {

	if (user_ptr) {
		User *user = (User *) user_ptr;

		str_delete (user->id);
		str_delete (user->name);
		str_delete (user->username);
		str_delete (user->password);
		str_delete (user->role);

		free (user_ptr);

		printf ("user_delete () - User has been deleted!\n");
	}

}

static int user_comparator (const void *a, const void *b) {

	return strcmp (((User *) a)->username->str, ((User *) b)->username->str);

}

static void user_print (User *user) {

	if (user) {
		printf ("id: %s\n", user->id->str);
		printf ("name: %s\n", user->name->str);
		printf ("username: %s\n", user->username->str);
		printf ("role: %s\n", user->role->str);
	}

}

// {
//   "id": "5eb2b13f0051f70011e9d3af",
//   "name": "Erick Salas",
//   "username": "erick",
//   "role": "god",
//   "iat": 1596532954
// }
static void *user_parse_from_json (void *user_json_ptr) {

	json_t *user_json = (json_t *) user_json_ptr;

	User *user = user_new ();
	if (user) {
		const char *id = NULL;
		const char *name = NULL;
		const char *username = NULL;
		const char *role = NULL;

		if (!json_unpack (
			user_json,
			"{s:s, s:s, s:s, s:s, s:i}",
			"id", &id,
			"name", &name,
			"username", &username,
			"role", &role,
			"iat", &user->iat
		)) {
			user->id = str_new (id);
			user->name = str_new (name);
			user->username = str_new (username);
			user->role = str_new (role);

			user_print (user);
		}

		else {
			cerver_log_error ("user_parse_from_json () - json_unpack () has failed!");

			user_delete (user);
			user = NULL;
		}
	}

	return user;

}

// GET api/users
static void main_users_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg (200, "Users route works!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

static User *user_get_by_username (const char *username) {

	User *retval = NULL;

	for (ListElement *le = dlist_start (users); le; le = le->next) {
		if (!strcmp (((User *) le->data)->username->str, username)) {
			retval = (User *) le->data;
			break;
		}
	}

	return retval;

}

// POST api/users/login
static void users_login_handler (CerverReceive *cr, HttpRequest *request) {

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
				char *token = http_cerver_auth_generate_jwt ((HttpCerver *) cr->cerver->cerver_data, payload);
				if (token) {
					char *bearer = c_string_create ("Bearer %s", token);
					json_t *json = json_pack ("{s:s}", "token", bearer);
					char *json_str = json_dumps (json, 0);

					HttpResponse *res = http_response_create (200, json_str, strlen (json_str));
					if (res) {
						http_response_compile (res);
						http_response_print (res);
						http_response_send (res, cr->cerver, cr->connection);
						http_respponse_delete (res);
					}

					free (json_str);
					free (bearer);
					free (token);
				}

				else {
					HttpResponse *res = http_response_json_error (500, "Internal error!");
					if (res) {
						http_response_print (res);
						http_response_send (res, cr->cerver, cr->connection);
						http_respponse_delete (res);
					}
				}

				dlist_delete (payload);
			}

			else {
				HttpResponse *res = http_response_json_error (400, "Password is incorrect!");
				if (res) {
					http_response_print (res);
					http_response_send (res, cr->cerver, cr->connection);
					http_respponse_delete (res);
				}
			}
		}

		else {
			HttpResponse *res = http_response_json_error (404, "User not found!");
			if (res) {
				http_response_print (res);
				http_response_send (res, cr->cerver, cr->connection);
				http_respponse_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error (400, "Missing user values!");
		if (res) {
			http_response_print (res);
			http_response_send (res, cr->cerver, cr->connection);
			http_respponse_delete (res);
		}
	}

}

// POST api/users/register
static void users_register_handler (CerverReceive *cr, HttpRequest *request) {

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

			dlist_insert_after (users, dlist_end (users), user);

			printf ("\n");
			user_print (user);
			cerver_log_success ("Created a new user!");
			printf ("\n");

			HttpResponse *res = http_response_json_msg (200, "Created a new user!");
			if (res) {
				http_response_print (res);
				http_response_send (res, cr->cerver, cr->connection);
				http_respponse_delete (res);
			}
		}

		else {
			HttpResponse *res = http_response_json_error (500, "Internal error!");
			if (res) {
				http_response_print (res);
				http_response_send (res, cr->cerver, cr->connection);
				http_respponse_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error (400, "Missing user values!");
		if (res) {
			http_response_print (res);
			http_response_send (res, cr->cerver, cr->connection);
			http_respponse_delete (res);
		}
	}

}

// GET api/users/profile
static void users_profile_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;

	char *message = c_string_create ("%s profile!", user->name->str);

	HttpResponse *res = http_response_json_msg (200, message);
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

	free (message);

}

// *
static void catch_all_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg (200, "Cerver API implementation!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

#pragma endregion

#pragma region start

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Cerver Web API Example");
	printf ("\n");

	users = dlist_init (user_delete, user_comparator);

	api_cerver = cerver_create (CERVER_TYPE_WEB, "api-cerver", 8080, PROTOCOL_TCP, false, 2, 1000);
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
			char *s = c_string_create ("Failed to start %s!",
				api_cerver->info->name->str);
			if (s) {
				cerver_log_error (s);
				free (s);
			}

			cerver_delete (api_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (api_cerver);
	}

	return 0;

}

#pragma endregion