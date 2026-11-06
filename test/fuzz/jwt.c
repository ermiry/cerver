#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <cerver/http/jwt/alg.h>
#include <cerver/http/jwt/jwt.h>

// a token decoded and validated with the public key of the HTTP tests

#define JWT_PUBLIC_KEY			"./test/http/keys/key.pub"
#define JWT_KEY_SIZE			(1 << 13)

static unsigned char public_key[JWT_KEY_SIZE];
static int public_key_len = 0;

static void jwt_fuzz_key_load (void) {

	if (public_key_len) return;

	FILE *file = fopen (JWT_PUBLIC_KEY, "rb");
	if (!file) abort ();

	public_key_len = (int) fread (public_key, 1, JWT_KEY_SIZE - 1, file);
	(void) fclose (file);

	if (public_key_len <= 0) abort ();

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	jwt_fuzz_key_load ();

	char *token = (char *) malloc (size + 1);
	if (!token) return 0;

	(void) memcpy (token, data, size);
	token[size] = '\0';

	jwt_t *jwt = NULL;
	if (!jwt_decode (&jwt, token, public_key, public_key_len)) {
		jwt_valid_t *jwt_valid = NULL;
		if (!jwt_valid_new (&jwt_valid, JWT_ALG_RS256)) {
			(void) jwt_validate (jwt, jwt_valid);
			jwt_valid_free (jwt_valid);
		}

		jwt_free (jwt);
	}

	free (token);

	return 0;

}
