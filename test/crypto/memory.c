#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/memory.h>

#include "crypto.h"

#include "../test.h"

// the size of a secret, like the material of a key
#define CRYPTO_TEST_MEMORY_SIZE				(1 << 10)

#define CRYPTO_TEST_MEMORY_SECRET			0x5A

static void test_cleanse_zeros (void) {

	u8 *secret = (u8 *) malloc (CRYPTO_TEST_MEMORY_SIZE);
	test_check_ptr (secret);

	(void) memset (secret, CRYPTO_TEST_MEMORY_SECRET, CRYPTO_TEST_MEMORY_SIZE);
	cerver_crypto_cleanse (secret, CRYPTO_TEST_MEMORY_SIZE);

	for (size_t i = 0; i < CRYPTO_TEST_MEMORY_SIZE; i++) {
		test_check_unsigned_eq (secret[i], 0, "a byte of the secret was not cleared");
	}

	free (secret);

}

static void test_cleanse_arguments (void) {

	u8 secret[CRYPTO_TEST_MEMORY_SIZE];
	(void) memset (secret, CRYPTO_TEST_MEMORY_SECRET, CRYPTO_TEST_MEMORY_SIZE);

	cerver_crypto_cleanse (NULL, CRYPTO_TEST_MEMORY_SIZE);

	cerver_crypto_cleanse (secret, 0);
	for (size_t i = 0; i < CRYPTO_TEST_MEMORY_SIZE; i++) {
		test_check_unsigned_eq (secret[i], CRYPTO_TEST_MEMORY_SECRET, "an empty cleanse wrote the buffer");
	}

}

void crypto_tests_memory (void) {

	(void) printf ("Testing CRYPTO memory...\n");

	test_run (test_cleanse_zeros);
	test_run (test_cleanse_arguments);

	(void) printf ("Done!\n");

}
