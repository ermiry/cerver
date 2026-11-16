#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>

#include <cerver/utils/base64.h>

#include "../test.h"
#include "../crypto/crypto.h"
#include "jwt.h"

// the path of a key of the tests with its extension
#define JWT_TEST_FILENAME_SIZE				128

// the separator of the parts of a token
#define JWT_TEST_SEPARATOR					'.'

char *jwt_test_file_read (const char *filename, size_t *size) {

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

// the text of the PEM of a key of the tests
static char *jwt_test_pem_read (const char *name, const bool is_private, size_t *size) {

	char filename[JWT_TEST_FILENAME_SIZE] = { 0 };
	(void) snprintf (
		filename, JWT_TEST_FILENAME_SIZE, "%s%s",
		name, is_private ? CRYPTO_TEST_KEY_PRIVATE_EXTENSION : CRYPTO_TEST_KEY_PUBLIC_EXTENSION
	);

	return jwt_test_file_read (filename, size);

}

CerverJwtKey *jwt_test_key_load (
	const CerverJwtAlgorithm algorithm, const char *name, const bool is_private, CerverJwtError *error
) {

	size_t size = 0;
	char *pem = jwt_test_pem_read (name, is_private, &size);

	CerverJwtKey *key = cerver_jwt_key_create_pem (algorithm, pem, size, is_private, error);

	free (pem);

	return key;

}

CerverJwtKey *jwt_test_key_pem (
	const CerverJwtAlgorithm algorithm, const char *name, const bool is_private
) {

	CerverJwtError error = CERVER_JWT_ERROR_MEMORY;
	CerverJwtKey *key = jwt_test_key_load (algorithm, name, is_private, &error);

	test_check (key != NULL, name);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, name);

	return key;

}

CerverJwtKey *jwt_test_key_jwk (const CerverJwtAlgorithm algorithm, const char *jwk) {

	CerverJwtError error = CERVER_JWT_ERROR_MEMORY;
	CerverJwtKey *key = cerver_jwt_key_create_jwk (algorithm, jwk, strlen (jwk), &error);

	test_check (key != NULL, jwk);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, jwk);

	return key;

}

CerverJwtKey *jwt_test_key_secret (const CerverJwtAlgorithm algorithm) {

	CerverJwtError error = CERVER_JWT_ERROR_MEMORY;
	CerverJwtKey *key = cerver_jwt_key_create_secret (algorithm, JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), &error);

	test_check_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_NONE, NULL);

	return key;

}

CerverCryptoKey *jwt_test_crypto_pem (const char *name, const bool is_private) {

	size_t size = 0;
	char *pem = jwt_test_pem_read (name, is_private, &size);

	CerverCryptoKey *key = cerver_crypto_key_create_pem (pem, size, is_private, NULL);
	test_check (key != NULL, name);

	free (pem);

	return key;

}

// the first two parts of a token: any header and any payload, each one in base64url
// returns the token with room for a signature of the largest size, and its characters
static char *jwt_test_signed_parts (
	const char *header, const void *payload, const size_t payload_size, size_t *length
) {

	const size_t header_size = strlen (header);

	char *token = (char *) calloc (
		base64url_encode_length (header_size) + base64url_encode_length (payload_size)
		+ base64url_encode_length (CERVER_CRYPTO_SIGNATURE_SIZE_MAX) + sizeof ("..") ,
		sizeof (char)
	);

	test_check_ptr (token);

	size_t position = base64url_encode (token, (const unsigned char *) header, header_size);
	token[position++] = JWT_TEST_SEPARATOR;
	position += base64url_encode (token + position, (const unsigned char *) payload, payload_size);

	*length = position;

	return token;

}

char *jwt_test_join (
	const char *header, const void *payload, const size_t payload_size,
	const u8 *signature, const size_t signature_size
) {

	test_check (signature_size <= CERVER_CRYPTO_SIGNATURE_SIZE_MAX, "the signature of the test is too long");

	size_t position = 0;
	char *token = jwt_test_signed_parts (header, payload, payload_size, &position);

	token[position++] = JWT_TEST_SEPARATOR;
	(void) base64url_encode (token + position, signature, signature_size);

	return token;

}

char *jwt_test_forge (
	const CerverCryptoKey *key, const CerverCryptoSignature signature,
	const char *header, const void *payload, const size_t payload_size
) {

	size_t position = 0;
	char *token = jwt_test_signed_parts (header, payload, payload_size, &position);

	u8 bytes[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	size_t size = 0;
	test_check (
		cerver_crypto_sign (key, signature, token, position, bytes, sizeof (bytes), &size), header
	);

	token[position++] = JWT_TEST_SEPARATOR;
	(void) base64url_encode (token + position, bytes, size);

	return token;

}

char *jwt_test_forge_secret (const char *header, const char *payload) {

	CerverCryptoKey *key = cerver_crypto_key_create_secret (JWT_TEST_SECRET, strlen (JWT_TEST_SECRET), NULL);
	test_check_ptr (key);

	char *token = jwt_test_forge (key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, header, payload, strlen (payload));

	cerver_crypto_key_delete (key);

	return token;

}
