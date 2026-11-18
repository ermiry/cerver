#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/http.h>
#include <cerver/http/security/auth.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>

#include "../../test.h"
#include "../http.h"

static HttpJwt *test_cerver_http_jwt_new (void) {

	HttpJwt *http_jwt = (HttpJwt *) cerver_http_jwt_new ();

	test_check_ptr (http_jwt);
	test_check_int_eq (http_jwt->n_values, 0, NULL);

	return http_jwt;

}

static void test_cerver_http_jwt_add_value (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	cerver_http_auth_jwt_add_value (http_jwt, "username", "ermiry");

	test_check_int_eq ((int) http_jwt->n_values, 1, NULL);

	test_check_str_eq ("username", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("username"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_str_eq ("ermiry", http_jwt->values[0].value_str, NULL);
	test_check_int_eq ((int) strlen ("ermiry"), (int) strlen (http_jwt->values[0].value_str), NULL);

	cerver_http_jwt_delete (http_jwt);

}

static void test_cerver_http_jwt_add_value_bool (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	cerver_http_auth_jwt_add_value_bool (http_jwt, "first", true);

	test_check_int_eq ((int) http_jwt->n_values, 1, NULL);

	test_check_str_eq ("first", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("first"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_bool_eq (true, http_jwt->values[0].value_bool, NULL);

	cerver_http_jwt_delete (http_jwt);
	
}

static void test_cerver_http_jwt_add_value_int (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	cerver_http_auth_jwt_add_value_int (http_jwt, "test", 128);

	test_check_int_eq ((int) http_jwt->n_values, 1, NULL);

	test_check_str_eq ("test", http_jwt->values[0].key, NULL);
	test_check_int_eq ((int) strlen ("test"), (int) strlen (http_jwt->values[0].key), NULL);
	test_check_int_eq (128, http_jwt->values[0].value_int, NULL);

	cerver_http_jwt_delete (http_jwt);
	
}

static void test_cerver_http_jwt_add_value_multiple (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	cerver_http_auth_jwt_add_value (http_jwt, "username", "ermiry");
	cerver_http_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	cerver_http_auth_jwt_add_value_int (http_jwt, "value", 7);
	cerver_http_auth_jwt_add_value_bool (http_jwt, "flag", false);
	cerver_http_auth_jwt_add_value (http_jwt, "name", "Erick Salas");

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

	cerver_http_jwt_delete (http_jwt);
	
}

static void test_cerver_http_jwt_add_value_many (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	cerver_http_auth_jwt_add_value (http_jwt, "username", "ermiry");
	cerver_http_auth_jwt_add_value (http_jwt, "email", "erick.salas@ermiry.com");
	cerver_http_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	cerver_http_auth_jwt_add_value (http_jwt, "role", "0123456789abcdf");

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

	cerver_http_jwt_delete (http_jwt);
	
}

// {
//   "iat": 1607555686,
//   "id": "1607555686",
//   "name": "Erick Salas",
//   "role": "common",
//   "username": "ermiry"
// }
static void test_cerver_http_jwt_generate_internal (HttpJwt *http_jwt) {

	cerver_http_auth_jwt_add_value_int (http_jwt, "iat", STATIC_DATE);
	cerver_http_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	cerver_http_auth_jwt_add_value (http_jwt, "name", "Erick Salas");
	cerver_http_auth_jwt_add_value (http_jwt, "role", "common");
	cerver_http_auth_jwt_add_value (http_jwt, "username", "ermiry");

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
static CerverJwtKey *test_cerver_http_jwt_private_key (void) {

	size_t private_key_len = 0;
	char *private_key = cerver_http_auth_load_key (
		"./test/http/keys/key.key", &private_key_len
	);

	test_check_ptr (private_key);
	test_check_int_ne ((int) private_key_len, 0);

	CerverJwtError error = CERVER_JWT_ERROR_MEMORY;
	CerverJwtKey *key = cerver_jwt_key_create_pem (
		CERVER_JWT_ALGORITHM_RS256, private_key, private_key_len, true, &error
	);

	free (private_key);

	test_check_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, NULL);

	return key;

}

// the token of the 2.x, byte by byte: RS256 has no random part
static void test_cerver_http_jwt_generate (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	test_cerver_http_jwt_generate_internal (http_jwt);

	CerverJwtKey *key = test_cerver_http_jwt_private_key ();

	char *token = cerver_http_auth_generate_jwt_actual (http_jwt, key);

	test_check_ptr (token);
	test_check_str_eq (token, base_token, NULL);
	test_check_str_len (token, strlen (base_token), NULL);

	// a second call generates the same token
	char *again = cerver_http_auth_generate_jwt_actual (http_jwt, key);
	test_check_ptr (again);
	test_check_str_eq (again, base_token, NULL);

	free (again);
	free (token);

	cerver_jwt_key_delete (key);

	cerver_http_jwt_delete (http_jwt);

}

static void test_cerver_http_jwt_generate_bearer (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	// add values to token
	test_cerver_http_jwt_generate_internal (http_jwt);

	CerverJwtKey *key = test_cerver_http_jwt_private_key ();

	// generate bearer token
	u8 result = cerver_http_auth_generate_bearer_jwt_actual (http_jwt, key);

	test_check_unsigned_eq (result, 0, NULL);

	char expected_token[1024] = { 0 };
	(void) snprintf (expected_token, 1024, "Bearer %s", base_token);

	test_check_str_eq (http_jwt->bearer, expected_token, NULL);
	test_check_str_len (http_jwt->bearer, strlen (expected_token), NULL);
	test_check_unsigned_eq (http_jwt->bearer_len, strlen (expected_token), NULL);

	cerver_jwt_key_delete (key);

	cerver_http_jwt_delete (http_jwt);

}

// the claims of a token are written with their keys sorted: the values in any order make the same token
static void test_cerver_http_jwt_generate_sorted (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	cerver_http_auth_jwt_add_value (http_jwt, "username", "ermiry");
	cerver_http_auth_jwt_add_value (http_jwt, "role", "common");
	cerver_http_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	cerver_http_auth_jwt_add_value_int (http_jwt, "iat", STATIC_DATE);
	cerver_http_auth_jwt_add_value (http_jwt, "name", "Erick Salas");

	CerverJwtKey *key = test_cerver_http_jwt_private_key ();

	char *token = cerver_http_auth_generate_jwt_actual (http_jwt, key);
	test_check_ptr (token);
	test_check_str_eq (token, base_token, NULL);
	free (token);

	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt_actual (http_jwt, key), 0, NULL);
	test_check_str_eq (http_jwt->bearer + HTTP_JWT_BEARER_PREFIX_LENGTH, base_token, NULL);

	cerver_jwt_key_delete (key);

	cerver_http_jwt_delete (http_jwt);

}

// an HttpJwt with a value that is not a claim has no token and no bearer
static void test_cerver_http_jwt_not_generated (HttpJwt *http_jwt, const CerverJwtKey *key) {

	test_check_null_ptr (cerver_http_auth_generate_jwt_actual (http_jwt, key));

	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt_actual (http_jwt, key), 1, NULL);
	test_check_unsigned_eq (http_jwt->bearer_len, 0, NULL);
	test_check_str_empty (http_jwt->bearer);

	cerver_http_jwt_delete (http_jwt);

}

// a key more than once, a value without a key and a text that is not UTF-8 are not claims
static void test_cerver_http_jwt_generate_invalid (void) {

	CerverJwtKey *key = test_cerver_http_jwt_private_key ();

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	cerver_http_auth_jwt_add_value (http_jwt, "role", "common");
	cerver_http_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	test_cerver_http_jwt_not_generated (http_jwt, key);

	// the same key with values of other types
	http_jwt = test_cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value_int (http_jwt, "iat", STATIC_DATE);
	cerver_http_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	cerver_http_auth_jwt_add_value_bool (http_jwt, "iat", true);
	test_cerver_http_jwt_not_generated (http_jwt, key);

	http_jwt = test_cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value (http_jwt, "id", STATIC_ID);
	cerver_http_auth_jwt_add_value (http_jwt, "", "value");
	test_cerver_http_jwt_not_generated (http_jwt, key);

	http_jwt = test_cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value (http_jwt, "id", "not \xff UTF-8");
	test_cerver_http_jwt_not_generated (http_jwt, key);

	http_jwt = test_cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value_int (http_jwt, "not \xc3 UTF-8", STATIC_DATE);
	test_cerver_http_jwt_not_generated (http_jwt, key);

	// a token without values has the claims of an empty object
	http_jwt = test_cerver_http_jwt_new ();
	char *token = cerver_http_auth_generate_jwt_actual (http_jwt, key);
	test_check_ptr (token);
	test_check (strstr (token, ".e30.") != NULL, token);
	free (token);
	cerver_http_jwt_delete (http_jwt);

	cerver_jwt_key_delete (key);

}

// without a key there is no token
static void test_cerver_http_jwt_generate_without_key (void) {

	HttpJwt *http_jwt = test_cerver_http_jwt_new ();

	test_cerver_http_jwt_generate_internal (http_jwt);

	test_check_null_ptr (cerver_http_auth_generate_jwt_actual (http_jwt, NULL));

	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt_actual (http_jwt, NULL), 1, NULL);
	test_check_unsigned_eq (http_jwt->bearer_len, 0, NULL);

	cerver_http_jwt_delete (http_jwt);

}

void http_tests_jwt (void) {

	(void) printf ("Testing HTTP jwt...\n");

	test_cerver_http_jwt_add_value ();

	test_cerver_http_jwt_add_value_bool ();

	test_cerver_http_jwt_add_value_int ();

	test_cerver_http_jwt_add_value_multiple ();

	test_cerver_http_jwt_add_value_many ();

	test_cerver_http_jwt_generate ();

	test_cerver_http_jwt_generate_bearer ();

	test_cerver_http_jwt_generate_sorted ();

	test_cerver_http_jwt_generate_invalid ();

	test_cerver_http_jwt_generate_without_key ();

	(void) printf ("Done!\n");

}
