#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>

#include "../test.h"
#include "../crypto/crypto.h"
#include "../crypto/vectors.h"
#include "jwt.h"

// a value that is in none of the tables
#define JWT_TEST_OUT_OF_TABLE				99

// the least bytes of the secret of each HMAC, the ones of its digest
#define JWT_TEST_SHA256_SIZE				32
#define JWT_TEST_SHA384_SIZE				48
#define JWT_TEST_SHA512_SIZE				64

// what is not a key
#define JWT_TEST_KEYS_TEXT					"this is not the PEM nor the JWK of a key"

// a secret with the start of a PEM in its middle, and one that only has a part of it
#define JWT_TEST_SECRET_WITH_PEM			"0123456789abcdef0123456789abcdef-----BEGIN0123456789abcdef"
#define JWT_TEST_SECRET_ALMOST_PEM			"0123456789abcdef0123456789abcdef-----BEGI0123456789abcdef-----"

// the JWK of a secret of three bytes
#define JWT_TEST_SHORT_JWK					"{\"kty\":\"oct\",\"k\":\"AQID\"}"

// the keys of the provider that the algorithms are tried with
typedef enum JwtTestKey {

	JWT_TEST_KEY_SECRET			= 0,
	JWT_TEST_KEY_RSA			= 1,
	JWT_TEST_KEY_WEAK			= 2,
	JWT_TEST_KEY_P256			= 3,
	JWT_TEST_KEY_P384			= 4,
	JWT_TEST_KEY_P521			= 5,
	JWT_TEST_KEY_ED25519		= 6,
	JWT_TEST_KEY_ED448			= 7,

	JWT_TEST_KEYS				= 8

} JwtTestKey;

// the PEM of each one, the secret has none
static const char *jwt_test_key_names[JWT_TEST_KEYS] = {
	NULL,
	CRYPTO_TEST_KEY_RSA,
	CRYPTO_TEST_KEY_WEAK,
	CRYPTO_TEST_KEY_P256,
	CRYPTO_TEST_KEY_P384,
	CRYPTO_TEST_KEY_P521,
	CRYPTO_TEST_KEY_ED25519,
	CRYPTO_TEST_KEY_ED448
};

// an algorithm and the keys of the tests that it takes
typedef struct JwtTestRule {

	CerverJwtAlgorithm algorithm;
	bool takes[JWT_TEST_KEYS];

} JwtTestRule;

static const JwtTestRule jwt_test_rules[] = {
	{ CERVER_JWT_ALGORITHM_HS256, { true, false, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_HS384, { true, false, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_HS512, { true, false, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_RS256, { false, true, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_RS384, { false, true, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_RS512, { false, true, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_ES256, { false, false, false, true, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_ES384, { false, false, false, false, true, false, false, false } },
	{ CERVER_JWT_ALGORITHM_ES512, { false, false, false, false, false, true, false, false } },
	{ CERVER_JWT_ALGORITHM_PS256, { false, true, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_PS384, { false, true, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_PS512, { false, true, false, false, false, false, false, false } },
	{ CERVER_JWT_ALGORITHM_EDDSA, { false, false, false, false, false, false, true, true } }
};

#define JWT_TEST_RULES						(sizeof (jwt_test_rules) / sizeof (jwt_test_rules[0]))

// a new key of the provider: the key of the tokens takes it
static CerverCryptoKey *jwt_test_crypto_key (const JwtTestKey key, const bool is_private) {

	if (key == JWT_TEST_KEY_SECRET) {
		CerverCryptoKey *secret = cerver_crypto_key_create_secret (JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), NULL);
		test_check_ptr (secret);

		return secret;
	}

	return jwt_test_crypto_pem (jwt_test_key_names[key], is_private);

}

// an algorithm with a key: the key of its tokens only when the algorithm takes the key
static void jwt_test_rule_check (
	const JwtTestRule *rule, const JwtTestKey key, const bool is_private
) {

	const char *name = cerver_jwt_algorithm_to_string (rule->algorithm);

	CerverJwtError error = CERVER_JWT_ERROR_MEMORY;
	CerverJwtKey *jwt_key = cerver_jwt_key_create (rule->algorithm, jwt_test_crypto_key (key, is_private), &error);

	if (rule->takes[key]) {
		test_check (jwt_key != NULL, name);
		test_check_int_eq (error, CERVER_JWT_ERROR_NONE, name);

		test_check_int_eq (cerver_jwt_key_algorithm (jwt_key), rule->algorithm, name);
		test_check_bool_eq (cerver_jwt_key_is_private (jwt_key), is_private || (key == JWT_TEST_KEY_SECRET), name);
		test_check (cerver_jwt_key_crypto (jwt_key) != NULL, name);
	}

	else {
		test_check (jwt_key == NULL, name);
		test_check_int_eq (error, CERVER_JWT_ERROR_KEY, name);
	}

	cerver_jwt_key_delete (jwt_key);

}

// every algorithm with every key, the private one and the public one
static void test_key_rules (void) {

	for (size_t i = 0; i < JWT_TEST_RULES; i++) {
		for (size_t key = 0; key < JWT_TEST_KEYS; key++) {
			jwt_test_rule_check (&jwt_test_rules[i], (JwtTestKey) key, true);
			jwt_test_rule_check (&jwt_test_rules[i], (JwtTestKey) key, false);
		}
	}

}

// what is not an algorithm has no key, and the key of the provider that was given is released
static void test_key_algorithms (void) {

	static const int not_algorithms[] = {
		CERVER_JWT_ALGORITHM_NONE, CERVER_JWT_ALGORITHMS, JWT_TEST_OUT_OF_TABLE, -1
	};

	for (size_t i = 0; i < (sizeof (not_algorithms) / sizeof (not_algorithms[0])); i++) {
		const CerverJwtAlgorithm algorithm = (CerverJwtAlgorithm) not_algorithms[i];

		for (size_t key = 0; key < JWT_TEST_KEYS; key++) {
			CerverJwtError error = CERVER_JWT_ERROR_NONE;
			test_check_null_ptr (
				cerver_jwt_key_create (algorithm, jwt_test_crypto_key ((JwtTestKey) key, true), &error)
			);
			test_check_int_eq (error, CERVER_JWT_ERROR_ALGORITHM, NULL);
		}

		CerverJwtError error = CERVER_JWT_ERROR_NONE;
		test_check_null_ptr (
			cerver_jwt_key_create_secret (algorithm, JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), &error)
		);
		test_check_int_eq (error, CERVER_JWT_ERROR_ALGORITHM, NULL);

		error = CERVER_JWT_ERROR_NONE;
		test_check_null_ptr (jwt_test_key_load (algorithm, CRYPTO_TEST_KEY_RSA, true, &error));
		test_check_int_eq (error, CERVER_JWT_ERROR_ALGORITHM, NULL);

		error = CERVER_JWT_ERROR_NONE;
		test_check_null_ptr (
			cerver_jwt_key_create_jwk (algorithm, CRYPTO_VECTOR_SECRET_JWK, strlen (CRYPTO_VECTOR_SECRET_JWK), &error)
		);
		test_check_int_eq (error, CERVER_JWT_ERROR_ALGORITHM, NULL);
	}

}

static void test_key_arguments (void) {

	CerverJwtError error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create (CERVER_JWT_ALGORITHM_HS256, NULL, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	// without a key there is nothing to try the algorithm with
	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create (CERVER_JWT_ALGORITHM_NONE, NULL, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS256, NULL, strlen (JWT_TEST_SECRET), &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SECRET, 0, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_pem (CERVER_JWT_ALGORITHM_RS256, NULL, strlen (JWT_TEST_KEYS_TEXT), true, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_pem (CERVER_JWT_ALGORITHM_RS256, JWT_TEST_KEYS_TEXT, 0, false, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_jwk (CERVER_JWT_ALGORITHM_HS256, NULL, strlen (JWT_TEST_KEYS_TEXT), &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_jwk (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_KEYS_TEXT, 0, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	// the error is optional, when the key is created and when it is not
	test_check_null_ptr (cerver_jwt_key_create (CERVER_JWT_ALGORITHM_HS256, NULL, NULL));
	test_check_null_ptr (cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_NONE, JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), NULL));
	test_check_null_ptr (cerver_jwt_key_create_pem (CERVER_JWT_ALGORITHM_RS256, JWT_TEST_KEYS_TEXT, strlen (JWT_TEST_KEYS_TEXT), true, NULL));
	test_check_null_ptr (cerver_jwt_key_create_jwk (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_KEYS_TEXT, strlen (JWT_TEST_KEYS_TEXT), NULL));

	CerverJwtKey *key = cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), NULL);
	test_check_ptr (key);
	cerver_jwt_key_delete (key);

	// a NULL key is no key
	test_check_int_eq (cerver_jwt_key_algorithm (NULL), CERVER_JWT_ALGORITHM_NONE, NULL);
	test_check_false (cerver_jwt_key_is_private (NULL));
	test_check_null_ptr (cerver_jwt_key_crypto (NULL));
	cerver_jwt_key_delete (NULL);

}

// a secret of some bytes with an algorithm
static void jwt_test_secret_check (
	const CerverJwtAlgorithm algorithm, const size_t size, const CerverJwtError expected
) {

	CerverJwtError error = CERVER_JWT_ERROR_MEMORY;
	CerverJwtKey *key = cerver_jwt_key_create_secret (algorithm, JWT_TEST_SECRET, size, &error);

	test_check_int_eq (error, expected, cerver_jwt_algorithm_to_string (algorithm));
	test_check_bool_eq (key != NULL, expected == CERVER_JWT_ERROR_NONE, cerver_jwt_algorithm_to_string (algorithm));

	if (key) {
		test_check_int_eq (cerver_jwt_key_algorithm (key), algorithm, NULL);
		test_check_true (cerver_jwt_key_is_private (key));
		test_check_int_eq (cerver_crypto_key_type (cerver_jwt_key_crypto (key)), CERVER_CRYPTO_KEY_TYPE_SECRET, NULL);
	}

	cerver_jwt_key_delete (key);

}

// the secret of an HMAC has at least the bytes of its digest
static void test_key_secret (void) {

	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SHA256_SIZE, CERVER_JWT_ERROR_NONE);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SHA256_SIZE - 1, CERVER_JWT_ERROR_KEY);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS256, sizeof (char), CERVER_JWT_ERROR_KEY);

	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS384, JWT_TEST_SHA384_SIZE, CERVER_JWT_ERROR_NONE);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS384, JWT_TEST_SHA384_SIZE - 1, CERVER_JWT_ERROR_KEY);

	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS512, JWT_TEST_SHA512_SIZE, CERVER_JWT_ERROR_NONE);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_HS512, JWT_TEST_SHA512_SIZE - 1, CERVER_JWT_ERROR_KEY);

	// a secret is not the key of the other algorithms
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_RS256, JWT_TEST_SHA512_SIZE, CERVER_JWT_ERROR_KEY);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_PS512, JWT_TEST_SHA512_SIZE, CERVER_JWT_ERROR_KEY);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_ES256, JWT_TEST_SHA512_SIZE, CERVER_JWT_ERROR_KEY);
	jwt_test_secret_check (CERVER_JWT_ALGORITHM_EDDSA, JWT_TEST_SHA512_SIZE, CERVER_JWT_ERROR_KEY);

	// a secret with more bytes than the largest one
	u8 *large = (u8 *) calloc (CERVER_CRYPTO_SECRET_SIZE_MAX + 1, sizeof (u8));
	test_check_ptr (large);

	CerverJwtError error = CERVER_JWT_ERROR_NONE;
	CerverJwtKey *key = cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS512, large, CERVER_CRYPTO_SECRET_SIZE_MAX, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, NULL);
	cerver_jwt_key_delete (key);

	test_check_null_ptr (
		cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS512, large, CERVER_CRYPTO_SECRET_SIZE_MAX + 1, &error)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_ARGUMENTS, NULL);

	free (large);

}

// the text of a PEM is never the secret of an HMAC: the public key of a cerver is known by everyone
static void test_key_secret_pem (void) {

	size_t size = 0;
	char *pem = jwt_test_file_read (CRYPTO_TEST_KEY_RSA CRYPTO_TEST_KEY_PUBLIC_EXTENSION, &size);

	CerverJwtError error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS256, pem, size, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS512, pem, size, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	free (pem);

	pem = jwt_test_file_read (CRYPTO_TEST_KEY_P256 CRYPTO_TEST_KEY_PRIVATE_EXTENSION, &size);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS256, pem, size, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	free (pem);

	// the start of a PEM anywhere in the secret, also as its last bytes
	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_secret (
			CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SECRET_WITH_PEM, strlen (JWT_TEST_SECRET_WITH_PEM), &error
		)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_secret (
			CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SECRET_WITH_PEM, strlen ("0123456789abcdef0123456789abcdef-----BEGIN"), &error
		)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	// without its last character it is a secret
	CerverJwtKey *key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SECRET_WITH_PEM, strlen ("0123456789abcdef0123456789abcdef-----BEGI"), &error
	);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, NULL);
	cerver_jwt_key_delete (key);

	key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SECRET_ALMOST_PEM, strlen (JWT_TEST_SECRET_ALMOST_PEM), &error
	);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, NULL);
	cerver_jwt_key_delete (key);

}

// a PEM that is not the key of the algorithm
static void jwt_test_pem_fails (
	const CerverJwtAlgorithm algorithm, const char *name, const bool is_private
) {

	CerverJwtError error = CERVER_JWT_ERROR_NONE;
	test_check (jwt_test_key_load (algorithm, name, is_private, &error) == NULL, name);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, name);

}

static void test_key_pem (void) {

	CerverJwtKey *key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_PS384, CRYPTO_TEST_KEY_RSA, true);
	test_check_int_eq (cerver_jwt_key_algorithm (key), CERVER_JWT_ALGORITHM_PS384, NULL);
	test_check_true (cerver_jwt_key_is_private (key));
	test_check_int_eq (cerver_crypto_key_type (cerver_jwt_key_crypto (key)), CERVER_CRYPTO_KEY_TYPE_RSA, NULL);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_ES384, CRYPTO_TEST_KEY_P384, false);
	test_check_int_eq (cerver_jwt_key_algorithm (key), CERVER_JWT_ALGORITHM_ES384, NULL);
	test_check_false (cerver_jwt_key_is_private (key));
	cerver_jwt_key_delete (key);

	// the PEM of PKCS #1 and the one of SEC 1
	key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_TRADITIONAL, true);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_pem (CERVER_JWT_ALGORITHM_ES256, CRYPTO_TEST_KEY_SEC1, true);
	cerver_jwt_key_delete (key);

	// a text that is not a PEM
	CerverJwtError error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_pem (CERVER_JWT_ALGORITHM_RS256, JWT_TEST_KEYS_TEXT, strlen (JWT_TEST_KEYS_TEXT), true, &error)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	// a PEM with a passphrase, and a public PEM that is read as a private one
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_ENCRYPTED, true);

	size_t size = 0;
	char *pem = jwt_test_file_read (CRYPTO_TEST_KEY_RSA CRYPTO_TEST_KEY_PUBLIC_EXTENSION, &size);
	test_check_null_ptr (cerver_jwt_key_create_pem (CERVER_JWT_ALGORITHM_RS256, pem, size, true, &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);
	free (pem);

	// the keys that cerver does not use, and the ones that their algorithm does not take
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_ES256, CRYPTO_TEST_KEY_SECP256K1, true);
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_EDDSA, CRYPTO_TEST_KEY_X25519, false);
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_RS256, CRYPTO_TEST_KEY_WEAK, true);
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_PS256, CRYPTO_TEST_KEY_WEAK, false);
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_ES256, CRYPTO_TEST_KEY_P384, false);
	jwt_test_pem_fails (CERVER_JWT_ALGORITHM_HS256, CRYPTO_TEST_KEY_RSA, false);

}

static void test_key_jwk (void) {

	CerverJwtKey *key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_HS256, CRYPTO_VECTOR_SECRET_JWK);
	test_check_int_eq (cerver_jwt_key_algorithm (key), CERVER_JWT_ALGORITHM_HS256, NULL);
	test_check_true (cerver_jwt_key_is_private (key));
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_RS512, CRYPTO_VECTOR_RSA_PUBLIC_JWK);
	test_check_int_eq (cerver_jwt_key_algorithm (key), CERVER_JWT_ALGORITHM_RS512, NULL);
	test_check_false (cerver_jwt_key_is_private (key));
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_PS256, CRYPTO_VECTOR_RSA_PRIVATE_JWK);
	test_check_true (cerver_jwt_key_is_private (key));
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_ES256, CRYPTO_VECTOR_P256_PRIVATE_JWK);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_ES512, CRYPTO_VECTOR_P521_PUBLIC_JWK);
	cerver_jwt_key_delete (key);

	key = jwt_test_key_jwk (CERVER_JWT_ALGORITHM_EDDSA, CRYPTO_VECTOR_ED25519_PRIVATE_JWK);
	test_check_int_eq (cerver_crypto_key_type (cerver_jwt_key_crypto (key)), CERVER_CRYPTO_KEY_TYPE_ED25519, NULL);
	cerver_jwt_key_delete (key);

	// the algorithm is the one of the call: the "alg" of the JWK is not read
	key = jwt_test_key_jwk (
		CERVER_JWT_ALGORITHM_PS384, "{\"alg\":\"RS256\",\"use\":\"sig\"," CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS "}"
	);
	test_check_int_eq (cerver_jwt_key_algorithm (key), CERVER_JWT_ALGORITHM_PS384, NULL);
	cerver_jwt_key_delete (key);

	// a JWK that was made to encrypt is not the key of a token
	CerverJwtError error = CERVER_JWT_ERROR_NONE;
	const char *encryption = { "{\"use\":\"enc\"," CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS "}" };
	test_check_null_ptr (cerver_jwt_key_create_jwk (CERVER_JWT_ALGORITHM_RS256, encryption, strlen (encryption), &error));
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	// a text that is not a JWK, and the JWK of a key that the algorithm does not take
	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_jwk (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_KEYS_TEXT, strlen (JWT_TEST_KEYS_TEXT), &error)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_jwk (CERVER_JWT_ALGORITHM_HS256, JWT_TEST_SHORT_JWK, strlen (JWT_TEST_SHORT_JWK), &error)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_jwk (
			CERVER_JWT_ALGORITHM_ES384, CRYPTO_VECTOR_P256_PUBLIC_JWK, strlen (CRYPTO_VECTOR_P256_PUBLIC_JWK), &error
		)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

	error = CERVER_JWT_ERROR_NONE;
	test_check_null_ptr (
		cerver_jwt_key_create_jwk (
			CERVER_JWT_ALGORITHM_HS256, CRYPTO_VECTOR_RSA_PUBLIC_JWK, strlen (CRYPTO_VECTOR_RSA_PUBLIC_JWK), &error
		)
	);
	test_check_int_eq (error, CERVER_JWT_ERROR_KEY, NULL);

}

void jwt_tests_keys (void) {

	(void) printf ("Testing JWT keys...\n");

	test_run (test_key_rules);
	test_run (test_key_algorithms);
	test_run (test_key_arguments);
	test_run (test_key_secret);
	test_run (test_key_secret_pem);
	test_run (test_key_pem);
	test_run (test_key_jwk);

	(void) printf ("Done!\n");

}
