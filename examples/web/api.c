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
#include <cerver/http/json/json.h>
#include <cerver/http/jwt/alg.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

Cerver *api_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {
	
	if (api_cerver) {
		cerver_stats_print (api_cerver);
		cerver_teardown (api_cerver);
	} 

	exit (0);

}

#pragma endregion

#pragma region users

typedef struct User {

	estring *id;
	estring *name;
	estring *username;
	estring *role;
	time_t iat;

} User;

static User *user_new (void) {

	User *user = (User *) malloc (sizeof (User));
	if (user) {
		user->id = NULL;
		user->name = NULL;
		user->username = NULL;
		user->role = NULL;
		user->iat = 0;
	}

	return user;

}

static void user_delete (void *user_ptr) {

	if (user_ptr) {
		User *user = (User *) user_ptr;

		estring_delete (user->id);
		estring_delete (user->name);
		estring_delete (user->username);
		estring_delete (user->role);

		free (user_ptr);

		printf ("user_delete () - User has been deleted!\n");
	}

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
			user->id = estring_new (id);
			user->name = estring_new (name);
			user->username = estring_new (username);
			user->role = estring_new (role);

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

// POST api/users/login
static void users_login_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg (200, "Users login!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

// POST api/users/register
static void users_register_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg (200, "Users register!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
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

	api_cerver = cerver_create (CERVER_TYPE_WEB, "api-cerver", 8080, PROTOCOL_TCP, false, 2, 1000);
	if (api_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (api_cerver, 4096);
		cerver_set_thpool_n_threads (api_cerver, 4);
		cerver_set_handler_type (api_cerver, CERVER_HANDLER_TYPE_THREADS);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) api_cerver->cerver_data;

		http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
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