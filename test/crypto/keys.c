#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>

#include "../test.h"
#include "crypto.h"

// a value that is in none of the tables
#define CRYPTO_TEST_KEYS_OUT_OF_TABLE		99

#define CRYPTO_TEST_KEYS_SECRET				"the secret of the keys of the tests"

#define CRYPTO_TEST_KEYS_BYTE_BITS			8

// the bits that the provider reports
#define CRYPTO_TEST_KEYS_RSA_BITS			2048
#define CRYPTO_TEST_KEYS_WEAK_BITS			1024
#define CRYPTO_TEST_KEYS_P256_BITS			256
#define CRYPTO_TEST_KEYS_P384_BITS			384
#define CRYPTO_TEST_KEYS_P521_BITS			521
#define CRYPTO_TEST_KEYS_ED25519_BITS		256
#define CRYPTO_TEST_KEYS_ED448_BITS			456

// the first bytes of a PEM: the start of its first line
#define CRYPTO_TEST_KEYS_PEM_START			16

// what is not a PEM
#define CRYPTO_TEST_KEYS_TEXT				"this is not the PEM of a key"

// a PEM whose base64 is not a key
#define CRYPTO_TEST_KEYS_EMPTY_PEM			"-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----\n"

// one of the keys of the tests and what it is
typedef struct CryptoTestKey {

	const char *name;

	CerverCryptoKeyType type;
	unsigned int bits;
	CerverCryptoCurve curve;

} CryptoTestKey;

static const CryptoTestKey crypto_test_keys[] = {
	{ CRYPTO_TEST_KEY_RSA, CERVER_CRYPTO_KEY_TYPE_RSA, CRYPTO_TEST_KEYS_RSA_BITS, CERVER_CRYPTO_CURVE_NONE },
	{ CRYPTO_TEST_KEY_WEAK, CERVER_CRYPTO_KEY_TYPE_RSA, CRYPTO_TEST_KEYS_WEAK_BITS, CERVER_CRYPTO_CURVE_NONE },
	{ CRYPTO_TEST_KEY_P256, CERVER_CRYPTO_KEY_TYPE_EC, CRYPTO_TEST_KEYS_P256_BITS, CERVER_CRYPTO_CURVE_P256 },
	{ CRYPTO_TEST_KEY_P384, CERVER_CRYPTO_KEY_TYPE_EC, CRYPTO_TEST_KEYS_P384_BITS, CERVER_CRYPTO_CURVE_P384 },
	{ CRYPTO_TEST_KEY_P521, CERVER_CRYPTO_KEY_TYPE_EC, CRYPTO_TEST_KEYS_P521_BITS, CERVER_CRYPTO_CURVE_P521 },
	{ CRYPTO_TEST_KEY_ED25519, CERVER_CRYPTO_KEY_TYPE_ED25519, CRYPTO_TEST_KEYS_ED25519_BITS, CERVER_CRYPTO_CURVE_NONE },
	{ CRYPTO_TEST_KEY_ED448, CERVER_CRYPTO_KEY_TYPE_ED448, CRYPTO_TEST_KEYS_ED448_BITS, CERVER_CRYPTO_CURVE_NONE }
};

#define CRYPTO_TEST_KEYS					(sizeof (crypto_test_keys) / sizeof (crypto_test_keys[0]))

static void test_key_strings (void) {

	test_check_str_eq (cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_NONE), "None", NULL);
	test_check_str_eq (cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_SECRET), "Secret", NULL);
	test_check_str_eq (cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_RSA), "RSA", NULL);
	test_check_str_eq (cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_EC), "EC", NULL);
	test_check_str_eq (cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_ED25519), "Ed25519", NULL);
	test_check_str_eq (cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_ED448), "Ed448", NULL);
	test_check_str_eq (
		cerver_crypto_key_type_to_string ((CerverCryptoKeyType) CRYPTO_TEST_KEYS_OUT_OF_TABLE), "None", NULL
	);

	test_check_str_eq (cerver_crypto_curve_to_string (CERVER_CRYPTO_CURVE_NONE), "None", NULL);
	test_check_str_eq (cerver_crypto_curve_to_string (CERVER_CRYPTO_CURVE_P256), "P-256", NULL);
	test_check_str_eq (cerver_crypto_curve_to_string (CERVER_CRYPTO_CURVE_P384), "P-384", NULL);
	test_check_str_eq (cerver_crypto_curve_to_string (CERVER_CRYPTO_CURVE_P521), "P-521", NULL);
	test_check_str_eq (
		cerver_crypto_curve_to_string ((CerverCryptoCurve) CRYPTO_TEST_KEYS_OUT_OF_TABLE), "None", NULL
	);

	test_check_str_eq (cerver_crypto_key_error_to_string (CERVER_CRYPTO_KEY_ERROR_NONE), "None", NULL);
	test_check_str_eq (
		cerver_crypto_key_error_to_string (CERVER_CRYPTO_KEY_ERROR_ARGUMENTS), "The key has no material", NULL
	);
	test_check_str_eq (
		cerver_crypto_key_error_to_string (CERVER_CRYPTO_KEY_ERROR_FORMAT),
		"The material of the key could not be read", NULL
	);
	test_check_str_eq (
		cerver_crypto_key_error_to_string (CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED),
		"The key is of a type or of a curve that cerver does not use", NULL
	);
	test_check_str_eq (
		cerver_crypto_key_error_to_string (CERVER_CRYPTO_KEY_ERROR_MEMORY), "The key could not be allocated", NULL
	);
	test_check_str_eq (
		cerver_crypto_key_error_to_string ((CerverCryptoKeyError) CRYPTO_TEST_KEYS_OUT_OF_TABLE), "None", NULL
	);

}

// a NULL key is no key
static void test_key_null (void) {

	test_check_int_eq (cerver_crypto_key_type (NULL), CERVER_CRYPTO_KEY_TYPE_NONE, NULL);
	test_check_false (cerver_crypto_key_is_private (NULL));
	test_check_unsigned_eq (cerver_crypto_key_bits (NULL), 0, NULL);
	test_check_int_eq (cerver_crypto_key_curve (NULL), CERVER_CRYPTO_CURVE_NONE, NULL);

	cerver_crypto_key_delete (NULL);

}

static void test_key_secret (void) {

	const size_t size = strlen (CRYPTO_TEST_KEYS_SECRET);

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = cerver_crypto_key_create_secret (CRYPTO_TEST_KEYS_SECRET, size, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);

	test_check_int_eq (cerver_crypto_key_type (key), CERVER_CRYPTO_KEY_TYPE_SECRET, NULL);
	test_check_true (cerver_crypto_key_is_private (key));
	test_check_unsigned_eq (cerver_crypto_key_bits (key), size * CRYPTO_TEST_KEYS_BYTE_BITS, NULL);
	test_check_int_eq (cerver_crypto_key_curve (key), CERVER_CRYPTO_CURVE_NONE, NULL);

	cerver_crypto_key_delete (key);

	// the error is optional
	key = cerver_crypto_key_create_secret (CRYPTO_TEST_KEYS_SECRET, size, NULL);
	test_check_ptr (key);
	cerver_crypto_key_delete (key);

	// a secret of a single byte, and the largest one
	key = cerver_crypto_key_create_secret (CRYPTO_TEST_KEYS_SECRET, sizeof (char), &error);
	test_check_ptr (key);
	test_check_unsigned_eq (cerver_crypto_key_bits (key), CRYPTO_TEST_KEYS_BYTE_BITS, NULL);
	cerver_crypto_key_delete (key);

	u8 *large = (u8 *) calloc (CERVER_CRYPTO_SECRET_SIZE_MAX + 1, sizeof (u8));
	test_check_ptr (large);

	key = cerver_crypto_key_create_secret (large, CERVER_CRYPTO_SECRET_SIZE_MAX, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);
	test_check_unsigned_eq (
		cerver_crypto_key_bits (key), CERVER_CRYPTO_SECRET_SIZE_MAX * CRYPTO_TEST_KEYS_BYTE_BITS, NULL
	);
	cerver_crypto_key_delete (key);

	// one byte more is not a secret
	test_check_null_ptr (cerver_crypto_key_create_secret (large, CERVER_CRYPTO_SECRET_SIZE_MAX + 1, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	free (large);

	error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check_null_ptr (cerver_crypto_key_create_secret (NULL, size, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check_null_ptr (cerver_crypto_key_create_secret (CRYPTO_TEST_KEYS_SECRET, 0, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	test_check_null_ptr (cerver_crypto_key_create_secret (NULL, 0, NULL));

}

static void test_key_check (
	const CryptoTestKey *expected, const bool is_private
) {

	CerverCryptoKey *key = crypto_test_key_get (expected->name, is_private);

	test_check_int_eq (cerver_crypto_key_type (key), expected->type, expected->name);
	test_check_bool_eq (cerver_crypto_key_is_private (key), is_private, expected->name);
	test_check_unsigned_eq (cerver_crypto_key_bits (key), expected->bits, expected->name);
	test_check_int_eq (cerver_crypto_key_curve (key), expected->curve, expected->name);

	cerver_crypto_key_delete (key);

}

// every key of the tests, from its private and from its public PEM
static void test_key_pem (void) {

	for (size_t i = 0; i < CRYPTO_TEST_KEYS; i++) {
		test_key_check (&crypto_test_keys[i], true);
		test_key_check (&crypto_test_keys[i], false);
	}

}

// the PEM of PKCS #1 and the one of SEC 1 are also private keys
static void test_key_pem_formats (void) {

	CerverCryptoKey *key = crypto_test_key_get (CRYPTO_TEST_KEY_TRADITIONAL, true);
	test_check_int_eq (cerver_crypto_key_type (key), CERVER_CRYPTO_KEY_TYPE_RSA, NULL);
	test_check_unsigned_eq (cerver_crypto_key_bits (key), CRYPTO_TEST_KEYS_RSA_BITS, NULL);
	test_check_true (cerver_crypto_key_is_private (key));
	cerver_crypto_key_delete (key);

	key = crypto_test_key_get (CRYPTO_TEST_KEY_SEC1, true);
	test_check_int_eq (cerver_crypto_key_type (key), CERVER_CRYPTO_KEY_TYPE_EC, NULL);
	test_check_int_eq (cerver_crypto_key_curve (key), CERVER_CRYPTO_CURVE_P256, NULL);
	test_check_true (cerver_crypto_key_is_private (key));
	cerver_crypto_key_delete (key);

}

// the PEM is read by its size: it does not need to end with a null byte, and what follows it is not read
static void test_key_pem_size (void) {

	size_t size = 0;
	char *pem = crypto_test_file_read (CRYPTO_TEST_KEY_P256 CRYPTO_TEST_KEY_PUBLIC_EXTENSION, &size);

	char *exact = (char *) malloc (size);
	test_check_ptr (exact);
	(void) memcpy (exact, pem, size);

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = cerver_crypto_key_create_pem (exact, size, false, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);
	cerver_crypto_key_delete (key);

	free (exact);

	// the error is optional
	key = cerver_crypto_key_create_pem (pem, size, false, NULL);
	test_check_ptr (key);
	cerver_crypto_key_delete (key);

	// only the start of the PEM is not a key
	test_check_null_ptr (cerver_crypto_key_create_pem (pem, CRYPTO_TEST_KEYS_PEM_START, false, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	// neither is the PEM without its last line
	test_check_null_ptr (cerver_crypto_key_create_pem (pem, size - CRYPTO_TEST_KEYS_PEM_START, false, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	free (pem);

}

static void test_key_pem_fails (
	const char *name, const bool is_private, const CerverCryptoKeyError expected
) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check (crypto_test_key_load (name, is_private, &error) == NULL, name);
	test_check_int_eq (error, expected, name);

}

static void test_key_pem_errors (void) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;

	test_check_null_ptr (cerver_crypto_key_create_pem (NULL, sizeof (CRYPTO_TEST_KEYS_TEXT), true, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check_null_ptr (cerver_crypto_key_create_pem (CRYPTO_TEST_KEYS_TEXT, 0, false, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	test_check_null_ptr (cerver_crypto_key_create_pem (NULL, 0, false, NULL));

	// a text that is not a PEM, and a PEM that has no key
	test_check_null_ptr (
		cerver_crypto_key_create_pem (CRYPTO_TEST_KEYS_TEXT, strlen (CRYPTO_TEST_KEYS_TEXT), true, &error)
	);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	test_check_null_ptr (
		cerver_crypto_key_create_pem (CRYPTO_TEST_KEYS_TEXT, strlen (CRYPTO_TEST_KEYS_TEXT), false, &error)
	);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	test_check_null_ptr (
		cerver_crypto_key_create_pem (CRYPTO_TEST_KEYS_EMPTY_PEM, strlen (CRYPTO_TEST_KEYS_EMPTY_PEM), false, &error)
	);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	// a private PEM is not a public one
	size_t size = 0;
	char *pem = crypto_test_file_read (CRYPTO_TEST_KEY_RSA CRYPTO_TEST_KEY_PRIVATE_EXTENSION, &size);
	test_check_null_ptr (cerver_crypto_key_create_pem (pem, size, false, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);
	free (pem);

	// and a public PEM is not a private one
	pem = crypto_test_file_read (CRYPTO_TEST_KEY_RSA CRYPTO_TEST_KEY_PUBLIC_EXTENSION, &size);
	test_check_null_ptr (cerver_crypto_key_create_pem (pem, size, true, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);
	free (pem);

	// a PEM with a passphrase is never decrypted, and nothing asks for it
	test_key_pem_fails (CRYPTO_TEST_KEY_ENCRYPTED, true, CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// a key of RSA with more bits than the largest one: its signatures would not fit in their buffer
	test_key_pem_fails (CRYPTO_TEST_KEY_HUGE, false, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);

	// the keys that cerver does not use: another curve and another type
	test_key_pem_fails (CRYPTO_TEST_KEY_SECP256K1, true, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	test_key_pem_fails (CRYPTO_TEST_KEY_SECP256K1, false, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	test_key_pem_fails (CRYPTO_TEST_KEY_X25519, true, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	test_key_pem_fails (CRYPTO_TEST_KEY_X25519, false, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);

}

void crypto_tests_keys (void) {

	(void) printf ("Testing CRYPTO keys...\n");

	test_run (test_key_strings);
	test_run (test_key_null);
	test_run (test_key_secret);
	test_run (test_key_pem);
	test_run (test_key_pem_formats);
	test_run (test_key_pem_size);
	test_run (test_key_pem_errors);

	(void) printf ("Done!\n");

}
