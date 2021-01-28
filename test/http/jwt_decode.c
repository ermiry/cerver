#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <cerver/cerver.h>

#include <cerver/http/http.h>

#include <cerver/http/jwt/alg.h>

#include <app/users.h>

#include "http.h"

#include "../test.h"

void http_tests_jwt_decode (void) {

	(void) printf ("Testing HTTP jwt decode integration...\n");

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
	User *decoded_user = NULL;

	bool result = http_cerver_auth_validate_jwt (
		http_cerver,
		bearer_token,
		user_parse_from_json,
		((void **) &decoded_user)
	);

	test_check_bool_eq (result, true, NULL);
	test_check_ptr (decoded_user);

	test_check_int_eq (decoded_user->iat, STATIC_DATE, NULL);
	test_check_str_eq (decoded_user->id->str, STATIC_ID, NULL);
	test_check_str_len (decoded_user->id->str, strlen (STATIC_ID), NULL);
	test_check_str_eq (decoded_user->name->str, "Erick Salas", NULL);
	test_check_str_len (decoded_user->name->str, strlen ("Erick Salas"), NULL);
	test_check_str_eq (decoded_user->username->str, "ermiry", NULL);
	test_check_str_len (decoded_user->username->str, strlen ("ermiry"), NULL);
	test_check_str_eq (decoded_user->role->str, "common", NULL);
	test_check_str_len (decoded_user->role->str, strlen ("common"), NULL);

	user_delete (decoded_user);

	cerver_teardown (test_cerver);

	(void) printf ("Done!\n");

}