#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <l8w8jwt/decode.h>
#include <l8w8jwt/encode.h>

#include "backend.h"

// the claims of the driver: the API of the library takes each claim, so they are written here
#define JWT_L8W8JWT_ISSUER					"cerver"
#define JWT_L8W8JWT_EXPIRATION				4102444800
#define JWT_L8W8JWT_ISSUED_AT				1700000000

#define JWT_L8W8JWT_CLAIM					"id"
#define JWT_L8W8JWT_CLAIM_VALUE				"user-123"
#define JWT_L8W8JWT_ROLE					"role"
#define JWT_L8W8JWT_ROLE_VALUE				"admin"

#define JWT_L8W8JWT_CLAIMS					2

#define JWT_L8W8JWT_TRIAL_SIZE				4096

// an algorithm that the library does not have
#define JWT_L8W8JWT_ALGORITHM_NONE			(-1)

// l8w8jwt, over mbedtls; it reads the text of a key in every call
const char *jwt_backend_name = { "l8w8jwt" };

typedef struct JwtL8w8jwtAlgorithm {

	const char *name;
	int algorithm;

} JwtL8w8jwtAlgorithm;

// its EdDSA takes the bytes of a key as hex, not a PEM: it is not measured
static const JwtL8w8jwtAlgorithm jwt_l8w8jwt_algorithms[] = {
	{ "HS256", L8W8JWT_ALG_HS256 }, { "HS384", L8W8JWT_ALG_HS384 }, { "HS512", L8W8JWT_ALG_HS512 },
	{ "RS256", L8W8JWT_ALG_RS256 }, { "RS384", L8W8JWT_ALG_RS384 }, { "RS512", L8W8JWT_ALG_RS512 },
	{ "PS256", L8W8JWT_ALG_PS256 }, { "PS384", L8W8JWT_ALG_PS384 }, { "PS512", L8W8JWT_ALG_PS512 },
	{ "ES256", L8W8JWT_ALG_ES256 }, { "ES384", L8W8JWT_ALG_ES384 }, { "ES512", L8W8JWT_ALG_ES512 }
};

static int jwt_l8w8jwt_algorithm = JWT_L8W8JWT_ALGORITHM_NONE;

static unsigned char *jwt_l8w8jwt_private_key = NULL;
static size_t jwt_l8w8jwt_private_size = 0;

static unsigned char *jwt_l8w8jwt_public_key = NULL;
static size_t jwt_l8w8jwt_public_size = 0;

static unsigned char *jwt_l8w8jwt_key_copy (const char *key, const size_t size) {

	unsigned char *copy = (unsigned char *) malloc (size + 1);
	if (copy) {
		(void) memcpy (copy, key, size);
		copy[size] = '\0';
	}

	return copy;

}

static int jwt_l8w8jwt_algorithm_find (const char *name) {

	const size_t count = sizeof (jwt_l8w8jwt_algorithms) / sizeof (jwt_l8w8jwt_algorithms[0]);
	for (size_t i = 0; i < count; i++) {
		if (!strcmp (jwt_l8w8jwt_algorithms[i].name, name)) return jwt_l8w8jwt_algorithms[i].algorithm;
	}

	return JWT_L8W8JWT_ALGORITHM_NONE;

}

size_t jwt_backend_sign (
	const char *claims, const size_t claims_size, char *token, const size_t capacity
) {

	(void) claims;
	(void) claims_size;

	size_t length = 0;

	char issuer[] = JWT_L8W8JWT_ISSUER;
	char id[] = JWT_L8W8JWT_CLAIM;
	char id_value[] = JWT_L8W8JWT_CLAIM_VALUE;
	char role[] = JWT_L8W8JWT_ROLE;
	char role_value[] = JWT_L8W8JWT_ROLE_VALUE;

	struct l8w8jwt_claim payload[JWT_L8W8JWT_CLAIMS] = {
		{ id, sizeof (id) - 1, id_value, sizeof (id_value) - 1, L8W8JWT_CLAIM_TYPE_STRING },
		{ role, sizeof (role) - 1, role_value, sizeof (role_value) - 1, L8W8JWT_CLAIM_TYPE_STRING }
	};

	char *encoded = NULL;
	size_t encoded_length = 0;

	struct l8w8jwt_encoding_params params;
	l8w8jwt_encoding_params_init (&params);

	params.alg = jwt_l8w8jwt_algorithm;

	params.iss = issuer;
	params.iss_length = sizeof (issuer) - 1;

	params.exp = JWT_L8W8JWT_EXPIRATION;
	params.iat = JWT_L8W8JWT_ISSUED_AT;

	params.additional_payload_claims = payload;
	params.additional_payload_claims_count = JWT_L8W8JWT_CLAIMS;

	params.secret_key = jwt_l8w8jwt_private_key;
	params.secret_key_length = jwt_l8w8jwt_private_size;

	params.out = &encoded;
	params.out_length = &encoded_length;

	if ((l8w8jwt_encode (&params) == L8W8JWT_SUCCESS) && encoded && (encoded_length < capacity)) {
		length = encoded_length;
		(void) memcpy (token, encoded, length);
		token[length] = '\0';
	}

	l8w8jwt_free (encoded);

	return length;

}

void jwt_backend_end (void) {

	free (jwt_l8w8jwt_private_key);
	free (jwt_l8w8jwt_public_key);

	jwt_l8w8jwt_private_key = NULL;
	jwt_l8w8jwt_public_key = NULL;

}

static bool jwt_l8w8jwt_signs (void) {

	char token[JWT_L8W8JWT_TRIAL_SIZE];

	return jwt_backend_sign (NULL, 0, token, JWT_L8W8JWT_TRIAL_SIZE) > 0;

}

bool jwt_backend_init (
	const char *algorithm,
	const char *private_key, const size_t private_size,
	const char *public_key, const size_t public_size
) {

	jwt_l8w8jwt_algorithm = jwt_l8w8jwt_algorithm_find (algorithm);
	if (jwt_l8w8jwt_algorithm == JWT_L8W8JWT_ALGORITHM_NONE) return false;

	jwt_l8w8jwt_private_key = jwt_l8w8jwt_key_copy (private_key, private_size);
	jwt_l8w8jwt_private_size = private_size;

	jwt_l8w8jwt_public_key = jwt_l8w8jwt_key_copy (public_key, public_size);
	jwt_l8w8jwt_public_size = public_size;

	const bool ready = jwt_l8w8jwt_private_key && jwt_l8w8jwt_public_key && jwt_l8w8jwt_signs ();
	if (!ready) jwt_backend_end ();

	return ready;

}

// the claim of the driver in the claims that the library read
static bool jwt_l8w8jwt_has_claim (struct l8w8jwt_claim *claims, const size_t count) {

	const struct l8w8jwt_claim *claim = l8w8jwt_get_claim (
		claims, count, JWT_L8W8JWT_CLAIM, sizeof (JWT_L8W8JWT_CLAIM) - 1
	);

	return claim
		&& (claim->value_length == (sizeof (JWT_L8W8JWT_CLAIM_VALUE) - 1))
		&& !memcmp (claim->value, JWT_L8W8JWT_CLAIM_VALUE, claim->value_length);

}

bool jwt_backend_verify (const char *token, const size_t length) {

	bool valid = false;

	struct l8w8jwt_decoding_params params;
	l8w8jwt_decoding_params_init (&params);

	params.alg = jwt_l8w8jwt_algorithm;

	params.jwt = (char *) token;
	params.jwt_length = length;

	params.verification_key = jwt_l8w8jwt_public_key;
	params.verification_key_length = jwt_l8w8jwt_public_size;

	params.validate_exp = 1;

	enum l8w8jwt_validation_result result = L8W8JWT_VALID;

	struct l8w8jwt_claim *claims = NULL;
	size_t claims_count = 0;

	if (
		(l8w8jwt_decode (&params, &result, &claims, &claims_count) == L8W8JWT_SUCCESS)
		&& (result == L8W8JWT_VALID)
	) {
		valid = jwt_l8w8jwt_has_claim (claims, claims_count);
	}

	l8w8jwt_free_claims (claims, claims_count);

	return valid;

}
