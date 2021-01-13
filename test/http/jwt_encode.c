#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <cerver/cerver.h>

#include <cerver/http/http.h>

#include <cerver/http/jwt/alg.h>

#include "http.h"
#include "../test.h"
#include "../users.h"

void http_tests_jwt_encode (void) {

	(void) printf ("Testing HTTP jwt encode integration...\n");

	srand ((unsigned int) time (NULL));

	Cerver *test_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"test-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	test_check_ptr (test_cerver);

	/*** web cerver configuration ***/
	test_check_ptr (test_cerver->cerver_data);
	HttpCerver *http_cerver = (HttpCerver *) test_cerver->cerver_data;

	http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
	test_check_int_eq (http_cerver->jwt_alg, JWT_ALG_RS256, NULL);

	http_cerver_auth_set_jwt_priv_key_filename (http_cerver, private_key);
	test_check_ptr (http_cerver->jwt_opt_key_name);
	test_check_str_eq (http_cerver->jwt_opt_key_name->str, private_key, NULL);

	http_cerver_auth_set_jwt_pub_key_filename (http_cerver, public_key);
	test_check_ptr (http_cerver->jwt_opt_pub_key_name);
	test_check_str_eq (http_cerver->jwt_opt_pub_key_name->str, public_key, NULL);

	http_cerver_init (http_cerver);

	/*** test ***/
	// create a new user
	User *user = user_new ();
	test_check_ptr (user);

	user->id = str_create (STATIC_ID);
	user->name = str_new ("Erick Salas");
	user->username = str_new ("ermiry");
	user->password = str_new ("password");
	user->role = str_new ("common");

	// user_print (user);

	// create token
	HttpJwt *http_jwt = http_cerver_auth_jwt_new ();
	http_cerver_auth_jwt_add_value_int (http_jwt, "iat", STATIC_DATE);
	http_cerver_auth_jwt_add_value (http_jwt, "id", user->id->str);
	http_cerver_auth_jwt_add_value (http_jwt, "name", user->name->str);
	http_cerver_auth_jwt_add_value (http_jwt, "username", user->username->str);
	http_cerver_auth_jwt_add_value (http_jwt, "role", user->role->str);

	u8 result = http_cerver_auth_generate_bearer_jwt_json (
		http_cerver, http_jwt
	);

	test_check_unsigned_eq (result, 0, NULL);

	char expected_result[1024] = { 0 };
	(void) snprintf (expected_result, 1024, "{\"token\": \"Bearer %s\"}", base_token);
	// printf ("Expected: %s\n\n", expected_result);

	test_check_str_eq (http_jwt->json, expected_result, NULL);

	http_cerver_auth_jwt_delete (http_jwt);

	cerver_teardown (test_cerver);

	(void) printf ("Done!\n");

}