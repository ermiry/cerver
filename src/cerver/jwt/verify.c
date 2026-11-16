#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/crypto/signature.h"

#include "cerver/json/document.h"
#include "cerver/json/json.h"
#include "cerver/json/value.h"

#include "cerver/jwt/claims.h"
#include "cerver/jwt/internal.h"
#include "cerver/jwt/jwt.h"
#include "cerver/jwt/verify.h"

#include "cerver/utils/base64.h"

// the members of a header that are read (RFC 7515 section 4.1)
#define JWT_HEADER_ALGORITHM				"alg"
#define JWT_HEADER_CRITICAL					"crit"

// the claims that a header can repeat (RFC 7519 section 5.3)
#define JWT_HEADER_ISSUER					"iss"
#define JWT_HEADER_SUBJECT					"sub"
#define JWT_HEADER_AUDIENCE					"aud"

// the three parts of a token, inside its characters
typedef struct JwtParts {

	const char *header;
	size_t header_length;

	const char *claims;
	size_t claims_length;

	const char *signature;
	size_t signature_length;

	// the characters that are signed: the header, its separator and the claims
	size_t signed_length;

} JwtParts;

#pragma region parts

// a token is three parts with characters, between two separators
static bool jwt_parts_split (
	const char *token, const size_t token_length, JwtParts *parts
) {

	const char *end = token + token_length;

	const char *first = (const char *) memchr (token, JWT_SEPARATOR, token_length);
	if (!first) return false;

	const char *second = (const char *) memchr (first + 1, JWT_SEPARATOR, (size_t) (end - (first + 1)));
	if (!second) return false;

	if (memchr (second + 1, JWT_SEPARATOR, (size_t) (end - (second + 1)))) return false;

	parts->header = token;
	parts->header_length = (size_t) (first - token);

	parts->claims = first + 1;
	parts->claims_length = (size_t) (second - (first + 1));

	parts->signature = second + 1;
	parts->signature_length = (size_t) (end - (second + 1));

	parts->signed_length = (size_t) (second - token);

	return parts->header_length && parts->claims_length && parts->signature_length;

}

// a part of a token that is a JSON: decoded into the arena with the bytes that the reader writes after it,
// and read inside its own text; a key more than once and a null character in a string are refused
static CerverJwtError jwt_part_read (
	Arena *arena, const char *part, const size_t part_length,
	const size_t depth, const CerverJwtError not_an_object,
	const CerverJsonValue **object
) {

	size_t length = 0;
	unsigned char *text = (unsigned char *) arena_alloc (
		arena, base64url_decode_length (part_length) + CERVER_JSON_PADDING
	);

	if (!text) return CERVER_JWT_ERROR_MEMORY;

	if (base64url_decode (text, &length, part, part_length)) return CERVER_JWT_ERROR_FORMAT;

	CerverJsonLimits limits = CERVER_ZERO_INITIALIZER;
	limits.depth = depth;

	CerverJsonError json_error = CERVER_ZERO_INITIALIZER;
	const CerverJsonValue *root = cerver_json_document_root (
		cerver_json_parse_in_situ (
			arena, (char *) text, length,
			CERVER_JSON_PARSE_REJECT_DUPLICATES | CERVER_JSON_PARSE_REJECT_NUL,
			&limits, &json_error
		)
	);

	if (json_error.code == CERVER_JSON_ERROR_MEMORY) return CERVER_JWT_ERROR_MEMORY;

	if (!cerver_json_is_object (root)) return not_an_object;

	*object = root;

	return CERVER_JWT_ERROR_NONE;

}

#pragma endregion

#pragma region header

// the "alg" of a header is exactly the string of the algorithm of the key: the token never chooses it
static bool jwt_header_algorithm_matches (
	const CerverJwtKey *key, const CerverJsonValue *algorithm
) {

	const char *expected = cerver_jwt_algorithm_to_string (key->algorithm);
	const size_t length = strlen (expected);

	return (cerver_json_string_length (algorithm) == length)
		&& !memcmp (cerver_json_string_value (algorithm), expected, length);

}

// the members of a header that was read
static CerverJwtError jwt_header_check (
	const CerverJwtKey *key, const CerverJsonValue *header
) {

	const CerverJsonValue *algorithm = cerver_json_object_get (header, JWT_HEADER_ALGORITHM);
	if (!cerver_json_is_string (algorithm)) return CERVER_JWT_ERROR_HEADER;

	if (!jwt_header_algorithm_matches (key, algorithm)) return CERVER_JWT_ERROR_ALGORITHM;

	// an extension that must be understood: none is
	if (cerver_json_object_get (header, JWT_HEADER_CRITICAL)) return CERVER_JWT_ERROR_HEADER;

	return CERVER_JWT_ERROR_NONE;

}

// the header of a token; the one that cerver writes for the algorithm of the key has nothing to read,
// and it leaves no header: any other is read in the arena
static CerverJwtError jwt_header_read (
	const CerverJwtKey *key, Arena *arena, const JwtParts *parts,
	const CerverJsonValue **header
) {

	if (
		(parts->header_length == key->header_length)
		&& !memcmp (parts->header, key->header, key->header_length)
	) {
		return CERVER_JWT_ERROR_NONE;
	}

	if (parts->header_length > CERVER_JWT_HEADER_SIZE_MAX) return CERVER_JWT_ERROR_SIZE;

	CerverJwtError error = jwt_part_read (
		arena, parts->header, parts->header_length,
		CERVER_JWT_HEADER_DEPTH, CERVER_JWT_ERROR_HEADER, header
	);

	if (error == CERVER_JWT_ERROR_NONE) error = jwt_header_check (key, *header);

	return error;

}

// a claim that the header repeats has the value of the one of the claims
static bool jwt_header_repeats (
	const CerverJsonValue *header, const CerverJsonValue *claims, const char *name
) {

	const CerverJsonValue *repeated = cerver_json_object_get (header, name);
	const CerverJsonValue *claim = cerver_json_object_get (claims, name);

	return !repeated || !claim || cerver_json_equal (repeated, claim);

}

static CerverJwtError jwt_header_check_repeated (
	const CerverJsonValue *header, const CerverJsonValue *claims
) {

	// the header that cerver writes repeats nothing
	if (!header) return CERVER_JWT_ERROR_NONE;

	return (
		jwt_header_repeats (header, claims, JWT_HEADER_ISSUER)
		&& jwt_header_repeats (header, claims, JWT_HEADER_SUBJECT)
		&& jwt_header_repeats (header, claims, JWT_HEADER_AUDIENCE)
	) ? CERVER_JWT_ERROR_NONE : CERVER_JWT_ERROR_HEADER;

}

#pragma endregion

#pragma region signature

// the third part of a token is the signature of the first two with the key
static CerverJwtError jwt_signature_check (
	const CerverJwtKey *key, const char *token, const JwtParts *parts
) {

	// a signature that is longer than the largest one is not decoded
	if (base64url_decode_length (parts->signature_length) > CERVER_CRYPTO_SIGNATURE_SIZE_MAX) {
		return CERVER_JWT_ERROR_SIGNATURE;
	}

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t signature_size = 0;
	if (base64url_decode (signature, &signature_size, parts->signature, parts->signature_length)) {
		return CERVER_JWT_ERROR_FORMAT;
	}

	return cerver_crypto_verify (
		key->key, key->signature, token, parts->signed_length, signature, signature_size
	) ? CERVER_JWT_ERROR_NONE : CERVER_JWT_ERROR_SIGNATURE;

}

#pragma endregion

#pragma region verify

// the most characters of a token: the limit of its validation, never over the one of every token
static size_t jwt_verify_size_max (const CerverJwtValidation *validation) {

	return (validation && validation->max_size && (validation->max_size < CERVER_JWT_SIZE_MAX)) ?
		validation->max_size : CERVER_JWT_SIZE_MAX;

}

// the claims of a token whose signature was verified, read and validated
static CerverJwtError jwt_verify_claims (
	Arena *arena, const JwtParts *parts,
	const CerverJsonValue *header, const CerverJwtValidation *validation,
	const CerverJsonValue **claims
) {

	CerverJwtError error = jwt_part_read (
		arena, parts->claims, parts->claims_length,
		CERVER_JWT_CLAIMS_DEPTH, CERVER_JWT_ERROR_CLAIMS, claims
	);

	if (error == CERVER_JWT_ERROR_NONE) error = jwt_header_check_repeated (header, *claims);

	if (error == CERVER_JWT_ERROR_NONE) error = cerver_jwt_claims_validate (*claims, validation);

	return error;

}

// the parts of a token in their order: its header, its signature and, only after it, its claims
static CerverJwtError jwt_verify_parts (
	const CerverJwtKey *key, Arena *arena,
	const char *token, const JwtParts *parts,
	const CerverJwtValidation *validation,
	const CerverJsonValue **claims
) {

	const CerverJsonValue *header = NULL;
	CerverJwtError error = jwt_header_read (key, arena, parts, &header);

	if (error == CERVER_JWT_ERROR_NONE) error = jwt_signature_check (key, token, parts);

	if (error == CERVER_JWT_ERROR_NONE) error = jwt_verify_claims (arena, parts, header, validation, claims);

	return error;

}

CerverJwtError cerver_jwt_verify (
	const CerverJwtKey *key, Arena *arena,
	const char *token, const size_t token_length,
	const CerverJwtValidation *validation,
	const CerverJsonValue **claims
) {

	if (claims) *claims = NULL;

	if (!key || !arena || !token) return CERVER_JWT_ERROR_ARGUMENTS;

	if (token_length > jwt_verify_size_max (validation)) return CERVER_JWT_ERROR_SIZE;

	JwtParts parts;
	if (!jwt_parts_split (token, token_length, &parts)) return CERVER_JWT_ERROR_FORMAT;

	const CerverJsonValue *verified = NULL;
	const CerverJwtError error = jwt_verify_parts (key, arena, token, &parts, validation, &verified);

	// the claims of a token that is not valid are never given
	if (claims && (error == CERVER_JWT_ERROR_NONE)) *claims = verified;

	return error;

}

#pragma endregion
