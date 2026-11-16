#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>

#include <cerver/utils/base64.h>

#include "../test.h"
#include "crypto.h"

// the path of a key of the tests with its extension
#define CRYPTO_TEST_FILENAME_SIZE			128

#define CRYPTO_TEST_HEX_DIGITS				2
#define CRYPTO_TEST_HEX_SHIFT				4
#define CRYPTO_TEST_HEX_LETTER				10

char *crypto_test_file_read (const char *filename, size_t *size) {

	FILE *file = fopen (filename, "rb");
	test_check (file != NULL, filename);

	test_check_int_eq (fseek (file, 0, SEEK_END), 0, NULL);
	const long length = ftell (file);
	test_check (length >= 0, filename);
	rewind (file);

	char *text = (char *) malloc ((size_t) length + 1);
	test_check_ptr (text);

	test_check_unsigned_eq (fread (text, sizeof (char), (size_t) length, file), (size_t) length, NULL);
	text[length] = '\0';

	(void) fclose (file);

	*size = (size_t) length;

	return text;

}

CerverCryptoKey *crypto_test_key_load (
	const char *name, const bool is_private, CerverCryptoKeyError *error
) {

	char filename[CRYPTO_TEST_FILENAME_SIZE] = { 0 };
	(void) snprintf (
		filename, CRYPTO_TEST_FILENAME_SIZE, "%s%s",
		name, is_private ? CRYPTO_TEST_KEY_PRIVATE_EXTENSION : CRYPTO_TEST_KEY_PUBLIC_EXTENSION
	);

	size_t size = 0;
	char *pem = crypto_test_file_read (filename, &size);

	CerverCryptoKey *key = cerver_crypto_key_create_pem (pem, size, is_private, error);

	free (pem);

	return key;

}

CerverCryptoKey *crypto_test_key_get (const char *name, const bool is_private) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = crypto_test_key_load (name, is_private, &error);

	test_check (key != NULL, name);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, name);

	return key;

}

static unsigned int crypto_test_hex_value (const char character) {

	return (character >= 'a') ?
		(unsigned int) (character - 'a') + CRYPTO_TEST_HEX_LETTER :
		(unsigned int) (character - '0');

}

size_t crypto_test_hex_decode (const char *hex, u8 *bytes, const size_t capacity) {

	const size_t size = strlen (hex) / CRYPTO_TEST_HEX_DIGITS;
	test_check (size <= capacity, "the buffer of the test is too small for the hex");

	for (size_t i = 0; i < size; i++) {
		bytes[i] = (u8) (
			(crypto_test_hex_value (hex[i * CRYPTO_TEST_HEX_DIGITS]) << CRYPTO_TEST_HEX_SHIFT)
			| crypto_test_hex_value (hex[(i * CRYPTO_TEST_HEX_DIGITS) + 1])
		);
	}

	return size;

}

size_t crypto_test_base64url_decode (const char *encoded, u8 *bytes, const size_t capacity) {

	const size_t length = strlen (encoded);
	test_check (base64url_decode_length (length) <= capacity, "the buffer of the test is too small for the base64url");

	size_t size = 0;
	test_check_unsigned_eq (base64url_decode (bytes, &size, encoded, length), 0, encoded);

	return size;

}
