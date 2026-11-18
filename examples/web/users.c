#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/security/auth.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include <users.h>

// GET /api/users
void main_users_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Users route works!"
	);

}

// answers with the bearer token of the user
static void users_login_token (CerverHttpContext *context, const User *user) {

	HttpJwt *http_jwt = cerver_http_auth_jwt_new ();
	cerver_http_auth_jwt_add_value_int (http_jwt, "iat", time (NULL));
	cerver_http_auth_jwt_add_value (http_jwt, "id", user->id->str);
	cerver_http_auth_jwt_add_value (http_jwt, "name", user->name->str);
	cerver_http_auth_jwt_add_value (http_jwt, "username", user->username->str);
	cerver_http_auth_jwt_add_value (http_jwt, "role", user->role->str);

	if (!cerver_http_auth_generate_bearer_jwt_json (
		cerver_http_context_http (context), http_jwt
	)) {
		(void) cerver_http_response_send_json (
			context, http_jwt->json, strlen (http_jwt->json)
		);
	}

	else {
		(void) cerver_http_response_send_json_error (
			context, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal error!"
		);
	}

	cerver_http_auth_jwt_delete (http_jwt);

}

static void users_login_user (
	CerverHttpContext *context, const User *user, const CerverHttpView password
) {

	if (cerver_http_view_equals (password, user->password->str)) {
		users_login_token (context, user);
	}

	else {
		(void) cerver_http_response_send_json_error (
			context, HTTP_STATUS_BAD_REQUEST, "Password is incorrect!"
		);
	}

}

// POST /api/users/login
void users_login_handler (CerverHttpContext *context) {

	// the values of the user, from the form of the request
	const CerverHttpView username = cerver_http_request_form_value (context, "username");
	const CerverHttpView password = cerver_http_request_form_value (context, "password");

	if (cerver_http_view_is_missing (username) || cerver_http_view_is_missing (password)) {
		(void) cerver_http_response_send_json_error (
			context, HTTP_STATUS_BAD_REQUEST, "Missing user values!"
		);

		return;
	}

	const User *user = user_get_by_username (username.data);
	if (user) {
		users_login_user (context, user, password);
	}

	else {
		(void) cerver_http_response_send_json_error (
			context, HTTP_STATUS_NOT_FOUND, "User not found!"
		);
	}

}

static User *users_register_user (
	const CerverHttpView name, const CerverHttpView username, const CerverHttpView password
) {

	User *user = user_new ();
	if (user) {
		user->id = str_create ("%ld", time (NULL));
		user->name = str_new (name.data);
		user->username = str_new (username.data);
		user->password = str_new (password.data);
		user->role = str_new ("common");

		user_add (user);

		printf ("\n");
		user_print (user);
		cerver_log_success ("Created a new user!");
		printf ("\n");
	}

	return user;

}

// POST /api/users/register
void users_register_handler (CerverHttpContext *context) {

	// the values of the user, from the form of the request
	const CerverHttpView name = cerver_http_request_form_value (context, "name");
	const CerverHttpView username = cerver_http_request_form_value (context, "username");
	const CerverHttpView password = cerver_http_request_form_value (context, "password");

	if (
		cerver_http_view_is_missing (name)
		|| cerver_http_view_is_missing (username)
		|| cerver_http_view_is_missing (password)
	) {
		(void) cerver_http_response_send_json_error (
			context, HTTP_STATUS_BAD_REQUEST, "Missing user values!"
		);
	}

	else if (users_register_user (name, username, password)) {
		(void) cerver_http_response_send_json_message (
			context, HTTP_STATUS_OK, "Created a new user!"
		);
	}

	else {
		(void) cerver_http_response_send_json_error (
			context, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal error!"
		);
	}

}

// GET /api/users/profile
void users_profile_handler (CerverHttpContext *context) {

	// the user that the decode data method of the endpoint created from the token
	const User *user = (const User *) cerver_http_request_decoded_data (context);

	char *message = c_string_create ("%s profile!", user->name->str);

	(void) cerver_http_response_send_json_message (context, HTTP_STATUS_OK, message);

	free (message);

}
