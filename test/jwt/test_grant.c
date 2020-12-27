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

static void test_jwt_add_grant (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "test");
	test_check_int_eq (ret, 0, NULL);

	/* No duplicates */
	ret = jwt_add_grant (jwt, "iss", "other");
	test_check_int_eq (ret, EEXIST, NULL);

	/* No duplicates for int */
	ret = jwt_add_grant_int (jwt, "iat", (long) time (NULL));
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", (long) time (NULL));
	test_check_int_eq (ret, EEXIST, NULL);

	jwt_free (jwt);

}

static void test_jwt_get_grant (void) {

	jwt_t *jwt = NULL;
	const char *val;
	const char testval[] = "testing";
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", testval);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant (jwt, "iss");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, testval, NULL);

	jwt_free (jwt);

}

static void test_jwt_add_grant_int (void) {

	jwt_t *jwt = NULL;
	long val;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant_int (jwt, "int", 1);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant_int (jwt, "int");
	test_check (val, NULL);

	val = jwt_get_grant_int (jwt, "not found");
	test_check_int_eq (errno, ENOENT, NULL);

	jwt_free (jwt);

}

static void test_jwt_add_grant_bool (void) {

	jwt_t *jwt = NULL;
	int val;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant_bool (jwt, "admin", 1);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant_bool (jwt, "admin");
	test_check (val, NULL);

	ret = jwt_add_grant_bool (jwt, "test", 0);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant_bool (jwt, "test");
	test_check (!val, NULL);

	val = jwt_get_grant_bool (jwt, "not found");
	test_check_int_eq (errno, ENOENT, NULL);

	jwt_free (jwt);
}

static void test_jwt_del_grants (void) {

	jwt_t *jwt = NULL;
	const char *val;
	const char testval[] = "testing";
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", testval);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "other", testval);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_del_grants (jwt, "iss");
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant (jwt, "iss");
	test_check (val == NULL, NULL);

	/* Delete non existent. */
	ret = jwt_del_grants (jwt, "iss");
	test_check_int_eq (ret, 0, NULL);

	/* Delete all grants. */
	ret = jwt_del_grants (jwt, NULL);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant (jwt, "other");
	test_check (val == NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_grant_invalid (void) {

	jwt_t *jwt = NULL;
	const char *val;
	long valint = 0;
	int valbool = 0;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", NULL);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_add_grant_int (jwt, "", (long)time(NULL));
	test_check_int_eq (ret, EINVAL, NULL);

	val = jwt_get_grant (jwt, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (val == NULL, NULL);

	valint = jwt_get_grant_int (jwt, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (valint == 0, NULL);

	valbool = jwt_get_grant_bool (jwt, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (valbool == 0, NULL);

	jwt_free (jwt);

}

static void test_jwt_grants_json (void) {

	const char *json = "{\"id\":\"FVvGYTr3FhiURCFebsBOpBqTbzHdX/DvImiA2yheXr8=\","
		"\"iss\":\"localhost\",\"other\":[\"foo\",\"bar\"],"
		"\"ref\":\"385d6518-fb73-45fc-b649-0527d8576130\","
		"\"scopes\":\"storage\",\"sub\":\"user0\"}";

	jwt_t *jwt = NULL;
	const char *val;
	char *json_val;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grants_json (jwt, json);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_grant (jwt, "ref");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "385d6518-fb73-45fc-b649-0527d8576130", NULL);

	json_val = jwt_get_grants_json (NULL, "other");
	test_check (json_val == NULL, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	json_val = jwt_get_grants_json (jwt, "other");
	test_check (json_val != NULL, NULL);
	test_check_str_eq (json_val, "[\"foo\",\"bar\"]", NULL);

	jwt_free_str (json_val);

	json_val = jwt_get_grants_json (jwt, NULL);
	test_check (json_val != NULL, NULL);
	test_check_str_eq (json_val, json, NULL);

	jwt_free_str (json_val);

	jwt_free (jwt);

}

void jwt_tests_grant (void) {

	(void) printf ("Testing JWT grant...\n");

	test_jwt_add_grant ();
	test_jwt_add_grant_int ();
	test_jwt_add_grant_bool ();
	test_jwt_get_grant ();
	test_jwt_del_grants ();
	test_jwt_grant_invalid ();
	test_jwt_grants_json ();

	(void) printf ("Done!\n");

}