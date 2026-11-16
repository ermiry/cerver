#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/sign.h>
#include <cerver/jwt/verify.h>

#include <cerver/utils/base64.h>

#include "../test.h"
#include "../crypto/crypto.h"
#include "../crypto/vectors.h"
#include "jwt.h"
#include "vectors.h"

// the examples of RFC 7515 appendix A expire at 1300819380: one second before, they are valid
#define VERIFY_TEST_EXAMPLE_EXPIRATION		1300819380
#define VERIFY_TEST_EXAMPLE_NOW				(VERIFY_TEST_EXAMPLE_EXPIRATION - 1)

// the claims of the tokens of the tests
#define VERIFY_TEST_CLAIMS					"{\"id\":\"verify\",\"value\":7}"

// another secret of an HMAC
#define VERIFY_TEST_OTHER_SECRET			"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"

// the characters of a text of the tests
#define VERIFY_TEST_TEXT_SIZE				2048

// the bytes of a header with a kid without characters, and the ones of the longest header: 1024 characters
#define VERIFY_TEST_HEADER_FIXED_SIZE		(sizeof ("{\"alg\":\"HS256\",\"kid\":\"\"}") - 1)
#define VERIFY_TEST_HEADER_SIZE_MAX			768

// the arrays inside the object of some claims that have the most levels
#define VERIFY_TEST_CLAIMS_ARRAYS			(CERVER_JWT_CLAIMS_DEPTH - 1)

// the characters of the last group of the HMAC of SHA-256 in base64url: without them the signature is
// still canonical, with two bytes less
#define VERIFY_TEST_LAST_GROUP				3

// a signature with one byte more than the largest one, in base64url, and one with many times its bytes:
// decoded into the buffer of a signature, it would be written over the stack of the call
#define VERIFY_TEST_LONG_SIGNATURE_LENGTH	base64url_encode_length (CERVER_CRYPTO_SIGNATURE_SIZE_MAX + 1)
#define VERIFY_TEST_HUGE_SIGNATURE_LENGTH	base64url_encode_length (CERVER_CRYPTO_SIGNATURE_SIZE_MAX << 5)

// a whole group of base64url, three bytes, and its characters
#define VERIFY_TEST_GROUP					"AAAA"
#define VERIFY_TEST_GROUP_LENGTH			(sizeof (VERIFY_TEST_GROUP) - 1)

#define VERIFY_TEST_THREADS					4
#define VERIFY_TEST_THREAD_ROUNDS			64

// the arena of the claims of the tests and the key of HS256 with the secret of the tests
static Arena *verify_test_arena = NULL;
static CerverJwtKey *verify_test_key = NULL;

#pragma region methods

// verifies a token: its claims are there only when it is valid
static CerverJwtError verify_test (
	const CerverJwtKey *key, const char *token, const CerverJwtValidation *validation
) {

	// a pointer that is not NULL: an error must clear it
	const CerverJsonValue *claims = (const CerverJsonValue *) token;

	const CerverJwtError error = cerver_jwt_verify (
		key, verify_test_arena, token, strlen (token), validation, &claims
	);

	test_check_bool_eq (claims != NULL, error == CERVER_JWT_ERROR_NONE, token);

	return error;

}

// the token of a header and a payload with the HMAC of the secret of the tests is taken by its key like that
static void verify_test_forged (
	const char *header, const char *payload, const CerverJwtError expected
) {

	char *token = jwt_test_forge_secret (header, payload);

	test_check_int_eq (verify_test (verify_test_key, token, NULL), expected, header);

	free (token);

}

// a token with one of its characters changed for another one of base64url
static char *verify_test_changed (const char *token, const size_t position) {

	char *changed = strdup (token);
	test_check_ptr (changed);

	changed[position] = (changed[position] == 'A') ? 'B' : 'A';

	return changed;

}

// the token of the first two parts as they are given, already in base64url, with the HMAC of the secret
static char *verify_test_forge_encoded (const char *parts) {

	CerverCryptoKey *key = cerver_crypto_key_create_secret (JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), NULL);
	test_check_ptr (key);

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	size_t size = 0;
	test_check_true (
		cerver_crypto_sign (
			key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, parts, strlen (parts), signature, sizeof (signature), &size
		)
	);

	cerver_crypto_key_delete (key);

	const size_t length = strlen (parts);
	char *token = (char *) calloc (length + base64url_encode_length (size) + sizeof ("."), sizeof (char));
	test_check_ptr (token);

	(void) memcpy (token, parts, length);
	token[length] = '.';
	(void) base64url_encode (token + length + 1, signature, size);

	return token;

}

// a token that the key of the tests signs
static char *verify_test_sign (const CerverJwtKey *key, const char *claims) {

	char *token = (char *) calloc (JWT_TEST_TOKEN_SIZE, sizeof (char));
	test_check_ptr (token);

	size_t length = 0;
	test_check_int_eq (
		cerver_jwt_sign (key, claims, strlen (claims), token, JWT_TEST_TOKEN_SIZE, &length),
		CERVER_JWT_ERROR_NONE, claims
	);

	return token;

}

#pragma endregion

#pragma region examples

// the claims of the examples of RFC 7515 appendix A.1 to A.3
static void verify_test_example_claims (const CerverJwtKey *key, const char *token) {

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = VERIFY_TEST_EXAMPLE_NOW;

	const CerverJsonValue *claims = NULL;
	test_check_int_eq (
		cerver_jwt_verify (key, verify_test_arena, token, strlen (token), &validation, &claims),
		CERVER_JWT_ERROR_NONE, token
	);

	test_check_true (cerver_json_is_object (claims));
	test_check_unsigned_eq (cerver_json_object_size (claims), 3, NULL);
	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (claims, "iss")), "joe", NULL);
	test_check_int_eq (
		cerver_json_integer_value (cerver_json_object_get (claims, "exp")), VERIFY_TEST_EXAMPLE_EXPIRATION, NULL
	);
	test_check_true (cerver_json_boolean_value (cerver_json_object_get (claims, "http://example.com/is_root")));

	// at its expiration, and with the clock, the example expired
	validation.now = VERIFY_TEST_EXAMPLE_EXPIRATION;
	test_check_int_eq (verify_test (key, token, &validation), CERVER_JWT_ERROR_EXPIRED, token);
	test_check_int_eq (verify_test (key, token, NULL), CERVER_JWT_ERROR_EXPIRED, token);

	// with a character of its claims changed it was not signed: the claims are not even read
	const size_t payload = (size_t) (strchr (token, '.') - token) + 1;
	char *changed = verify_test_changed (token, payload + 1);
	validation.now = VERIFY_TEST_EXAMPLE_NOW;
	test_check_int_eq (verify_test (key, changed, &validation), CERVER_JWT_ERROR_SIGNATURE, token);
	free (changed);

}

// RFC 7515 appendix A.1 (the JWT of RFC 7519 section 3.1), A.2 and A.3: HS256, RS256 and ES256
static void test_verify_examples (void) {

	CerverJwtKey *key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_HS256, CRYPTO_VECTOR_SECRET_JWK);
	verify_test_example_claims (key, CRYPTO_VECTOR_SECRET_MESSAGE "." CRYPTO_VECTOR_SECRET_SIGNATURE);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_RS256, CRYPTO_VECTOR_RSA_PUBLIC_JWK);
	verify_test_example_claims (key, CRYPTO_VECTOR_RSA_MESSAGE "." CRYPTO_VECTOR_RSA_SIGNATURE);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_RS256, CRYPTO_VECTOR_RSA_PRIVATE_JWK);
	verify_test_example_claims (key, CRYPTO_VECTOR_RSA_MESSAGE "." CRYPTO_VECTOR_RSA_SIGNATURE);
	cerver_jwt_key_delete (key);

	// the S of this signature is in the upper half of the order: a token of another signer is taken
	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_ES256, CRYPTO_VECTOR_P256_PUBLIC_JWK);
	verify_test_example_claims (key, CRYPTO_VECTOR_P256_MESSAGE "." CRYPTO_VECTOR_P256_SIGNATURE);
	cerver_jwt_key_delete (key);

}

// an example whose payload is not the JSON of some claims: its signature is verified, and only then the
// claims are refused; with a character of its payload changed, the signature is the error
static void verify_test_example_payload (
	const CerverJwtAlgorithm algorithm, const char *jwk, const char *token
) {

	CerverJwtKey *key = jwt_test_key_jwk (algorithm, jwk);

	test_check_int_eq (verify_test (key, token, NULL), CERVER_JWT_ERROR_CLAIMS, token);

	const size_t payload = (size_t) (strchr (token, '.') - token) + 1;
	char *changed = verify_test_changed (token, payload + 1);
	test_check_int_eq (verify_test (key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, token);
	free (changed);

	// and with the first character of its signature changed
	const size_t signature = (size_t) (strrchr (token, '.') - token) + 1;
	changed = verify_test_changed (token, signature);
	test_check_int_eq (verify_test (key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, token);
	free (changed);

	cerver_jwt_key_delete (key);

}

// RFC 7515 appendix A.4, RFC 8037 appendix A.4 and RFC 7520 sections 4.1 to 4.4:
// ES512, EdDSA, RS256, PS384, ES512 and HS256 over a payload that is a text
static void test_verify_examples_payload (void) {

	verify_test_example_payload (
		CERVER_JWT_ALGORITHM_ES512, CRYPTO_VECTOR_P521_PUBLIC_JWK,
		CRYPTO_VECTOR_P521_MESSAGE "." CRYPTO_VECTOR_P521_SIGNATURE
	);

	verify_test_example_payload (
		CERVER_JWT_ALGORITHM_EDDSA, CRYPTO_VECTOR_ED25519_PUBLIC_JWK,
		CRYPTO_VECTOR_ED25519_MESSAGE "." CRYPTO_VECTOR_ED25519_SIGNATURE
	);

	verify_test_example_payload (CERVER_JWT_ALGORITHM_RS256, JWT_VECTOR_RSA_PUBLIC_JWK, JWT_VECTOR_RS256_TOKEN);
	verify_test_example_payload (CERVER_JWT_ALGORITHM_RS256, JWT_VECTOR_RSA_PRIVATE_JWK, JWT_VECTOR_RS256_TOKEN);
	verify_test_example_payload (CERVER_JWT_ALGORITHM_PS384, JWT_VECTOR_RSA_PUBLIC_JWK, JWT_VECTOR_PS384_TOKEN);
	verify_test_example_payload (CERVER_JWT_ALGORITHM_ES512, JWT_VECTOR_CURVE_PUBLIC_JWK, JWT_VECTOR_ES512_TOKEN);
	verify_test_example_payload (CERVER_JWT_ALGORITHM_ES512, JWT_VECTOR_CURVE_PRIVATE_JWK, JWT_VECTOR_ES512_TOKEN);
	verify_test_example_payload (CERVER_JWT_ALGORITHM_HS256, JWT_VECTOR_SECRET_JWK, JWT_VECTOR_HS256_TOKEN);

	// the same key with another algorithm: the token is of the algorithm of its header
	CerverJwtKey *key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_PS256, JWT_VECTOR_RSA_PUBLIC_JWK);
	test_check_int_eq (verify_test (key, JWT_VECTOR_RS256_TOKEN, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	test_check_int_eq (verify_test (key, JWT_VECTOR_PS384_TOKEN, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	cerver_jwt_key_delete (key);

}

#pragma endregion

#pragma region token

static void test_verify_arguments (void) {

	char *token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);
	const size_t length = strlen (token);

	const CerverJsonValue *claims = (const CerverJsonValue *) token;
	test_check_int_eq (
		cerver_jwt_verify (NULL, verify_test_arena, token, length, NULL, &claims), CERVER_JWT_ERROR_ARGUMENTS, NULL
	);
	test_check_null_ptr (claims);

	claims = (const CerverJsonValue *) token;
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, NULL, token, length, NULL, &claims), CERVER_JWT_ERROR_ARGUMENTS, NULL
	);
	test_check_null_ptr (claims);

	claims = (const CerverJsonValue *) token;
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, NULL, length, NULL, &claims),
		CERVER_JWT_ERROR_ARGUMENTS, NULL
	);
	test_check_null_ptr (claims);

	// the claims are optional, when the token is valid and when it is not
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, token, length, NULL, NULL), CERVER_JWT_ERROR_NONE, NULL
	);
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, token, length - VERIFY_TEST_LAST_GROUP, NULL, NULL),
		CERVER_JWT_ERROR_SIGNATURE, NULL
	);
	test_check_int_eq (
		cerver_jwt_verify (NULL, NULL, NULL, 0, NULL, NULL), CERVER_JWT_ERROR_ARGUMENTS, NULL
	);

	// the token is read by its length: what follows it is not part of it
	char longer[JWT_TEST_TOKEN_SIZE] = { 0 };
	(void) snprintf (longer, JWT_TEST_TOKEN_SIZE, "%s.more", token);
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, longer, length, NULL, &claims), CERVER_JWT_ERROR_NONE, NULL
	);
	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (claims, "id")), "verify", NULL);
	test_check_int_eq (cerver_json_integer_value (cerver_json_object_get (claims, "value")), 7, NULL);

	free (token);

}

// a token with more characters than its limit is not read
static void test_verify_size (void) {

	char *token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);
	const size_t length = strlen (token);

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;

	validation.max_size = length;
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_NONE, NULL);

	validation.max_size = length - 1;
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_SIZE, NULL);

	validation.max_size = sizeof (char);
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_SIZE, NULL);

	free (token);

	// the limit of every token: no validation takes one that is longer
	char *large = (char *) malloc (CERVER_JWT_SIZE_MAX + 1);
	test_check_ptr (large);
	(void) memset (large, 'a', CERVER_JWT_SIZE_MAX + 1);

	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, large, CERVER_JWT_SIZE_MAX + 1, NULL, NULL),
		CERVER_JWT_ERROR_SIZE, NULL
	);

	validation.max_size = 0;
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, large, CERVER_JWT_SIZE_MAX + 1, &validation, NULL),
		CERVER_JWT_ERROR_SIZE, NULL
	);

	validation.max_size = (size_t) -1;
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, large, CERVER_JWT_SIZE_MAX + 1, &validation, NULL),
		CERVER_JWT_ERROR_SIZE, NULL
	);

	// with one character less it is read: it has no parts
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, large, CERVER_JWT_SIZE_MAX, NULL, NULL),
		CERVER_JWT_ERROR_FORMAT, NULL
	);

	free (large);

}

// a token is three parts with characters
static void test_verify_parts (void) {

	static const char *not_tokens[] = {
		"", ".", "..", "...", "a", "a.b", ".b.c", "a..c", "a.b.", "a.b.c.d", "a.b.c.", ".a.b.c",
		"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30",
		"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30.", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..AAAA",
		".e30.AAAA",
		// four parts are not a token, whatever its first one is: it is not read
		"e30.e30.AAAA.AAAA", "AAAA.e30.AAAA.AAAA", "e30.e30.AAAA.", "e30.e30..AAAA"
	};

	for (size_t i = 0; i < (sizeof (not_tokens) / sizeof (not_tokens[0])); i++) {
		test_check_int_eq (verify_test (verify_test_key, not_tokens[i], NULL), CERVER_JWT_ERROR_FORMAT, not_tokens[i]);
	}

	// a token with a fourth part, with a character after it and with one before it
	char *token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);

	char text[JWT_TEST_TOKEN_SIZE] = { 0 };
	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s.AAAA", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s\n", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s ", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, " %s", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "Bearer %s", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);

	free (token);

}

#pragma endregion

#pragma region header

// the headers that are taken: the one that cerver writes and any other object with the "alg" of the key
static void test_verify_header (void) {

	static const char *headers[] = {
		JWT_TEST_HEADER ("HS256"),
		"{\"typ\":\"JWT\",\"alg\":\"HS256\"}",
		"{\"alg\":\"HS256\"}",
		" { \"alg\" : \"HS256\" , \"typ\" : \"JWT\" } ",
		"{\"typ\":\"JWT\",\r\n \"alg\":\"HS256\"}",
		"{\"alg\":\"HS256\",\"typ\":\"at+jwt\",\"kid\":\"the key\",\"cty\":\"text\",\"jku\":\"u\",\"x5c\":[\"a\",\"b\"]}",
		"{\"alg\":\"HS256\",\"typ\":7,\"kid\":null,\"b64\":true}",
		"{\"alg\":\"\\u0048S256\"}",
		"{\"alg\":\"HS256\",\"deep\":[[[1]]]}"
	};

	for (size_t i = 0; i < (sizeof (headers) / sizeof (headers[0])); i++) {
		verify_test_forged (headers[i], VERIFY_TEST_CLAIMS, CERVER_JWT_ERROR_NONE);
	}

	// what is not the JSON of an object
	static const char *not_objects[] = {
		"{\"alg\":\"HS256\"", "{\"alg\":\"HS256\"}}", "{alg:\"HS256\"}", "{'alg':'HS256'}",
		"[]", "[{\"alg\":\"HS256\"}]", "\"HS256\"", "null", "true", "7", "HS256", " ",
		"{\"alg\":\"HS256\"}{\"alg\":\"HS256\"}"
	};

	for (size_t i = 0; i < (sizeof (not_objects) / sizeof (not_objects[0])); i++) {
		verify_test_forged (not_objects[i], VERIFY_TEST_CLAIMS, CERVER_JWT_ERROR_HEADER);
	}

	// an object without an "alg" that is a string
	static const char *not_strings[] = {
		"{}", "{\"typ\":\"JWT\"}", "{\"ALG\":\"HS256\"}", "{\"alg \":\"HS256\"}",
		"{\"alg\":7}", "{\"alg\":null}", "{\"alg\":true}", "{\"alg\":[\"HS256\"]}", "{\"alg\":{\"alg\":\"HS256\"}}"
	};

	for (size_t i = 0; i < (sizeof (not_strings) / sizeof (not_strings[0])); i++) {
		verify_test_forged (not_strings[i], VERIFY_TEST_CLAIMS, CERVER_JWT_ERROR_HEADER);
	}

	// a key more than once, a null character and more levels than the limit
	static const char *refused[] = {
		"{\"alg\":\"HS256\",\"alg\":\"HS256\"}", "{\"alg\":\"none\",\"alg\":\"HS256\"}",
		"{\"alg\":\"HS256\",\"alg\":\"none\"}", "{\"alg\":\"HS256\",\"typ\":\"JWT\",\"typ\":\"JWT\"}",
		"{\"alg\":\"HS256\\u0000\"}", "{\"alg\":\"HS256\",\"kid\":\"a\\u0000b\"}", "{\"alg\":\"HS256\",\"a\\u0000\":1}",
		"{\"alg\":\"HS256\",\"deep\":[[[[1]]]]}"
	};

	for (size_t i = 0; i < (sizeof (refused) / sizeof (refused[0])); i++) {
		verify_test_forged (refused[i], VERIFY_TEST_CLAIMS, CERVER_JWT_ERROR_HEADER);
	}

	// an extension that must be understood
	static const char *critical[] = {
		"{\"alg\":\"HS256\",\"crit\":[\"exp\"],\"exp\":1}", "{\"alg\":\"HS256\",\"crit\":[]}",
		"{\"alg\":\"HS256\",\"crit\":null}", "{\"alg\":\"HS256\",\"crit\":[\"b64\"],\"b64\":false}",
		"{\"crit\":[\"alg\"],\"alg\":\"HS256\"}"
	};

	for (size_t i = 0; i < (sizeof (critical) / sizeof (critical[0])); i++) {
		verify_test_forged (critical[i], VERIFY_TEST_CLAIMS, CERVER_JWT_ERROR_HEADER);
	}

}

// the header that cerver writes is known by all its characters and only by them: with more after them, or
// without the last ones, it is read like any other header, and it is not the JSON of an object
static void test_verify_header_known (void) {

	char *token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);
	const size_t header_length = (size_t) (strchr (token, '.') - token);
	const int claims_length = (int) ((size_t) (strrchr (token, '.') - token) - header_length);

	char parts[JWT_TEST_TOKEN_SIZE] = { 0 };
	(void) snprintf (
		parts, JWT_TEST_TOKEN_SIZE, "%.*s" VERIFY_TEST_GROUP "%.*s",
		(int) header_length, token, claims_length, token + header_length
	);

	char *longer = verify_test_forge_encoded (parts);
	test_check_int_eq (verify_test (verify_test_key, longer, NULL), CERVER_JWT_ERROR_HEADER, NULL);
	free (longer);

	(void) snprintf (
		parts, JWT_TEST_TOKEN_SIZE, "%.*s%.*s",
		(int) (header_length - VERIFY_TEST_GROUP_LENGTH), token, claims_length, token + header_length
	);

	char *shorter = verify_test_forge_encoded (parts);
	test_check_int_eq (verify_test (verify_test_key, shorter, NULL), CERVER_JWT_ERROR_HEADER, NULL);
	free (shorter);

	free (token);

}

// the "alg" of a token is exactly the string of the algorithm of its key
static void test_verify_header_algorithm (void) {

	static const char *algorithms[] = {
		"none", "None", "NONE", "nOnE", "", " ", "hs256", "Hs256", "HS256 ", " HS256", "HS25", "HS2566",
		"HS384", "HS512", "RS256", "RS384", "RS512", "ES256", "ES384", "ES512", "PS256", "PS384", "PS512", "EdDSA",
		"HS256\\n", "HMAC-SHA256", "Invalid"
	};

	for (size_t i = 0; i < (sizeof (algorithms) / sizeof (algorithms[0])); i++) {
		char header[VERIFY_TEST_TEXT_SIZE] = { 0 };
		(void) snprintf (header, VERIFY_TEST_TEXT_SIZE, "{\"alg\":\"%s\",\"typ\":\"JWT\"}", algorithms[i]);

		verify_test_forged (header, VERIFY_TEST_CLAIMS, CERVER_JWT_ERROR_ALGORITHM);
	}

	// the key of another HMAC with the same secret: the header of the token is not its own
	CerverJwtKey *other = jwt_test_key_secret (CERVER_JWT_ALGORITHM_HS384);

	char *token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	free (token);

	token = verify_test_sign (other, VERIFY_TEST_CLAIMS);
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_NONE, NULL);
	free (token);

	cerver_jwt_key_delete (other);

}

// a header with a kid of some characters
static void verify_test_header_padded (const size_t header_size, const CerverJwtError expected) {

	char *header = (char *) calloc (header_size + 1, sizeof (char));
	test_check_ptr (header);

	(void) memset (header, 'k', header_size);
	(void) memcpy (header, "{\"alg\":\"HS256\",\"kid\":\"", strlen ("{\"alg\":\"HS256\",\"kid\":\""));
	(void) memcpy (header + header_size - strlen ("\"}"), "\"}", strlen ("\"}"));

	verify_test_forged (header, VERIFY_TEST_CLAIMS, expected);

	free (header);

}

// a header with more characters than its limit is not read
static void test_verify_header_size (void) {

	test_check_unsigned_eq (
		base64url_encode_length (VERIFY_TEST_HEADER_SIZE_MAX), CERVER_JWT_HEADER_SIZE_MAX,
		"the header of the test is not the longest one"
	);

	verify_test_header_padded (VERIFY_TEST_HEADER_FIXED_SIZE + 1, CERVER_JWT_ERROR_NONE);
	verify_test_header_padded (VERIFY_TEST_HEADER_SIZE_MAX, CERVER_JWT_ERROR_NONE);
	verify_test_header_padded (VERIFY_TEST_HEADER_SIZE_MAX + 1, CERVER_JWT_ERROR_SIZE);
	verify_test_header_padded (VERIFY_TEST_TEXT_SIZE, CERVER_JWT_ERROR_SIZE);

}

// a part that is not canonical base64url: padding, the other alphabet and bits that are left
static void test_verify_canonical (void) {

	// a header of 16 bytes: its last character has four bits that are not used
	char *token = jwt_test_forge_secret ("{\"alg\":\"HS256\" }", VERIFY_TEST_CLAIMS);
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);

	const size_t header_end = (size_t) (strchr (token, '.') - token) - 1;
	test_check_int_eq (token[header_end], 'Q', "the last character of the header of the test");

	token[header_end] = 'R';
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_FORMAT, NULL);
	free (token);

	// the header and the claims with padding
	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiIgfQ==.e30");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_FORMAT, NULL);
	free (token);

	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.e30=");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_FORMAT, NULL);
	free (token);

	// the same parts as they must be
	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.e30");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);
	free (token);

	// the claims {"a":"~~~"} with the alphabet of base64, and the ones that leave bits: {"a":1}
	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.eyJhIjoifn5+In0");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_FORMAT, NULL);
	free (token);

	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.eyJhIjoifn5-In0");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);
	free (token);

	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.eyJhIjoxfR");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_FORMAT, NULL);
	free (token);

	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.eyJhIjoxfQ");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);
	free (token);

	// a part with a single character in its last group
	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.eyJhIjoxfQAAA");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_FORMAT, NULL);
	free (token);

	// the same claims with a byte after them are decoded, and they are not the JSON of an object
	token = verify_test_forge_encoded ("eyJhbGciOiJIUzI1NiJ9.eyJhIjoxfQA");
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_CLAIMS, NULL);
	free (token);

	// the signature with padding and with a character that is not of the alphabet
	token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);

	char text[JWT_TEST_TOKEN_SIZE] = { 0 };
	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s=", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s", token);
	text[strlen (text) - 2] = '+';
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_FORMAT, NULL);

	free (token);

}

// the claims that a header repeats are the ones of the claims
static void test_verify_header_repeated (void) {

	verify_test_forged (
		"{\"alg\":\"HS256\",\"iss\":\"issuer\",\"sub\":\"subject\",\"aud\":\"audience\"}",
		"{\"iss\":\"issuer\",\"sub\":\"subject\",\"aud\":\"audience\"}", CERVER_JWT_ERROR_NONE
	);

	verify_test_forged (
		"{\"alg\":\"HS256\",\"aud\":[\"one\",\"two\"]}", "{\"aud\":[\"one\",\"two\"]}", CERVER_JWT_ERROR_NONE
	);

	// a claim that only the header has, or only the claims
	verify_test_forged ("{\"alg\":\"HS256\",\"iss\":\"issuer\"}", "{\"sub\":\"subject\"}", CERVER_JWT_ERROR_NONE);
	verify_test_forged ("{\"alg\":\"HS256\"}", "{\"iss\":\"issuer\"}", CERVER_JWT_ERROR_NONE);

	// another value
	verify_test_forged ("{\"alg\":\"HS256\",\"iss\":\"other\"}", "{\"iss\":\"issuer\"}", CERVER_JWT_ERROR_HEADER);
	verify_test_forged ("{\"alg\":\"HS256\",\"sub\":\"other\"}", "{\"sub\":\"subject\"}", CERVER_JWT_ERROR_HEADER);
	verify_test_forged ("{\"alg\":\"HS256\",\"aud\":\"other\"}", "{\"aud\":\"audience\"}", CERVER_JWT_ERROR_HEADER);
	verify_test_forged ("{\"alg\":\"HS256\",\"iss\":\"issuer \"}", "{\"iss\":\"issuer\"}", CERVER_JWT_ERROR_HEADER);

	// another type
	verify_test_forged ("{\"alg\":\"HS256\",\"iss\":7}", "{\"iss\":\"7\"}", CERVER_JWT_ERROR_HEADER);
	verify_test_forged ("{\"alg\":\"HS256\",\"aud\":[\"audience\"]}", "{\"aud\":\"audience\"}", CERVER_JWT_ERROR_HEADER);
	verify_test_forged ("{\"alg\":\"HS256\",\"aud\":[\"two\",\"one\"]}", "{\"aud\":[\"one\",\"two\"]}", CERVER_JWT_ERROR_HEADER);
	verify_test_forged ("{\"alg\":\"HS256\",\"sub\":null}", "{\"sub\":\"subject\"}", CERVER_JWT_ERROR_HEADER);

	// the other claims are not compared
	verify_test_forged ("{\"alg\":\"HS256\",\"exp\":1,\"id\":\"a\"}", "{\"id\":\"b\"}", CERVER_JWT_ERROR_NONE);

}

#pragma endregion

#pragma region algorithm

// the token never chooses how it is verified: a key of RSA never computes an HMAC with its own text
static void test_verify_confusion (void) {

	size_t pem_size = 0;
	char *pem = jwt_test_file_read (CRYPTO_TEST_KEY_RSA CRYPTO_TEST_KEY_PUBLIC_EXTENSION, &pem_size);

	// the public key, that everyone has, as the secret of an HMAC
	CerverCryptoKey *secret = cerver_crypto_key_create_secret (pem, pem_size, NULL);
	test_check_ptr (secret);

	CerverJwtKey *public_key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_RSA, false);

	char *token = jwt_test_forge (
		secret, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256,
		JWT_TEST_HEADER ("HS256"), VERIFY_TEST_CLAIMS, strlen (VERIFY_TEST_CLAIMS)
	);

	test_check_int_eq (verify_test (public_key, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	free (token);

	// the same HMAC under the header of the key is not its signature
	token = jwt_test_forge (
		secret, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256,
		JWT_TEST_HEADER ("RS256"), VERIFY_TEST_CLAIMS, strlen (VERIFY_TEST_CLAIMS)
	);

	test_check_int_eq (verify_test (public_key, token, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	free (token);

	cerver_crypto_key_delete (secret);
	free (pem);

	// a token of the private key of RSA is not one of a secret, nor of the same key with PSS
	CerverJwtKey *private_key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_RSA, true);
	token = verify_test_sign (private_key, VERIFY_TEST_CLAIMS);

	test_check_int_eq (verify_test (public_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);

	CerverJwtKey *other = jwt_test_key_pem (CERVER_JWT_ALGORITHM_PS256, CRYPTO_TEST_KEY_RSA, false);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	cerver_jwt_key_delete (other);

	other = jwt_test_key_pem (CERVER_JWT_ALGORITHM_RS384, CRYPTO_TEST_KEY_RSA, false);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	cerver_jwt_key_delete (other);

	free (token);

	// its signature under the header of PSS is not one of PSS
	CerverCryptoKey *crypto_key = jwt_test_crypto_pem (CRYPTO_TEST_KEY_RSA, true);
	token = jwt_test_forge (
		crypto_key, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256,
		JWT_TEST_HEADER ("PS256"), VERIFY_TEST_CLAIMS, strlen (VERIFY_TEST_CLAIMS)
	);

	other = jwt_test_key_pem (CERVER_JWT_ALGORITHM_PS256, CRYPTO_TEST_KEY_RSA, false);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	cerver_jwt_key_delete (other);

	free (token);
	cerver_crypto_key_delete (crypto_key);

	cerver_jwt_key_delete (private_key);
	cerver_jwt_key_delete (public_key);

}

// a token without a signature is the one of no key
static void verify_test_none (const CerverJwtKey *key) {

	static const u8 zeros[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };

	// without its third part
	test_check_int_eq (
		verify_test (key, "eyJhbGciOiJub25lIn0.eyJpZCI6InZlcmlmeSJ9.", NULL), CERVER_JWT_ERROR_FORMAT, NULL
	);
	test_check_int_eq (
		verify_test (key, "eyJhbGciOiJub25lIn0.eyJpZCI6InZlcmlmeSJ9", NULL), CERVER_JWT_ERROR_FORMAT, NULL
	);

	// with the bytes of a signature of every size of the algorithms
	static const size_t sizes[] = { 1, 32, 48, 64, 96, 114, 132, 256 };
	for (size_t i = 0; i < (sizeof (sizes) / sizeof (sizes[0])); i++) {
		char *token = jwt_test_join ("{\"alg\":\"none\"}", VERIFY_TEST_CLAIMS, strlen (VERIFY_TEST_CLAIMS), zeros, sizes[i]);
		test_check_int_eq (verify_test (key, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
		free (token);

		token = jwt_test_join ("{\"alg\":\"none\",\"typ\":\"JWT\"}", VERIFY_TEST_CLAIMS, strlen (VERIFY_TEST_CLAIMS), zeros, sizes[i]);
		test_check_int_eq (verify_test (key, token, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
		free (token);
	}

}

static void test_verify_none (void) {

	verify_test_none (verify_test_key);

	CerverJwtKey *key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_RSA, false);
	verify_test_none (key);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_PS512, CRYPTO_TEST_KEY_RSA, true);
	verify_test_none (key);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_ES256, CRYPTO_TEST_KEY_P256, false);
	verify_test_none (key);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_EDDSA, CRYPTO_TEST_KEY_ED25519, false);
	verify_test_none (key);
	cerver_jwt_key_delete (key);

}

#pragma endregion

#pragma region signature

static void test_verify_signature (void) {

	char *token = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);
	const size_t length = strlen (token);
	const size_t signature = (size_t) (strrchr (token, '.') - token) + 1;

	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_NONE, NULL);

	// a character of the signature changed, of the claims and of the header
	char *changed = verify_test_changed (token, signature);
	test_check_int_eq (verify_test (verify_test_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	free (changed);

	changed = verify_test_changed (token, signature + ((length - signature) / 2));
	test_check_int_eq (verify_test (verify_test_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	free (changed);

	changed = verify_test_changed (token, signature - 3);
	test_check_int_eq (verify_test (verify_test_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	free (changed);

	// a signature with less bytes and with more
	char text[JWT_TEST_TOKEN_SIZE] = { 0 };
	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s", token);
	text[length - VERIFY_TEST_LAST_GROUP] = '\0';
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s", token);
	text[signature + 4] = '\0';
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%sAAAA", token);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	// the signature of another secret, and the one of other claims
	CerverJwtKey *other = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, VERIFY_TEST_OTHER_SECRET, strlen (VERIFY_TEST_OTHER_SECRET), NULL
	);

	test_check_ptr (other);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	char *other_token = verify_test_sign (other, VERIFY_TEST_CLAIMS);
	test_check_int_eq (verify_test (verify_test_key, other_token, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	test_check_int_eq (verify_test (other, other_token, NULL), CERVER_JWT_ERROR_NONE, NULL);
	free (other_token);

	cerver_jwt_key_delete (other);

	other_token = verify_test_sign (verify_test_key, "{\"id\":\"other\"}");
	(void) snprintf (text, JWT_TEST_TOKEN_SIZE, "%s", token);
	(void) snprintf (text + signature, JWT_TEST_TOKEN_SIZE - signature, "%s", strrchr (other_token, '.') + 1);
	test_check_int_eq (verify_test (verify_test_key, text, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	free (other_token);

	// a signature with the most bytes, and one with more: it is not decoded
	char *large = (char *) calloc (signature + VERIFY_TEST_HUGE_SIGNATURE_LENGTH + 1, sizeof (char));
	test_check_ptr (large);

	(void) memcpy (large, token, signature);
	(void) memset (large + signature, 'A', base64url_encode_length (CERVER_CRYPTO_SIGNATURE_SIZE_MAX));
	test_check_int_eq (verify_test (verify_test_key, large, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	(void) memset (large + signature, 'A', VERIFY_TEST_LONG_SIGNATURE_LENGTH);
	test_check_int_eq (verify_test (verify_test_key, large, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	(void) memset (large + signature, 'A', VERIFY_TEST_HUGE_SIGNATURE_LENGTH);
	test_check_int_eq (verify_test (verify_test_key, large, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);

	free (large);

	free (token);

}

// a token of every kind of key, with a character changed in each of its parts
static void verify_test_signature_key (
	const CerverJwtAlgorithm algorithm, const char *name
) {

	CerverJwtKey *private_key = jwt_test_key_pem (algorithm, name, true);
	CerverJwtKey *public_key = jwt_test_key_pem (algorithm, name, false);

	char *token = verify_test_sign (private_key, VERIFY_TEST_CLAIMS);
	const size_t signature = (size_t) (strrchr (token, '.') - token) + 1;

	test_check_int_eq (verify_test (public_key, token, NULL), CERVER_JWT_ERROR_NONE, name);

	char *changed = verify_test_changed (token, signature);
	test_check_int_eq (verify_test (public_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, name);
	free (changed);

	changed = verify_test_changed (token, signature + 7);
	test_check_int_eq (verify_test (public_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, name);
	free (changed);

	changed = verify_test_changed (token, signature - 3);
	test_check_int_eq (verify_test (public_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, name);
	free (changed);

	// the token of the same claims of the secret of the tests is not its token
	changed = verify_test_sign (verify_test_key, VERIFY_TEST_CLAIMS);
	test_check_int_eq (verify_test (public_key, changed, NULL), CERVER_JWT_ERROR_ALGORITHM, name);
	free (changed);

	free (token);

	cerver_jwt_key_delete (private_key);
	cerver_jwt_key_delete (public_key);

}

static void test_verify_signature_keys (void) {

	verify_test_signature_key (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_RSA);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_RS512, CRYPTO_TEST_KEY_RSA);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_PS256, CRYPTO_TEST_KEY_RSA);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_PS384, CRYPTO_TEST_KEY_RSA);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_ES256, CRYPTO_TEST_KEY_P256);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_ES384, CRYPTO_TEST_KEY_P384);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_ES512, CRYPTO_TEST_KEY_P521);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_EDDSA, CRYPTO_TEST_KEY_ED25519);
	verify_test_signature_key (CERVER_JWT_ALGORITHM_EDDSA, CRYPTO_TEST_KEY_ED448);

	// a token of a key of a curve is not one of another key of the same curve
	CerverJwtKey *private_key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_ES256, CRYPTO_TEST_KEY_P256, true);
	CerverJwtKey *other = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_ES256, CRYPTO_VECTOR_P256_PUBLIC_JWK);

	char *token = verify_test_sign (private_key, VERIFY_TEST_CLAIMS);
	test_check_int_eq (verify_test (other, token, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL);
	free (token);

	cerver_jwt_key_delete (other);
	cerver_jwt_key_delete (private_key);

}

#pragma endregion

#pragma region claims

// some claims with an array inside another, some times
static void verify_test_nested (char *claims, const size_t arrays) {

	size_t position = (size_t) snprintf (claims, VERIFY_TEST_TEXT_SIZE, "{\"deep\":");

	for (size_t i = 0; i < arrays; i++) claims[position++] = '[';
	for (size_t i = 0; i < arrays; i++) claims[position++] = ']';

	claims[position++] = '}';
	claims[position] = '\0';

}

// the second part of a token is the JSON of an object
static void test_verify_claims (void) {

	static const char *objects[] = {
		"{}", " { } ", "{\"a\":1}", "{\"a\":[1,2,{\"b\":null}],\"c\":{\"d\":\"e\"}}",
		"{\"text\":\"\\u00f1 \xc3\xb1\"}", "{\"\":\"\"}", "{\"alg\":\"none\"}"
	};

	for (size_t i = 0; i < (sizeof (objects) / sizeof (objects[0])); i++) {
		verify_test_forged (JWT_TEST_HEADER ("HS256"), objects[i], CERVER_JWT_ERROR_NONE);
		verify_test_forged ("{\"alg\":\"HS256\"}", objects[i], CERVER_JWT_ERROR_NONE);
	}

	// what is not: also with the header that cerver writes
	static const char *not_objects[] = {
		"[]", "[{\"a\":1}]", "\"text\"", "7", "1.5", "true", "null", " ", "text", "Payload",
		"{", "{\"a\":1", "{\"a\":1}}", "{\"a\":1}{\"a\":1}", "{\"a\":1} x", "{a:1}", "{\"a\":01}", "{\"a\":\"\xff\"}",
		"{\"a\":1,\"a\":1}", "{\"a\":1,\"b\":2,\"a\":3}", "{\"a\":{\"b\":1,\"b\":2}}",
		"{\"a\":\"\\u0000\"}", "{\"a\\u0000\":1}", "{\"a\":[\"b\\u0000c\"]}",
		"{\"exp\":99999999999999999999999999}", "{\"nbf\":-9223372036854775809}", "{\"a\":1e999}"
	};

	for (size_t i = 0; i < (sizeof (not_objects) / sizeof (not_objects[0])); i++) {
		verify_test_forged (JWT_TEST_HEADER ("HS256"), not_objects[i], CERVER_JWT_ERROR_CLAIMS);
		verify_test_forged ("{\"alg\":\"HS256\"}", not_objects[i], CERVER_JWT_ERROR_CLAIMS);
	}

	// the most levels, and one more
	char claims[VERIFY_TEST_TEXT_SIZE] = { 0 };
	verify_test_nested (claims, VERIFY_TEST_CLAIMS_ARRAYS);
	verify_test_forged (JWT_TEST_HEADER ("HS256"), claims, CERVER_JWT_ERROR_NONE);

	verify_test_nested (claims, VERIFY_TEST_CLAIMS_ARRAYS + 1);
	verify_test_forged (JWT_TEST_HEADER ("HS256"), claims, CERVER_JWT_ERROR_CLAIMS);

}

// the claims are only read after the signature: a token that was not signed has no other error
static void test_verify_claims_order (void) {

	static const char *payloads[] = {
		"not the json of an object", "[]", "{\"a\":1,\"a\":2}", "{\"exp\":1}", "{\"exp\":\"text\"}", "{\"nbf\":99999999999}"
	};

	static const CerverJwtError errors[] = {
		CERVER_JWT_ERROR_CLAIMS, CERVER_JWT_ERROR_CLAIMS, CERVER_JWT_ERROR_CLAIMS,
		CERVER_JWT_ERROR_EXPIRED, CERVER_JWT_ERROR_CLAIMS, CERVER_JWT_ERROR_NOT_BEFORE
	};

	for (size_t i = 0; i < (sizeof (payloads) / sizeof (payloads[0])); i++) {
		char *token = jwt_test_forge_secret (JWT_TEST_HEADER ("HS256"), payloads[i]);

		test_check_int_eq (verify_test (verify_test_key, token, NULL), errors[i], payloads[i]);

		char *changed = verify_test_changed (token, (size_t) (strrchr (token, '.') - token) + 1);
		test_check_int_eq (verify_test (verify_test_key, changed, NULL), CERVER_JWT_ERROR_SIGNATURE, payloads[i]);
		free (changed);

		free (token);
	}

	// the header is read before the signature: a token with both wrong has the error of its header
	char *token = jwt_test_forge_secret ("{\"alg\":\"none\"}", "[]");
	char *changed = verify_test_changed (token, (size_t) (strrchr (token, '.') - token) + 1);
	test_check_int_eq (verify_test (verify_test_key, changed, NULL), CERVER_JWT_ERROR_ALGORITHM, NULL);
	free (changed);
	free (token);

}

// the validation of the call reaches the claims
static void test_verify_validation (void) {

	char *token = jwt_test_forge_secret (
		JWT_TEST_HEADER ("HS256"),
		"{\"exp\":2000,\"nbf\":1000,\"iat\":1000,\"iss\":\"issuer\",\"sub\":\"subject\",\"aud\":[\"one\",\"two\"]}"
	);

	CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
	validation.now = 1500;
	validation.require_expiration = true;
	validation.issuer = "issuer";
	validation.subject = "subject";
	validation.audience = "two";

	const CerverJsonValue *claims = NULL;
	test_check_int_eq (
		cerver_jwt_verify (verify_test_key, verify_test_arena, token, strlen (token), &validation, &claims),
		CERVER_JWT_ERROR_NONE, NULL
	);

	test_check_unsigned_eq (cerver_json_object_size (claims), 6, NULL);
	test_check_int_eq (cerver_json_integer_value (cerver_json_object_get (claims, "exp")), 2000, NULL);
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_object_get (claims, "aud")), 2, NULL);

	validation.now = 2000;
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_EXPIRED, NULL);

	validation.now = 999;
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_NOT_BEFORE, NULL);

	validation.leeway = 1;
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_NONE, NULL);

	validation.now = 1500;
	validation.issuer = "other";
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_ISSUER, NULL);

	validation.issuer = NULL;
	validation.subject = "other";
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_SUBJECT, NULL);

	validation.subject = NULL;
	validation.audience = "three";
	test_check_int_eq (verify_test (verify_test_key, token, &validation), CERVER_JWT_ERROR_AUDIENCE, NULL);

	// with the clock the token expired
	test_check_int_eq (verify_test (verify_test_key, token, NULL), CERVER_JWT_ERROR_EXPIRED, NULL);

	free (token);

}

#pragma endregion

#pragma region threads

// signs and verifies tokens with the key of the tests, the same one that the other threads use
static void *verify_test_thread (void *argument) {

	const size_t thread = *(const size_t *) argument;

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (size_t round = 0; round < VERIFY_TEST_THREAD_ROUNDS; round++) {
		char claims[VERIFY_TEST_TEXT_SIZE] = { 0 };
		(void) snprintf (claims, VERIFY_TEST_TEXT_SIZE, "{\"thread\":%zu,\"round\":%zu}", thread, round);

		char token[JWT_TEST_TOKEN_SIZE] = { 0 };
		size_t length = 0;
		test_check_int_eq (
			cerver_jwt_sign (verify_test_key, claims, strlen (claims), token, sizeof (token), &length),
			CERVER_JWT_ERROR_NONE, NULL
		);

		const CerverJsonValue *verified = NULL;
		test_check_int_eq (
			cerver_jwt_verify (verify_test_key, arena, token, length, NULL, &verified), CERVER_JWT_ERROR_NONE, NULL
		);

		test_check_int_eq (cerver_json_integer_value (cerver_json_object_get (verified, "thread")), (i64) thread, NULL);
		test_check_int_eq (cerver_json_integer_value (cerver_json_object_get (verified, "round")), (i64) round, NULL);

		token[length - 2] = (token[length - 2] == 'A') ? 'B' : 'A';
		test_check_int_eq (
			cerver_jwt_verify (verify_test_key, arena, token, length, NULL, NULL), CERVER_JWT_ERROR_SIGNATURE, NULL
		);
	}

	arena_delete (arena);

	return NULL;

}

static void test_verify_threads (void) {

	pthread_t threads[VERIFY_TEST_THREADS];
	size_t numbers[VERIFY_TEST_THREADS] = { 0 };

	for (size_t i = 0; i < VERIFY_TEST_THREADS; i++) {
		numbers[i] = i;
		test_check_int_eq (pthread_create (&threads[i], NULL, verify_test_thread, &numbers[i]), 0, NULL);
	}

	for (size_t i = 0; i < VERIFY_TEST_THREADS; i++) {
		test_check_int_eq (pthread_join (threads[i], NULL), 0, NULL);
	}

}

#pragma endregion

void jwt_tests_verify (void) {

	(void) printf ("Testing JWT verify...\n");

	verify_test_arena = arena_create (0);
	test_check_ptr (verify_test_arena);

	verify_test_key = jwt_test_key_secret (CERVER_JWT_ALGORITHM_HS256);

	test_run (test_verify_examples);
	test_run (test_verify_examples_payload);
	test_run (test_verify_arguments);
	test_run (test_verify_size);
	test_run (test_verify_parts);
	test_run (test_verify_header);
	test_run (test_verify_header_known);
	test_run (test_verify_header_algorithm);
	test_run (test_verify_header_size);
	test_run (test_verify_canonical);
	test_run (test_verify_header_repeated);
	test_run (test_verify_confusion);
	test_run (test_verify_none);
	test_run (test_verify_signature);
	test_run (test_verify_signature_keys);
	test_run (test_verify_claims);
	test_run (test_verify_claims_order);
	test_run (test_verify_validation);
	test_run (test_verify_threads);

	cerver_jwt_key_delete (verify_test_key);
	verify_test_key = NULL;

	arena_delete (verify_test_arena);
	verify_test_arena = NULL;

	(void) printf ("Done!\n");

}
