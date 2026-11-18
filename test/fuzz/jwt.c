#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/http/core/http.h>
#include "cerver/http/core/types.h"
#include <cerver/http/security/auth.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/sign.h>
#include <cerver/jwt/verify.h>

#include <cerver/utils/base64.h>

// the first byte chooses how the rest is taken:
// a token for the RS256 public key of the first API; one for an HS256 key, whose claims are signed again and
// verified again when it is valid; one for an ES256 key; the bearer of a cerver; and a header and a payload
// that are signed with the secret, so what is after the signature is reached

#define JWT_FUZZ_RSA_PUBLIC_KEY			"./test/http/keys/key.pub"
#define JWT_FUZZ_ES256_PUBLIC_KEY		"./test/crypto/keys/p256.pub"
#define JWT_FUZZ_SECRET					"0123456789abcdef0123456789abcdef"

#define JWT_FUZZ_KEY_SIZE				(1 << 13)

// the time of every validation: a token is valid or not whatever the clock says
#define JWT_FUZZ_NOW					1700000000

// the first byte of an input is its mode from this character: the seeds of the corpus start with a digit
#define JWT_FUZZ_MODE_FIRST				'0'

// what separates the header of the payload of a forged token
#define JWT_FUZZ_FORGE_SEPARATOR		'\n'

#define JWT_FUZZ_TOKEN_SEPARATOR		'.'

typedef enum JwtFuzzMode {

	JWT_FUZZ_MODE_RAW		= 0,
	JWT_FUZZ_MODE_HMAC		= 1,
	JWT_FUZZ_MODE_ECDSA		= 2,
	JWT_FUZZ_MODE_BEARER	= 3,
	JWT_FUZZ_MODE_FORGE		= 4,

	JWT_FUZZ_MODES			= 5

} JwtFuzzMode;

static CerverJwtKey *rsa_key = NULL;
static CerverJwtKey *hmac_key = NULL;
static CerverJwtKey *es256_key = NULL;

// the secret as a key of the provider, to sign what the key of the tokens never would
static CerverCryptoKey *secret_key = NULL;

static HttpCerver *http_cerver = NULL;

static Arena *arena = NULL;

static CerverJwtValidation validation;

static CerverJwtKey *jwt_fuzz_key_load (const CerverJwtAlgorithm algorithm, const char *filename) {

	static char pem[JWT_FUZZ_KEY_SIZE];

	FILE *file = fopen (filename, "rb");
	if (!file) abort ();

	const size_t length = fread (pem, 1, sizeof (pem), file);
	(void) fclose (file);

	CerverJwtKey *key = cerver_jwt_key_create_pem (algorithm, pem, length, false, NULL);
	if (!key) abort ();

	return key;

}

static void jwt_fuzz_keys_load (void) {

	if (rsa_key) return;

	rsa_key = jwt_fuzz_key_load (CERVER_JWT_ALGORITHM_RS256, JWT_FUZZ_RSA_PUBLIC_KEY);
	es256_key = jwt_fuzz_key_load (CERVER_JWT_ALGORITHM_ES256, JWT_FUZZ_ES256_PUBLIC_KEY);

	hmac_key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, JWT_FUZZ_SECRET, strlen (JWT_FUZZ_SECRET), NULL
	);

	secret_key = cerver_crypto_key_create_secret (JWT_FUZZ_SECRET, strlen (JWT_FUZZ_SECRET), NULL);

	arena = arena_create (0);

	// a cerver that verifies with the HS256 key, without starting it
	http_cerver = (HttpCerver *) cerver_http_new ();
	if (!hmac_key || !secret_key || !arena || !http_cerver) abort ();

	http_cerver->jwt_alg = CERVER_JWT_ALGORITHM_HS256;
	http_cerver->jwt_public_key = hmac_key;

	(void) memset (&validation, 0, sizeof (validation));
	validation.now = JWT_FUZZ_NOW;

}

// a token for a key: its claims are there only when it is valid
static const CerverJsonValue *jwt_fuzz_verify (
	const CerverJwtKey *key, const char *token, const size_t length
) {

	const CerverJsonValue *claims = NULL;
	const CerverJwtError error = cerver_jwt_verify (key, arena, token, length, &validation, &claims);

	if ((error == CERVER_JWT_ERROR_NONE) != (claims != NULL)) abort ();
	if (claims && !cerver_json_is_object (claims)) abort ();

	return claims;

}

// the claims of a valid token are signed again with the same key and verified again: the same claims
static void jwt_fuzz_sign_again (const CerverJsonValue *claims) {

	const size_t claims_size = cerver_json_write (claims, NULL, 0, CERVER_JSON_WRITE_COMPACT);
	if (!claims_size) abort ();

	// claims that are too long have no token
	const size_t capacity = cerver_jwt_sign_size (hmac_key, claims_size);
	if (!capacity) return;

	char *text = (char *) malloc (claims_size + 1);
	char *token = (char *) malloc (capacity);
	if (!text || !token) abort ();

	(void) cerver_json_write (claims, text, claims_size + 1, CERVER_JSON_WRITE_COMPACT);

	size_t length = 0;
	if (cerver_jwt_sign (hmac_key, text, claims_size, token, capacity, &length) != CERVER_JWT_ERROR_NONE) abort ();
	if ((length + 1) != capacity) abort ();

	const CerverJsonValue *again = jwt_fuzz_verify (hmac_key, token, length);
	if (!again || !cerver_json_equal (claims, again)) abort ();

	free (token);
	free (text);

}

static void jwt_fuzz_hmac (const char *token, const size_t length) {

	const CerverJsonValue *claims = jwt_fuzz_verify (hmac_key, token, length);

	if (claims) jwt_fuzz_sign_again (claims);

}

static void jwt_fuzz_bearer (const char *bearer) {

	void *decoded = NULL;
	if (cerver_http_auth_validate_jwt (http_cerver, bearer, http_decode_data_into_json, &decoded)) {
		free (decoded);
	}

}

// a header and a payload with the HMAC of the secret: any header and any claims with a valid signature
static void jwt_fuzz_forge (const char *text, const size_t length) {

	const char *separator = (const char *) memchr (text, JWT_FUZZ_FORGE_SEPARATOR, length);
	if (!separator) return;

	const size_t header_size = (size_t) (separator - text);
	const size_t payload_size = length - header_size - 1;

	char *token = (char *) malloc (
		base64url_encode_length (header_size) + base64url_encode_length (payload_size)
		+ base64url_encode_length (CERVER_CRYPTO_SIGNATURE_SIZE_MAX) + sizeof ("..")
	);

	if (!token) abort ();

	size_t position = base64url_encode (token, (const unsigned char *) text, header_size);
	token[position++] = JWT_FUZZ_TOKEN_SEPARATOR;
	position += base64url_encode (token + position, (const unsigned char *) separator + 1, payload_size);

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t signature_size = 0;
	if (
		!cerver_crypto_sign (
			secret_key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, token, position,
			signature, sizeof (signature), &signature_size
		)
	) {
		abort ();
	}

	token[position++] = JWT_FUZZ_TOKEN_SEPARATOR;
	position += base64url_encode (token + position, signature, signature_size);

	jwt_fuzz_hmac (token, position);

	free (token);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	jwt_fuzz_keys_load ();

	if (!size) return 0;

	const JwtFuzzMode mode = (JwtFuzzMode) ((uint8_t) (data[0] - JWT_FUZZ_MODE_FIRST) % JWT_FUZZ_MODES);
	const size_t length = size - 1;

	char *token = (char *) malloc (length + 1);
	if (!token) return 0;

	(void) memcpy (token, data + 1, length);
	token[length] = '\0';

	arena_reset (arena);

	switch (mode) {
		case JWT_FUZZ_MODE_RAW: (void) jwt_fuzz_verify (rsa_key, token, length); break;
		case JWT_FUZZ_MODE_HMAC: jwt_fuzz_hmac (token, length); break;
		case JWT_FUZZ_MODE_ECDSA: (void) jwt_fuzz_verify (es256_key, token, length); break;
		case JWT_FUZZ_MODE_BEARER: jwt_fuzz_bearer (token); break;
		case JWT_FUZZ_MODE_FORGE: jwt_fuzz_forge (token, length); break;

		default: break;
	}

	free (token);

	return 0;

}
