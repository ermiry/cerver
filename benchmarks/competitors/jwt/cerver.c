#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <cerver/collections/arena.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/sign.h>
#include <cerver/jwt/verify.h>

#include "backend.h"

// the claim that the driver looks for and its value
#define JWT_CERVER_CLAIM					"id"
#define JWT_CERVER_CLAIM_VALUE				"user-123"

const char *jwt_backend_name = { "cerver" };

static CerverJwtKey *jwt_cerver_private_key = NULL;
static CerverJwtKey *jwt_cerver_public_key = NULL;

// the arena of the claims, reset for each token as the one of a request
static Arena *jwt_cerver_arena = NULL;

// the keys of an algorithm: a secret for the HMAC, the PEM of each key for the others
static CerverJwtKey *jwt_cerver_key (
	const CerverJwtAlgorithm algorithm, const char *key, const size_t size, const bool is_private
) {

	const bool is_secret = (algorithm >= CERVER_JWT_ALGORITHM_HS256) && (algorithm <= CERVER_JWT_ALGORITHM_HS512);

	return is_secret ?
		cerver_jwt_key_create_secret (algorithm, key, size, NULL) :
		cerver_jwt_key_create_pem (algorithm, key, size, is_private, NULL);

}

bool jwt_backend_init (
	const char *algorithm,
	const char *private_key, const size_t private_size,
	const char *public_key, const size_t public_size
) {

	const CerverJwtAlgorithm jwt_algorithm = cerver_jwt_algorithm_from_string (algorithm, strlen (algorithm));

	jwt_cerver_private_key = jwt_cerver_key (jwt_algorithm, private_key, private_size, true);
	jwt_cerver_public_key = jwt_cerver_key (jwt_algorithm, public_key, public_size, false);

	jwt_cerver_arena = arena_create (0);

	return jwt_cerver_private_key && jwt_cerver_public_key && jwt_cerver_arena;

}

size_t jwt_backend_sign (
	const char *claims, const size_t claims_size, char *token, const size_t capacity
) {

	size_t length = 0;

	(void) cerver_jwt_sign (jwt_cerver_private_key, claims, claims_size, token, capacity, &length);

	return length;

}

bool jwt_backend_verify (const char *token, const size_t length) {

	arena_reset (jwt_cerver_arena);

	const CerverJsonValue *claims = NULL;
	if (cerver_jwt_verify (jwt_cerver_public_key, jwt_cerver_arena, token, length, NULL, &claims) != CERVER_JWT_ERROR_NONE) {
		return false;
	}

	const CerverJsonValue *id = cerver_json_object_get (claims, JWT_CERVER_CLAIM);

	return cerver_json_is_string (id) && !strcmp (cerver_json_string_value (id), JWT_CERVER_CLAIM_VALUE);

}

void jwt_backend_end (void) {

	cerver_jwt_key_delete (jwt_cerver_private_key);
	cerver_jwt_key_delete (jwt_cerver_public_key);

	arena_delete (jwt_cerver_arena);

	jwt_cerver_private_key = NULL;
	jwt_cerver_public_key = NULL;
	jwt_cerver_arena = NULL;

}
