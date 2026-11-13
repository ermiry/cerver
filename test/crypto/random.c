#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/random.h>

#include "crypto.h"

#include "../test.h"

// the bytes of a session id or of a request id
#define CRYPTO_TEST_RANDOM_SMALL			32

// more than one page and more than one block of the kernel
#define CRYPTO_TEST_RANDOM_LARGE			(1 << 16)

// the zero bytes expected in CRYPTO_TEST_RANDOM_LARGE random bytes are 256,
// a buffer that was never filled has all of them
#define CRYPTO_TEST_RANDOM_ZEROS_MAX		(1 << 10)

// a value that a buffer has before it is filled
#define CRYPTO_TEST_RANDOM_FILL				0xAB

static size_t crypto_test_random_zeros (const u8 *buffer, const size_t size) {

	size_t zeros = 0;
	for (size_t i = 0; i < size; i++) {
		if (!buffer[i]) zeros += 1;
	}

	return zeros;

}

static void test_random_bytes_differ (void) {

	u8 first[CRYPTO_TEST_RANDOM_SMALL] = { 0 };
	u8 second[CRYPTO_TEST_RANDOM_SMALL] = { 0 };

	test_check_true (cerver_crypto_random_bytes (first, CRYPTO_TEST_RANDOM_SMALL));
	test_check_true (cerver_crypto_random_bytes (second, CRYPTO_TEST_RANDOM_SMALL));

	test_check (memcmp (first, second, CRYPTO_TEST_RANDOM_SMALL) != 0, "two reads gave the same bytes");

}

static void test_random_bytes_large (void) {

	u8 *buffer = (u8 *) calloc (CRYPTO_TEST_RANDOM_LARGE, sizeof (u8));
	test_check_ptr (buffer);

	test_check_true (cerver_crypto_random_bytes (buffer, CRYPTO_TEST_RANDOM_LARGE));

	const size_t zeros = crypto_test_random_zeros (buffer, CRYPTO_TEST_RANDOM_LARGE);
	test_check (zeros < CRYPTO_TEST_RANDOM_ZEROS_MAX, "the buffer was not filled");

	free (buffer);

}

static void test_random_bytes_arguments (void) {

	u8 buffer[CRYPTO_TEST_RANDOM_SMALL];
	(void) memset (buffer, CRYPTO_TEST_RANDOM_FILL, CRYPTO_TEST_RANDOM_SMALL);

	test_check_false (cerver_crypto_random_bytes (NULL, CRYPTO_TEST_RANDOM_SMALL));
	test_check_false (cerver_crypto_random_bytes (NULL, 0));

	test_check_true (cerver_crypto_random_bytes (buffer, 0));
	for (size_t i = 0; i < CRYPTO_TEST_RANDOM_SMALL; i++) {
		test_check_unsigned_eq (buffer[i], CRYPTO_TEST_RANDOM_FILL, "an empty read wrote the buffer");
	}

}

void crypto_tests_random (void) {

	(void) printf ("Testing CRYPTO random...\n");

	test_run (test_random_bytes_differ);
	test_run (test_random_bytes_large);
	test_run (test_random_bytes_arguments);

	(void) printf ("Done!\n");

}
