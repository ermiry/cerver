#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/http/jwt/jwt.h>

#include "../test.h"

#include "jwt.h"

static void test_jwt_decode (void) {

	const char token[] = "eyJhbGciOiJub25lIn0.eyJpc3MiOiJmaWxlcy5jeXBo"
		"cmUuY29tIiwic3ViIjoidXNlcjAifQ.";

	jwt_alg_t alg = JWT_ALG_NONE;
	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, NULL, 0);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	alg = jwt_get_alg (jwt);
	test_check (alg == JWT_ALG_NONE, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_invalid_final_dot (void) {

	const char token[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzM4NCJ9."
		"eyJpc3MiOiJmaWxlcy5jeXBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, NULL, 0);
	test_check_int_eq (ret, EINVAL, NULL);
	test_check (jwt == NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_invalid_alg (void) {

	const char token[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIQUhBSCJ9."
		"eyJpc3MiOiJmaWxlcy5jeXBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ.";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, NULL, 0);
	test_check_int_eq (ret, EINVAL, NULL);
	test_check (jwt == NULL, NULL);

	jwt_free (jwt);
}

static void test_jwt_decode_ignore_typ (void) {

	const char token[] = "eyJ0eXAiOiJBTEwiLCJhbGciOiJIUzI1NiJ9."
		"eyJpc3MiOiJmaWxlcy5jeXBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ.";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, NULL, 0);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_invalid_head (void) {

	const char token[] = "yJ0eXAiOiJKV1QiLCJhbGciOiJIUzM4NCJ9."
		"eyJpc3MiOiJmaWxlcy5jeXBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ.";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, NULL, 0);
	test_check_int_eq (ret, EINVAL, NULL);
	test_check (jwt == NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_alg_none_with_key (void) {

	const char token[] = "eyJhbGciOiJub25lIn0."
		"eyJpc3MiOiJmaWxlcy5jeXBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ.";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, (const unsigned char *)"key", 3);
	test_check_int_eq (ret, EINVAL, NULL);
	test_check (jwt == NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_invalid_body (void) {

	const char token[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzM4NCJ9."
		"eyJpc3MiOiJmaWxlcy5jeBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ.";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, NULL, 0);
	test_check_int_eq (ret, EINVAL, NULL);
	test_check (jwt == NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_hs256 (void) {

	const char token[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3Mi"
		"OiJmaWxlcy5jeXBocmUuY29tIiwic3ViIjoidXNlcjAif"
		"Q.dLFbrHVViu1e3VD1yeCd9aaLNed-bfXhSsF0Gh56fBg";

	unsigned char key256[128] = "012345678901234567890123456789XY";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, key256, 32);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_hs256_issue_1 (void) {

	const char token[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIi"
		"OiJzb21lLWxvbmctdXVpZCIsImZpcnN0TmFtZSI6ImhlbGxvIiwibGFzdE"
		"5hbWUiOiJ3b3JsZCIsInJvbGVzIjpbInRoaXMiLCJ0aGF0IiwidGhlb3Ro"
		"ZXIiXSwiaXNzIjoiaXNzdWVyIiwicGVyc29uSWQiOiI3NWJiM2NjNy1iOT"
		"MzLTQ0ZjAtOTNjNi0xNDdiMDgyZmFkYjUiLCJleHAiOjE5MDg4MzUyMDAs"
		"ImlhdCI6MTQ4ODgxOTYwMCwidXNlcm5hbWUiOiJoZWxsby53b3JsZCJ9.t"
		"JoAl_pvq95hK7GKqsp5TU462pLTbmSYZc1fAHzcqWM";

	const unsigned char key256[] = "\x00\x11\x22\x33\x44\x55\x66\x77\x88"
		"\x99\xAA\xBB\xCC\xDD\xEE\xFF\x00\x11\x22\x33\x44\x55\x66"
		"\x77\x88\x99\xAA\xBB\xCC\xDD\xEE\xFF";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, key256, sizeof(key256));
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_hs256_issue_2 (void) {

	const char token[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIi"
		"OiJzb21lLWxvbmctdXVpZCIsImZpcnN0TmFtZSI6ImhlbGxvIiwibGFzdE"
		"5hbWUiOiJ3b3JsZCIsInJvbGVzIjpbInRoaXMiLCJ0aGF0IiwidGhlb3Ro"
		"ZXIiXSwiaXNzIjoiaXNzdWVyIiwicGVyc29uSWQiOiI3NWJiM2NjNy1iOT"
		"MzLTQ0ZjAtOTNjNi0xNDdiMDgyZmFkYjUiLCJleHAiOjE5MDg4MzUyMDAs"
		"ImlhdCI6MTQ4ODgxOTYwMCwidXNlcm5hbWUiOiJoZWxsby53b3JsZCJ9.G"
		"pCRdGxE4uClX6Vg7eAPwG-37ZvNBQXyfcldKzDG_QI";

	const char key256[] = "00112233445566778899AABBCCDDEEFF001122334455"
		"66778899AABBCCDDEEFF";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, (const unsigned char *) key256, strlen (key256));
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_hs384 (void) {

	const char token[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzM4NCJ9."
		"eyJpc3MiOiJmaWxlcy5jeXBocmUuY29tIiwic"
		"3ViIjoidXNlcjAifQ.xqea3OVgPEMxsCgyikr"
		"R3gGv4H2yqMyXMm7xhOlQWpA-NpT6n2a1d7TD"
		"GgU6LOe4";

	const unsigned char key384[128] = "aaaabbbbccccddddeeeeffffg"
		"ggghhhhiiiijjjjkkkkllll";

	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_decode (&jwt, token, key384, 48);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

static void test_jwt_decode_hs512 (void) {

    const char token[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzUxMiJ9.eyJpc3Mi"
		"OiJmaWxlcy5jeXBocmUuY29tIiwic3ViIjoidXNlcjAif"
		"Q.u-4XQB1xlYV8SgAnKBof8fOWOtfyNtc1ytTlc_vHo0U"
		"lh5uGT238te6kSacnVzBbC6qwzVMT1806oa1Y8_8EOg";

	unsigned char key512[128] = "012345678901234567890123456789XY"
		"012345678901234567890123456789XY";

	jwt_t *jwt;
	int ret;

	ret = jwt_decode (&jwt, token, key512, 64);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_free (jwt);

}

void jwt_tests_decode (void) {

	(void) printf ("Testing JWT decode...\n");

	test_jwt_decode ();
	test_jwt_decode_invalid_alg ();
	test_jwt_decode_ignore_typ ();
	test_jwt_decode_invalid_head ();
	test_jwt_decode_alg_none_with_key ();
	test_jwt_decode_invalid_body ();
	test_jwt_decode_invalid_final_dot ();
	test_jwt_decode_hs256 ();
	test_jwt_decode_hs384 ();
	test_jwt_decode_hs512 ();

	(void) printf ("Done!\n");

}