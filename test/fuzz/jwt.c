#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/http.h>
#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>
#include <cerver/http/jwt/alg.h>
#include <cerver/http/jwt/jwt.h>
#include <cerver/http/jwt/key.h>
#include <cerver/http/security/auth.h>

// the first byte chooses how the rest is decoded:
// the raw RS256 public key of the first API, an HS256 key that also encodes
// what it decodes and decodes it again, an ES256 key, and the bearer of a cerver

#define JWT_FUZZ_RSA_PUBLIC_KEY			"./test/http/keys/key.pub"
#define JWT_FUZZ_ES256_PUBLIC_KEY		"./test/jwt/keys/ec_key_prime256v1-pub.pem"
#define JWT_FUZZ_SECRET					"0123456789abcdef0123456789abcdef"

#define JWT_FUZZ_KEY_SIZE				(1 << 13)

typedef enum JwtFuzzMode {

	JWT_FUZZ_MODE_RAW		= 0,
	JWT_FUZZ_MODE_HMAC		= 1,
	JWT_FUZZ_MODE_ECDSA		= 2,
	JWT_FUZZ_MODE_BEARER	= 3,

	JWT_FUZZ_MODES			= 4

} JwtFuzzMode;

static unsigned char rsa_public_key[JWT_FUZZ_KEY_SIZE];
static size_t rsa_public_key_length = 0;

static JwtKey *hmac_key = NULL;
static JwtKey *es256_key = NULL;

static HttpCerver *http_cerver = NULL;

static size_t jwt_fuzz_file_read (
	const char *filename, unsigned char *buffer, const size_t size
) {

	FILE *file = fopen (filename, "rb");
	if (!file) abort ();

	const size_t length = fread (buffer, 1, size - 1, file);
	(void) fclose (file);

	if (!length) abort ();

	return length;

}

static void jwt_fuzz_keys_load (void) {

	if (rsa_public_key_length) return;

	rsa_public_key_length = jwt_fuzz_file_read (
		JWT_FUZZ_RSA_PUBLIC_KEY, rsa_public_key, sizeof (rsa_public_key)
	);

	hmac_key = jwt_key_create (
		JWT_ALG_HS256, JWT_KEY_TYPE_PRIVATE,
		(const unsigned char *) JWT_FUZZ_SECRET, strlen (JWT_FUZZ_SECRET), NULL
	);

	unsigned char es256_material[JWT_FUZZ_KEY_SIZE] = { 0 };
	const size_t es256_length = jwt_fuzz_file_read (
		JWT_FUZZ_ES256_PUBLIC_KEY, es256_material, sizeof (es256_material)
	);

	es256_key = jwt_key_create (
		JWT_ALG_ES256, JWT_KEY_TYPE_PUBLIC, es256_material, es256_length, NULL
	);

	// a cerver that verifies with the HS256 key, without starting it
	http_cerver = (HttpCerver *) http_cerver_new ();
	if (!hmac_key || !es256_key || !http_cerver) abort ();

	http_cerver->jwt_alg = JWT_ALG_HS256;
	http_cerver->jwt_public_key = hmac_key;

}

static void jwt_fuzz_raw (const char *token) {

	jwt_t *jwt = NULL;
	if (!jwt_decode (&jwt, token, rsa_public_key, rsa_public_key_length)) {
		jwt_valid_t *jwt_valid = NULL;
		if (!jwt_valid_new (&jwt_valid, JWT_ALG_RS256)) {
			(void) jwt_validate (jwt, jwt_valid);
			jwt_valid_free (jwt_valid);
		}

		jwt_free (jwt);
	}

}

// what is decoded is encoded with the same key and decoded again with the same grants
static void jwt_fuzz_hmac (const char *token, const size_t length) {

	jwt_t *jwt = NULL;
	if (jwt_decode_with_key (&jwt, token, length, hmac_key)) return;

	char *encoded = jwt_encode_with_key (jwt, hmac_key);
	if (!encoded) abort ();

	// the compact grants can need more memory for each byte than the ones with spaces
	json_set_parser_memory_factor (0);

	jwt_t *decoded = NULL;
	const int decode_result = jwt_decode_with_key (&decoded, encoded, strlen (encoded), hmac_key);

	json_set_parser_memory_factor (JSON_PARSER_DEFAULT_MEMORY_FACTOR);

	if (decode_result) abort ();
	if (!json_equal (jwt->grants, decoded->grants)) abort ();

	jwt_free (decoded);
	jwt_free_str (encoded);
	jwt_free (jwt);

}

static void jwt_fuzz_ecdsa (const char *token, const size_t length) {

	jwt_t *jwt = NULL;
	if (!jwt_decode_with_key (&jwt, token, length, es256_key)) jwt_free (jwt);

}

static void jwt_fuzz_bearer (const char *bearer) {

	void *decoded = NULL;
	if (http_cerver_auth_validate_jwt (http_cerver, bearer, http_decode_data_into_json, &decoded)) {
		free (decoded);
	}

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	jwt_fuzz_keys_load ();

	if (!size) return 0;

	const JwtFuzzMode mode = (JwtFuzzMode) (data[0] % JWT_FUZZ_MODES);
	const size_t length = size - 1;

	char *token = (char *) malloc (length + 1);
	if (!token) return 0;

	(void) memcpy (token, data + 1, length);
	token[length] = '\0';

	switch (mode) {
		case JWT_FUZZ_MODE_RAW: jwt_fuzz_raw (token); break;
		case JWT_FUZZ_MODE_HMAC: jwt_fuzz_hmac (token, length); break;
		case JWT_FUZZ_MODE_ECDSA: jwt_fuzz_ecdsa (token, length); break;
		case JWT_FUZZ_MODE_BEARER: jwt_fuzz_bearer (token); break;

		default: break;
	}

	free (token);

	return 0;

}
