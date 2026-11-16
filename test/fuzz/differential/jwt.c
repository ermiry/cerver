#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jwt.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/verify.h>

#include <cerver/utils/base64.h>

// the JWT of cerver against the libjwt of the system (the one that the JWT of the 2.x came from), only where
// it is installed (Dockerfile.fuzz): every token goes to the two of them with the same HS256 secret and the
// same time. A token that cerver takes is one that libjwt takes, with the same claims; a token that only
// libjwt takes was refused by a rule that cerver adds, and never by its signature or its algorithm

#define DIFFERENTIAL_SECRET				"0123456789abcdef0123456789abcdef"

#define DIFFERENTIAL_NOW				1700000000

#define DIFFERENTIAL_FORGE_SEPARATOR	'\n'
#define DIFFERENTIAL_TOKEN_SEPARATOR	'.'

// libjwt checks the claims that the header of a token repeats
#define DIFFERENTIAL_HEADERS			1

typedef enum DifferentialMode {

	DIFFERENTIAL_MODE_TOKEN		= 0,
	DIFFERENTIAL_MODE_FORGE		= 1,

	DIFFERENTIAL_MODES			= 2

} DifferentialMode;

static CerverJwtKey *differential_key = NULL;
static CerverCryptoKey *differential_secret = NULL;

static Arena *differential_arena = NULL;

static void differential_init (void) {

	if (differential_key) return;

	differential_key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, DIFFERENTIAL_SECRET, strlen (DIFFERENTIAL_SECRET), NULL
	);

	differential_secret = cerver_crypto_key_create_secret (DIFFERENTIAL_SECRET, strlen (DIFFERENTIAL_SECRET), NULL);

	differential_arena = arena_create (0);

	if (!differential_key || !differential_secret || !differential_arena) abort ();

}

// what libjwt checks of a token that it decoded: its algorithm, the claims that its header repeats and its times
static bool differential_library_validates (jwt_t *jwt) {

	bool valid = false;

	jwt_valid_t *jwt_valid = NULL;
	if (!jwt_valid_new (&jwt_valid, JWT_ALG_HS256)) {
		(void) jwt_valid_set_headers (jwt_valid, DIFFERENTIAL_HEADERS);
		(void) jwt_valid_set_now (jwt_valid, DIFFERENTIAL_NOW);

		valid = (jwt_validate (jwt, jwt_valid) == JWT_VALIDATION_SUCCESS);

		jwt_valid_free (jwt_valid);
	}

	return valid;

}

// the token as libjwt takes it, with the secret as the key of its HMAC
// returns its token when it is valid, that the caller frees
static jwt_t *differential_library (const char *token) {

	jwt_t *jwt = NULL;
	if (
		jwt_decode (
			&jwt, token, (const unsigned char *) DIFFERENTIAL_SECRET, (int) strlen (DIFFERENTIAL_SECRET)
		)
	) {
		return NULL;
	}

	if (!differential_library_validates (jwt)) {
		jwt_free (jwt);
		jwt = NULL;
	}

	return jwt;

}

// the errors of the rules that only the JWT of cerver has: libjwt takes a token that breaks one of them
static bool differential_is_own_rule (const CerverJwtError error) {

	bool is_own = false;

	switch (error) {
		// a header with crit, with a key more than once or with more levels; and a claim that it repeats
		// with another type
		case CERVER_JWT_ERROR_HEADER:
		// a time that is not an integer, claims with more levels, with a key more than once or with a NUL
		case CERVER_JWT_ERROR_CLAIMS:
		// a time that is a real, or one of -1, is not checked by libjwt
		case CERVER_JWT_ERROR_EXPIRED:
		case CERVER_JWT_ERROR_NOT_BEFORE:
		// "iat" is never checked by libjwt
		case CERVER_JWT_ERROR_ISSUED_AT:
		// base64url that is not canonical in a part that libjwt does not check, and the longer header
		case CERVER_JWT_ERROR_FORMAT:
		case CERVER_JWT_ERROR_SIZE:
			is_own = true;
			break;

		default: break;
	}

	return is_own;

}

// whether a value has, at any level, an integer that only fits in 64 bits without sign:
// the JSON of cerver reads it and Jansson, the JSON of libjwt, refuses its whole text
static bool differential_has_unsigned (const CerverJsonValue *value) {

	bool found = cerver_json_type (value) == CERVER_JSON_TYPE_UNSIGNED;

	if (cerver_json_is_array (value)) {
		CerverJsonArrayIterator iterator = cerver_json_array_iterator (value);
		const CerverJsonValue *element = NULL;
		while (!found && (element = cerver_json_array_iterator_next (&iterator))) {
			found = differential_has_unsigned (element);
		}
	}

	else if (cerver_json_is_object (value)) {
		CerverJsonObjectIterator iterator = cerver_json_object_iterator (value);
		const CerverJsonValue *key = NULL;
		const CerverJsonValue *member = NULL;
		while (!found && cerver_json_object_iterator_next (&iterator, &key, &member)) {
			found = differential_has_unsigned (member);
		}
	}

	return found;

}

// whether the header of a token, its first part, has one of those integers: cerver does not give the header
// of a token, so it is read again here
static bool differential_header_has_unsigned (const char *token, const size_t length) {

	bool found = false;

	const char *separator = (const char *) memchr (token, DIFFERENTIAL_TOKEN_SEPARATOR, length);
	const size_t header_length = separator ? (size_t) (separator - token) : 0;

	size_t size = 0;
	unsigned char *header = (unsigned char *) malloc (base64url_decode_length (header_length) + 1);
	if (!header) abort ();

	if (header_length && !base64url_decode (header, &size, token, header_length)) {
		CerverJsonDocument *document = cerver_json_parse (
			NULL, (const char *) header, size, CERVER_JSON_FLAGS_NONE, NULL, NULL
		);

		found = differential_has_unsigned (cerver_json_document_root (document));

		cerver_json_document_delete (document);
	}

	free (header);

	return found;

}

// the claims that libjwt read are the ones that cerver read
static void differential_claims (jwt_t *jwt, const CerverJsonValue *claims) {

	char *grants = jwt_get_grants_json (jwt, NULL);
	if (!grants) abort ();

	CerverJsonDocument *document = cerver_json_parse (
		NULL, grants, strlen (grants), CERVER_JSON_PARSE_BIG_AS_REAL, NULL, NULL
	);

	if (!document) abort ();

	if (!cerver_json_equal (claims, cerver_json_document_root (document))) {
		(void) fprintf (stderr, "the claims differ: %s\n", grants);
		abort ();
	}

	cerver_json_document_delete (document);

	jwt_free_str (grants);

}

// the token goes to both; libjwt reads a string, so a token with a null byte inside only goes to cerver
static void differential_token (const char *token, const size_t length) {

	CerverJwtValidation validation;
	(void) memset (&validation, 0, sizeof (validation));
	validation.now = DIFFERENTIAL_NOW;

	const CerverJsonValue *claims = NULL;
	const CerverJwtError error = cerver_jwt_verify (
		differential_key, differential_arena, token, length, &validation, &claims
	);

	if (strlen (token) != length) return;

	jwt_t *jwt = differential_library (token);

	// what cerver takes, libjwt takes: all but a header or claims with a number that only the JSON of cerver reads
	if (
		(error == CERVER_JWT_ERROR_NONE) && !jwt
		&& !differential_has_unsigned (claims) && !differential_header_has_unsigned (token, length)
	) {
		(void) fprintf (stderr, "only cerver takes: %.*s\n", (int) length, token);
		abort ();
	}

	// what only libjwt takes broke a rule that cerver adds
	if (jwt && (error != CERVER_JWT_ERROR_NONE) && !differential_is_own_rule (error)) {
		(void) fprintf (
			stderr, "only libjwt takes (%s): %.*s\n", cerver_jwt_error_to_string (error), (int) length, token
		);

		abort ();
	}

	if (jwt && (error == CERVER_JWT_ERROR_NONE)) differential_claims (jwt, claims);

	jwt_free (jwt);

}

// a header and a payload with the HMAC of the secret
static void differential_forge (const char *text, const size_t length) {

	const char *separator = (const char *) memchr (text, DIFFERENTIAL_FORGE_SEPARATOR, length);
	if (!separator) return;

	const size_t header_size = (size_t) (separator - text);
	const size_t payload_size = length - header_size - 1;

	char *token = (char *) malloc (
		base64url_encode_length (header_size) + base64url_encode_length (payload_size)
		+ base64url_encode_length (CERVER_CRYPTO_SIGNATURE_SIZE_MAX) + sizeof ("..")
	);

	if (!token) abort ();

	size_t position = base64url_encode (token, (const unsigned char *) text, header_size);
	token[position++] = DIFFERENTIAL_TOKEN_SEPARATOR;
	position += base64url_encode (token + position, (const unsigned char *) separator + 1, payload_size);

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t signature_size = 0;
	if (
		!cerver_crypto_sign (
			differential_secret, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, token, position,
			signature, sizeof (signature), &signature_size
		)
	) {
		abort ();
	}

	token[position++] = DIFFERENTIAL_TOKEN_SEPARATOR;
	position += base64url_encode (token + position, signature, signature_size);

	differential_token (token, position);

	free (token);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	differential_init ();

	if (!size) return 0;

	const DifferentialMode mode = (DifferentialMode) (data[0] % DIFFERENTIAL_MODES);
	const size_t length = size - 1;

	char *text = (char *) malloc (length + 1);
	if (!text) return 0;

	(void) memcpy (text, data + 1, length);
	text[length] = '\0';

	arena_reset (differential_arena);

	if (mode == DIFFERENTIAL_MODE_TOKEN) differential_token (text, length);
	else differential_forge (text, length);

	free (text);

	return 0;

}
