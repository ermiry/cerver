#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/sign.h>
#include <cerver/jwt/verify.h>

#include <cerver/utils/base64.h>

#include "../test.h"
#include "../crypto/crypto.h"
#include "jwt.h"

// the claims that the keys of the tests sign
#define JWT_TEST_CLAIMS						"{\"id\":\"sign\",\"value\":7}"

// a token of the HTTP of the 2.x, with the JWT that this one replaces: its secret, its claims and its bytes
#define JWT_TEST_KNOWN_SECRET				"0123456789abcdef0123456789abcdef"
#define JWT_TEST_KNOWN_CLAIMS				"{\"id\":\"bench\"}"
#define JWT_TEST_KNOWN_TOKEN				\
	"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpZCI6ImJlbmNoIn0.3TAmk5kUhCB7ekKomaB0Lj3TleEkd8jvyTzPU72RMT8"

// the byte that fills a buffer that must not be written
#define JWT_TEST_MARK						0x5A

// the characters of the header of every algorithm in base64url
#define JWT_TEST_HEADER_LENGTH				36

// the separators of a token
#define JWT_TEST_SEPARATORS					2

// the first size of some claims whose characters in base64url, four for every three bytes, wrap around a size
#define JWT_TEST_WRAPPING_SIZE				((((size_t) -1 / 4) + 1) * 3)

// an algorithm with the key of the tests that signs its tokens
typedef struct JwtTestSigner {

	CerverJwtAlgorithm algorithm;
	const char *header;

	// the PEM of its key, NULL for the secret
	const char *key;

	// the bytes of its signatures
	size_t signature_size;

	// the same claims have the same token
	bool deterministic;

} JwtTestSigner;

static const JwtTestSigner jwt_test_signers[] = {
	{ CERVER_JWT_ALGORITHM_HS256, JWT_TEST_HEADER ("HS256"), NULL, 32, true },
	{ CERVER_JWT_ALGORITHM_HS384, JWT_TEST_HEADER ("HS384"), NULL, 48, true },
	{ CERVER_JWT_ALGORITHM_HS512, JWT_TEST_HEADER ("HS512"), NULL, 64, true },
	{ CERVER_JWT_ALGORITHM_RS256, JWT_TEST_HEADER ("RS256"), CRYPTO_TEST_KEY_RSA, 256, true },
	{ CERVER_JWT_ALGORITHM_RS384, JWT_TEST_HEADER ("RS384"), CRYPTO_TEST_KEY_RSA, 256, true },
	{ CERVER_JWT_ALGORITHM_RS512, JWT_TEST_HEADER ("RS512"), CRYPTO_TEST_KEY_RSA, 256, true },
	{ CERVER_JWT_ALGORITHM_ES256, JWT_TEST_HEADER ("ES256"), CRYPTO_TEST_KEY_P256, 64, false },
	{ CERVER_JWT_ALGORITHM_ES384, JWT_TEST_HEADER ("ES384"), CRYPTO_TEST_KEY_P384, 96, false },
	{ CERVER_JWT_ALGORITHM_ES512, JWT_TEST_HEADER ("ES512"), CRYPTO_TEST_KEY_P521, 132, false },
	{ CERVER_JWT_ALGORITHM_PS256, JWT_TEST_HEADER ("PS256"), CRYPTO_TEST_KEY_RSA, 256, false },
	{ CERVER_JWT_ALGORITHM_PS384, JWT_TEST_HEADER ("PS384"), CRYPTO_TEST_KEY_RSA, 256, false },
	{ CERVER_JWT_ALGORITHM_PS512, JWT_TEST_HEADER ("PS512"), CRYPTO_TEST_KEY_RSA, 256, false },
	{ CERVER_JWT_ALGORITHM_EDDSA, JWT_TEST_HEADER ("EdDSA"), CRYPTO_TEST_KEY_ED25519, 64, true },
	{ CERVER_JWT_ALGORITHM_EDDSA, JWT_TEST_HEADER ("EdDSA"), CRYPTO_TEST_KEY_ED448, 114, true }
};

#define JWT_TEST_SIGNERS					(sizeof (jwt_test_signers) / sizeof (jwt_test_signers[0]))

static CerverJwtKey *jwt_test_signer_key (const JwtTestSigner *signer, const bool is_private) {

	return signer->key ?
		jwt_test_key_pem (signer->algorithm, signer->key, is_private) :
		jwt_test_key_secret (signer->algorithm);

}

// the characters of the token of some claims with a signer
static size_t jwt_test_signer_length (const JwtTestSigner *signer, const size_t claims_size) {

	return JWT_TEST_HEADER_LENGTH
		+ base64url_encode_length (claims_size)
		+ base64url_encode_length (signer->signature_size)
		+ JWT_TEST_SEPARATORS;

}

// the token of the HTTP of the 2.x, byte by byte: the header of the algorithm, the claims as they were
// given and the HMAC
static void test_sign_known (void) {

	CerverJwtKey *key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, JWT_TEST_KNOWN_SECRET, strlen (JWT_TEST_KNOWN_SECRET), NULL
	);

	test_check_ptr (key);

	char token[JWT_TEST_TOKEN_SIZE] = { 0 };
	size_t length = 0;
	test_check_int_eq (
		cerver_jwt_sign (key, JWT_TEST_KNOWN_CLAIMS, strlen (JWT_TEST_KNOWN_CLAIMS), token, sizeof (token), &length),
		CERVER_JWT_ERROR_NONE, NULL
	);

	test_check_str_eq (token, JWT_TEST_KNOWN_TOKEN, NULL);
	test_check_unsigned_eq (length, strlen (JWT_TEST_KNOWN_TOKEN), NULL);

	test_check_unsigned_eq (
		cerver_jwt_sign_size (key, strlen (JWT_TEST_KNOWN_CLAIMS)), sizeof (JWT_TEST_KNOWN_TOKEN), NULL
	);

	cerver_jwt_key_delete (key);

}

// the first part of a token is the header that cerver writes for its algorithm
static void jwt_test_header_check (const JwtTestSigner *signer, const char *token) {

	const char *separator = strchr (token, '.');
	test_check (separator != NULL, signer->header);
	test_check_unsigned_eq ((size_t) (separator - token), JWT_TEST_HEADER_LENGTH, signer->header);

	unsigned char header[JWT_TEST_HEADER_LENGTH] = { 0 };
	size_t header_size = 0;
	test_check_unsigned_eq (
		base64url_decode (header, &header_size, token, JWT_TEST_HEADER_LENGTH), 0, signer->header
	);

	test_check_unsigned_eq (header_size, strlen (signer->header), signer->header);
	test_check (!memcmp (header, signer->header, header_size), signer->header);

}

// a signer signs, and its public key takes the token and gives its claims
static void jwt_test_signer_check (const JwtTestSigner *signer) {

	const size_t claims_size = strlen (JWT_TEST_CLAIMS);

	CerverJwtKey *private_key = jwt_test_signer_key (signer, true);
	CerverJwtKey *public_key = jwt_test_signer_key (signer, false);

	const size_t expected = jwt_test_signer_length (signer, claims_size);
	test_check_unsigned_eq (cerver_jwt_sign_size (private_key, claims_size), expected + 1, signer->header);
	test_check_unsigned_eq (cerver_jwt_sign_size (public_key, claims_size), expected + 1, signer->header);

	char token[JWT_TEST_TOKEN_SIZE] = { 0 };
	size_t length = 0;
	test_check_int_eq (
		cerver_jwt_sign (private_key, JWT_TEST_CLAIMS, claims_size, token, sizeof (token), &length),
		CERVER_JWT_ERROR_NONE, signer->header
	);

	test_check_unsigned_eq (length, expected, signer->header);
	test_check_unsigned_eq (strlen (token), expected, signer->header);

	jwt_test_header_check (signer, token);

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	const CerverJsonValue *claims = NULL;
	test_check_int_eq (
		cerver_jwt_verify (public_key, arena, token, length, NULL, &claims), CERVER_JWT_ERROR_NONE, signer->header
	);

	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (claims, "id")), "sign", signer->header);
	test_check_int_eq (cerver_json_integer_value (cerver_json_object_get (claims, "value")), 7, signer->header);

	// the key that signed also takes it
	test_check_int_eq (
		cerver_jwt_verify (private_key, arena, token, length, NULL, NULL), CERVER_JWT_ERROR_NONE, signer->header
	);

	// the same claims again: the same token only when the algorithm has no random part
	char again[JWT_TEST_TOKEN_SIZE] = { 0 };
	size_t again_length = 0;
	test_check_int_eq (
		cerver_jwt_sign (private_key, JWT_TEST_CLAIMS, claims_size, again, sizeof (again), &again_length),
		CERVER_JWT_ERROR_NONE, signer->header
	);

	test_check_unsigned_eq (again_length, length, signer->header);
	test_check_bool_eq (!strcmp (token, again), signer->deterministic, signer->header);
	test_check_int_eq (
		cerver_jwt_verify (public_key, arena, again, again_length, NULL, NULL), CERVER_JWT_ERROR_NONE, signer->header
	);

	arena_delete (arena);

	cerver_jwt_key_delete (private_key);
	cerver_jwt_key_delete (public_key);

}

static void test_sign_algorithms (void) {

	for (size_t i = 0; i < JWT_TEST_SIGNERS; i++) {
		jwt_test_signer_check (&jwt_test_signers[i]);
	}

}

// a sign that fails writes no token and leaves no length
static void jwt_test_sign_fails (
	const CerverJwtKey *key, const char *claims, const size_t claims_size,
	const size_t capacity, const CerverJwtError expected
) {

	char token[JWT_TEST_TOKEN_SIZE];
	(void) memset (token, JWT_TEST_MARK, sizeof (token));

	size_t length = JWT_TEST_TOKEN_SIZE;
	test_check_int_eq (cerver_jwt_sign (key, claims, claims_size, token, capacity, &length), expected, NULL);
	test_check_unsigned_eq (length, 0, NULL);

	for (size_t i = 0; i < sizeof (token); i++) {
		test_check_unsigned_eq ((unsigned char) token[i], JWT_TEST_MARK, "a sign that failed wrote its buffer");
	}

}

static void test_sign_errors (void) {

	const size_t claims_size = strlen (JWT_TEST_CLAIMS);

	for (size_t i = 0; i < JWT_TEST_SIGNERS; i++) {
		const JwtTestSigner *signer = &jwt_test_signers[i];

		CerverJwtKey *key = jwt_test_signer_key (signer, true);
		const size_t needed = cerver_jwt_sign_size (key, claims_size);

		// a buffer without the byte of the end of the token, and one without bytes
		jwt_test_sign_fails (key, JWT_TEST_CLAIMS, claims_size, needed - 1, CERVER_JWT_ERROR_SIZE);
		jwt_test_sign_fails (key, JWT_TEST_CLAIMS, claims_size, 0, CERVER_JWT_ERROR_SIZE);

		// a buffer with exactly the bytes of the token: the byte after it is not written
		char token[JWT_TEST_TOKEN_SIZE];
		(void) memset (token, JWT_TEST_MARK, sizeof (token));

		size_t length = 0;
		test_check_int_eq (
			cerver_jwt_sign (key, JWT_TEST_CLAIMS, claims_size, token, needed, &length), CERVER_JWT_ERROR_NONE, signer->header
		);
		test_check_unsigned_eq (length, needed - 1, signer->header);
		test_check_unsigned_eq ((unsigned char) token[length], 0, signer->header);
		test_check_unsigned_eq ((unsigned char) token[needed], JWT_TEST_MARK, signer->header);

		// claims without bytes, and the arguments that are missing
		jwt_test_sign_fails (key, JWT_TEST_CLAIMS, 0, JWT_TEST_TOKEN_SIZE, CERVER_JWT_ERROR_ARGUMENTS);
		jwt_test_sign_fails (key, NULL, claims_size, JWT_TEST_TOKEN_SIZE, CERVER_JWT_ERROR_ARGUMENTS);
		jwt_test_sign_fails (NULL, JWT_TEST_CLAIMS, claims_size, JWT_TEST_TOKEN_SIZE, CERVER_JWT_ERROR_ARGUMENTS);

		length = JWT_TEST_TOKEN_SIZE;
		test_check_int_eq (
			cerver_jwt_sign (key, JWT_TEST_CLAIMS, claims_size, NULL, JWT_TEST_TOKEN_SIZE, &length),
			CERVER_JWT_ERROR_ARGUMENTS, signer->header
		);
		test_check_unsigned_eq (length, 0, signer->header);

		test_check_int_eq (
			cerver_jwt_sign (key, JWT_TEST_CLAIMS, claims_size, token, JWT_TEST_TOKEN_SIZE, NULL),
			CERVER_JWT_ERROR_ARGUMENTS, signer->header
		);

		cerver_jwt_key_delete (key);

		// a public key never signs
		if (signer->key) {
			key = jwt_test_signer_key (signer, false);
			jwt_test_sign_fails (key, JWT_TEST_CLAIMS, claims_size, JWT_TEST_TOKEN_SIZE, CERVER_JWT_ERROR_KEY);
			cerver_jwt_key_delete (key);
		}
	}

}

// the claims whose token would have more characters than the largest one are not signed
static void test_sign_size (void) {

	CerverJwtKey *key = jwt_test_key_secret (CERVER_JWT_ALGORITHM_HS256);

	test_check_unsigned_eq (cerver_jwt_sign_size (NULL, strlen (JWT_TEST_CLAIMS)), 0, NULL);

	// the claims of the largest token: its characters without the header, the signature and the separators
	const size_t fixed = JWT_TEST_HEADER_LENGTH + base64url_encode_length (32) + JWT_TEST_SEPARATORS;
	const size_t largest = base64url_decode_length (CERVER_JWT_SIZE_MAX - fixed);

	test_check_unsigned_eq (
		JWT_TEST_HEADER_LENGTH + base64url_encode_length (largest) + base64url_encode_length (32) + JWT_TEST_SEPARATORS,
		CERVER_JWT_SIZE_MAX, "the claims of the test do not make the largest token"
	);

	test_check_unsigned_eq (cerver_jwt_sign_size (key, largest), CERVER_JWT_SIZE_MAX + 1, NULL);
	test_check_unsigned_eq (cerver_jwt_sign_size (key, largest + 1), 0, NULL);
	test_check_unsigned_eq (cerver_jwt_sign_size (key, CERVER_JWT_SIZE_MAX), 0, NULL);
	test_check_unsigned_eq (cerver_jwt_sign_size (key, CERVER_JWT_SIZE_MAX + 1), 0, NULL);
	test_check_unsigned_eq (cerver_jwt_sign_size (key, (size_t) -1), 0, NULL);

	// they are refused by their own size: the characters of these do not fit in a size, and they would be few
	test_check_unsigned_eq (cerver_jwt_sign_size (key, JWT_TEST_WRAPPING_SIZE), 0, NULL);

	char *claims = (char *) malloc (largest + 1);
	char *token = (char *) malloc (CERVER_JWT_SIZE_MAX + 1);
	test_check_ptr (claims);
	test_check_ptr (token);

	// the largest claims: a string in an object
	(void) memset (claims, 'a', largest + 1);
	(void) memcpy (claims, "{\"a\":\"", strlen ("{\"a\":\""));
	(void) memcpy (claims + largest - strlen ("\"}"), "\"}", strlen ("\"}"));

	size_t length = 0;
	test_check_int_eq (
		cerver_jwt_sign (key, claims, largest, token, CERVER_JWT_SIZE_MAX + 1, &length), CERVER_JWT_ERROR_NONE, NULL
	);
	test_check_unsigned_eq (length, CERVER_JWT_SIZE_MAX, NULL);

	// the largest token is one that is taken
	Arena *arena = arena_create (0);
	test_check_ptr (arena);
	test_check_int_eq (cerver_jwt_verify (key, arena, token, length, NULL, NULL), CERVER_JWT_ERROR_NONE, NULL);
	arena_delete (arena);

	// one byte more of claims has no token
	length = CERVER_JWT_SIZE_MAX;
	test_check_int_eq (
		cerver_jwt_sign (key, claims, largest + 1, token, CERVER_JWT_SIZE_MAX + 1, &length), CERVER_JWT_ERROR_SIZE, NULL
	);
	test_check_unsigned_eq (length, 0, NULL);

	free (claims);
	free (token);

	cerver_jwt_key_delete (key);

}

// the claims are signed as they are: the bytes of the second part are the ones that were given
static void test_sign_claims (void) {

	static const char *claims[] = {
		"{}", "{\"b\":1,\"a\":2}", "{ \"spaces\" : true }", "{\"text\":\"\\u00f1 \xc3\xb1\"}"
	};

	CerverJwtKey *key = jwt_test_key_secret (CERVER_JWT_ALGORITHM_HS256);

	for (size_t i = 0; i < (sizeof (claims) / sizeof (claims[0])); i++) {
		const size_t claims_size = strlen (claims[i]);

		char token[JWT_TEST_TOKEN_SIZE] = { 0 };
		size_t length = 0;
		test_check_int_eq (
			cerver_jwt_sign (key, claims[i], claims_size, token, sizeof (token), &length), CERVER_JWT_ERROR_NONE, claims[i]
		);

		const char *payload = token + JWT_TEST_HEADER_LENGTH + 1;
		const char *end = strchr (payload, '.');
		test_check (end != NULL, claims[i]);

		unsigned char decoded[JWT_TEST_TOKEN_SIZE] = { 0 };
		size_t decoded_size = 0;
		test_check_unsigned_eq (
			base64url_decode (decoded, &decoded_size, payload, (size_t) (end - payload)), 0, claims[i]
		);

		test_check_unsigned_eq (decoded_size, claims_size, claims[i]);
		test_check (!memcmp (decoded, claims[i], claims_size), claims[i]);
	}

	cerver_jwt_key_delete (key);

}

void jwt_tests_sign (void) {

	(void) printf ("Testing JWT sign...\n");

	test_run (test_sign_known);
	test_run (test_sign_algorithms);
	test_run (test_sign_errors);
	test_run (test_sign_size);
	test_run (test_sign_claims);

	(void) printf ("Done!\n");

}
