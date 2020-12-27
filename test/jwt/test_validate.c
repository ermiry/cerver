#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/http/jwt/jwt.h>

#include "../test.h"

#include "jwt.h"

jwt_t *jwt = NULL;

#define TS_CONST 1570732480L

const time_t iat = TS_CONST;
const time_t not_before = TS_CONST + 60L;
const time_t expires = TS_CONST + 600L;

static void __setup_jwt (void) {

	jwt_new (&jwt);
	(void) jwt_add_grant (jwt, "iss", "test");
	(void) jwt_add_grant (jwt, "sub", "user0");
	(void) jwt_add_grants_json (jwt, "{\"aud\": [\"svc1\",\"svc2\"]}");
	(void) jwt_add_grant_int (jwt, "iat", iat);
	(void) jwt_add_grant_bool (jwt, "admin", 1);
	(void) jwt_set_alg (jwt, JWT_ALG_NONE, NULL, 0);

}

static void __teardown_jwt (void) {

	jwt_free (jwt);
	jwt = NULL;

}

#define __VAL_EQ(__v, __e) do {									\
	unsigned int __r = jwt_validate (jwt, __v);					\
	test_check_int_eq (__r, __e, NULL);							\
	test_check_int_eq (__e, jwt_valid_get_status(__v), NULL);	\
} while(0);

static void test_jwt_validate_errno (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt ();
	test_check (jwt != NULL, NULL);

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	/* Validate fails with NULL jwt */
	ret = jwt_validate (NULL, jwt_valid);
	test_check_int_eq (ret, JWT_VALIDATION_ERROR, NULL);
	test_check_int_eq (JWT_VALIDATION_ERROR, jwt_valid_get_status (jwt_valid), NULL);

	/* Validate fails with NULL jwt_valid */
	ret = jwt_validate (jwt, NULL);
	test_check_int_eq (ret, JWT_VALIDATION_ERROR, NULL);

	/* Get status fails with NULL jwt_valid */
	test_check_int_eq (JWT_VALIDATION_ERROR, jwt_valid_get_status (NULL), NULL);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_algorithm (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt ();

	/* Matching algorithm is valid */
	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	jwt_valid_free (jwt_valid);

	/* Wrong algorithm is not valid */
	ret = jwt_valid_new (&jwt_valid, JWT_ALG_HS256);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	/* Starts with invalid */
	test_check_int_eq (JWT_VALIDATION_ERROR, jwt_valid_get_status(jwt_valid), NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_ALG_MISMATCH);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_require_grant (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;
	const char *valstr = NULL;
	int valnum = 0;

	__setup_jwt ();

	/* Valid when alg matches and all required grants match */
	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_add_grant (jwt_valid, "iss", "test");
	test_check_int_eq (ret, 0, NULL);

	/* No duplicates */
	ret = jwt_valid_add_grant (jwt_valid, "iss", "other");
	test_check_int_eq (ret, EEXIST, NULL);

	/* Grant has expected value */
	valstr = jwt_valid_get_grant (jwt_valid, "iss");
	test_check_ptr_ne (valstr, NULL);
	test_check_str_eq (valstr, "test", NULL);

	ret = jwt_valid_add_grant_int (jwt_valid, "iat", (long) iat);
	test_check_int_eq (ret, 0, NULL);

	/* No duplicates for int */
	ret = jwt_valid_add_grant_int (jwt_valid, "iat", (long) time (NULL));
	test_check_int_eq (ret, EEXIST, NULL);

	/* Grant has expected value */
	valnum = jwt_valid_get_grant_int (jwt_valid, "iat");
	test_check_long_int_eq ((long) valnum, (long) iat, NULL);

	ret = jwt_valid_add_grant_bool (jwt_valid, "admin", 1);
	test_check_int_eq (ret, 0, NULL);

	/* No duplicates for bool */
	ret = jwt_valid_add_grant_bool (jwt_valid, "admin", 0);
	test_check_int_eq (ret, EEXIST, NULL);

	/* Grant has expected value */
	valnum = jwt_valid_get_grant_bool (jwt_valid, "admin");
	test_check_int_eq (valnum, 1, NULL);

	ret = jwt_valid_add_grants_json (jwt_valid, "{\"aud\": [\"svc1\",\"svc2\"]}");
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_nonmatch_grant (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt();

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	/* Invalid when required grants don't match */
	ret = jwt_valid_add_grant (jwt_valid, "iss", "wrong");
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISMATCH);

	jwt_valid_del_grants (jwt_valid, NULL);

	/* Invalid when required grants don't match (int) */
	ret = jwt_valid_add_grant_int (jwt_valid, "iat", (long)time(NULL) + 1);
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISMATCH);

	jwt_valid_del_grants (jwt_valid, NULL);

	/* Invalid when required grants don't match (bool) */
	ret = jwt_valid_add_grant_bool (jwt_valid, "admin", 0);
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISMATCH);

	jwt_valid_del_grants (jwt_valid, NULL);

	/* Invalid when required grants don't match (json) */
	ret = jwt_valid_add_grants_json (jwt_valid, "{\"aud\": [\"svc3\",\"svc4\"]}");
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISMATCH);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_grant_bool (void) {

	jwt_valid_t *jwt_valid = NULL;
	int val;
	unsigned int ret = 0;

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_add_grant_bool (jwt_valid, "admin", 1);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_valid_get_grant_bool (jwt_valid, "admin");
	test_check (val, NULL);

	ret = jwt_valid_add_grant_bool (jwt_valid, "test", 0);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_valid_get_grant_bool (jwt_valid, "test");
	test_check (!val, NULL);

	val = jwt_valid_get_grant_bool (jwt_valid, "not found");
	test_check_int_eq (errno, ENOENT, NULL);

	jwt_valid_free (jwt_valid);

}

static void test_jwt_valid_del_grants (void) {

	jwt_valid_t *jwt_valid = NULL;
	const char *val;
	const char testval[] = "testing";
	unsigned int ret = 0;

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_add_grant (jwt_valid, "iss", testval);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_valid_add_grant (jwt_valid, "other", testval);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_valid_del_grants (jwt_valid, "iss");
	test_check_int_eq (ret, 0, NULL);

	val = jwt_valid_get_grant (jwt_valid, "iss");
	test_check (val == NULL, NULL);

	/* Delete non existent. */
	ret = jwt_valid_del_grants (jwt_valid, "iss");
	test_check_int_eq (ret, 0, NULL);

	/* Delete all grants. */
	ret = jwt_valid_del_grants (jwt_valid, NULL);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_valid_get_grant (jwt_valid, "other");
	test_check (val == NULL, NULL);

	jwt_valid_free (jwt_valid);

}

static void test_jwt_valid_invalid_grant (void) {

	jwt_valid_t *jwt_valid = NULL;
	const char *val;
	long valint = 0;
	long valbool = 0;
	unsigned int ret = 0;

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_add_grant (jwt_valid, "iss", NULL);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_valid_add_grant_int (jwt_valid, "", (long)time(NULL));
	test_check_int_eq (ret, EINVAL, NULL);

	val = jwt_valid_get_grant (jwt_valid, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (val == NULL, NULL);

	valint = jwt_valid_get_grant_int (jwt_valid, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (valint == 0, NULL);

	valbool = jwt_valid_get_grant_bool (jwt_valid, NULL);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check (valbool == 0, NULL);

	jwt_valid_free (jwt_valid);

}

static void test_jwt_valid_missing_grant (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt();

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	/* JWT is invalid when required grants are not present */
	ret = jwt_valid_add_grant (jwt_valid, "np-str", "test");
	test_check_int_eq (ret, 0, NULL);
	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISSING);

	jwt_valid_del_grants (jwt_valid, NULL);

	/* JWT is invalid when required grants are not present (int) */
	ret = jwt_valid_add_grant_int (jwt_valid, "np-int", 7);
	test_check_int_eq (ret, 0, NULL);
	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISSING);

	jwt_valid_del_grants (jwt_valid, NULL);

	/* JWT is invalid when required grants are not present (bool) */
	ret = jwt_valid_add_grant_int (jwt_valid, "np-bool", 1);
	test_check_int_eq (ret, 0, NULL);
	__VAL_EQ(jwt_valid, JWT_VALIDATION_GRANT_MISSING);

	jwt_valid_del_grants (jwt_valid, NULL);

	/* JWT is invalid when required grants are not present (json) */
	ret = jwt_valid_add_grants_json (jwt_valid, "{\"np-other\": [\"foo\",\"bar\"]}");
	test_check_int_eq (ret, 0, NULL);
	__VAL_EQ (jwt_valid, JWT_VALIDATION_GRANT_MISSING);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_not_before (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt ();
	jwt_add_grant_int ( jwt, "nbf", not_before);

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	/* JWT is invalid when now < not-before */
	ret = jwt_valid_set_now (jwt_valid, not_before - 1);
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_TOO_NEW);

	/* JWT is valid when now >= not-before */
	ret = jwt_valid_set_now (jwt_valid, not_before);
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_expires (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt ();
	jwt_add_grant_int (jwt, "exp", expires);

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	/* JWT is valid when now < expires */
	ret = jwt_valid_set_now (jwt_valid, (long)expires - 1);
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	/* JWT is invalid when now >= expires */
	ret = jwt_valid_set_now (jwt_valid, (long)expires);
	test_check_int_eq (ret, 0, NULL);

	__VAL_EQ (jwt_valid, JWT_VALIDATION_EXPIRED);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_headers (void) {

	jwt_valid_t *jwt_valid = NULL;
	unsigned int ret = 0;

	__setup_jwt();

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_set_headers (jwt_valid, 1);
	test_check_int_eq (ret, 0, NULL);

	/* JWT is valid when iss in hdr matches iss in body */
	jwt_add_header (jwt, "iss", "test");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	/* JWT is invalid when iss in hdr does not match iss in body */
	jwt_del_headers (jwt, "iss");
	jwt_add_header (jwt, "iss", "wrong");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_ISS_MISMATCH);

	/* JWT is valid when checking hdr and iss not replicated */
	jwt_del_headers (jwt, "iss");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	/* JWT is valid when iss in hdr matches iss in body */
	jwt_add_header (jwt, "sub", "user0");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	/* JWT is invalid when iss in hdr does not match iss in body */
	jwt_del_headers (jwt, "sub");
	jwt_add_header (jwt, "sub", "wrong");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUB_MISMATCH);

	/* JWT is valid when checking hdr and sub not replicated */
	jwt_del_headers (jwt, "sub");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	/* JWT is valid when checking hdr and aud matches */
	jwt_add_headers_json (jwt, "{\"aud\": [\"svc1\",\"svc2\"]}");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	/* JWT is invalid when checking hdr and aud does not match */
	jwt_del_headers (jwt, "aud");
	jwt_add_headers_json (jwt, "{\"aud\": [\"svc1\",\"svc2\",\"svc3\"]}");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_AUD_MISMATCH);

	/* JWT is invalid when checking hdr and aud does not match */
	jwt_del_headers (jwt, "aud");
	__VAL_EQ (jwt_valid, JWT_VALIDATION_SUCCESS);

	jwt_valid_free (jwt_valid);
	__teardown_jwt ();

}

static void test_jwt_valid_grants_json (void) {

	const char *json = "{\"id\":\"FVvGYTr3FhiURCFebsBOpBqTbzHdX/DvImiA2yheXr8=\","
		"\"iss\":\"localhost\",\"other\":[\"foo\",\"bar\"],"
		"\"ref\":\"385d6518-fb73-45fc-b649-0527d8576130\","
		"\"scopes\":\"storage\",\"sub\":\"user0\"}";

	jwt_valid_t *jwt_valid = NULL;
	const char *val;
	char *json_val;
	unsigned int ret = 0;

	ret = jwt_valid_new (&jwt_valid, JWT_ALG_NONE);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_add_grants_json (jwt_valid, json);
	test_check_int_eq (ret, 0, NULL);

	val = jwt_valid_get_grant (jwt_valid, "ref");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "385d6518-fb73-45fc-b649-0527d8576130", NULL);

	json_val = jwt_valid_get_grants_json (NULL, "other");
	test_check (json_val == NULL, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	json_val = jwt_valid_get_grants_json (jwt_valid, "other");
	test_check (json_val != NULL, NULL);
	test_check_str_eq (json_val, "[\"foo\",\"bar\"]", NULL);

	jwt_free_str (json_val);

	json_val = jwt_valid_get_grants_json (jwt_valid, NULL);
	test_check (json_val != NULL, NULL);
	test_check_str_eq (json_val, json, NULL);

	jwt_free_str (json_val);

	jwt_valid_free (jwt_valid);

}

void jwt_tests_validate (void) {

	(void) printf ("Testing JWT validate...\n");

	test_jwt_validate_errno ();
	test_jwt_valid_algorithm ();
	test_jwt_valid_require_grant ();
	test_jwt_valid_nonmatch_grant ();
	test_jwt_valid_invalid_grant ();
	test_jwt_valid_missing_grant ();
	test_jwt_valid_grant_bool ();
	test_jwt_valid_grants_json ();
	test_jwt_valid_del_grants ();
	test_jwt_valid_not_before ();
	test_jwt_valid_expires ();
	test_jwt_valid_headers ();

	(void) printf ("Done!\n");

}