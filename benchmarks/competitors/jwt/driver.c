#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../measure.h"
#include "backend.h"

// the keys of the tests of cerver, made with the openssl tool
#define JWT_KEYS_PATH						"test/crypto/keys/"

// the most bytes of the file of a key and of a token
#define JWT_KEY_SIZE						4096
#define JWT_TOKEN_SIZE						4096

// the secret of the HMAC, with the bytes of the digest of SHA-512
#define JWT_SECRET							"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

// the claims of a session: who it is, who issued it and until when; their keys are sorted
#define JWT_CLAIMS							\
	"{\"exp\":4102444800,\"iat\":1700000000,\"id\":\"user-123\",\"iss\":\"cerver\",\"role\":\"admin\"}"

static const char *jwt_module = { "jwt" };

typedef struct JwtCase {

	// the "alg" of the tokens, that is also the name of the case
	const char *algorithm;

	// the files of the keys, NULL for the secret
	const char *private_file;
	const char *public_file;

} JwtCase;

static const JwtCase jwt_cases[] = {
	{ "HS256", NULL, NULL },
	{ "HS512", NULL, NULL },
	{ "RS256", JWT_KEYS_PATH "rsa.key", JWT_KEYS_PATH "rsa.pub" },
	{ "PS256", JWT_KEYS_PATH "rsa.key", JWT_KEYS_PATH "rsa.pub" },
	{ "ES256", JWT_KEYS_PATH "p256.key", JWT_KEYS_PATH "p256.pub" },
	{ "ES384", JWT_KEYS_PATH "p384.key", JWT_KEYS_PATH "p384.pub" },
	{ "EdDSA", JWT_KEYS_PATH "ed25519.key", JWT_KEYS_PATH "ed25519.pub" }
};

// the token that is verified, signed once by the implementation
static char jwt_token[JWT_TOKEN_SIZE] = { 0 };
static size_t jwt_token_length = 0;

// the whole file of a key
static size_t jwt_key_read (const char *filename, char key[JWT_KEY_SIZE]) {

	size_t size = 0;

	FILE *file = fopen (filename, "rb");
	if (file) {
		size = fread (key, sizeof (char), JWT_KEY_SIZE - 1, file);
		key[size] = '\0';

		(void) fclose (file);
	}

	else {
		(void) fprintf (stderr, "the key %s can not be read\n", filename);
	}

	return size;

}

// the keys of a case in the implementation
static bool jwt_case_init (const JwtCase *jwt_case) {

	if (!jwt_case->private_file) {
		return jwt_backend_init (
			jwt_case->algorithm, JWT_SECRET, sizeof (JWT_SECRET) - 1, JWT_SECRET, sizeof (JWT_SECRET) - 1
		);
	}

	char private_key[JWT_KEY_SIZE] = { 0 };
	char public_key[JWT_KEY_SIZE] = { 0 };

	const size_t private_size = jwt_key_read (jwt_case->private_file, private_key);
	const size_t public_size = jwt_key_read (jwt_case->public_file, public_key);

	return private_size && public_size && jwt_backend_init (
		jwt_case->algorithm, private_key, private_size, public_key, public_size
	);

}

static bool jwt_work_sign (void *data) {

	(void) data;

	char token[JWT_TOKEN_SIZE];

	return jwt_backend_sign (JWT_CLAIMS, sizeof (JWT_CLAIMS) - 1, token, JWT_TOKEN_SIZE) > 0;

}

static bool jwt_work_verify (void *data) {

	(void) data;

	return jwt_backend_verify (jwt_token, jwt_token_length);

}

// a token of the implementation that it verifies, and that it does not once one of its claims changed
static bool jwt_case_check (const JwtCase *jwt_case) {

	jwt_token_length = jwt_backend_sign (JWT_CLAIMS, sizeof (JWT_CLAIMS) - 1, jwt_token, JWT_TOKEN_SIZE);

	bool succeeded = jwt_token_length && jwt_backend_verify (jwt_token, jwt_token_length);
	if (succeeded) {
		char changed[JWT_TOKEN_SIZE] = { 0 };
		(void) memcpy (changed, jwt_token, jwt_token_length);

		char *claims = strchr (changed, '.') + 2;
		*claims = (*claims == 'A') ? 'B' : 'A';

		succeeded = !jwt_backend_verify (changed, jwt_token_length);
	}

	if (!succeeded) {
		(void) fprintf (stderr, "%s did not sign and verify with %s\n", jwt_backend_name, jwt_case->algorithm);
	}

	return succeeded;

}

static bool jwt_measure (const JwtCase *jwt_case) {

	// an implementation without the algorithm has no lines of it
	if (!jwt_case_init (jwt_case)) return true;

	bool succeeded = jwt_case_check (jwt_case);

	MeasureResult result;
	if (succeeded) {
		succeeded = measure_run (jwt_work_sign, NULL, jwt_token_length, &result);
		if (succeeded) measure_print (jwt_module, jwt_case->algorithm, jwt_backend_name, "sign", &result);
	}

	if (succeeded) {
		succeeded = measure_run (jwt_work_verify, NULL, jwt_token_length, &result);
		if (succeeded) measure_print (jwt_module, jwt_case->algorithm, jwt_backend_name, "verify", &result);
	}

	jwt_backend_end ();

	return succeeded;

}

int main (void) {

	bool succeeded = true;

	const size_t count = sizeof (jwt_cases) / sizeof (JwtCase);
	for (size_t i = 0; succeeded && (i < count); i++) {
		succeeded = jwt_measure (&jwt_cases[i]);
	}

	return succeeded ? 0 : 1;

}
