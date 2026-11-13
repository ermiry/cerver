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

/* NOTE: ES signing will generate a different signature every time, so can't
 * be simply string compared for verification like we do with RS. */

static const char jwt_es256[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJFUzI1NiJ9.eyJpYXQ"
	"iOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWFhYLVl"
	"ZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.3AA32Mn5dMuJXxe03mxJcT"
	"fmif1eiv_doUCSVuMgny4DLKIZ3956SIGjeJpj3BSx2Lul7Zwy-PPuxyBwnL1jiWp7iw"
	"PN9G9tV75ylfWvcwkF20bQA9m1vDbUIl8PIK8Q";

static const char jwt_es384[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJFUzM4NCJ9.eyJpYXQ"
	"iOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWFhYLVl"
	"ZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.p6McjolhuIqel0DWaI2OrD"
	"oRYcxgSMnGFirdKT5jXpe9L801HBkouKBJSae8F7LLFUKiE2VVX_514WzkuExLQs2eB1"
	"L2Qahid5VFOK3hc7HcBL-rcCXa8d2tf_MudyrM";

static const char jwt_es512[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJFUzUxMiJ9.eyJpYXQ"
	"iOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWFhYLVl"
	"ZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9._i6CCfwqgk9IEFbKjNL8Ki"
	"tPT9NEnXn2-qCSq0UgqkZ3sY-R0cnzD-WzpsEA8QWC882Y-SWwN7qVxK9e45pHUy4jye"
	"YKXJj3agq9tZ61V3TM-BjcnMkERsV37nDQcfom";

static const char jwt_es_invalid[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJFUzI1NiJ9.eyJpYXQ"
	"iOjE0NzU5ODA1IAmCornholio6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWFhYLVl"
	"PN9G9tV75ylfWvcwkF20bQA9m1vDbUIl8PIK8Q";

// each ES algorithm has its own curve (RFC 7518 §3.4)
#define TEST_CERTS_P256_PRIVATE		"ec_key_prime256v1.pem"
#define TEST_CERTS_P256_PUBLIC		"ec_key_prime256v1-pub.pem"
#define TEST_CERTS_P384_PRIVATE		"ec_key_secp384r1.pem"
#define TEST_CERTS_P384_PUBLIC		"ec_key_secp384r1-pub.pem"
#define TEST_CERTS_P521_PRIVATE		"ec_key_secp521r1.pem"
#define TEST_CERTS_P521_PUBLIC		"ec_key_secp521r1-pub.pem"

static void __verify_jwt (
	const char *jwt_str, const jwt_alg_t alg, const char *public_key
) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key (public_key);

	ret = jwt_decode (&jwt, jwt_str, key, key_len);
	test_check_int_eq (ret, 0, NULL);
	test_check_ptr_ne (jwt, NULL);

	test_check (jwt_get_alg (jwt) == alg, NULL);

	jwt_free (jwt);

}

static jwt_t *__test_alg_jwt (void) {

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

	return jwt;

}

static void __test_alg_key (
	const jwt_alg_t alg, const char *private_key, const char *public_key
) {

	jwt_t *jwt = __test_alg_jwt ();
	int ret = 0;
	char *out;

	read_key (private_key);

	ret = jwt_set_alg (jwt, alg, key, key_len);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_ne (out, NULL);

	__verify_jwt (out, alg, public_key);

	jwt_free_str (out);
	jwt_free (jwt);

}

void test_jwt_encode_es256 (void) {

	__test_alg_key (JWT_ALG_ES256, TEST_CERTS_P256_PRIVATE, TEST_CERTS_P256_PUBLIC);

}

void test_jwt_encode_es384 (void) {

	__test_alg_key (JWT_ALG_ES384, TEST_CERTS_P384_PRIVATE, TEST_CERTS_P384_PUBLIC);

}

void test_jwt_verify_es384 (void) {

	__verify_jwt (jwt_es384, JWT_ALG_ES384, TEST_CERTS_P384_PUBLIC);

}

void test_jwt_encode_es512 (void) {

	__test_alg_key (JWT_ALG_ES512, TEST_CERTS_P521_PRIVATE, TEST_CERTS_P521_PUBLIC);

}

// upstream signed ES256 and ES512 with a P-384 key,
// a key of another curve is rejected when signing and when verifying
void test_jwt_es_other_curve (void) {

	jwt_t *jwt = __test_alg_jwt ();

	read_key (TEST_CERTS_P384_PRIVATE);

	test_check_int_eq (jwt_set_alg (jwt, JWT_ALG_ES256, key, key_len), 0, NULL);
	test_check_ptr_eq (jwt_encode_str (jwt), NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	jwt_free (jwt);

	jwt_t *decoded = NULL;
	read_key (TEST_CERTS_P384_PUBLIC);
	test_check_int_ne (jwt_decode (&decoded, jwt_es256, key, key_len), 0);
	test_check_ptr_eq (decoded, NULL);
	test_check_int_ne (jwt_decode (&decoded, jwt_es512, key, key_len), 0);
	test_check_ptr_eq (decoded, NULL);

}


void test_jwt_encode_ec_with_rsa (void) {

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out;

	ALLOC_JWT (&jwt);

	read_key ("rsa_key_4096.pem");

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_ES384, key, key_len);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_eq (out, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	jwt_free (jwt);

}

void test_jwt_verify_invalid_token (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("ec_key_secp384r1.pem");

	ret = jwt_decode (&jwt, jwt_es_invalid, key, JWT_ALG_ES256);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

void test_jwt_verify_invalid_alg (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("ec_key_secp384r1.pem");

	ret = jwt_decode (&jwt, jwt_es256, key, JWT_ALG_ES512);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

void test_jwt_verify_invalid_cert (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("ec_key_secp521r1-pub.pem");

	ret = jwt_decode (&jwt, jwt_es256, key, JWT_ALG_ES256);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

void test_jwt_verify_invalid_cert_file (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("ec_key_invalid-pub.pem");

	ret = jwt_decode (&jwt, jwt_es256, key, JWT_ALG_ES256);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

void test_jwt_encode_invalid_key (void) {

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	read_key ("ec_key_invalid.pem");

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_ES512, key, key_len);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str ( jwt);
	test_check_ptr_eq (out, NULL);

	jwt_free (jwt);

}

void jwt_tests_certs (void) {

	(void) printf ("Testing JWT certs...\n");

	test_jwt_encode_es256 ();
	test_jwt_encode_es384 ();
	test_jwt_verify_es384 ();
	test_jwt_encode_es512 ();
	test_jwt_es_other_curve ();
	test_jwt_encode_ec_with_rsa ();
	test_jwt_verify_invalid_token ();
	test_jwt_verify_invalid_alg ();
	test_jwt_verify_invalid_cert ();
	test_jwt_verify_invalid_cert_file ();
	test_jwt_encode_invalid_key ();

	(void) printf ("Done!\n");

}