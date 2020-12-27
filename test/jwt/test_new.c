#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/http/jwt/jwt.h>

#include "../test.h"

#include "jwt.h"

static void test_jwt_new (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_new (NULL);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_new (&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_dup (void) {

	jwt_t *jwt = NULL, *new_jwt = NULL;
	int ret = 0;
	const char *val = NULL;
	time_t now;
	long valint;

	new_jwt = jwt_dup (NULL);
	test_check (new_jwt == NULL, NULL);

	ret = jwt_new (&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	ret = jwt_add_grant (jwt, "iss", "test");
	test_check_int_eq (ret, 0, NULL);

	new_jwt = jwt_dup (jwt);
	test_check (new_jwt != NULL, NULL);

	val = jwt_get_grant (new_jwt, "iss");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "test", NULL);

	test_check_int_eq (jwt_get_alg(new_jwt), JWT_ALG_NONE, NULL);

	now = time (NULL);
	ret = jwt_add_grant_int (jwt, "iat", (long)now);
	test_check_int_eq (ret, 0, NULL);

	valint = jwt_get_grant_int (jwt, "iat");
	test_check (((long) now) == valint, NULL);

	jwt_free (new_jwt);
	jwt_free (jwt);

}

static void test_jwt_dup_signed (void) {

	unsigned char key256[128] = "012345678901234567890123456789XY";
	jwt_t *jwt = NULL, *new_jwt = NULL;
	int ret = 0;
	const char *val = NULL;

	ret = jwt_new (&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	ret = jwt_add_grant (jwt, "iss", "test");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS256, key256, 32);
	test_check_int_eq (ret, 0, NULL);

	new_jwt = jwt_dup (jwt);
	test_check (new_jwt != NULL, NULL);

	val = jwt_get_grant (new_jwt, "iss");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "test", NULL);

	test_check_int_eq (jwt_get_alg (new_jwt), JWT_ALG_HS256, NULL);

	jwt_free (new_jwt);
	jwt_free (jwt);

}

void jwt_tests_new (void) {

	(void) printf ("Testing JWT new_jwt...\n");

	test_jwt_new ();
	test_jwt_dup ();
	test_jwt_dup_signed ();

	(void) printf ("Done!\n");

}