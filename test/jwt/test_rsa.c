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

static const char jwt_rs256_2048[] = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.ey"
	"JpYXQiOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
	"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.cl9YHrbydUPb8dbij"
	"SZzpTa-r-Z2bFz8r1DEQeqGB2ncHlNvYRLa3wa-IbOSQGPVok9xMutxc2ngm0cvquOOW"
	"WVZIpYz3IdZQaCZ4G2PtTwnmhblSnqB-1ZvbUljBHjIoeXDTq2Msph2sjED9YKHKcjIm"
	"kwil1cp75bnZMoKW3kDuNdq1vUwZDLdE_YRMpA53sTsoXHNSBzQwrIFEdCA8OA2rS-9R"
	"IYtbLnKUZH4GXe2wb5y7pB21qqIdSl9k7yuD90k7LaCQDNLvrI1_cQB9wQcqqFA0qFc2"
	"UxbiRRsC65eRZ1PfdZ8I_scukh5Vts5PNaRdE-_y_bpZKPaUu-WwA";

static const char jwt_rs384_4096[] = "eyJhbGciOiJSUzM4NCIsInR5cCI6IkpXVCJ9.ey"
	"JpYXQiOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
	"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.W0hvRx5yJaR3serc6"
	"FoXy-0XeKxekQ3qIcIiBE4mTlH_E9vjO3Be_TAu2HTu4Th81VWzr6F5hbB2hGPP9UrCn"
	"gwVx-mmRDQBEzdpS2n_Quews9GE68VdVP6zoVeQnOx4Htyg8eobmFfxxiqEegLJNqyTb"
	"CJyZ7XeEDtLpa1iZBz__wlU5gHSinxogTsyfd4N5eRPD6_hjG6iGjQ2i6KmEhiR5SKYI"
	"FLIfMm872Va4TmgxRQjL-mO-4xpo5ySgpWHuRvN6rAE6tGGTswiQoQq3zQbLCF1DScHv"
	"owUbGaAHX46ACNbDHe1N8JbvVUyM0vrgKbmZ4Nlg5kf_b3MMYCQ_zlL2msTr-8NrU0Gc"
	"5g1f1IL2GVPEZPxQT0Z5c9mRGIBN82OyrCftfy3O-PRWU0yLM5Odt_QzL7KfIIYLZfws"
	"ADkqm3_-bst60DfTWWIr63smo1XAeLs1sqxO3QFxFHyaYcHl17QvX-2vxGJACzbjvGN4"
	"9MeACklEyDVcWOQyNPu5kgPGhyjmH_A8qlcKGka8OqYnNIOSj54GTf5gWObBrPIXN8mL"
	"7e64iI4dtlaA9-9KWLjpCLYlFGVMntu1YgzF9-xQ0RFxvcaHkXNVSQs61eMH-LH7kn2f"
	"cfFKjDDFhm7OHNfCNcgTzfCDKIfOgh1FPAa7gRIBauutr-9n-TW8TY";

static const char jwt_rs512_8192[] = "eyJhbGciOiJSUzUxMiIsInR5cCI6IkpXVCJ9.ey"
	"JpYXQiOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
	"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.Lon6Vh8pjAXq_XJfE"
	"r_0ppYuawbL-AYqjw0BQep0Dc5hU-qOMHjZslnvS8MMkdzvsjvr10O0HHive8cw5XMoj"
	"LndSXeKejzsLdYoU93cpmz-Eb_AX8XcWx201JlF8lDZYest1__yplNiSwrcfgsef6Upi"
	"07P8hjHjQqF2W-n037ckgJANQ4wcacSUEcxtjY1Sk5Y-FMsckx-lD60hCYYxcMUt7Uoz"
	"qXlFpk28H1xx7saVlheJGsgZYX2bVNW6PhT_VKjIwOP3MWBxndvlQfPG8ZJRWE2puCum"
	"MAV7ncs6JcIrOstXx4RtXxgwbbjXta114np7sxIMnA8pYe6q_uTstFIJYry0YkBfZ9rO"
	"PjHibREMOf7iFOwlls7bn_XwcBaYBEoo5R4yP8F_HVdr17gw6iorzA0iqnn_79WtQY2Z"
	"THOFU8v2NxHat_MvuIWy79v05NY852wMjuxA_Z7nd1DP5xv5YYOV1AKG9g1UrsdAyMK1"
	"75z75jy_HLmx_Y51zmDKwuovHMMZvtDOBtpM6nPVx0kTQkf6ngtpHGDtMxAd4SXmoUg8"
	"76zQ04p64Wgn0m2_cvr2D35j-4ddAiEtWGEVygEEcA7amyhh56yuM8K6bxAguZM_81Iq"
	"OuCJFD4qW6KtmYOFTpDPOnJuvVJLxHz7nixH8U-q5UcSW08ScbQq3XPjtoCAG6LKh94S"
	"q663kRjA8a6RpeC1GHFRM1Jit2LgGVY8YzxA0d6vXOB9gTKTmuvhvHKtWCRhCx-SorC0"
	"Zkn_910ZzwF3gVdbejvyxDfLa8eqLAr48SsBUtU-l69tu1X8tNJ6Ug549Y1yg_-BOzPz"
	"Zb4r3tFW4lV_zTVyNGxDp4bq8LoG9aMmcB04r-f6hnwe9hlA-6pBaZ0PD6eYHFpAXJAi"
	"XJc22VGZpI0f7G4BywOqEJ5qq5aivppxdLRQQQiHUk-_56zxx5q70301PH7rgf00Uuwa"
	"zZI00rQAB5XhOKqIXuGEYnL9-AFKLl0Gipr_kMqVPPZ1PgVTcDp1ZuLub_iC7_Fh4_aV"
	"lZL-Tx80Y0aRfiDiLGYtSlqiVYnASCqeyzs_10Bext7A0MjTiVd49h0Ek6vfEymJaRtL"
	"tomuPM1UTZEePW4sx-0ewW5D2OGP2sGjm56bM4LQfXlGo3sQJBzc8jG1JOLXtZ0S6QqV"
	"PIy02kuySuWXhUkfLKy7nCj1XFPohkrh32hef2Iml-ATnQzshIL2Ev7BL53W64XVaXsr"
	"qUNa4kV6TG20X7OvLQW81vdd8hIJIUq-xKNlQw25fMQpCy4matYx-FKZ-LCtigkzVFXg"
	"24qGD3tFIsOSuZUmEjYu1Sbn6FfO9AuPUnC-qwVwloNPPCAjP2bqUF9OA";

static const char jwt_rs256_invalid[] = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.ey"
	"JpYXQiOjE0NzU5ODA1NDUsImlzcyI6ImZpbGVzLmN5cGhyZS5jb20iLCJyZWYiOiJYWF"
	"hYLVlZWVktWlpaWi1BQUFBLUNDQ0MiLCJzdWIiOiJ1c2VyMCJ9.IAmCornholio";

static void test_jwt_encode_rs256 (void) {

	test_alg_key ("rsa_key_2048.pem", jwt_rs256_2048, JWT_ALG_RS256);

}

static void test_jwt_verify_rs256 (void) {

	verify_alg_key ("rsa_key_2048-pub.pem", jwt_rs256_2048, JWT_ALG_RS256);

}

static void test_jwt_validate_rs256 (void) {

	jwt_t *jwt = NULL;
	jwt_valid_t *jwt_valid = NULL;
	int ret = 0;

	read_key("rsa_key_2048-pub.pem");

	ret = jwt_decode (&jwt, jwt_rs256_2048, key, key_len);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	jwt_valid_new (&jwt_valid, JWT_ALG_RS256);
	test_check (jwt_valid != NULL, NULL);

	ret = jwt_valid_add_grant (jwt_valid, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_valid_add_grant_int (jwt_valid, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	test_check_int_eq (
		JWT_VALIDATION_SUCCESS, jwt_validate(jwt, jwt_valid), NULL
	);

	jwt_valid_free (jwt_valid);
	jwt_free (jwt);

}

static void test_jwt_encode_rs384 (void) {

	test_alg_key ("rsa_key_4096.pem", jwt_rs384_4096, JWT_ALG_RS384);

}

static void test_jwt_verify_rs384 (void) {

	verify_alg_key ("rsa_key_4096-pub.pem", jwt_rs384_4096, JWT_ALG_RS384);

}

static void test_jwt_encode_rs512 (void) {

	test_alg_key ("rsa_key_8192.pem", jwt_rs512_8192, JWT_ALG_RS512);

}

static void test_jwt_verify_rs512 (void) {

	verify_alg_key ("rsa_key_8192-pub.pem", jwt_rs512_8192, JWT_ALG_RS512);

}

static const char jwt_rsa_i37[] = "eyJraWQiOiJkWUoxTDVnbWd0eDlWVU9xbVpyd2F6cW"
	"NhK3B5c1lHNUl3N3RSUXB6a3Z3PSIsImFsZyI6IlJTMjU2In0.eyJzdWIiOiJhMDQyZj"
	"Y4My0xODNiLTQ1ZWUtOTZiYy1lNDdlYjhiMzc2MTYiLCJ0b2tlbl91c2UiOiJhY2Nlc3"
	"MiLCJzY29wZSI6ImF3cy5jb2duaXRvLnNpZ25pbi51c2VyLmFkbWluIiwiaXNzIjoiaH"
	"R0cHM6XC9cL2NvZ25pdG8taWRwLnVzLWVhc3QtMS5hbWF6b25hd3MuY29tXC91cy1lYX"
	"N0LTFfUWJvMXlMZ0ZIIiwiZXhwIjoxNDg1ODgyNDg5LCJpYXQiOjE0ODU4Nzg4ODksIm"
	"p0aSI6Ijg1MTBlMGVkLWU3N2UtNDJmZS1hMmI2LTgyMjAzMDcxZWQyOCIsImNsaWVudF"
	"9pZCI6IjdicTVhanV0czM1anVmamVnMGYwcmhzNnRpIiwidXNlcm5hbWUiOiJhZG1pbj"
	"MifQ.IZqzZEuwKCVT0acHk3p5DnzPSNxg1tLISt8wZCMAHJAnLSdtbtVibrCTZkTLP5z"
	"PD16MgzgsID_CFF2wZXPGBihhyihu1B5W8GimY4eQOKrt4qiLJgK-D8tG6MSZ2K_9DC3"
	"RwhMjrNL4lpu2YoSOgugRdKpJWy4zadtHKptFkKrkI8qjnDoDSkF0kt4I6S1xOcEPuVh"
	"EOrGsfKr5Bm1N3wX9OVQhcTiVugKrpU8x0Mv1AJYdaxKASOQ6fFlNquwfohgLDwy3By3"
	"xU6RoY6ZWhKm5dcGW7H9gqmr9X4aBmHDmYG5KQtodwf0LOYtprPAXCs9X7Ja-7ddJvko"
	"8mDObTA";

static void test_jwt_verify_rsa_i37 (void) {

	verify_alg_key ("rsa_key_i37-pub.pem", jwt_rsa_i37, JWT_ALG_RS256);

}

static void test_jwt_encode_rsa_with_ec (void) {

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out;

	ALLOC_JWT(&jwt);

	read_key("ec_key_secp384r1.pem");

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_RS384, key, key_len);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_eq (out, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	jwt_free (jwt);

}

static void test_jwt_verify_invalid_token (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key("rsa_key_2048.pem");

	ret = jwt_decode (&jwt, jwt_rs256_invalid, key, JWT_ALG_RS512);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

static void test_jwt_verify_invalid_alg (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("rsa_key_2048.pem");

	ret = jwt_decode (&jwt, jwt_rs256_2048, key, JWT_ALG_RS512);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

static void test_jwt_verify_invalid_cert (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("rsa_key_8192-pub.pem");

	ret = jwt_decode (&jwt, jwt_rs256_2048, key, JWT_ALG_RS256);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

static void test_jwt_verify_invalid_cert_file (void) {

	jwt_t *jwt = NULL;
	int ret = 0;

	read_key ("rsa_key_invalid-pub.pem");

	ret = jwt_decode (&jwt, jwt_rs256_2048, key, JWT_ALG_RS256);
	test_check_int_ne (ret, 0);
	test_check_ptr_eq (jwt, NULL);

}

static void test_jwt_encode_invalid_key (void) {

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;

	ALLOC_JWT (&jwt);

	read_key ("rsa_key_invalid.pem");

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", TS_CONST);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (jwt, JWT_ALG_RS512, key, key_len);
	test_check_int_eq (ret, 0, NULL);

	out = jwt_encode_str (jwt);
	test_check_ptr_eq (out, NULL);

	jwt_free (jwt);

}

void jwt_tests_rsa (void) {

	(void) printf ("Testing JWT rsa...\n");

	test_jwt_encode_rs256 ();
	test_jwt_verify_rs256 ();
	test_jwt_validate_rs256 ();
	test_jwt_encode_rs384 ();
	test_jwt_verify_rs384 ();
	test_jwt_encode_rs512 ();
	test_jwt_verify_rs512 ();
	test_jwt_verify_rsa_i37 ();
	test_jwt_encode_rsa_with_ec ();
	test_jwt_verify_invalid_token ();
	test_jwt_verify_invalid_alg ();
	test_jwt_verify_invalid_cert ();
	test_jwt_verify_invalid_cert_file ();
	test_jwt_encode_invalid_key ();

	(void) printf ("Done!\n");

}