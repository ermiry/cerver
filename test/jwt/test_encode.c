#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/http/jwt/jwt.h>

#include "../test.h"

#include "jwt.h"

/* Constant time to make tests consistent. */
#define TS_CONST	1475980545L

static void test_jwt_encode_fp (void) {

	FILE *out = NULL;
	jwt_t *jwt = NULL;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	out = fopen ("/dev/null", "w");
	test_check_ptr_ne (out, NULL);

	ret = jwt_encode_fp (jwt, out);
	test_check_int_eq (ret, 0, NULL);

	fclose (out);

	jwt_free (jwt);

}

static void test_jwt_encode_str (void) {

	const char res[] = "eyJhbGciOiJOb25lIn0.eyJpYXQiOjE0NzU5ODA1NDUsIm"
		"lzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWFhYLVlZWVktWlpa"
		"Wi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.";

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant(jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, res, NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

static void test_jwt_encode_alg_none (void) {

	const char res[] = "eyJhbGciOiJOb25lIn0.eyJhdWQiOiJ3d3cucGx1Z2dlcnM"
		"ubmwiLCJleHAiOjE0Nzc1MTQ4MTIsInN1YiI6IlBsdWdnZXJzIFNvZnR3YXJlIn0.";

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT(&jwt);

	ret = jwt_add_grant (jwt, "aud", "www.pluggers.nl");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "exp", 1477514812);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "Pluggers Software");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_NONE, NULL, 0);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, res, NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

static void test_jwt_encode_hs256 (void) {

	const char res[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE"
		"0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
		"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.B0a9gqWg"
		"PuuIx-EFXXSHQByCMHCzs0gjvY3-60oV4TY";
	
	unsigned char key256[128] = "012345678901234567890123456789XY";
	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS256, key256, 32);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, res, NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

static void test_jwt_encode_hs384 (void) {

	const char res[] = "eyJhbGciOiJIUzM4NCIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE"
		"0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
		"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.k5mpjWlu"
		"aj4EQxuvoyXHR9HVw_V4GMnguwcQvZplTDT_H2PS0DDoZ5NF-VLC8kgO";

	unsigned char key384[128] = "aaaabbbbccccddddeeeeffffgggghhhh"
		"iiiijjjjkkkkllll";

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS384, key384, 48);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, res,NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

static void test_jwt_encode_hs512 (void) {

	const char res[] = "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE"
		"0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
		"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.oxB_kx_h"
		"5DANiG5oZWPO90MFlkoMb7VGlEBDbBTpX_JThJ8md6UEsxFvwm2weeyHU4-"
		"MasEU4nzbVk4LZ0vrcg";

	unsigned char key512[128] = "012345678901234567890123456789XY"
		"012345678901234567890123456789XY";

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS512, key512, 64);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, res, NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

static void test_jwt_encode_change_alg (void) {

	const char res[] = "eyJhbGciOiJOb25lIn0.eyJpYXQiOjE0NzU5ODA1NDUsIm"
		"lzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWFhYLVlZWVktWlpa"
		"Wi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.";

	unsigned char key512[128] = "012345678901234567890123456789XY"
		"012345678901234567890123456789XY";

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS512, key512, 64);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_NONE, NULL, 0);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	test_check_str_eq (out, res, NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

static void test_jwt_encode_invalid (void) {

	unsigned char key512[128] = "012345678901234567890123456789XY"
		"012345678901234567890123456789XY";

	jwt_t *jwt = NULL;
	int ret = 0;

	ALLOC_JWT (&jwt);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS512, NULL, 0);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS512, NULL, 64);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_HS512, key512, 0);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_NONE, key512, 64);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_NONE, key512, 0);
	test_check_int_eq (ret, EINVAL, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_NONE, NULL, 64);
	test_check_int_eq (ret, EINVAL, NULL);

	/* Set a value that will never happen. */
	ret = jwt_set_alg (jwt, (jwt_alg_t) 999, NULL, 0);
	test_check_int_eq (ret, EINVAL, NULL);

	jwt_free (jwt);

}

void jwt_tests_encode (void) {

	(void) printf ("Testing JWT encode...\n");

	test_jwt_encode_fp ();

	test_jwt_encode_str ();

	test_jwt_encode_alg_none ();

	test_jwt_encode_hs256 ();

	test_jwt_encode_hs384 ();

	test_jwt_encode_hs512 ();

	test_jwt_encode_change_alg ();

	test_jwt_encode_invalid ();

	(void) printf ("Done!\n");

}