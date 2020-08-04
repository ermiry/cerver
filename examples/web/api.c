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
#include <cerver/http/json.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
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

// api/users
static void main_users_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Users route works!");
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
		http_respponse_delete (res);
	}

}

// api/users/login
static void users_login_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Users login!");
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
		http_respponse_delete (res);
	}

}

// api/users/register
static void users_register_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Users register!");
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
		http_respponse_delete (res);
	}

}

// api/users/:id
static void users_info_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_create ("User %s info!", request->params[0]->str);
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
		http_respponse_delete (res);
	}

}

// api/users/:id/profile
static void users_profile_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_create ("User %s profile!", request->params[0]->str);
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
		http_respponse_delete (res);
	}

}

// *
static void catch_all_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Cerver API implementation!");
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
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
		// /api/users
		HttpRoute *users_route = http_route_create ("api/users", main_users_handler);
		http_cerver_route_register (http_cerver, users_route);

		// register users child routes
		HttpRoute *users_login_route = http_route_create ("login", users_login_handler);
		http_route_child_add (users_route, users_login_route);

		HttpRoute *users_register_route = http_route_create ("register", users_register_handler);
		http_route_child_add (users_route, users_register_route);

		HttpRoute *users_info_route = http_route_create (":id", users_info_handler);
		http_route_set_auth (users_info_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
		http_route_child_add (users_route, users_info_route);

		HttpRoute *users_profile_route = http_route_create (":id/profile", users_profile_handler);
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