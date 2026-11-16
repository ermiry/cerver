#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>

#include <cerver/utils/base64.h>

#include "../test.h"
#include "crypto.h"
#include "vectors.h"

// the most bytes of the text of a JWK
#define JWK_TEST_SIZE_MAX					(1 << 16)

// a JWK that a test writes
#define JWK_TEST_TEXT_SIZE					1024

#define JWK_TEST_BYTE_BITS					8

// the bits that the provider reports
#define JWK_TEST_P256_BITS					256
#define JWK_TEST_P521_BITS					521
#define JWK_TEST_ED25519_BITS				256
#define JWK_TEST_ED448_BITS					456

// the bytes of the numbers of the curves
#define JWK_TEST_P256_SIZE					32
#define JWK_TEST_ED448_SIZE					57

// the key of the first test of Ed448 of RFC 8032 section 7.4
#define JWK_TEST_ED448_SECRET				\
	"6c82a562cb808d10d632be89c8513ebf6c929f34ddfa8c9f63c9960ef6e348a3"		\
	"528c8a3fcc2f044e39a3fc5b94492f8f032e7549a20098f95b"

#define JWK_TEST_ED448_PUBLIC				\
	"5fd7449b59b461fd2ce787ec616ad46a1da1342485a70e1f8a0ea75d80e96778"		\
	"edf124769b46c7061bd6783df1e50f6cd1fa1abeafe8256180"

// the secret of the second test of Ed25519 of RFC 8032 section 7.1, the one of another key
#define JWK_TEST_ED25519_OTHER_SECRET		"TM0Imyj_ltqdtsNG7BFOD1uKMZ81q6Yk2oz27U-4pvs"

// a secret of three bytes
#define JWK_TEST_SHORT_SECRET				"AQID"
#define JWK_TEST_SHORT_SECRET_BYTES			3

static CerverCryptoKey *jwk_test_create (const char *jwk, CerverCryptoKeyError *error) {

	return cerver_crypto_key_create_jwk (jwk, strlen (jwk), error);

}

// the text is the JWK of that key
static void jwk_test_is (
	const char *jwk,
	const CerverCryptoKeyType type, const bool is_private,
	const unsigned int bits, const CerverCryptoCurve curve
) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = jwk_test_create (jwk, &error);

	test_check (key != NULL, jwk);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, jwk);

	test_check_int_eq (cerver_crypto_key_type (key), type, jwk);
	test_check_bool_eq (cerver_crypto_key_is_private (key), is_private, jwk);
	test_check_unsigned_eq (cerver_crypto_key_bits (key), bits, jwk);
	test_check_int_eq (cerver_crypto_key_curve (key), curve, jwk);

	cerver_crypto_key_delete (key);

}

// the text is not the JWK of a key, with that error
static void jwk_test_fails (const char *jwk, const CerverCryptoKeyError expected) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check (jwk_test_create (jwk, &error) == NULL, jwk);
	test_check_int_eq (error, expected, jwk);

	// the error is optional
	test_check (jwk_test_create (jwk, NULL) == NULL, jwk);

}

static void test_jwk_arguments (void) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check_null_ptr (cerver_crypto_key_create_jwk (NULL, sizeof (CRYPTO_VECTOR_SECRET_JWK), &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check_null_ptr (cerver_crypto_key_create_jwk (CRYPTO_VECTOR_SECRET_JWK, 0, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS, NULL);

	test_check_null_ptr (cerver_crypto_key_create_jwk (NULL, 0, NULL));

	// the error is optional
	CerverCryptoKey *key = jwk_test_create (CRYPTO_VECTOR_SECRET_JWK, NULL);
	test_check_ptr (key);
	cerver_crypto_key_delete (key);

}

// the text is read by its size: it does not need a null byte, and what follows it is not read
static void test_jwk_size (void) {

	const size_t size = strlen (CRYPTO_VECTOR_SECRET_JWK);

	char *exact = (char *) malloc (size);
	test_check_ptr (exact);
	(void) memcpy (exact, CRYPTO_VECTOR_SECRET_JWK, size);

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = cerver_crypto_key_create_jwk (exact, size, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);
	cerver_crypto_key_delete (key);

	free (exact);

	// the text is not changed
	char copy[] = { CRYPTO_VECTOR_SECRET_JWK };
	key = cerver_crypto_key_create_jwk (copy, size, &error);
	test_check_ptr (key);
	test_check_str_eq (copy, CRYPTO_VECTOR_SECRET_JWK, NULL);
	cerver_crypto_key_delete (key);

	// without its last character it is not a JSON
	test_check_null_ptr (cerver_crypto_key_create_jwk (CRYPTO_VECTOR_SECRET_JWK, size - 1, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	// a text with the most bytes: the JWK and spaces
	char *large = (char *) malloc (JWK_TEST_SIZE_MAX + 1);
	test_check_ptr (large);
	(void) memset (large, ' ', JWK_TEST_SIZE_MAX + 1);
	(void) memcpy (large, CRYPTO_VECTOR_SECRET_JWK, size);

	key = cerver_crypto_key_create_jwk (large, JWK_TEST_SIZE_MAX, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);
	cerver_crypto_key_delete (key);

	// one byte more is not read
	test_check_null_ptr (cerver_crypto_key_create_jwk (large, JWK_TEST_SIZE_MAX + 1, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	free (large);

}

static void test_jwk_json (void) {

	// what is not the JSON of an object
	jwk_test_fails ("{", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("this is not a JWK", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("[]", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("\"oct\"", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("null", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("[" CRYPTO_VECTOR_SECRET_JWK "]", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails (CRYPTO_VECTOR_SECRET_JWK CRYPTO_VECTOR_SECRET_JWK, CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// an object without a type, or with one that is not a string
	jwk_test_fails ("{}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":1,\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":null,\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":[\"oct\"],\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// a type that cerver does not use: the names are exact
	jwk_test_fails ("{\"kty\":\"AES\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	jwk_test_fails ("{\"kty\":\"\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	jwk_test_fails ("{\"kty\":\"OCT\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	jwk_test_fails ("{\"kty\":\"oct \",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	jwk_test_fails ("{\"kty\":\"octet\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	jwk_test_fails ("{\"kty\":\"oc\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);

	// a member more than once, and a null byte in a string
	jwk_test_fails (
		"{\"kty\":\"oct\",\"k\":\"" JWK_TEST_SHORT_SECRET "\",\"kty\":\"oct\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"oct\",\"k\":\"" JWK_TEST_SHORT_SECRET "\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"oct\",\"k\":\"" JWK_TEST_SHORT_SECRET "\",\"kid\":\"a\\u0000b\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// the members that are not read can be anything
	jwk_test_is (
		"{\"use\":\"sig\",\"alg\":\"HS256\",\"kid\":\"the key\",\"key_ops\":[\"sign\",\"verify\"],"
		"\"x5c\":[[\"a\"],{\"b\":null}],\"ext\":true,\"n\":7,\"crv\":false,\"d\":{},"
		" \"kty\" : \"oct\" , \"k\" : \"" JWK_TEST_SHORT_SECRET "\" }",
		CERVER_CRYPTO_KEY_TYPE_SECRET, true,
		JWK_TEST_SHORT_SECRET_BYTES * JWK_TEST_BYTE_BITS, CERVER_CRYPTO_CURVE_NONE
	);

	// its levels: the object and the levels of the members that are not read
	jwk_test_is (
		"{\"kty\":\"oct\",\"k\":\"" JWK_TEST_SHORT_SECRET "\",\"deep\":[[[[[[[1]]]]]]]}",
		CERVER_CRYPTO_KEY_TYPE_SECRET, true,
		JWK_TEST_SHORT_SECRET_BYTES * JWK_TEST_BYTE_BITS, CERVER_CRYPTO_CURVE_NONE
	);
	jwk_test_fails (
		"{\"kty\":\"oct\",\"k\":\"" JWK_TEST_SHORT_SECRET "\",\"deep\":[[[[[[[[1]]]]]]]]}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

}

// the JWK of the short secret with something else in it
#define JWK_TEST_WITH(members)				"{" members ",\"kty\":\"oct\",\"k\":\"" JWK_TEST_SHORT_SECRET "\"}"

// a JWK that says what its key is for is one of signatures, or it is not a key of cerver
static void test_jwk_purpose (void) {

	static const char *signatures[] = {
		JWK_TEST_WITH ("\"use\":\"sig\""),
		JWK_TEST_WITH ("\"key_ops\":[\"sign\"]"),
		JWK_TEST_WITH ("\"key_ops\":[\"verify\"]"),
		JWK_TEST_WITH ("\"key_ops\":[\"sign\",\"verify\"]"),
		JWK_TEST_WITH ("\"key_ops\":[\"encrypt\",\"wrapKey\",\"verify\"]"),
		JWK_TEST_WITH ("\"key_ops\":[7,null,\"sign\"]"),
		JWK_TEST_WITH ("\"use\":\"sig\",\"key_ops\":[\"sign\"]")
	};

	for (size_t i = 0; i < (sizeof (signatures) / sizeof (signatures[0])); i++) {
		jwk_test_is (
			signatures[i], CERVER_CRYPTO_KEY_TYPE_SECRET, true,
			JWK_TEST_SHORT_SECRET_BYTES * JWK_TEST_BYTE_BITS, CERVER_CRYPTO_CURVE_NONE
		);
	}

	static const char *others[] = {
		JWK_TEST_WITH ("\"use\":\"enc\""),
		JWK_TEST_WITH ("\"use\":\"SIG\""),
		JWK_TEST_WITH ("\"use\":\"sig \""),
		JWK_TEST_WITH ("\"use\":\"si\""),
		JWK_TEST_WITH ("\"use\":\"\""),
		JWK_TEST_WITH ("\"use\":7"),
		JWK_TEST_WITH ("\"use\":null"),
		JWK_TEST_WITH ("\"use\":[\"sig\"]"),
		JWK_TEST_WITH ("\"key_ops\":[\"encrypt\"]"),
		JWK_TEST_WITH ("\"key_ops\":[\"encrypt\",\"decrypt\",\"wrapKey\"]"),
		JWK_TEST_WITH ("\"key_ops\":[]"),
		JWK_TEST_WITH ("\"key_ops\":[\"SIGN\",\"verif\",\"verify \"]"),
		JWK_TEST_WITH ("\"key_ops\":\"sign\""),
		JWK_TEST_WITH ("\"key_ops\":null"),
		JWK_TEST_WITH ("\"key_ops\":[[\"sign\"]]"),
		JWK_TEST_WITH ("\"key_ops\":{\"sign\":true}"),
		JWK_TEST_WITH ("\"use\":\"sig\",\"key_ops\":[\"encrypt\"]"),
		JWK_TEST_WITH ("\"use\":\"enc\",\"key_ops\":[\"sign\"]"),
		"{\"use\":\"enc\"," CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS "}",
		"{\"key_ops\":[\"encrypt\"]," CRYPTO_VECTOR_P256_PUBLIC_MEMBERS "}",
		"{\"use\":\"enc\",\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}"
	};

	for (size_t i = 0; i < (sizeof (others) / sizeof (others[0])); i++) {
		jwk_test_fails (others[i], CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
	}

}

static void test_jwk_secret (void) {

	jwk_test_is (
		CRYPTO_VECTOR_SECRET_JWK, CERVER_CRYPTO_KEY_TYPE_SECRET, true,
		CRYPTO_VECTOR_SECRET_BYTES * JWK_TEST_BYTE_BITS, CERVER_CRYPTO_CURVE_NONE
	);

	// without its secret, with one that is not a string, and with an empty one
	jwk_test_fails ("{\"kty\":\"oct\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":7}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":null}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":[\"" JWK_TEST_SHORT_SECRET "\"]}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":\"\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// a secret that is not canonical base64url: padding, another alphabet, a single character, bits left
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":\"AQI=\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":\"AQ+/\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":\"AQID \"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":\"AQIDA\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"oct\",\"k\":\"AQIDAR\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// the other alphabet of base64url is one
	jwk_test_is (
		"{\"kty\":\"oct\",\"k\":\"-_-_\"}", CERVER_CRYPTO_KEY_TYPE_SECRET, true,
		JWK_TEST_SHORT_SECRET_BYTES * JWK_TEST_BYTE_BITS, CERVER_CRYPTO_CURVE_NONE
	);

}

// the JWK of a real public key of RSA whose modulus has one byte more than the one of the largest key:
// a number of that size is not read, the provider never gets to check it
static void test_jwk_rsa_huge (void) {

	size_t size = 0;
	char *jwk = crypto_test_file_read (CRYPTO_TEST_KEY_HUGE ".jwk", &size);

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;
	test_check_null_ptr (cerver_crypto_key_create_jwk (jwk, size, &error));
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_FORMAT, NULL);

	free (jwk);

}

// a group of base64url that is three bytes with zeros, and the text of the JWK of a modulus after many of them
#define JWK_TEST_ZERO_GROUP					"AAAA"
#define JWK_TEST_ZERO_GROUP_BYTES			3
#define JWK_TEST_PADDED_SIZE				(1 << 12)

// the groups of zeros that have almost the bytes of the modulus of the largest key
#define JWK_TEST_ZERO_GROUPS				((CERVER_CRYPTO_RSA_BITS_MAX / JWK_TEST_BYTE_BITS) / JWK_TEST_ZERO_GROUP_BYTES)

// the zeros before a number are bytes of it: the modulus of the key of the tests after enough of them has more
// bytes than the one of the largest key, and it is not read (without its zeros it would be a key)
static void test_jwk_rsa_padded (void) {

	char *jwk = (char *) calloc (JWK_TEST_PADDED_SIZE, sizeof (char));
	test_check_ptr (jwk);

	size_t length = (size_t) snprintf (jwk, JWK_TEST_PADDED_SIZE, "{\"kty\":\"RSA\",\"n\":\"");
	for (size_t i = 0; i < JWK_TEST_ZERO_GROUPS; i++) {
		length += (size_t) snprintf (jwk + length, JWK_TEST_PADDED_SIZE - length, JWK_TEST_ZERO_GROUP);
	}

	length += (size_t) snprintf (
		jwk + length, JWK_TEST_PADDED_SIZE - length,
		CRYPTO_VECTOR_RSA_MODULUS "\",\"e\":\"" CRYPTO_VECTOR_RSA_EXPONENT "\"}"
	);

	test_check_true (length < JWK_TEST_PADDED_SIZE);

	jwk_test_fails (jwk, CERVER_CRYPTO_KEY_ERROR_FORMAT);

	free (jwk);

}

static void test_jwk_rsa (void) {

	test_jwk_rsa_huge ();
	test_jwk_rsa_padded ();

	jwk_test_is (
		CRYPTO_VECTOR_RSA_PUBLIC_JWK, CERVER_CRYPTO_KEY_TYPE_RSA, false,
		CRYPTO_VECTOR_RSA_BITS, CERVER_CRYPTO_CURVE_NONE
	);

	jwk_test_is (
		CRYPTO_VECTOR_RSA_PRIVATE_JWK, CERVER_CRYPTO_KEY_TYPE_RSA, true,
		CRYPTO_VECTOR_RSA_BITS, CERVER_CRYPTO_CURVE_NONE
	);

	// without its modulus or its exponent, or with one that is not a string or not base64url
	jwk_test_fails ("{\"kty\":\"RSA\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"RSA\",\"n\":\"" CRYPTO_VECTOR_RSA_MODULUS "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails ("{\"kty\":\"RSA\",\"e\":\"" CRYPTO_VECTOR_RSA_EXPONENT "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails (
		"{\"kty\":\"RSA\",\"n\":\"" CRYPTO_VECTOR_RSA_MODULUS "\",\"e\":65537}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"RSA\",\"n\":\"" CRYPTO_VECTOR_RSA_MODULUS "=\",\"e\":\"" CRYPTO_VECTOR_RSA_EXPONENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// numbers that are not a public key: an even modulus, an even exponent and the exponent 1
	jwk_test_fails (
		"{\"kty\":\"RSA\",\"n\":\"" CRYPTO_VECTOR_RSA_FIRST_PRIME "A\",\"e\":\"" CRYPTO_VECTOR_RSA_EXPONENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"RSA\",\"n\":\"" CRYPTO_VECTOR_RSA_MODULUS "\",\"e\":\"AQAA\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"RSA\",\"n\":\"" CRYPTO_VECTOR_RSA_MODULUS "\",\"e\":\"AQ\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a public key with one of the numbers of a private one
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS "," CRYPTO_VECTOR_RSA_FACTOR_MEMBERS "}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"qi\":\"" CRYPTO_VECTOR_RSA_COEFFICIENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a private key without its primes, or without one of their numbers
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT "\","
		"\"p\":\"" CRYPTO_VECTOR_RSA_FIRST_PRIME "\",\"q\":\"" CRYPTO_VECTOR_RSA_SECOND_PRIME "\","
		"\"dp\":\"" CRYPTO_VECTOR_RSA_FIRST_EXPONENT "\",\"dq\":\"" CRYPTO_VECTOR_RSA_SECOND_EXPONENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT "\","
		"\"q\":\"" CRYPTO_VECTOR_RSA_SECOND_PRIME "\","
		"\"dp\":\"" CRYPTO_VECTOR_RSA_FIRST_EXPONENT "\",\"dq\":\"" CRYPTO_VECTOR_RSA_SECOND_EXPONENT "\","
		"\"qi\":\"" CRYPTO_VECTOR_RSA_COEFFICIENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a private key whose numbers do not agree: another private exponent, and the primes in the other order
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_MODULUS "\","
		CRYPTO_VECTOR_RSA_FACTOR_MEMBERS "}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT "\","
		"\"p\":\"" CRYPTO_VECTOR_RSA_SECOND_PRIME "\",\"q\":\"" CRYPTO_VECTOR_RSA_FIRST_PRIME "\","
		"\"dp\":\"" CRYPTO_VECTOR_RSA_FIRST_EXPONENT "\",\"dq\":\"" CRYPTO_VECTOR_RSA_SECOND_EXPONENT "\","
		"\"qi\":\"" CRYPTO_VECTOR_RSA_COEFFICIENT "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a key with more than two primes
	jwk_test_fails (
		"{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_PRIVATE_EXPONENT "\","
		CRYPTO_VECTOR_RSA_FACTOR_MEMBERS ",\"oth\":[]}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);

}

// an EC key with its d after a zero byte: the same number with one byte more than its curve
static void test_jwk_curve_padded (void) {

	u8 secret[JWK_TEST_P256_SIZE + 1] = { 0 };
	test_check_unsigned_eq (
		crypto_test_base64url_decode (CRYPTO_VECTOR_P256_SECRET, secret + 1, JWK_TEST_P256_SIZE),
		JWK_TEST_P256_SIZE, NULL
	);

	char encoded[base64url_encode_length (JWK_TEST_P256_SIZE + 1) + 1];
	(void) base64url_encode (encoded, secret, JWK_TEST_P256_SIZE + 1);

	char jwk[JWK_TEST_TEXT_SIZE] = { 0 };
	(void) snprintf (jwk, JWK_TEST_TEXT_SIZE, "{" CRYPTO_VECTOR_P256_PUBLIC_MEMBERS ",\"d\":\"%s\"}", encoded);

	jwk_test_fails (jwk, CERVER_CRYPTO_KEY_ERROR_FORMAT);

}

static void test_jwk_curve (void) {

	jwk_test_is (
		CRYPTO_VECTOR_P256_PUBLIC_JWK, CERVER_CRYPTO_KEY_TYPE_EC, false,
		JWK_TEST_P256_BITS, CERVER_CRYPTO_CURVE_P256
	);
	jwk_test_is (
		CRYPTO_VECTOR_P256_PRIVATE_JWK, CERVER_CRYPTO_KEY_TYPE_EC, true,
		JWK_TEST_P256_BITS, CERVER_CRYPTO_CURVE_P256
	);
	jwk_test_is (
		CRYPTO_VECTOR_P521_PUBLIC_JWK, CERVER_CRYPTO_KEY_TYPE_EC, false,
		JWK_TEST_P521_BITS, CERVER_CRYPTO_CURVE_P521
	);
	jwk_test_is (
		CRYPTO_VECTOR_P521_PRIVATE_JWK, CERVER_CRYPTO_KEY_TYPE_EC, true,
		JWK_TEST_P521_BITS, CERVER_CRYPTO_CURVE_P521
	);

	// without its curve, with one that is not a string, and with one that cerver does not use
	jwk_test_fails (
		"{\"kty\":\"EC\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":256,\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"secp256k1\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"p-256\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"None\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"Ed25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);

	// without one of its coordinates, or with one that is not a string
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_X "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":[]}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\","
		"\"d\":true}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a point that is not of the curve: its coordinates in the other order, and the ones of another curve
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_Y "\",\"y\":\"" CRYPTO_VECTOR_P256_X "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-384\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P521_X "\",\"y\":\"" CRYPTO_VECTOR_P521_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a coordinate with less bytes than its curve
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" JWK_TEST_SHORT_SECRET "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" JWK_TEST_SHORT_SECRET "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a private number that is not the one of the point: the one of another key, and zero
	jwk_test_fails (
		"{" CRYPTO_VECTOR_P256_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_P256_X "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{" CRYPTO_VECTOR_P256_PUBLIC_MEMBERS ",\"d\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	test_jwk_curve_padded ();

}

// the JWK of a key of Ed448 from the hex of its bytes; without the secret, the public one
static void jwk_test_ed448_write (char *jwk, const char *secret_hex, const char *public_hex) {

	u8 bytes[JWK_TEST_ED448_SIZE] = { 0 };
	char encoded_public[base64url_encode_length (JWK_TEST_ED448_SIZE) + 1];
	char encoded_secret[base64url_encode_length (JWK_TEST_ED448_SIZE) + 1];

	test_check_unsigned_eq (
		crypto_test_hex_decode (public_hex, bytes, JWK_TEST_ED448_SIZE), JWK_TEST_ED448_SIZE, NULL
	);
	(void) base64url_encode (encoded_public, bytes, JWK_TEST_ED448_SIZE);

	if (secret_hex) {
		test_check_unsigned_eq (
			crypto_test_hex_decode (secret_hex, bytes, JWK_TEST_ED448_SIZE), JWK_TEST_ED448_SIZE, NULL
		);
		(void) base64url_encode (encoded_secret, bytes, JWK_TEST_ED448_SIZE);

		(void) snprintf (
			jwk, JWK_TEST_TEXT_SIZE, "{\"kty\":\"OKP\",\"crv\":\"Ed448\",\"x\":\"%s\",\"d\":\"%s\"}",
			encoded_public, encoded_secret
		);
	}

	else {
		(void) snprintf (jwk, JWK_TEST_TEXT_SIZE, "{\"kty\":\"OKP\",\"crv\":\"Ed448\",\"x\":\"%s\"}", encoded_public);
	}

}

static void test_jwk_edwards (void) {

	jwk_test_is (
		CRYPTO_VECTOR_ED25519_PUBLIC_JWK, CERVER_CRYPTO_KEY_TYPE_ED25519, false,
		JWK_TEST_ED25519_BITS, CERVER_CRYPTO_CURVE_NONE
	);
	jwk_test_is (
		CRYPTO_VECTOR_ED25519_PRIVATE_JWK, CERVER_CRYPTO_KEY_TYPE_ED25519, true,
		JWK_TEST_ED25519_BITS, CERVER_CRYPTO_CURVE_NONE
	);

	char jwk[JWK_TEST_TEXT_SIZE] = { 0 };
	jwk_test_ed448_write (jwk, NULL, JWK_TEST_ED448_PUBLIC);
	jwk_test_is (jwk, CERVER_CRYPTO_KEY_TYPE_ED448, false, JWK_TEST_ED448_BITS, CERVER_CRYPTO_CURVE_NONE);

	jwk_test_ed448_write (jwk, JWK_TEST_ED448_SECRET, JWK_TEST_ED448_PUBLIC);
	jwk_test_is (jwk, CERVER_CRYPTO_KEY_TYPE_ED448, true, JWK_TEST_ED448_BITS, CERVER_CRYPTO_CURVE_NONE);

	// a secret of Ed448 whose public key is another one: the bytes of the public key as its secret
	jwk_test_ed448_write (jwk, JWK_TEST_ED448_PUBLIC, JWK_TEST_ED448_PUBLIC);
	jwk_test_fails (jwk, CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// without its curve, with one that is not a string, and with one that cerver does not use
	jwk_test_fails ("{\"kty\":\"OKP\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":25519,\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"X25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"ed25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_X "\",\"y\":\"" CRYPTO_VECTOR_P256_Y "\"}",
		CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"RSA\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}", CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED
	);

	// without its public key, also with its secret, and with one that is not a string
	jwk_test_fails ("{\"kty\":\"OKP\",\"crv\":\"Ed25519\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"d\":\"" CRYPTO_VECTOR_ED25519_SECRET "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails ("{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":32}", CERVER_CRYPTO_KEY_ERROR_FORMAT);

	// a key with the bytes of another curve
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"Ed448\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":\"" JWK_TEST_SHORT_SECRET "\"}", CERVER_CRYPTO_KEY_ERROR_FORMAT
	);
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\","
		"\"d\":\"" JWK_TEST_SHORT_SECRET "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	// a secret whose public key is another one
	jwk_test_fails (
		"{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\","
		"\"d\":\"" JWK_TEST_ED25519_OTHER_SECRET "\"}",
		CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

}

void crypto_tests_jwk (void) {

	(void) printf ("Testing CRYPTO jwk...\n");

	test_run (test_jwk_arguments);
	test_run (test_jwk_size);
	test_run (test_jwk_json);
	test_run (test_jwk_purpose);
	test_run (test_jwk_secret);
	test_run (test_jwk_rsa);
	test_run (test_jwk_curve);
	test_run (test_jwk_edwards);

	(void) printf ("Done!\n");

}
