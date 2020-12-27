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

static void test_jwt_add_header (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_header (jwt, "iss", "test");
	test_check_int_eq (ret, 0, NULL);

	/* No duplicates */
	ret = jwt_add_header (jwt, "iss", "other");
	test_check_int_eq (ret, EEXIST, NULL);

	/* No duplicates for int */
	ret = jwt_add_header_int (jwt, "iat", (long) time (NULL));
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_header_int (jwt, "iat", (long) time (NULL));
	test_check_int_eq (ret, EEXIST, NULL);

	jwt_free(jwt);

}

static void test_jwt_get_header (void) {

	jwt_t *jwt = NULL;
	const char *val;
	const char testval[] = "testing";
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_header (jwt, "iss", testval);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header (jwt, "iss");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, testval, NULL);

	jwt_free (jwt);

}

static void test_jwt_add_header_int (void) {

	jwt_t *jwt = NULL;
	long val = 0;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_header_int (jwt, "int", 1);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header_int (jwt, "int");
	test_check (val, NULL);

	val = jwt_get_header_int (jwt, "not found");
	test_check_int_eq (errno, ENOENT, NULL);

	jwt_free (jwt);

}

static void test_jwt_add_header_bool (void) {

	jwt_t *jwt = NULL;
	int val = 0;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_header_bool (jwt, "admin", 1);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header_bool (jwt, "admin");
	test_check (val, NULL);

	ret = jwt_add_header_bool (jwt, "test", 0);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header_bool (jwt, "test");
	test_check (!val, NULL);

	val = jwt_get_header_bool (jwt, "not found");
	test_check_int_eq (errno, ENOENT, NULL);

	jwt_free (jwt);

}

static void test_jwt_del_headers (void) {

	jwt_t *jwt = NULL;
	const char *val;
	const char testval[] = "testing";
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_header (jwt, "iss", testval);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_header (jwt, "other", testval);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_del_headers (jwt, "iss");
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header (jwt, "iss");
	test_check (val == NULL, NULL);

	/* Delete non existent. */
	ret = jwt_del_headers (jwt, "iss");
	test_check_int_eq (ret, 0, NULL);

	/* Delete all headers. */
	ret = jwt_del_headers (jwt, NULL);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header (jwt, "other");
	test_check (val == NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_header_invalid (void) {

	jwt_t *jwt = NULL;
	const char *val;
	long valint = 0;
	int valbool = 0;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_header (jwt, "iss", NULL);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_add_header_int (jwt, "", (long) time (NULL));
	test_check_int_eq (ret, EINVAL, NULL);

	val = jwt_get_header (jwt, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (val == NULL, NULL);

	valint = jwt_get_header_int (jwt, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (valint == 0, NULL);

	valbool = jwt_get_header_bool (jwt, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (valbool == 0, NULL);

	jwt_free (jwt);

}

static void test_jwt_headers_json (void) {

	const char *json = "{\"id\":\"FVvGYTr3FhiURCFebsBOpBqTbzHdX/DvImiA2yheXr8=\","
		"\"iss\":\"localhost\",\"other\":[\"foo\",\"bar\"],"
		"\"ref\":\"385d6518-fb73-45fc-b649-0527d8576130\","
		"\"scopes\":\"storage\",\"sub\":\"user0\"}";

	jwt_t *jwt = NULL;
	const char *val;
	char *json_val = NULL;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_headers_json (jwt, json);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_get_header (jwt, "ref");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "385d6518-fb73-45fc-b649-0527d8576130", NULL);

	json_val = jwt_get_headers_json (NULL, "other");
	test_check (json_val == NULL, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	json_val = jwt_get_headers_json (jwt, "other");
	test_check (json_val != NULL, NULL);
	test_check_str_eq (json_val, "[\"foo\",\"bar\"]", NULL);

	jwt_free_str (json_val);

	json_val = jwt_get_headers_json (jwt, NULL);
	test_check (json_val != NULL, NULL);
	test_check_str_eq (json_val, json, NULL);

	jwt_free_str (json_val);

	jwt_free (jwt);

}

void jwt_tests_header (void) {

	(void) printf ("Testing JWT header...\n");

	test_jwt_add_header ();
	test_jwt_add_header_int ();
	test_jwt_add_header_bool ();
	test_jwt_get_header ();
	test_jwt_del_headers ();
	test_jwt_header_invalid ();
	test_jwt_headers_json ();

	(void) printf ("Done!\n");

}