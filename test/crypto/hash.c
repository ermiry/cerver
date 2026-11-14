#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/hash.h>

#include "../test.h"
#include "crypto.h"

// the hex characters of a SHA-1 digest
#define CRYPTO_TEST_SHA1_HEX_LENGTH			(CERVER_CRYPTO_SHA1_SIZE * 2)

// the long message of FIPS 180: one million 'a'
#define CRYPTO_TEST_SHA1_MILLION			1000000
#define CRYPTO_TEST_SHA1_MILLION_CHARACTER	'a'

#define CRYPTO_TEST_HEX_SHIFT				4
#define CRYPTO_TEST_HEX_MASK				0x0F

static const char *crypto_test_hex_digits = { "0123456789abcdef" };

typedef struct CryptoTestSha1Vector {

	const char *input;
	const char *digest;

} CryptoTestSha1Vector;

// the vectors of FIPS 180 and of RFC 3174
static const CryptoTestSha1Vector crypto_test_sha1_vectors[] = {
	{ "", "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
	{ "abc", "a9993e364706816aba3e25717850c26c9cd0d89d" },
	{
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"84983e441c3bd26ebaae4aa1f95129e5e54670f1"
	},
	{
		"dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
		"b37a4f2cc0624f1690f64606cf385945b2bec4ea"
	}
};

static const char *crypto_test_sha1_million_digest = { "34aa973cd4c4daa4f61eeb2bdbad27316534016f" };

static void crypto_test_hex (const u8 digest[CERVER_CRYPTO_SHA1_SIZE], char *hex) {

	for (size_t i = 0; i < CERVER_CRYPTO_SHA1_SIZE; i++) {
		*hex++ = crypto_test_hex_digits[(digest[i] >> CRYPTO_TEST_HEX_SHIFT) & CRYPTO_TEST_HEX_MASK];
		*hex++ = crypto_test_hex_digits[digest[i] & CRYPTO_TEST_HEX_MASK];
	}

	*hex = '\0';

}

static void crypto_test_sha1_check (const void *data, const size_t size, const char *expected) {

	u8 digest[CERVER_CRYPTO_SHA1_SIZE] = { 0 };
	char hex[CRYPTO_TEST_SHA1_HEX_LENGTH + 1] = { 0 };

	test_check_true (cerver_crypto_sha1 (data, size, digest));

	crypto_test_hex (digest, hex);
	test_check_str_eq (hex, expected, NULL);

}

static void test_sha1_vectors (void) {

	const size_t count = sizeof (crypto_test_sha1_vectors) / sizeof (CryptoTestSha1Vector);
	for (size_t i = 0; i < count; i++) {
		const CryptoTestSha1Vector *vector = &crypto_test_sha1_vectors[i];
		crypto_test_sha1_check (vector->input, strlen (vector->input), vector->digest);
	}

}

static void test_sha1_million (void) {

	char *input = (char *) malloc (CRYPTO_TEST_SHA1_MILLION);
	test_check_ptr (input);

	(void) memset (input, CRYPTO_TEST_SHA1_MILLION_CHARACTER, CRYPTO_TEST_SHA1_MILLION);
	crypto_test_sha1_check (input, CRYPTO_TEST_SHA1_MILLION, crypto_test_sha1_million_digest);

	free (input);

}

static void test_sha1_arguments (void) {

	u8 digest[CERVER_CRYPTO_SHA1_SIZE] = { 0 };

	// no data is an empty input
	crypto_test_sha1_check (NULL, 0, crypto_test_sha1_vectors[0].digest);

	test_check_false (cerver_crypto_sha1 (NULL, CERVER_CRYPTO_SHA1_SIZE, digest));
	test_check_false (cerver_crypto_sha1 ("abc", strlen ("abc"), NULL));

}

void crypto_tests_hash (void) {

	(void) printf ("Testing CRYPTO hash...\n");

	test_run (test_sha1_vectors);
	test_run (test_sha1_million);
	test_run (test_sha1_arguments);

	(void) printf ("Done!\n");

}
