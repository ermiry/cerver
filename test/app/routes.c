#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/json/json.h>
#include <cerver/http/jwt/alg.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include "users.h"

// GET /api/users
void main_users_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (HTTP_STATUS_OK, "Users route works!");
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// POST /api/users/login
void users_login_handler (
	const HttpReceive *http_receive,
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

				HttpJwt *http_jwt = http_cerver_auth_jwt_new ();
				http_cerver_auth_jwt_add_value_int (http_jwt, "iat", time (NULL));
				http_cerver_auth_jwt_add_value (http_jwt, "id", user->id->str);
				http_cerver_auth_jwt_add_value (http_jwt, "name", user->name->str);
				http_cerver_auth_jwt_add_value (http_jwt, "username", user->username->str);
				http_cerver_auth_jwt_add_value (http_jwt, "role", user->role->str);

				if (!http_cerver_auth_generate_bearer_jwt_json (
					(HttpCerver *) http_receive->cr->cerver->cerver_data,
					http_jwt
				)) {
					HttpResponse *res = http_response_create (
						HTTP_STATUS_OK, http_jwt->json, strlen (http_jwt->json)
					);

					if (res) {
						http_response_compile (res);
						#ifdef EXAMPLES_DEBUG
						http_response_print (res);
						#endif
						http_response_send (res, http_receive);
						http_response_delete (res);
					}
				}

				else {
					HttpResponse *res = http_response_json_error (
						HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal error!"
					);

					if (res) {
						http_response_print (res);
						#ifdef EXAMPLES_DEBUG
						http_response_send (res, http_receive);
						#endif
						http_response_delete (res);
					}
				}

				http_cerver_auth_jwt_delete (http_jwt);
			}

			else {
				HttpResponse *res = http_response_json_error (
					HTTP_STATUS_BAD_REQUEST, "Password is incorrect!"
				);

				if (res) {
					http_response_print (res);
					#ifdef EXAMPLES_DEBUG
					http_response_send (res, http_receive);
					#endif
					http_response_delete (res);
				}
			}
		}

		else {
			HttpResponse *res = http_response_json_error (
				HTTP_STATUS_NOT_FOUND, "User not found!"
			);

			if (res) {
				#ifdef EXAMPLES_DEBUG
				http_response_print (res);
				#endif
				http_response_send (res, http_receive);
				http_response_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error (
			HTTP_STATUS_BAD_REQUEST, "Missing user values!"
		);

		if (res) {
			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif
			http_response_send (res, http_receive);
			http_response_delete (res);
		}
	}

}

// POST /api/users/register
void users_register_handler (
	const HttpReceive *http_receive,
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

			HttpResponse *res = http_response_json_msg (
				HTTP_STATUS_OK, "Created a new user!"
			);

			if (res) {
				#ifdef EXAMPLES_DEBUG
				http_response_print (res);
				#endif
				http_response_send (res, http_receive);
				http_response_delete (res);
			}
		}

		else {
			HttpResponse *res = http_response_json_error (
				HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal error!"
			);
			if (res) {
				#ifdef EXAMPLES_DEBUG
				http_response_print (res);
				#endif
				http_response_send (res, http_receive);
				http_response_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error (
			HTTP_STATUS_BAD_REQUEST, "Missing user values!"
		);

		if (res) {
			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif
			http_response_send (res, http_receive);
			http_response_delete (res);
		}
	}

}

// GET /api/users/profile
void users_profile_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	User *user = (User *) request->decoded_data;

	char *message = c_string_create ("%s profile!", user->name->str);

	HttpResponse *res = http_response_json_msg (HTTP_STATUS_OK, message);
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

	free (message);

}