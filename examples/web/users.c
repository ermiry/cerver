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

#include <users.h>

// GET /api/users
void main_users_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg ((http_status) 200, "Users route works!");
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_respponse_delete (res);
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

				DoubleList *payload = dlist_init (key_value_pair_delete, NULL);
				(void) dlist_insert_at_end_unsafe (payload, key_value_pair_create ("id", user->id->str));
				(void) dlist_insert_at_end_unsafe (payload, key_value_pair_create ("name", user->name->str));
				(void) dlist_insert_at_end_unsafe (payload, key_value_pair_create ("username", user->username->str));
				(void) dlist_insert_at_end_unsafe (payload, key_value_pair_create ("role", user->role->str));

				// generate & send back auth token
				char *token = http_cerver_auth_generate_jwt (
					(HttpCerver *) http_receive->cr->cerver->cerver_data, payload
				);

				if (token) {
					char *bearer = c_string_create ("Bearer %s", token);
					json_t *json = json_pack ("{s:s}", "token", bearer);
					char *json_str = json_dumps (json, 0);

					HttpResponse *res = http_response_create (200, json_str, strlen (json_str));
					if (res) {
						http_response_compile (res);
						#ifdef EXAMPLES_DEBUG
						http_response_print (res);
						#endif
						http_response_send (res, http_receive);
						http_respponse_delete (res);
					}

					free (json_str);
					free (bearer);
					free (token);
				}

				else {
					HttpResponse *res = http_response_json_error (
						(http_status) 500, "Internal error!"
					);
					if (res) {
						http_response_print (res);
						#ifdef EXAMPLES_DEBUG
						http_response_send (res, http_receive);
						#endif
						http_respponse_delete (res);
					}
				}

				dlist_delete (payload);
			}

			else {
				HttpResponse *res = http_response_json_error (
					(http_status) 400, "Password is incorrect!"
				);
				if (res) {
					http_response_print (res);
					#ifdef EXAMPLES_DEBUG
					http_response_send (res, http_receive);
					#endif
					http_respponse_delete (res);
				}
			}
		}

		else {
			HttpResponse *res = http_response_json_error (
				(http_status) 404, "User not found!"
			);
			if (res) {
				#ifdef EXAMPLES_DEBUG
				http_response_print (res);
				#endif
				http_response_send (res, http_receive);
				http_respponse_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error (
			(http_status) 400, "Missing user values!"
		);
		if (res) {
			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif
			http_response_send (res, http_receive);
			http_respponse_delete (res);
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
				(http_status) 200, "Created a new user!"
			);
			if (res) {
				#ifdef EXAMPLES_DEBUG
				http_response_print (res);
				#endif
				http_response_send (res, http_receive);
				http_respponse_delete (res);
			}
		}

		else {
			HttpResponse *res = http_response_json_error (
				(http_status) 500, "Internal error!"
			);
			if (res) {
				#ifdef EXAMPLES_DEBUG
				http_response_print (res);
				#endif
				http_response_send (res, http_receive);
				http_respponse_delete (res);
			}
		}
	}

	else {
		HttpResponse *res = http_response_json_error (
			(http_status) 400, "Missing user values!"
		);
		if (res) {
			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif
			http_response_send (res, http_receive);
			http_respponse_delete (res);
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

	HttpResponse *res = http_response_json_msg ((http_status) 200, message);
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

	free (message);

}