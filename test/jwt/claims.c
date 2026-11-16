#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/jwt/claims.h>
#include <cerver/jwt/jwt.h>

#include "../test.h"
#include "jwt.h"

// the time of the validations of the tests, and their leeway
#define CLAIMS_TEST_NOW						1000
#define CLAIMS_TEST_LEEWAY					5

// the seconds around the clock of the claims that are checked against it
#define CLAIMS_TEST_CLOCK_SECONDS			3600

#define CLAIMS_TEST_TEXT_SIZE				256

// the arena where the claims of the tests are read
static Arena *claims_test_arena = NULL;

// the claims of a text against a validation
static CerverJwtError claims_test (const char *text, const CerverJwtValidation *validation) {

	CerverJsonDocument *document = cerver_json_parse (
		claims_test_arena, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, NULL
	);

	test_check (document != NULL, text);

	return cerver_jwt_claims_validate (cerver_json_document_root (document), validation);

}

// the claims of a text at the time of the tests, with a leeway
static CerverJwtError claims_test_at (const char *text, const u32 leeway) {

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = CLAIMS_TEST_NOW;
	validation.leeway = leeway;

	return claims_test (text, &validation);

}

// the claims are an object
static void test_claims_object (void) {

	test_check_int_eq (cerver_jwt_claims_validate (NULL, NULL), CERVER_JWT_ERROR_CLAIMS, NULL);

	static const char *not_objects[] = { "[]", "\"text\"", "7", "true", "null", "[{\"exp\":1}]" };
	for (size_t i = 0; i < (sizeof (not_objects) / sizeof (not_objects[0])); i++) {
		test_check_int_eq (claims_test (not_objects[i], NULL), CERVER_JWT_ERROR_CLAIMS, not_objects[i]);
		test_check_int_eq (claims_test_at (not_objects[i], 0), CERVER_JWT_ERROR_CLAIMS, not_objects[i]);
	}

	// without a validation nothing is asked for: any claim that is not a time is taken
	static const char *objects[] = {
		"{}", "{\"id\":\"claims\"}", "{\"iss\":7,\"sub\":null,\"aud\":{\"a\":1}}", "{\"EXP\":\"text\",\"Nbf\":1.5}"
	};

	for (size_t i = 0; i < (sizeof (objects) / sizeof (objects[0])); i++) {
		test_check_int_eq (claims_test (objects[i], NULL), CERVER_JWT_ERROR_NONE, objects[i]);
		test_check_int_eq (claims_test_at (objects[i], 0), CERVER_JWT_ERROR_NONE, objects[i]);
	}

}

// a token is valid while the time is before "exp" plus the leeway
static void test_claims_expiration (void) {

	test_check_int_eq (claims_test_at ("{\"exp\":1001}", 0), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":1000}", 0), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":999}", 0), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":0}", 0), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":-1}", 0), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":-9223372036854775808}", 0), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":9223372036854775807}", 0), CERVER_JWT_ERROR_NONE, NULL);

	// the leeway is in favor of the token
	test_check_int_eq (claims_test_at ("{\"exp\":1000}", CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":996}", CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":995}", CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":994}", CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":9223372036854775807}", CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_NONE, NULL);

	// the largest leeway
	test_check_int_eq (claims_test_at ("{\"exp\":-4294966294}", (u32) -1), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test_at ("{\"exp\":-4294966295}", (u32) -1), CERVER_JWT_ERROR_EXPIRED, NULL);

	// a token without "exp" never expires, unless one is required
	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = CLAIMS_TEST_NOW;
	test_check_int_eq (claims_test ("{}", &validation), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test ("{\"nbf\":1,\"iat\":1}", &validation), CERVER_JWT_ERROR_NONE, NULL);

	validation.require_expiration = true;
	test_check_int_eq (claims_test ("{}", &validation), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test ("{\"nbf\":1,\"iat\":1}", &validation), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test ("{\"Exp\":1001}", &validation), CERVER_JWT_ERROR_EXPIRED, NULL);
	test_check_int_eq (claims_test ("{\"exp\":1001}", &validation), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test ("{\"exp\":1000}", &validation), CERVER_JWT_ERROR_EXPIRED, NULL);

}

// a time that must have been reached: "nbf" and "iat", each one with its error
static void claims_test_reached (const char *name, const CerverJwtError not_reached) {

	// the text of the claims of each case, that is also the message of its check
	char buffer[CLAIMS_TEST_TEXT_SIZE] = { 0 };
	char *claims = buffer;

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":999}", name);
	test_check_int_eq (claims_test_at (claims, 0), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":1000}", name);
	test_check_int_eq (claims_test_at (claims, 0), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":1001}", name);
	test_check_int_eq (claims_test_at (claims, 0), not_reached, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":9223372036854775807}", name);
	test_check_int_eq (claims_test_at (claims, 0), not_reached, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":-9223372036854775808}", name);
	test_check_int_eq (claims_test_at (claims, 0), CERVER_JWT_ERROR_NONE, claims);

	// the leeway is in favor of the token
	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":1001}", name);
	test_check_int_eq (claims_test_at (claims, CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":1005}", name);
	test_check_int_eq (claims_test_at (claims, CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":1006}", name);
	test_check_int_eq (claims_test_at (claims, CLAIMS_TEST_LEEWAY), not_reached, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":4294968295}", name);
	test_check_int_eq (claims_test_at (claims, (u32) -1), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":4294968296}", name);
	test_check_int_eq (claims_test_at (claims, (u32) -1), not_reached, claims);

}

static void test_claims_reached (void) {

	claims_test_reached ("nbf", CERVER_JWT_ERROR_NOT_BEFORE);
	claims_test_reached ("iat", CERVER_JWT_ERROR_ISSUED_AT);

}

// a time is an integer that fits in 64 bits with sign: anything else in its place is not a claim
static void claims_test_time_types (const char *name) {

	static const char *values[] = {
		"1001.0", "1001.5", "1e3", "1E3", "1001e0", "0.0", "-0.0", "\"1001\"", "\"\"", "true", "false", "null",
		"[1001]", "[]", "{\"exp\":1001}", "{}",
		"9223372036854775808", "18446744073709551615"
	};

	for (size_t i = 0; i < (sizeof (values) / sizeof (values[0])); i++) {
		char buffer[CLAIMS_TEST_TEXT_SIZE] = { 0 };
		const char *claims = buffer;
		(void) snprintf (buffer, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":%s}", name, values[i]);

		test_check_int_eq (claims_test_at (claims, 0), CERVER_JWT_ERROR_CLAIMS, claims);
		test_check_int_eq (claims_test_at (claims, CLAIMS_TEST_LEEWAY), CERVER_JWT_ERROR_CLAIMS, claims);
		test_check_int_eq (claims_test (claims, NULL), CERVER_JWT_ERROR_CLAIMS, claims);
	}

}

static void test_claims_time_types (void) {

	claims_test_time_types ("exp");
	claims_test_time_types ("nbf");
	claims_test_time_types ("iat");

}

// without a time in the validation the claims are checked against the clock
static void test_claims_clock (void) {

	const i64 now = (i64) time (NULL);

	// the text of the claims of each case, that is also the message of its check
	char buffer[CLAIMS_TEST_TEXT_SIZE] = { 0 };
	char *claims = buffer;

	(void) snprintf (
		claims, CLAIMS_TEST_TEXT_SIZE, "{\"exp\":%lld,\"nbf\":%lld,\"iat\":%lld}",
		(long long) (now + CLAIMS_TEST_CLOCK_SECONDS), (long long) (now - CLAIMS_TEST_CLOCK_SECONDS),
		(long long) (now - CLAIMS_TEST_CLOCK_SECONDS)
	);

	test_check_int_eq (claims_test (claims, NULL), CERVER_JWT_ERROR_NONE, claims);

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"exp\":%lld}", (long long) (now - CLAIMS_TEST_CLOCK_SECONDS));
	test_check_int_eq (claims_test (claims, NULL), CERVER_JWT_ERROR_EXPIRED, claims);
	test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_EXPIRED, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"nbf\":%lld}", (long long) (now + CLAIMS_TEST_CLOCK_SECONDS));
	test_check_int_eq (claims_test (claims, NULL), CERVER_JWT_ERROR_NOT_BEFORE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"iat\":%lld}", (long long) (now + CLAIMS_TEST_CLOCK_SECONDS));
	test_check_int_eq (claims_test (claims, NULL), CERVER_JWT_ERROR_ISSUED_AT, claims);

	// with a leeway of more than those seconds they are valid again
	validation.leeway = 2 * CLAIMS_TEST_CLOCK_SECONDS;
	test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_NONE, claims);

}

// a claim that the validation asks for is a string with exactly its characters
static void claims_test_string (
	const char *name, const CerverJwtError mismatch
) {

	static const char *others[] = {
		"\"Expected\"", "\"EXPECTED\"", "\"expected \"", "\" expected\"", "\"expecte\"", "\"expectedd\"", "\"\"",
		"\"expected\\u0000\"", "\"expected\\u0000more\"", "\"other\"",
		"7", "true", "null", "[\"expected\"]", "{\"expected\":1}"
	};

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = CLAIMS_TEST_NOW;

	if (mismatch == CERVER_JWT_ERROR_ISSUER) validation.issuer = "expected";
	else validation.subject = "expected";

	// the text of the claims of each case, that is also the message of its check
	char buffer[CLAIMS_TEST_TEXT_SIZE] = { 0 };
	char *claims = buffer;
	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":\"expected\"}", name);
	test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_NONE, claims);

	for (size_t i = 0; i < (sizeof (others) / sizeof (others[0])); i++) {
		(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":%s}", name, others[i]);

		test_check_int_eq (claims_test (claims, &validation), mismatch, claims);

		// without the validation asking for it, anything is taken
		test_check_int_eq (claims_test_at (claims, 0), CERVER_JWT_ERROR_NONE, claims);
	}

	// a token without the claim, and with the claim under another name
	test_check_int_eq (claims_test ("{}", &validation), mismatch, name);
	test_check_int_eq (claims_test ("{\"ISS\":\"expected\",\"Sub\":\"expected\"}", &validation), mismatch, name);

	// a validation that asks for a string without characters
	if (mismatch == CERVER_JWT_ERROR_ISSUER) validation.issuer = "";
	else validation.subject = "";

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":\"\"}", name);
	test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_NONE, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":\"expected\"}", name);
	test_check_int_eq (claims_test (claims, &validation), mismatch, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":7}", name);
	test_check_int_eq (claims_test (claims, &validation), mismatch, claims);

	(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"%s\":[]}", name);
	test_check_int_eq (claims_test (claims, &validation), mismatch, claims);

	test_check_int_eq (claims_test ("{}", &validation), mismatch, name);

}

static void test_claims_strings (void) {

	claims_test_string ("iss", CERVER_JWT_ERROR_ISSUER);
	claims_test_string ("sub", CERVER_JWT_ERROR_SUBJECT);

	// each one is its own claim
	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = CLAIMS_TEST_NOW;
	validation.issuer = "issuer";
	validation.subject = "subject";

	test_check_int_eq (
		claims_test ("{\"iss\":\"issuer\",\"sub\":\"subject\"}", &validation), CERVER_JWT_ERROR_NONE, NULL
	);
	test_check_int_eq (
		claims_test ("{\"iss\":\"subject\",\"sub\":\"issuer\"}", &validation), CERVER_JWT_ERROR_ISSUER, NULL
	);
	test_check_int_eq (
		claims_test ("{\"iss\":\"issuer\",\"sub\":\"issuer\"}", &validation), CERVER_JWT_ERROR_SUBJECT, NULL
	);

}

// "aud" is the expected string or an array that has it
static void test_claims_audience (void) {

	static const char *valid[] = {
		"\"expected\"", "[\"expected\"]", "[\"other\",\"expected\"]", "[\"expected\",\"other\"]",
		"[\"one\",\"two\",\"expected\",\"three\"]", "[7,null,[\"a\"],{\"b\":1},\"expected\"]", "[\"expected\",\"expected\"]"
	};

	static const char *others[] = {
		"\"other\"", "\"Expected\"", "\"expected \"", "\"expecte\"", "\"\"", "\"expected\\u0000\"",
		"[]", "[\"other\"]", "[\"one\",\"two\"]", "[\"Expected\",\"expected \",\"expecte\",\"\"]",
		"[[\"expected\"]]", "[{\"expected\":\"expected\"}]", "[\"expected\\u0000\"]",
		"7", "true", "null", "{\"expected\":1}", "{\"aud\":\"expected\"}"
	};

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = CLAIMS_TEST_NOW;
	validation.audience = "expected";

	// the text of the claims of each case, that is also the message of its check
	char buffer[CLAIMS_TEST_TEXT_SIZE] = { 0 };
	char *claims = buffer;

	for (size_t i = 0; i < (sizeof (valid) / sizeof (valid[0])); i++) {
		(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"aud\":%s}", valid[i]);
		test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_NONE, claims);
	}

	for (size_t i = 0; i < (sizeof (others) / sizeof (others[0])); i++) {
		(void) snprintf (claims, CLAIMS_TEST_TEXT_SIZE, "{\"aud\":%s}", others[i]);
		test_check_int_eq (claims_test (claims, &validation), CERVER_JWT_ERROR_AUDIENCE, claims);

		// without the validation asking for it, anything is taken
		test_check_int_eq (claims_test_at (claims, 0), CERVER_JWT_ERROR_NONE, claims);
	}

	test_check_int_eq (claims_test ("{}", &validation), CERVER_JWT_ERROR_AUDIENCE, NULL);
	test_check_int_eq (claims_test ("{\"AUD\":\"expected\"}", &validation), CERVER_JWT_ERROR_AUDIENCE, NULL);

	// a validation that asks for a string without characters
	validation.audience = "";
	test_check_int_eq (claims_test ("{\"aud\":\"\"}", &validation), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test ("{\"aud\":[\"a\",\"\"]}", &validation), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (claims_test ("{\"aud\":[]}", &validation), CERVER_JWT_ERROR_AUDIENCE, NULL);
	test_check_int_eq (claims_test ("{\"aud\":\"expected\"}", &validation), CERVER_JWT_ERROR_AUDIENCE, NULL);
	test_check_int_eq (claims_test ("{}", &validation), CERVER_JWT_ERROR_AUDIENCE, NULL);

}

// the claims are checked in their order, and the first one that is not valid is the error
static void test_claims_order (void) {

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = CLAIMS_TEST_NOW;
	validation.issuer = "issuer";
	validation.subject = "subject";
	validation.audience = "audience";

	test_check_int_eq (
		claims_test ("{\"exp\":1,\"nbf\":2000,\"iat\":2000,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_EXPIRED, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":\"1\",\"nbf\":2000,\"iat\":2000,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_CLAIMS, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1,\"nbf\":\"2000\",\"iat\":2000,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_EXPIRED, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":2000,\"iat\":2000,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_NOT_BEFORE, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":2000,\"iat\":2.5,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_NOT_BEFORE, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":500,\"iat\":2000,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_ISSUED_AT, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":500,\"iat\":2.5,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_CLAIMS, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":500,\"iat\":500,\"iss\":\"a\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_ISSUER, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":500,\"iat\":500,\"iss\":\"issuer\",\"sub\":\"b\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_SUBJECT, NULL
	);
	test_check_int_eq (
		claims_test ("{\"exp\":1500,\"nbf\":500,\"iat\":500,\"iss\":\"issuer\",\"sub\":\"subject\",\"aud\":\"c\"}", &validation),
		CERVER_JWT_ERROR_AUDIENCE, NULL
	);
	test_check_int_eq (
		claims_test (
			"{\"exp\":1500,\"nbf\":500,\"iat\":500,\"iss\":\"issuer\",\"sub\":\"subject\",\"aud\":\"audience\"}", &validation
		),
		CERVER_JWT_ERROR_NONE, NULL
	);

}

void jwt_tests_claims (void) {

	(void) printf ("Testing JWT claims...\n");

	claims_test_arena = arena_create (0);
	test_check_ptr (claims_test_arena);

	test_run (test_claims_object);
	test_run (test_claims_expiration);
	test_run (test_claims_reached);
	test_run (test_claims_time_types);
	test_run (test_claims_clock);
	test_run (test_claims_strings);
	test_run (test_claims_audience);
	test_run (test_claims_order);

	arena_delete (claims_test_arena);
	claims_test_arena = NULL;

	(void) printf ("Done!\n");

}
