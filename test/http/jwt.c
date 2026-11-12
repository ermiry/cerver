#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/http/auth.h>
#include <cerver/http/http.h>
#include <cerver/http/jwt/alg.h>
#include <cerver/http/jwt/jwt.h>
#include <cerver/http/jwt/key.h>

#include "../test.h"
#include "http.h"

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

// {
//   "iat": 1607555686,
//   "id": "1607555686",
//   "name": "Erick Salas",
//   "role": "common",
//   "username": "ermiry"
// }
static void test_http_jwt_generate_internal (HttpJwt *http_jwt) {

	http_cerver_auth_jwt_add_value_int (http_jwt, "iat", STATIC_DATE);
	http_cerver_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	http_cerver_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	http_cerver_auth_jwt_add_value (http_jwt, "role", "common");
	http_cerver_auth_jwt_add_value (http_jwt, "username", "ermiry");

	test_check_int_eq ((int) http_jwt->n_values, 5, NULL);

	test_check_str_eq ("iat", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("iat"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_int_eq (STATIC_DATE, http_jwt->values[0].value_int, NULL);

	test_check_str_eq ("id", http_jwt->values[1].key, NULL);
	test_check_int_eq ((int) strlen ("id"), (int) strlen (http_jwt->values[1].key), NULL);
	test_check_str_eq (STATIC_ID, http_jwt->values[1].value_str, NULL);
	test_check_int_eq ((int) strlen (STATIC_ID), (int) strlen (http_jwt->values[1].value_str), NULL);

	test_check_str_eq ("name", http_jwt->values[2].key, NULL);
	test_check_int_eq ((int) strlen ("name"), (int) strlen (http_jwt->values[2].key), NULL);
	test_check_str_eq ("Erick Salas", http_jwt->values[2].value_str, NULL);
	test_check_int_eq ((int) strlen ("Erick Salas"), (int) strlen (http_jwt->values[2].value_str), NULL);

	test_check_str_eq ("role", http_jwt->values[3].key, NULL);
	test_check_int_eq ((int) strlen ("role"), (int) strlen (http_jwt->values[3].key), NULL);
	test_check_str_eq ("common", http_jwt->values[3].value_str, NULL);
	test_check_int_eq ((int) strlen ("common"), (int) strlen (http_jwt->values[3].value_str), NULL);

	test_check_str_eq ("username", http_jwt->values[4].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[4].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[4].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[4].value_str), NULL);

}

// the RS256 private key of the tests, parsed once as the cerver does
static JwtKey *test_http_jwt_private_key (void) {

	size_t private_key_len = 0;
	char *private_key = http_cerver_auth_load_key (
		"./test/http/keys/key.key", &private_key_len
	);

	test_check_ptr (private_key);
	test_check_int_ne ((int) private_key_len, 0);

	JwtKeyError error = JWT_KEY_ERROR_NONE;
	JwtKey *key = jwt_key_create (
		JWT_ALG_RS256, JWT_KEY_TYPE_PRIVATE,
		(const unsigned char *) private_key, private_key_len, &error
	);

	free (private_key);

	test_check_ptr (key);
	test_check_int_eq (error, JWT_KEY_ERROR_NONE, NULL);

	return key;

}

static void test_http_jwt_generate (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	test_http_jwt_generate_internal (http_jwt);

	JwtKey *key = test_http_jwt_private_key ();

	char *token = http_cerver_auth_generate_jwt_actual (http_jwt, key);

	test_check_ptr (token);
	test_check_str_eq (token, base_token, NULL);
	test_check_str_len (token, strlen (base_token), NULL);

	// a second call replaces the first jwt, it is not lost
	char *again = http_cerver_auth_generate_jwt_actual (http_jwt, key);
	test_check_ptr (again);
	test_check_str_eq (again, base_token, NULL);

	free (again);
	free (token);

	jwt_key_delete (key);

	http_jwt_delete (http_jwt);

}

static void test_http_jwt_generate_bearer (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	// add values to token
	test_http_jwt_generate_internal (http_jwt);

	JwtKey *key = test_http_jwt_private_key ();

	// generate bearer token
	u8 result = http_cerver_auth_generate_bearer_jwt_actual (http_jwt, key);

	test_check_unsigned_eq (result, 0, NULL);

	char expected_token[1024] = { 0 };
	(void) snprintf (expected_token, 1024, "Bearer %s", base_token);

	test_check_str_eq (http_jwt->bearer, expected_token, NULL);
	test_check_str_len (http_jwt->bearer, strlen (expected_token), NULL);
	test_check_unsigned_eq (http_jwt->bearer_len, strlen (expected_token), NULL);

	jwt_key_delete (key);

	http_jwt_delete (http_jwt);

}

// without a key there is no token
static void test_http_jwt_generate_without_key (void) {

	HttpJwt *http_jwt = test_http_jwt_new ();

	test_http_jwt_generate_internal (http_jwt);

	test_check_null_ptr (http_cerver_auth_generate_jwt_actual (http_jwt, NULL));

	test_check_unsigned_eq (http_cerver_auth_generate_bearer_jwt_actual (http_jwt, NULL), 1, NULL);
	test_check_unsigned_eq (http_jwt->bearer_len, 0, NULL);

	http_jwt_delete (http_jwt);

}

// {"typ": "JWT"} as the header, a token without "alg" (found by the jwt fuzzer)
static const char *token_without_alg = { "eyJ0eXAiOiJKV1QifQ.e30." };

static const char *jwt_test_key = { "not-a-key" };

static void test_http_jwt_decode_without_alg (void) {

	jwt_t *jwt = NULL;
	test_check_int_ne (
		jwt_decode (&jwt, token_without_alg, (const unsigned char *) jwt_test_key, (int) strlen (jwt_test_key)),
		0
	);

	test_check_null_ptr (jwt);

	test_check_int_eq (jwt_str_alg (NULL), JWT_ALG_INVAL, NULL);

}

void http_tests_jwt (void) {

	(void) printf ("Testing HTTP jwt...\n");

	test_http_jwt_decode_without_alg ();

	test_http_jwt_add_value ();

	test_http_jwt_add_value_bool ();

	test_http_jwt_add_value_int ();

	test_http_jwt_add_value_multiple ();

	test_http_jwt_add_value_many ();

	test_http_jwt_generate ();

	test_http_jwt_generate_bearer ();

	test_http_jwt_generate_without_key ();

	(void) printf ("Done!\n");

}
