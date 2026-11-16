#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/jwt/jwt.h>

#include "../test.h"
#include "jwt.h"

// a value that is in none of the tables
#define JWT_TEST_OUT_OF_TABLE				99

// an algorithm and its string, the "alg" of its tokens
typedef struct JwtTestAlgorithm {

	CerverJwtAlgorithm algorithm;
	const char *string;

} JwtTestAlgorithm;

static const JwtTestAlgorithm jwt_test_algorithms[] = {
	{ CERVER_JWT_ALGORITHM_HS256, "HS256" },
	{ CERVER_JWT_ALGORITHM_HS384, "HS384" },
	{ CERVER_JWT_ALGORITHM_HS512, "HS512" },
	{ CERVER_JWT_ALGORITHM_RS256, "RS256" },
	{ CERVER_JWT_ALGORITHM_RS384, "RS384" },
	{ CERVER_JWT_ALGORITHM_RS512, "RS512" },
	{ CERVER_JWT_ALGORITHM_ES256, "ES256" },
	{ CERVER_JWT_ALGORITHM_ES384, "ES384" },
	{ CERVER_JWT_ALGORITHM_ES512, "ES512" },
	{ CERVER_JWT_ALGORITHM_PS256, "PS256" },
	{ CERVER_JWT_ALGORITHM_PS384, "PS384" },
	{ CERVER_JWT_ALGORITHM_PS512, "PS512" },
	{ CERVER_JWT_ALGORITHM_EDDSA, "EdDSA" }
};

#define JWT_TEST_ALGORITHMS					(sizeof (jwt_test_algorithms) / sizeof (jwt_test_algorithms[0]))

// the texts that are not the string of an algorithm
static const char *jwt_test_not_algorithms[] = {
	"", "none", "None", "NONE", "hs256", "Hs256", "HS25", "HS2566", "HS256 ", " HS256", "HS128",
	"RS", "ES256K", "EDDSA", "eddsa", "Ed25519", "A128KW", "dir", "Invalid"
};

#define JWT_TEST_NOT_ALGORITHMS				(sizeof (jwt_test_not_algorithms) / sizeof (jwt_test_not_algorithms[0]))

static void test_algorithm_strings (void) {

	test_check_unsigned_eq (JWT_TEST_ALGORITHMS + 1, CERVER_JWT_ALGORITHMS, NULL);
	test_check_int_eq (jwt_test_algorithms[JWT_TEST_ALGORITHMS - 1].algorithm, CERVER_JWT_ALGORITHMS - 1, NULL);

	for (size_t i = 0; i < JWT_TEST_ALGORITHMS; i++) {
		const JwtTestAlgorithm *test = &jwt_test_algorithms[i];

		test_check_str_eq (cerver_jwt_algorithm_to_string (test->algorithm), test->string, NULL);

		test_check_int_eq (
			cerver_jwt_algorithm_from_string (test->string, strlen (test->string)), test->algorithm, test->string
		);
	}

	test_check_str_eq (cerver_jwt_algorithm_to_string (CERVER_JWT_ALGORITHM_NONE), "None", NULL);
	test_check_str_eq (
		cerver_jwt_algorithm_to_string ((CerverJwtAlgorithm) JWT_TEST_OUT_OF_TABLE), "None", NULL
	);

}

// only the exact string of an algorithm is one: none is never an algorithm
static void test_algorithm_from_string (void) {

	for (size_t i = 0; i < JWT_TEST_NOT_ALGORITHMS; i++) {
		const char *text = jwt_test_not_algorithms[i];

		test_check_int_eq (
			cerver_jwt_algorithm_from_string (text, strlen (text)), CERVER_JWT_ALGORITHM_NONE, text
		);
	}

	test_check_int_eq (cerver_jwt_algorithm_from_string (NULL, 0), CERVER_JWT_ALGORITHM_NONE, NULL);
	test_check_int_eq (
		cerver_jwt_algorithm_from_string (NULL, strlen ("HS256")), CERVER_JWT_ALGORITHM_NONE, NULL
	);

	// the string is read by its length: what follows it is not part of it
	test_check_int_eq (
		cerver_jwt_algorithm_from_string ("HS256\",\"typ\"", strlen ("HS256")), CERVER_JWT_ALGORITHM_HS256, NULL
	);
	test_check_int_eq (
		cerver_jwt_algorithm_from_string ("HS256", strlen ("HS25")), CERVER_JWT_ALGORITHM_NONE, NULL
	);
	test_check_int_eq (
		cerver_jwt_algorithm_from_string ("HS256", sizeof ("HS256")), CERVER_JWT_ALGORITHM_NONE, NULL
	);

}

static void test_error_strings (void) {

	test_check_str_eq (cerver_jwt_error_to_string (CERVER_JWT_ERROR_NONE), "No error", NULL);
	test_check_str_eq (cerver_jwt_error_to_string (CERVER_JWT_ERROR_ARGUMENTS), "An argument is not valid", NULL);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_MEMORY), "The memory could not be allocated", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_KEY),
		"The key is not one that the algorithm takes or it can not sign", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_SIZE),
		"The token is bigger than its limit or than its buffer", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_FORMAT),
		"The token is not three parts of canonical base64url", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_HEADER), "The header is not an object that cerver takes", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_ALGORITHM),
		"The algorithm of the token is not the one of the key", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_SIGNATURE), "The signature is not the one of the token", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_CLAIMS),
		"The claims are not an object or one of them has another type", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_EXPIRED),
		"The token expired or it has no expiration and one is required", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_NOT_BEFORE), "The token is not valid yet", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_ISSUED_AT), "The token was issued in the future", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_ISSUER),
		"The issuer of the token is not the expected one", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_SUBJECT),
		"The subject of the token is not the expected one", NULL
	);
	test_check_str_eq (
		cerver_jwt_error_to_string (CERVER_JWT_ERROR_AUDIENCE),
		"The audience of the token does not have the expected one", NULL
	);

	test_check_str_eq (cerver_jwt_error_to_string ((CerverJwtError) JWT_TEST_OUT_OF_TABLE), "No error", NULL);

}

void jwt_tests_algorithms (void) {

	(void) printf ("Testing JWT algorithms...\n");

	test_run (test_algorithm_strings);
	test_run (test_algorithm_from_string);
	test_run (test_error_strings);

	(void) printf ("Done!\n");

}
