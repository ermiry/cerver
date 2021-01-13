#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <cerver/cerver.h>
#include <cerver/version.h>

#include <cerver/http/http.h>

#include <cerver/http/jwt/alg.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include "users.h"

static Cerver *api_cerver = NULL;

int main (int argc, char **argv) {

	srand (time (NULL));

	cerver_init ();

	cerver_version_print_full ();

	cerver_log_debug ("Cerver JWT encode test");
	printf ("\n");

	api_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"api-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	if (api_cerver) {
		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) api_cerver->cerver_data;

		http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
		http_cerver_auth_set_jwt_priv_key_filename (http_cerver, "keys/key.key");
		http_cerver_auth_set_jwt_pub_key_filename (http_cerver, "keys/key.key.pub");

		http_cerver_init (http_cerver);

		/*** test ***/
		// create a new user
		User *user = user_new ();
		if (user) {
			user->id = str_create ("%ld", time (NULL));
			user->name = str_new ("Erick Salas");
			user->username = str_new ("ermiry");
			user->password = str_new ("password");
			user->role = str_new ("common");

			user_print (user);

			// create token
			HttpJwt *http_jwt = http_cerver_auth_jwt_new ();
			http_cerver_auth_jwt_add_value (http_jwt, "id", user->id->str);
			http_cerver_auth_jwt_add_value (http_jwt, "name", user->name->str);
			http_cerver_auth_jwt_add_value (http_jwt, "username", user->username->str);
			http_cerver_auth_jwt_add_value (http_jwt, "role", user->role->str);

			if (!http_cerver_auth_generate_bearer_jwt_json (
				http_cerver, http_jwt
			)) {
				(void) printf ("\n\n%s\n\n", http_jwt->json);
			}

			else {
				cerver_log_error ("Failed to generate token!\n");
			}

			http_cerver_auth_jwt_delete (http_jwt);
		}

		cerver_teardown (api_cerver);
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (api_cerver);
	}

	cerver_end ();

	return 0;

}