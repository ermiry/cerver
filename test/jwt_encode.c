#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <cerver/cerver.h>
#include <cerver/version.h>

#include <cerver/http/http.h>
#include <cerver/http/json/json.h>
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
			DoubleList *payload = dlist_init (key_value_pair_delete, NULL);
			dlist_insert_at_end_unsafe (payload, key_value_pair_create ("id", user->id->str));
			dlist_insert_at_end_unsafe (payload, key_value_pair_create ("name", user->name->str));
			dlist_insert_at_end_unsafe (payload, key_value_pair_create ("username", user->username->str));
			dlist_insert_at_end_unsafe (payload, key_value_pair_create ("role", user->role->str));

			char *token = http_cerver_auth_generate_jwt (
				http_cerver, payload
			);

			if (token) {
				char *bearer = c_string_create ("Bearer %s", token);
				json_t *json = json_pack ("{s:s}", "token", bearer);
				char *json_str = json_dumps (json, 0);

				(void) printf ("\n\n%s\n\n", json_str);

				free (json_str);
				free (bearer);
				free (token);
			}

			else {
				cerver_log_error ("Failed to generate token!\n");
			}

			dlist_delete (payload);
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