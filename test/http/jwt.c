#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/http/http.h>

#include "../test.h"

static HttpJwt *test_http_jwt_new (void) {

	HttpJwt *http_jwt = (HttpJwt *) http_jwt_new ();

	test_check_ptr (http_jwt);
	test_check_null_ptr (http_jwt->jwt);
	test_check_int_eq (http_jwt->n_values, 0, NULL);

	return http_jwt;

}

static void test_http_jwt_add_value (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");

	test_check_int_eq ((int) http_jwt->n_values, 1, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	http_jwt_delete (http_jwt);

}

static void test_http_jwt_add_value_bool (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value_bool (http_jwt, "first", true);

	test_check_int_eq ((int) http_jwt->n_values, 1, NULL);

	test_check_str_eq ("first", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("first"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_bool_eq (true, http_jwt->values[0].value_bool, NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_add_value_int (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value_int (http_jwt, "test", 128);

	test_check_int_eq ((int) http_jwt->n_values, 1, NULL);

	test_check_str_eq ("test", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("test"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_int_eq (128, http_jwt->values[0].value_int, NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_add_value_multiple (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");
	http_cerver_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	http_cerver_auth_jwt_add_value_int (http_jwt, "value", 7);
	http_cerver_auth_jwt_add_value_bool (http_jwt, "flag", false);
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");

	test_check_int_eq ((int) http_jwt->n_values, 5, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	test_check_str_eq ("email", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("email"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq ("erick.salas@ermiry.com", http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen ("erick.salas@ermiry.com"), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("value", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("value"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_int_eq (7, http_jwt->values[2].value_int, NULL);

	test_check_str_eq ("flag", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("flag"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_bool_eq (false, http_jwt->values[3].value_bool, NULL);

	test_check_str_eq ("name", http_jwt->values[4].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[4].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[4].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[4].value_str), NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_add_value_many (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");
	http_cerver_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	http_cerver_auth_jwt_add_value (http_jwt, "role", "0123456789abcdf");

	test_check_int_eq ((int) http_jwt->n_values, 4, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	test_check_str_eq ("email", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("email"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq ("erick.salas@ermiry.com", http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen ("erick.salas@ermiry.com"), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("name", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[2].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[2].value_str), NULL);

	test_check_str_eq ("role", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("role"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_str_eq ("0123456789abcdf", http_jwt->values[3].value_str, NULL);
	test_check_int_eq ((int) strlen ("0123456789abcdf"), (int) strlen (http_jwt->values[3].value_str), NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_generate (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");
	http_cerver_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	http_cerver_auth_jwt_add_value (http_jwt, "role", "0123456789abcdf");

	test_check_int_eq ((int) http_jwt->n_values, 4, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	test_check_str_eq ("email", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("email"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq ("erick.salas@ermiry.com", http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen ("erick.salas@ermiry.com"), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("name", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[2].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[2].value_str), NULL);

	test_check_str_eq ("role", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("role"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_str_eq ("0123456789abcdf", http_jwt->values[3].value_str, NULL);
	test_check_int_eq ((int) strlen ("0123456789abcdf"), (int) strlen (http_jwt->values[3].value_str), NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_generate_bearer (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");
	http_cerver_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	http_cerver_auth_jwt_add_value (http_jwt, "role", "0123456789abcdf");

	test_check_int_eq ((int) http_jwt->n_values, 4, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	test_check_str_eq ("email", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("email"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq ("erick.salas@ermiry.com", http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen ("erick.salas@ermiry.com"), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("name", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[2].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[2].value_str), NULL);

	test_check_str_eq ("role", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("role"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_str_eq ("0123456789abcdf", http_jwt->values[3].value_str, NULL);
	test_check_int_eq ((int) strlen ("0123456789abcdf"), (int) strlen (http_jwt->values[3].value_str), NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_generate_bearer_json (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");
	http_cerver_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	http_cerver_auth_jwt_add_value (http_jwt, "role", "0123456789abcdf");

	test_check_int_eq ((int) http_jwt->n_values, 4, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	test_check_str_eq ("email", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("email"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq ("erick.salas@ermiry.com", http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen ("erick.salas@ermiry.com"), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("name", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[2].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[2].value_str), NULL);

	test_check_str_eq ("role", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("role"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_str_eq ("0123456789abcdf", http_jwt->values[3].value_str, NULL);
	test_check_int_eq ((int) strlen ("0123456789abcdf"), (int) strlen (http_jwt->values[3].value_str), NULL);

	http_jwt_delete (http_jwt);
	
}

static void test_http_jwt_generate_bearer_json_with_value (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");
	http_cerver_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	http_cerver_auth_jwt_add_value (http_jwt, "role", "0123456789abcdf");

	test_check_int_eq ((int) http_jwt->n_values, 4, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	test_check_str_eq ("email", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("email"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq ("erick.salas@ermiry.com", http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen ("erick.salas@ermiry.com"), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("name", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[2].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[2].value_str), NULL);

	test_check_str_eq ("role", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("role"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_str_eq ("0123456789abcdf", http_jwt->values[3].value_str, NULL);
	test_check_int_eq ((int) strlen ("0123456789abcdf"), (int) strlen (http_jwt->values[3].value_str), NULL);

	http_jwt_delete (http_jwt);
	
}

void http_tests_jwt (void) {

	(void) printf ("Testing HTTP jwt...\n");

	test_http_jwt_add_value ();

	test_http_jwt_add_value_bool ();

	test_http_jwt_add_value_int ();

	test_http_jwt_add_value_multiple ();

	test_http_jwt_add_value_many ();

	test_http_jwt_generate ();

	test_http_jwt_generate_bearer ();

	test_http_jwt_generate_bearer_json ();

	test_http_jwt_generate_bearer_json_with_value ();

	(void) printf ("Done!\n");

}