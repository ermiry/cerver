#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <pthread.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/utils/base64.h>

#include "../test.h"
#include "crypto.h"
#include "vectors.h"

// a value that is in none of the tables
#define SIGNATURE_TEST_OUT_OF_TABLE				99

// the secret of the keys of the HMAC
#define SIGNATURE_TEST_SECRET					"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

#define SIGNATURE_TEST_MESSAGE					"the message that the keys of the tests sign"
#define SIGNATURE_TEST_OTHER_MESSAGE			"the message that the keys of the tests sigN"

// the byte that fills a buffer that must not be written
#define SIGNATURE_TEST_MARK						0xA5

// the bit of a byte of a signature that a test changes
#define SIGNATURE_TEST_BIT						0x01

// the bytes of the signatures of the keys of the tests
#define SIGNATURE_TEST_SHA256_SIZE				32
#define SIGNATURE_TEST_SHA384_SIZE				48
#define SIGNATURE_TEST_SHA512_SIZE				64
#define SIGNATURE_TEST_RSA_SIZE					256
#define SIGNATURE_TEST_WEAK_SIZE				128
#define SIGNATURE_TEST_P256_SIZE				64
#define SIGNATURE_TEST_P384_SIZE				96
#define SIGNATURE_TEST_P521_SIZE				132
#define SIGNATURE_TEST_ED25519_SIZE				64
#define SIGNATURE_TEST_ED448_SIZE				114

// the buffers of the vectors
#define SIGNATURE_TEST_VECTOR_SIZE				256
#define SIGNATURE_TEST_JWK_SIZE					1024

// the signatures of an ECDSA whose S is checked: without the rule, half of them would have a high one
#define SIGNATURE_TEST_LOW_SIGNATURES			64

#define SIGNATURE_TEST_THREADS					4
#define SIGNATURE_TEST_THREAD_ROUNDS			64

#pragma region keys

typedef enum SignatureTestKey {

	SIGNATURE_TEST_KEY_SECRET		= 0,
	SIGNATURE_TEST_KEY_RSA			= 1,
	SIGNATURE_TEST_KEY_P256			= 2,
	SIGNATURE_TEST_KEY_P384			= 3,
	SIGNATURE_TEST_KEY_P521			= 4,
	SIGNATURE_TEST_KEY_ED25519		= 5,
	SIGNATURE_TEST_KEY_ED448		= 6,

	SIGNATURE_TEST_KEYS				= 7

} SignatureTestKey;

// the PEM of each key, the secret has none
static const char *signature_test_key_names[SIGNATURE_TEST_KEYS] = {
	NULL,
	CRYPTO_TEST_KEY_RSA,
	CRYPTO_TEST_KEY_P256,
	CRYPTO_TEST_KEY_P384,
	CRYPTO_TEST_KEY_P521,
	CRYPTO_TEST_KEY_ED25519,
	CRYPTO_TEST_KEY_ED448
};

// the keys of the tests: the ones that sign and the ones that only verify (the secret is the same key)
static CerverCryptoKey *signature_test_private_keys[SIGNATURE_TEST_KEYS] = { NULL };
static CerverCryptoKey *signature_test_public_keys[SIGNATURE_TEST_KEYS] = { NULL };

static void signature_test_keys_load (void) {

	signature_test_private_keys[SIGNATURE_TEST_KEY_SECRET] = cerver_crypto_key_create_secret (
		SIGNATURE_TEST_SECRET, strlen (SIGNATURE_TEST_SECRET), NULL
	);

	test_check_ptr (signature_test_private_keys[SIGNATURE_TEST_KEY_SECRET]);

	signature_test_public_keys[SIGNATURE_TEST_KEY_SECRET] = cerver_crypto_key_create_secret (
		SIGNATURE_TEST_SECRET, strlen (SIGNATURE_TEST_SECRET), NULL
	);

	test_check_ptr (signature_test_public_keys[SIGNATURE_TEST_KEY_SECRET]);

	for (size_t i = SIGNATURE_TEST_KEY_RSA; i < SIGNATURE_TEST_KEYS; i++) {
		signature_test_private_keys[i] = crypto_test_key_get (signature_test_key_names[i], true);
		signature_test_public_keys[i] = crypto_test_key_get (signature_test_key_names[i], false);
	}

}

static void signature_test_keys_delete (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_KEYS; i++) {
		cerver_crypto_key_delete (signature_test_private_keys[i]);
		cerver_crypto_key_delete (signature_test_public_keys[i]);

		signature_test_private_keys[i] = NULL;
		signature_test_public_keys[i] = NULL;
	}

}

// an algorithm with the key of the tests that it takes
typedef struct SignatureTestCase {

	CerverCryptoSignature algorithm;
	const char *name;

	SignatureTestKey key;

	// the bytes of its signatures with that key
	size_t size;

	// the same message has the same signature
	bool deterministic;

} SignatureTestCase;

static const SignatureTestCase signature_test_cases[] = {
	{ CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, "HMAC-SHA256", SIGNATURE_TEST_KEY_SECRET, SIGNATURE_TEST_SHA256_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_HMAC_SHA384, "HMAC-SHA384", SIGNATURE_TEST_KEY_SECRET, SIGNATURE_TEST_SHA384_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_HMAC_SHA512, "HMAC-SHA512", SIGNATURE_TEST_KEY_SECRET, SIGNATURE_TEST_SHA512_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256, "RSA-PKCS1-SHA256", SIGNATURE_TEST_KEY_RSA, SIGNATURE_TEST_RSA_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA384, "RSA-PKCS1-SHA384", SIGNATURE_TEST_KEY_RSA, SIGNATURE_TEST_RSA_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA512, "RSA-PKCS1-SHA512", SIGNATURE_TEST_KEY_RSA, SIGNATURE_TEST_RSA_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, "RSA-PSS-SHA256", SIGNATURE_TEST_KEY_RSA, SIGNATURE_TEST_RSA_SIZE, false },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA384, "RSA-PSS-SHA384", SIGNATURE_TEST_KEY_RSA, SIGNATURE_TEST_RSA_SIZE, false },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA512, "RSA-PSS-SHA512", SIGNATURE_TEST_KEY_RSA, SIGNATURE_TEST_RSA_SIZE, false },
	{ CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256, "ECDSA-P256-SHA256", SIGNATURE_TEST_KEY_P256, SIGNATURE_TEST_P256_SIZE, false },
	{ CERVER_CRYPTO_SIGNATURE_ECDSA_P384_SHA384, "ECDSA-P384-SHA384", SIGNATURE_TEST_KEY_P384, SIGNATURE_TEST_P384_SIZE, false },
	{ CERVER_CRYPTO_SIGNATURE_ECDSA_P521_SHA512, "ECDSA-P521-SHA512", SIGNATURE_TEST_KEY_P521, SIGNATURE_TEST_P521_SIZE, false },
	{ CERVER_CRYPTO_SIGNATURE_ED25519, "Ed25519", SIGNATURE_TEST_KEY_ED25519, SIGNATURE_TEST_ED25519_SIZE, true },
	{ CERVER_CRYPTO_SIGNATURE_ED448, "Ed448", SIGNATURE_TEST_KEY_ED448, SIGNATURE_TEST_ED448_SIZE, true }
};

#define SIGNATURE_TEST_CASES					(sizeof (signature_test_cases) / sizeof (signature_test_cases[0]))

#pragma endregion

#pragma region tables

static void test_signature_strings (void) {

	test_check_str_eq (cerver_crypto_signature_to_string (CERVER_CRYPTO_SIGNATURE_NONE), "None", NULL);

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		test_check_str_eq (
			cerver_crypto_signature_to_string (signature_test_cases[i].algorithm), signature_test_cases[i].name, NULL
		);
	}

	test_check_str_eq (
		cerver_crypto_signature_to_string ((CerverCryptoSignature) SIGNATURE_TEST_OUT_OF_TABLE), "None", NULL
	);

	// the table has every algorithm and the one that is none
	test_check_unsigned_eq (SIGNATURE_TEST_CASES + 1, CERVER_CRYPTO_SIGNATURES, NULL);
	test_check_int_eq (signature_test_cases[SIGNATURE_TEST_CASES - 1].algorithm, CERVER_CRYPTO_SIGNATURES - 1, NULL);

}

// an algorithm only takes its key, the private one or the public one, and its signatures have their bytes
static void test_signature_takes (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];

		for (size_t key = 0; key < SIGNATURE_TEST_KEYS; key++) {
			const bool expected = key == (size_t) test->key;

			test_check_bool_eq (
				cerver_crypto_signature_takes (signature_test_private_keys[key], test->algorithm), expected, test->name
			);
			test_check_bool_eq (
				cerver_crypto_signature_takes (signature_test_public_keys[key], test->algorithm), expected, test->name
			);

			test_check_unsigned_eq (
				cerver_crypto_signature_size (signature_test_private_keys[key], test->algorithm),
				expected ? test->size : 0, test->name
			);
			test_check_unsigned_eq (
				cerver_crypto_signature_size (signature_test_public_keys[key], test->algorithm),
				expected ? test->size : 0, test->name
			);
		}

		test_check_false (cerver_crypto_signature_takes (NULL, test->algorithm));
		test_check_unsigned_eq (cerver_crypto_signature_size (NULL, test->algorithm), 0, test->name);
	}

	// no key is the one of what is not an algorithm
	for (size_t key = 0; key < SIGNATURE_TEST_KEYS; key++) {
		test_check_false (cerver_crypto_signature_takes (signature_test_private_keys[key], CERVER_CRYPTO_SIGNATURE_NONE));
		test_check_false (
			cerver_crypto_signature_takes (
				signature_test_private_keys[key], (CerverCryptoSignature) CERVER_CRYPTO_SIGNATURES
			)
		);
		test_check_false (
			cerver_crypto_signature_takes (
				signature_test_private_keys[key], (CerverCryptoSignature) SIGNATURE_TEST_OUT_OF_TABLE
			)
		);

		test_check_unsigned_eq (
			cerver_crypto_signature_size (signature_test_private_keys[key], CERVER_CRYPTO_SIGNATURE_NONE), 0, NULL
		);
		test_check_unsigned_eq (
			cerver_crypto_signature_size (
				signature_test_private_keys[key], (CerverCryptoSignature) SIGNATURE_TEST_OUT_OF_TABLE
			),
			0, NULL
		);
	}

	// the signatures of a key of RSA have the bytes of its modulus
	CerverCryptoKey *weak = crypto_test_key_get (CRYPTO_TEST_KEY_WEAK, true);
	test_check_unsigned_eq (
		cerver_crypto_signature_size (weak, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256), SIGNATURE_TEST_WEAK_SIZE, NULL
	);
	cerver_crypto_key_delete (weak);

}

#pragma endregion

#pragma region vectors

// a test of RFC 4231: a key and a message, each one a hex or a byte some times, and their three HMAC
typedef struct SignatureTestMac {

	const char *key_hex;
	u8 key_byte;
	size_t key_size;

	const char *message;
	u8 message_byte;
	size_t message_size;

	const char *sha256;
	const char *sha384;
	const char *sha512;

} SignatureTestMac;

// the test cases 1, 2, 3, 4, 6 and 7 of RFC 4231 section 4 (the 5 truncates its HMAC)
static const SignatureTestMac signature_test_macs[] = {
	{
		NULL, 0x0b, 20,
		"Hi There", 0, 0,
		"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
		"afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59c"
		"faea9ea9076ede7f4af152e8b2fa9cb6",
		"87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
		"daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854"
	},
	{
		"4a656665", 0, 0,
		"what do ya want for nothing?", 0, 0,
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
		"af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e"
		"8e2240ca5e69e2c78b3239ecfab21649",
		"164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
		"9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737"
	},
	{
		NULL, 0xaa, 20,
		NULL, 0xdd, 50,
		"773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
		"88062608d3e6ad8a0aa2ace014c8a86f0aa635d947ac9febe83ef4e55966144b"
		"2a5ab39dc13814b94e3ab6e101a34f27",
		"fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
		"bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb"
	},
	{
		"0102030405060708090a0b0c0d0e0f10111213141516171819", 0, 0,
		NULL, 0xcd, 50,
		"82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
		"3e8a69b7783c25851933ab6290af6ca77a9981480850009cc5577c6e1f573b4e"
		"6801dd23c4a7d679ccf8a386c674cffb",
		"b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
		"a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd"
	},
	{
		NULL, 0xaa, 131,
		"Test Using Larger Than Block-Size Key - Hash Key First", 0, 0,
		"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
		"4ece084485813e9088d2c63a041bc5b44f9ef1012a2b588f3cd11f05033ac4c6"
		"0c2ef6ab4030fe8296248df163f44952",
		"80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
		"6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598"
	},
	{
		NULL, 0xaa, 131,
		"This is a test using a larger than block-size key and a larger than block-size data. "
		"The key needs to be hashed before being used by the HMAC algorithm.", 0, 0,
		"9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
		"6617178e941f020d351e2f254e8fd32c602420feb0b8fb9adccebb82461e99c5"
		"a678cc31e799176d3860e6110c46523e",
		"e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
		"b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58"
	}
};

#define SIGNATURE_TEST_MACS						(sizeof (signature_test_macs) / sizeof (signature_test_macs[0]))

// the signature of the message with the key is the expected one, and it is verified
static void signature_test_exact (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	const u8 *expected, const size_t expected_size
) {

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	size_t size = 0;

	test_check_true (
		cerver_crypto_sign (key, algorithm, message, message_size, signature, sizeof (signature), &size)
	);

	test_check_unsigned_eq (size, expected_size, cerver_crypto_signature_to_string (algorithm));
	test_check (!memcmp (signature, expected, expected_size), cerver_crypto_signature_to_string (algorithm));

	test_check_true (cerver_crypto_verify (key, algorithm, message, message_size, expected, expected_size));

}

static void signature_test_mac_check (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const u8 *message, const size_t message_size, const char *expected_hex
) {

	u8 expected[SIGNATURE_TEST_SHA512_SIZE] = { 0 };
	const size_t expected_size = crypto_test_hex_decode (expected_hex, expected, sizeof (expected));

	signature_test_exact (key, algorithm, message, message_size, expected, expected_size);

}

static void signature_test_mac (const SignatureTestMac *vector) {

	u8 secret[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	size_t secret_size = vector->key_size;
	if (vector->key_hex) secret_size = crypto_test_hex_decode (vector->key_hex, secret, sizeof (secret));
	else (void) memset (secret, vector->key_byte, secret_size);

	u8 message[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	size_t message_size = vector->message_size;
	if (vector->message) {
		message_size = strlen (vector->message);
		(void) memcpy (message, vector->message, message_size);
	}

	else {
		(void) memset (message, vector->message_byte, message_size);
	}

	CerverCryptoKey *key = cerver_crypto_key_create_secret (secret, secret_size, NULL);
	test_check_ptr (key);

	signature_test_mac_check (key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, message, message_size, vector->sha256);
	signature_test_mac_check (key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA384, message, message_size, vector->sha384);
	signature_test_mac_check (key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA512, message, message_size, vector->sha512);

	cerver_crypto_key_delete (key);

}

static void test_signature_mac_vectors (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_MACS; i++) {
		signature_test_mac (&signature_test_macs[i]);
	}

}

// a test of RFC 8032: the bytes of a key of an Edwards curve, a message and its signature
typedef struct SignatureTestEdwards {

	const char *curve;
	CerverCryptoSignature algorithm;

	const char *secret;
	const char *public_key;
	const char *message;
	const char *signature;

} SignatureTestEdwards;

// the first three tests of Ed25519 (section 7.1) and the first two of Ed448 (section 7.4)
static const SignatureTestEdwards signature_test_edwards[] = {
	{
		"Ed25519", CERVER_CRYPTO_SIGNATURE_ED25519,
		"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
		"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
		"",
		"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
		"5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
	},
	{
		"Ed25519", CERVER_CRYPTO_SIGNATURE_ED25519,
		"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
		"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
		"72",
		"92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
		"085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
	},
	{
		"Ed25519", CERVER_CRYPTO_SIGNATURE_ED25519,
		"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
		"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
		"af82",
		"6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
		"18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
	},
	{
		"Ed448", CERVER_CRYPTO_SIGNATURE_ED448,
		"6c82a562cb808d10d632be89c8513ebf6c929f34ddfa8c9f63c9960ef6e348a3"
		"528c8a3fcc2f044e39a3fc5b94492f8f032e7549a20098f95b",
		"5fd7449b59b461fd2ce787ec616ad46a1da1342485a70e1f8a0ea75d80e96778"
		"edf124769b46c7061bd6783df1e50f6cd1fa1abeafe8256180",
		"",
		"533a37f6bbe457251f023c0d88f976ae2dfb504a843e34d2074fd823d41a591f"
		"2b233f034f628281f2fd7a22ddd47d7828c59bd0a21bfd3980ff0d2028d4b18a"
		"9df63e006c5d1c2d345b925d8dc00b4104852db99ac5c7cdda8530a113a0f4db"
		"b61149f05a7363268c71d95808ff2e652600"
	},
	{
		"Ed448", CERVER_CRYPTO_SIGNATURE_ED448,
		"c4eab05d357007c632f3dbb48489924d552b08fe0c353a0d4a1f00acda2c463a"
		"fbea67c5e8d2877c5e3bc397a659949ef8021e954e0a12274e",
		"43ba28f430cdff456ae531545f7ecd0ac834a55d9358c0372bfa0c6c6798c086"
		"6aea01eb00742802b8438ea4cb82169c235160627b4c3a9480",
		"03",
		"26b8f91727bd62897af15e41eb43c377efb9c610d48f2335cb0bd0087810f435"
		"2541b143c4b981b7e18f62de8ccdf633fc1bf037ab7cd779805e0dbcc0aae1cb"
		"cee1afb2e027df36bc04dcecbf154336c19f0af7e0a6472905e799f1953d2a0f"
		"f3348ab21aa4adafd1d234441cf807c03a00"
	}
};

#define SIGNATURE_TEST_EDWARDS					(sizeof (signature_test_edwards) / sizeof (signature_test_edwards[0]))

// the base64url of the bytes of a hex
static void signature_test_hex_to_base64url (const char *hex, char *encoded) {

	u8 bytes[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	const size_t size = crypto_test_hex_decode (hex, bytes, sizeof (bytes));

	(void) base64url_encode (encoded, bytes, size);

}

// the key of a test of RFC 8032, from the JWK of its bytes
static CerverCryptoKey *signature_test_edwards_key (
	const SignatureTestEdwards *vector, const bool is_private
) {

	char encoded_secret[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	char encoded_public[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	signature_test_hex_to_base64url (vector->secret, encoded_secret);
	signature_test_hex_to_base64url (vector->public_key, encoded_public);

	char jwk[SIGNATURE_TEST_JWK_SIZE] = { 0 };
	if (is_private) {
		(void) snprintf (
			jwk, SIGNATURE_TEST_JWK_SIZE, "{\"kty\":\"OKP\",\"crv\":\"%s\",\"x\":\"%s\",\"d\":\"%s\"}",
			vector->curve, encoded_public, encoded_secret
		);
	}

	else {
		(void) snprintf (
			jwk, SIGNATURE_TEST_JWK_SIZE, "{\"kty\":\"OKP\",\"crv\":\"%s\",\"x\":\"%s\"}",
			vector->curve, encoded_public
		);
	}

	CerverCryptoKey *key = cerver_crypto_key_create_jwk (jwk, strlen (jwk), NULL);
	test_check (key != NULL, vector->curve);

	return key;

}

static void signature_test_edwards_vector (const SignatureTestEdwards *vector) {

	u8 message[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	const size_t message_size = crypto_test_hex_decode (vector->message, message, sizeof (message));

	u8 expected[SIGNATURE_TEST_VECTOR_SIZE] = { 0 };
	const size_t expected_size = crypto_test_hex_decode (vector->signature, expected, sizeof (expected));

	CerverCryptoKey *private_key = signature_test_edwards_key (vector, true);
	signature_test_exact (private_key, vector->algorithm, message, message_size, expected, expected_size);
	cerver_crypto_key_delete (private_key);

	CerverCryptoKey *public_key = signature_test_edwards_key (vector, false);
	test_check_true (
		cerver_crypto_verify (public_key, vector->algorithm, message, message_size, expected, expected_size)
	);

	// the signature of an empty message is not the one of a message with a zero byte
	test_check_false (
		cerver_crypto_verify (public_key, vector->algorithm, message, message_size + 1, expected, expected_size)
	);

	cerver_crypto_key_delete (public_key);

}

static void test_signature_edwards_vectors (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_EDWARDS; i++) {
		signature_test_edwards_vector (&signature_test_edwards[i]);
	}

}

static CerverCryptoKey *signature_test_jwk (const char *jwk) {

	CerverCryptoKey *key = cerver_crypto_key_create_jwk (jwk, strlen (jwk), NULL);
	test_check (key != NULL, jwk);

	return key;

}

// the examples of RFC 7515 appendix A and of RFC 8037 appendix A, with the keys of their JWK
static void test_signature_examples (void) {

	u8 expected[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };

	// A.1: the HMAC of the example
	CerverCryptoKey *key = signature_test_jwk (CRYPTO_VECTOR_SECRET_JWK);
	size_t expected_size = crypto_test_base64url_decode (CRYPTO_VECTOR_SECRET_SIGNATURE, expected, sizeof (expected));
	signature_test_exact (
		key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256,
		CRYPTO_VECTOR_SECRET_MESSAGE, strlen (CRYPTO_VECTOR_SECRET_MESSAGE), expected, expected_size
	);
	cerver_crypto_key_delete (key);

	// A.2: the private key signs what the example has, and the public one verifies it
	key = signature_test_jwk (CRYPTO_VECTOR_RSA_PRIVATE_JWK);
	expected_size = crypto_test_base64url_decode (CRYPTO_VECTOR_RSA_SIGNATURE, expected, sizeof (expected));
	signature_test_exact (
		key, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256,
		CRYPTO_VECTOR_RSA_MESSAGE, strlen (CRYPTO_VECTOR_RSA_MESSAGE), expected, expected_size
	);
	cerver_crypto_key_delete (key);

	key = signature_test_jwk (CRYPTO_VECTOR_RSA_PUBLIC_JWK);
	test_check_true (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256,
			CRYPTO_VECTOR_RSA_MESSAGE, strlen (CRYPTO_VECTOR_RSA_MESSAGE), expected, expected_size
		)
	);

	// the same signature is not one of PSS, nor of another digest
	test_check_false (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256,
			CRYPTO_VECTOR_RSA_MESSAGE, strlen (CRYPTO_VECTOR_RSA_MESSAGE), expected, expected_size
		)
	);
	test_check_false (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA384,
			CRYPTO_VECTOR_RSA_MESSAGE, strlen (CRYPTO_VECTOR_RSA_MESSAGE), expected, expected_size
		)
	);
	cerver_crypto_key_delete (key);

	// A.3: a signature of another signer, whose S is in the upper half of the order, is verified
	key = signature_test_jwk (CRYPTO_VECTOR_P256_PUBLIC_JWK);
	expected_size = crypto_test_base64url_decode (CRYPTO_VECTOR_P256_SIGNATURE, expected, sizeof (expected));
	test_check_unsigned_eq (expected_size, SIGNATURE_TEST_P256_SIZE, NULL);
	test_check_true (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256,
			CRYPTO_VECTOR_P256_MESSAGE, strlen (CRYPTO_VECTOR_P256_MESSAGE), expected, expected_size
		)
	);
	cerver_crypto_key_delete (key);

	key = signature_test_jwk (CRYPTO_VECTOR_P256_PRIVATE_JWK);
	test_check_true (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256,
			CRYPTO_VECTOR_P256_MESSAGE, strlen (CRYPTO_VECTOR_P256_MESSAGE), expected, expected_size
		)
	);
	cerver_crypto_key_delete (key);

	// A.4
	key = signature_test_jwk (CRYPTO_VECTOR_P521_PUBLIC_JWK);
	expected_size = crypto_test_base64url_decode (CRYPTO_VECTOR_P521_SIGNATURE, expected, sizeof (expected));
	test_check_unsigned_eq (expected_size, SIGNATURE_TEST_P521_SIZE, NULL);
	test_check_true (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_ECDSA_P521_SHA512,
			CRYPTO_VECTOR_P521_MESSAGE, strlen (CRYPTO_VECTOR_P521_MESSAGE), expected, expected_size
		)
	);
	cerver_crypto_key_delete (key);

	// RFC 8037 A.4 and A.5
	key = signature_test_jwk (CRYPTO_VECTOR_ED25519_PRIVATE_JWK);
	expected_size = crypto_test_base64url_decode (CRYPTO_VECTOR_ED25519_SIGNATURE, expected, sizeof (expected));
	signature_test_exact (
		key, CERVER_CRYPTO_SIGNATURE_ED25519,
		CRYPTO_VECTOR_ED25519_MESSAGE, strlen (CRYPTO_VECTOR_ED25519_MESSAGE), expected, expected_size
	);
	cerver_crypto_key_delete (key);

	key = signature_test_jwk (CRYPTO_VECTOR_ED25519_PUBLIC_JWK);
	test_check_true (
		cerver_crypto_verify (
			key, CERVER_CRYPTO_SIGNATURE_ED25519,
			CRYPTO_VECTOR_ED25519_MESSAGE, strlen (CRYPTO_VECTOR_ED25519_MESSAGE), expected, expected_size
		)
	);
	cerver_crypto_key_delete (key);

}

#pragma endregion

#pragma region rules

static size_t signature_test_sign (
	const SignatureTestCase *test, const void *message, const size_t message_size, u8 *signature
) {

	size_t size = 0;

	test_check (
		cerver_crypto_sign (
			signature_test_private_keys[test->key], test->algorithm,
			message, message_size, signature, CERVER_CRYPTO_SIGNATURE_SIZE_MAX, &size
		),
		test->name
	);

	test_check_unsigned_eq (size, test->size, test->name);

	return size;

}

static bool signature_test_verify (
	const SignatureTestCase *test, const bool is_private,
	const void *message, const size_t message_size, const u8 *signature, const size_t size
) {

	return cerver_crypto_verify (
		is_private ? signature_test_private_keys[test->key] : signature_test_public_keys[test->key],
		test->algorithm, message, message_size, signature, size
	);

}

// a signature with one bit changed is not the one of the message
static void signature_test_changed (
	const SignatureTestCase *test, u8 *signature, const size_t size, const size_t position
) {

	signature[position] ^= SIGNATURE_TEST_BIT;

	test_check (
		!signature_test_verify (
			test, false, SIGNATURE_TEST_MESSAGE, strlen (SIGNATURE_TEST_MESSAGE), signature, size
		),
		test->name
	);

	signature[position] ^= SIGNATURE_TEST_BIT;

}

static void signature_test_round_trip (const SignatureTestCase *test) {

	const size_t message_size = strlen (SIGNATURE_TEST_MESSAGE);

	// one byte more than the largest signature, for the signature with a byte after it
	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX + 1] = { 0 };
	const size_t size = signature_test_sign (test, SIGNATURE_TEST_MESSAGE, message_size, signature);

	// the key that signed and its public key verify the signature
	test_check (signature_test_verify (test, true, SIGNATURE_TEST_MESSAGE, message_size, signature, size), test->name);
	test_check (signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, signature, size), test->name);

	// it is not the signature of another message, of a part of it or of an empty one
	test_check (
		!signature_test_verify (test, false, SIGNATURE_TEST_OTHER_MESSAGE, message_size, signature, size), test->name
	);
	test_check (
		!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size - 1, signature, size), test->name
	);
	test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, 0, signature, size), test->name);
	test_check (!signature_test_verify (test, false, NULL, 0, signature, size), test->name);

	// a signature with a bit changed, at its start, in its middle and at its end
	signature_test_changed (test, signature, size, 0);
	signature_test_changed (test, signature, size, size / 2);
	signature_test_changed (test, signature, size, size - 1);

	// a signature with one byte less, with one more and without bytes
	test_check (
		!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, signature, size - 1), test->name
	);
	test_check (
		!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, signature, size + 1), test->name
	);
	test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, signature, 0), test->name);

	// a signature that is only zeros
	u8 zeros[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, zeros, size), test->name);

	// the same message again: the same bytes only when the algorithm has no random part
	u8 again[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	test_check_unsigned_eq (signature_test_sign (test, SIGNATURE_TEST_MESSAGE, message_size, again), size, test->name);
	test_check_bool_eq (!memcmp (signature, again, size), test->deterministic, test->name);
	test_check (signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, again, size), test->name);

}

static void test_signature_round_trips (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		signature_test_round_trip (&signature_test_cases[i]);
	}

}

// the signature of an algorithm is not the one of another algorithm with the same key
static void test_signature_algorithms (void) {

	const size_t message_size = strlen (SIGNATURE_TEST_MESSAGE);

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];

		u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		const size_t size = signature_test_sign (test, SIGNATURE_TEST_MESSAGE, message_size, signature);

		for (size_t other = 0; other < SIGNATURE_TEST_CASES; other++) {
			test_check_bool_eq (
				cerver_crypto_verify (
					signature_test_public_keys[test->key], signature_test_cases[other].algorithm,
					SIGNATURE_TEST_MESSAGE, message_size, signature, size
				),
				other == i, test->name
			);
		}

		test_check (
			!cerver_crypto_verify (
				signature_test_public_keys[test->key], CERVER_CRYPTO_SIGNATURE_NONE,
				SIGNATURE_TEST_MESSAGE, message_size, signature, size
			),
			test->name
		);
	}

}

// the signature of a key is not the one of another key of its type
static void signature_test_other_key (
	const CerverCryptoSignature algorithm, const SignatureTestKey key, const char *other_jwk
) {

	const size_t message_size = strlen (SIGNATURE_TEST_MESSAGE);

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	size_t size = 0;
	test_check_true (
		cerver_crypto_sign (
			signature_test_private_keys[key], algorithm,
			SIGNATURE_TEST_MESSAGE, message_size, signature, sizeof (signature), &size
		)
	);

	CerverCryptoKey *other = signature_test_jwk (other_jwk);

	test_check_true (cerver_crypto_signature_takes (other, algorithm));
	test_check (
		!cerver_crypto_verify (other, algorithm, SIGNATURE_TEST_MESSAGE, message_size, signature, size),
		cerver_crypto_signature_to_string (algorithm)
	);

	cerver_crypto_key_delete (other);

}

static void test_signature_keys (void) {

	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, SIGNATURE_TEST_KEY_SECRET, CRYPTO_VECTOR_SECRET_JWK);
	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_HMAC_SHA512, SIGNATURE_TEST_KEY_SECRET, CRYPTO_VECTOR_SECRET_JWK);
	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256, SIGNATURE_TEST_KEY_RSA, CRYPTO_VECTOR_RSA_PUBLIC_JWK);
	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, SIGNATURE_TEST_KEY_RSA, CRYPTO_VECTOR_RSA_PUBLIC_JWK);
	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256, SIGNATURE_TEST_KEY_P256, CRYPTO_VECTOR_P256_PUBLIC_JWK);
	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_ECDSA_P521_SHA512, SIGNATURE_TEST_KEY_P521, CRYPTO_VECTOR_P521_PUBLIC_JWK);
	signature_test_other_key (CERVER_CRYPTO_SIGNATURE_ED25519, SIGNATURE_TEST_KEY_ED25519, CRYPTO_VECTOR_ED25519_PUBLIC_JWK);

}

// a sign that fails writes nothing and leaves no size
static void signature_test_sign_fails (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size, const size_t capacity
) {

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	(void) memset (signature, SIGNATURE_TEST_MARK, sizeof (signature));

	size_t size = SIGNATURE_TEST_OUT_OF_TABLE;
	test_check_false (cerver_crypto_sign (key, algorithm, message, message_size, signature, capacity, &size));
	test_check_unsigned_eq (size, 0, cerver_crypto_signature_to_string (algorithm));

	for (size_t i = 0; i < sizeof (signature); i++) {
		test_check_unsigned_eq (signature[i], SIGNATURE_TEST_MARK, "a sign that failed wrote its buffer");
	}

}

static void test_signature_sign_errors (void) {

	const size_t message_size = strlen (SIGNATURE_TEST_MESSAGE);

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];
		const CerverCryptoKey *key = signature_test_private_keys[test->key];

		// a public key never signs
		if (test->key != SIGNATURE_TEST_KEY_SECRET) {
			signature_test_sign_fails (
				signature_test_public_keys[test->key], test->algorithm,
				SIGNATURE_TEST_MESSAGE, message_size, CERVER_CRYPTO_SIGNATURE_SIZE_MAX
			);
		}

		// a buffer with one byte less than the signature, and one without bytes
		signature_test_sign_fails (key, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, test->size - 1);
		signature_test_sign_fails (key, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, 0);

		// a message without bytes that has a size
		signature_test_sign_fails (key, test->algorithm, NULL, message_size, CERVER_CRYPTO_SIGNATURE_SIZE_MAX);

		// a buffer with exactly the bytes of the signature
		u8 exact[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		size_t size = 0;
		test_check (
			cerver_crypto_sign (key, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, exact, test->size, &size),
			test->name
		);
		test_check_unsigned_eq (size, test->size, test->name);

		// the arguments that are missing
		test_check_false (
			cerver_crypto_sign (key, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, NULL, test->size, &size)
		);
		test_check_unsigned_eq (size, 0, test->name);
		test_check_false (
			cerver_crypto_sign (key, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, exact, test->size, NULL)
		);

		signature_test_sign_fails (NULL, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, CERVER_CRYPTO_SIGNATURE_SIZE_MAX);

		// the key of another algorithm
		for (size_t other = 0; other < SIGNATURE_TEST_KEYS; other++) {
			if (other != (size_t) test->key) {
				signature_test_sign_fails (
					signature_test_private_keys[other], test->algorithm,
					SIGNATURE_TEST_MESSAGE, message_size, CERVER_CRYPTO_SIGNATURE_SIZE_MAX
				);
			}
		}
	}

	// what is not an algorithm
	for (size_t key = 0; key < SIGNATURE_TEST_KEYS; key++) {
		signature_test_sign_fails (
			signature_test_private_keys[key], CERVER_CRYPTO_SIGNATURE_NONE,
			SIGNATURE_TEST_MESSAGE, message_size, CERVER_CRYPTO_SIGNATURE_SIZE_MAX
		);
		signature_test_sign_fails (
			signature_test_private_keys[key], (CerverCryptoSignature) SIGNATURE_TEST_OUT_OF_TABLE,
			SIGNATURE_TEST_MESSAGE, message_size, CERVER_CRYPTO_SIGNATURE_SIZE_MAX
		);
	}

}

static void test_signature_verify_errors (void) {

	const size_t message_size = strlen (SIGNATURE_TEST_MESSAGE);

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];

		u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		const size_t size = signature_test_sign (test, SIGNATURE_TEST_MESSAGE, message_size, signature);

		test_check_false (cerver_crypto_verify (NULL, test->algorithm, SIGNATURE_TEST_MESSAGE, message_size, signature, size));
		test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, NULL, size), test->name);
		test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, NULL, 0), test->name);
		test_check (!signature_test_verify (test, false, NULL, message_size, signature, size), test->name);

		// the key of another algorithm
		for (size_t other = 0; other < SIGNATURE_TEST_KEYS; other++) {
			if (other != (size_t) test->key) {
				test_check (
					!cerver_crypto_verify (
						signature_test_public_keys[other], test->algorithm,
						SIGNATURE_TEST_MESSAGE, message_size, signature, size
					),
					test->name
				);
			}
		}

		test_check (
			!cerver_crypto_verify (
				signature_test_public_keys[test->key], (CerverCryptoSignature) SIGNATURE_TEST_OUT_OF_TABLE,
				SIGNATURE_TEST_MESSAGE, message_size, signature, size
			),
			test->name
		);
	}

}

// an empty message is signed, as NULL or as a buffer without bytes: both are the same message
static void test_signature_empty (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];

		u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		const size_t size = signature_test_sign (test, NULL, 0, signature);

		test_check (signature_test_verify (test, false, NULL, 0, signature, size), test->name);
		test_check (signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, 0, signature, size), test->name);
		test_check (
			!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, sizeof (char), signature, size), test->name
		);

		const size_t other_size = signature_test_sign (test, SIGNATURE_TEST_MESSAGE, 0, signature);
		test_check (signature_test_verify (test, false, NULL, 0, signature, other_size), test->name);
	}

}

// the key keeps a copy of its secret
static void test_signature_secret_copy (void) {

	char secret[] = { SIGNATURE_TEST_SECRET };

	CerverCryptoKey *key = cerver_crypto_key_create_secret (secret, strlen (secret), NULL);
	test_check_ptr (key);

	(void) memset (secret, 0, sizeof (secret));

	u8 expected[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	const size_t expected_size = signature_test_sign (
		&signature_test_cases[0], SIGNATURE_TEST_MESSAGE, strlen (SIGNATURE_TEST_MESSAGE), expected
	);

	signature_test_exact (
		key, signature_test_cases[0].algorithm,
		SIGNATURE_TEST_MESSAGE, strlen (SIGNATURE_TEST_MESSAGE), expected, expected_size
	);

	cerver_crypto_key_delete (key);

}

// the half of the order of each curve: the largest S of a signature
#define SIGNATURE_TEST_P256_HALF_ORDER			\
	"7fffffff800000007fffffffffffffffde737d56d38bcf4279dce5617e3192a8"

#define SIGNATURE_TEST_P384_HALF_ORDER			\
	"7fffffffffffffffffffffffffffffffffffffffffffffffe3b1a6c0fa1b96ef"		\
	"ac0d06d9245853bd76760cb5666294b9"

#define SIGNATURE_TEST_P521_HALF_ORDER			\
	"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"		\
	"fffd28c343c1df97cb35bfe600a47b84d2e81ddae4dc44ce23d75db7db8f489c"		\
	"3204"

// the S of every signature of an ECDSA is in the lower half of the order of its curve
static void signature_test_low (const SignatureTestCase *test, const char *half_order_hex) {

	u8 half_order[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	const size_t coordinate_size = crypto_test_hex_decode (half_order_hex, half_order, sizeof (half_order));
	test_check_unsigned_eq (coordinate_size * 2, test->size, test->name);

	for (size_t i = 0; i < SIGNATURE_TEST_LOW_SIGNATURES; i++) {
		u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		const size_t size = signature_test_sign (test, &i, sizeof (i), signature);

		test_check (memcmp (signature + coordinate_size, half_order, coordinate_size) <= 0, test->name);
		test_check (signature_test_verify (test, false, &i, sizeof (i), signature, size), test->name);
	}

}

static void test_signature_low (void) {

	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];

		if (test->key == SIGNATURE_TEST_KEY_P256) signature_test_low (test, SIGNATURE_TEST_P256_HALF_ORDER);
		if (test->key == SIGNATURE_TEST_KEY_P384) signature_test_low (test, SIGNATURE_TEST_P384_HALF_ORDER);
		if (test->key == SIGNATURE_TEST_KEY_P521) signature_test_low (test, SIGNATURE_TEST_P521_HALF_ORDER);
	}

}

#pragma endregion

#pragma region provider

#ifdef CERVER_CRYPTO_OPENSSL

typedef unsigned long (*SignatureTestErrorsPeek) (void);
typedef void (*SignatureTestErrorsClear) (void);

// a method of OpenSSL, that libcerver.so loaded: the tests are not linked to it
static void *signature_test_openssl (const char *name) {

	void *symbol = dlsym (RTLD_DEFAULT, name);
	test_check (symbol != NULL, name);

	return symbol;

}

// what fails in the provider leaves no error in the queue of OpenSSL of the thread:
// the next one that reads it (the SSL_get_error () of the application) would take it as its own
static void test_signature_errors_queue (void) {

	SignatureTestErrorsPeek peek = NULL;
	SignatureTestErrorsClear clear = NULL;
	*(void **) (&peek) = signature_test_openssl ("ERR_peek_error");
	*(void **) (&clear) = signature_test_openssl ("ERR_clear_error");

	clear ();
	test_check_unsigned_eq (peek (), 0, NULL);

	// a PEM that is not a key, one with a passphrase and one of a key that cerver does not use
	test_check_null_ptr (cerver_crypto_key_create_pem (SIGNATURE_TEST_MESSAGE, strlen (SIGNATURE_TEST_MESSAGE), true, NULL));
	test_check_unsigned_eq (peek (), 0, "a PEM that is not a key left an error");

	test_check_null_ptr (cerver_crypto_key_create_pem (SIGNATURE_TEST_MESSAGE, strlen (SIGNATURE_TEST_MESSAGE), false, NULL));
	test_check_unsigned_eq (peek (), 0, "a PEM that is not a public key left an error");

	test_check_null_ptr (crypto_test_key_load (CRYPTO_TEST_KEY_ENCRYPTED, true, NULL));
	test_check_unsigned_eq (peek (), 0, "a PEM with a passphrase left an error");

	test_check_null_ptr (crypto_test_key_load (CRYPTO_TEST_KEY_SECP256K1, true, NULL));
	test_check_unsigned_eq (peek (), 0, "a key of another curve left an error");

	// the JWK of the numbers that are not a key
	const char *jwk = { "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" CRYPTO_VECTOR_P256_Y "\",\"y\":\"" CRYPTO_VECTOR_P256_X "\"}" };
	test_check_null_ptr (cerver_crypto_key_create_jwk (jwk, strlen (jwk), NULL));
	test_check_unsigned_eq (peek (), 0, "a point that is not of its curve left an error");

	jwk = "{" CRYPTO_VECTOR_RSA_PUBLIC_MEMBERS ",\"d\":\"" CRYPTO_VECTOR_RSA_MODULUS "\"," CRYPTO_VECTOR_RSA_FACTOR_MEMBERS "}";
	test_check_null_ptr (cerver_crypto_key_create_jwk (jwk, strlen (jwk), NULL));
	test_check_unsigned_eq (peek (), 0, "a key of RSA whose numbers do not agree left an error");

	jwk = "{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":\"" CRYPTO_VECTOR_ED25519_X "\",\"d\":\"" CRYPTO_VECTOR_P256_X "\"}";
	test_check_null_ptr (cerver_crypto_key_create_jwk (jwk, strlen (jwk), NULL));
	test_check_unsigned_eq (peek (), 0, "a secret of another public key left an error");

	// a signature that is not the one of the message
	const size_t message_size = strlen (SIGNATURE_TEST_MESSAGE);
	for (size_t i = 0; i < SIGNATURE_TEST_CASES; i++) {
		const SignatureTestCase *test = &signature_test_cases[i];

		u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		const size_t size = signature_test_sign (test, SIGNATURE_TEST_MESSAGE, message_size, signature);
		test_check_unsigned_eq (peek (), 0, test->name);

		test_check (!signature_test_verify (test, false, SIGNATURE_TEST_OTHER_MESSAGE, message_size, signature, size), test->name);
		test_check_unsigned_eq (peek (), 0, test->name);

		u8 zeros[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, zeros, size), test->name);
		test_check_unsigned_eq (peek (), 0, test->name);

		test_check (!signature_test_verify (test, false, SIGNATURE_TEST_MESSAGE, message_size, signature, size - 1), test->name);
		test_check_unsigned_eq (peek (), 0, test->name);
	}

}

#else

static void test_signature_errors_queue (void) {}

#endif

#pragma endregion

#pragma region threads

// signs and verifies with every key of the tests, the same ones that the other threads use
static void *signature_test_thread (void *argument) {

	const size_t thread = *(const size_t *) argument;

	for (size_t round = 0; round < SIGNATURE_TEST_THREAD_ROUNDS; round++) {
		const SignatureTestCase *test = &signature_test_cases[(thread + round) % SIGNATURE_TEST_CASES];

		const size_t message[] = { thread, round };

		u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
		const size_t size = signature_test_sign (test, message, sizeof (message), signature);

		test_check (signature_test_verify (test, false, message, sizeof (message), signature, size), test->name);
		test_check (signature_test_verify (test, true, message, sizeof (message), signature, size), test->name);

		signature[0] ^= SIGNATURE_TEST_BIT;
		test_check (!signature_test_verify (test, false, message, sizeof (message), signature, size), test->name);
	}

	return NULL;

}

// a key is used by many threads at the same time
static void test_signature_threads (void) {

	pthread_t threads[SIGNATURE_TEST_THREADS];
	size_t numbers[SIGNATURE_TEST_THREADS] = { 0 };

	for (size_t i = 0; i < SIGNATURE_TEST_THREADS; i++) {
		numbers[i] = i;
		test_check_int_eq (pthread_create (&threads[i], NULL, signature_test_thread, &numbers[i]), 0, NULL);
	}

	for (size_t i = 0; i < SIGNATURE_TEST_THREADS; i++) {
		test_check_int_eq (pthread_join (threads[i], NULL), 0, NULL);
	}

}

#pragma endregion

void crypto_tests_signatures (void) {

	(void) printf ("Testing CRYPTO signatures...\n");

	signature_test_keys_load ();

	test_run (test_signature_strings);
	test_run (test_signature_takes);
	test_run (test_signature_mac_vectors);
	test_run (test_signature_edwards_vectors);
	test_run (test_signature_examples);
	test_run (test_signature_round_trips);
	test_run (test_signature_algorithms);
	test_run (test_signature_keys);
	test_run (test_signature_sign_errors);
	test_run (test_signature_verify_errors);
	test_run (test_signature_empty);
	test_run (test_signature_secret_copy);
	test_run (test_signature_low);
	test_run (test_signature_errors_queue);
	test_run (test_signature_threads);

	signature_test_keys_delete ();

	(void) printf ("Done!\n");

}
