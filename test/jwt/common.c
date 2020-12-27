#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/http/jwt/jwt.h>

#include "../test.h"

#include "common.h"
#include "jwt.h"

unsigned char key[16384] = { 0 };
size_t key_len = 0;

void read_key (const char *key_file) {

	FILE *fp = fopen(key_file, "r");
	char *key_path;
	int ret = 0;

	ret = asprintf (&key_path, "./test/jwt/keys/%s", key_file);
	test_check_int_gt (ret, 0);

	fp = fopen (key_path, "r");
	test_check_ptr_ne (fp, NULL);

	jwt_free_str (key_path);

	key_len = fread (key, 1, sizeof (key), fp);
	test_check_int_ne (key_len, 0);

	test_check_int_eq (ferror (fp), 0, NULL);

	(void) fclose (fp);

	key[key_len] = '\0';

}

void test_alg_key (
	const char *key_file, const char *jwt_str,
	const jwt_alg_t alg
) {

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out;

	ALLOC_JWT (&jwt);

	read_key (key_file);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, alg, key, key_len);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, jwt_str, NULL);

	jwt_free_str (out);
	jwt_free (jwt);

}

void verify_alg_key (
	const char *key_file, const char *jwt_str,
	const jwt_alg_t alg
) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key(key_file);

	ret = jwt_decode( &jwt, jwt_str, key, key_len);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_valid_t *jwt_valid = NULL;
	jwt_valid_new (&jwt_valid, alg);

	test_check_int_eq (
		JWT_VALIDATION_SUCCESS, jwt_validate (jwt, jwt_valid), NULL
	);

	jwt_valid_free (jwt_valid);
	jwt_free (jwt);

}