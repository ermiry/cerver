#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jwt.h>

#include "backend.h"

#define JWT_LIBJWT_CLAIM					"id"
#define JWT_LIBJWT_CLAIM_VALUE				"user-123"

// the claims that a header repeats are checked
#define JWT_LIBJWT_HEADERS					1

// the claims of the token that tells whether the library signs with the algorithm
#define JWT_LIBJWT_TRIAL_CLAIMS				"{\"id\":\"user-123\"}"
#define JWT_LIBJWT_TRIAL_SIZE				4096

// libjwt, the library that the JWT of the 2.x came from: it reads the text of a key in every call
const char *jwt_backend_name = { "libjwt" };

static jwt_alg_t jwt_libjwt_algorithm = JWT_ALG_NONE;

static unsigned char *jwt_libjwt_private_key = NULL;
static int jwt_libjwt_private_size = 0;

static unsigned char *jwt_libjwt_public_key = NULL;
static int jwt_libjwt_public_size = 0;

static unsigned char *jwt_libjwt_key_copy (const char *key, const size_t size) {

	unsigned char *copy = (unsigned char *) malloc (size + 1);
	if (copy) {
		(void) memcpy (copy, key, size);
		copy[size] = '\0';
	}

	return copy;

}

size_t jwt_backend_sign (
	const char *claims, const size_t claims_size, char *token, const size_t capacity
) {

	(void) claims_size;

	size_t length = 0;

	jwt_t *jwt = NULL;
	if (
		!jwt_new (&jwt)
		&& !jwt_add_grants_json (jwt, claims)
		&& !jwt_set_alg (jwt, jwt_libjwt_algorithm, jwt_libjwt_private_key, jwt_libjwt_private_size)
	) {
		char *encoded = jwt_encode_str (jwt);
		if (encoded && (strlen (encoded) < capacity)) {
			length = strlen (encoded);
			(void) memcpy (token, encoded, length + 1);
		}

		jwt_free_str (encoded);
	}

	jwt_free (jwt);

	return length;

}

void jwt_backend_end (void) {

	free (jwt_libjwt_private_key);
	free (jwt_libjwt_public_key);

	jwt_libjwt_private_key = NULL;
	jwt_libjwt_public_key = NULL;

}

// the library has the name of an algorithm that it can not sign with its build
static bool jwt_libjwt_signs (void) {

	char token[JWT_LIBJWT_TRIAL_SIZE];

	return jwt_backend_sign (
		JWT_LIBJWT_TRIAL_CLAIMS, sizeof (JWT_LIBJWT_TRIAL_CLAIMS) - 1, token, JWT_LIBJWT_TRIAL_SIZE
	) > 0;

}

bool jwt_backend_init (
	const char *algorithm,
	const char *private_key, const size_t private_size,
	const char *public_key, const size_t public_size
) {

	jwt_libjwt_algorithm = jwt_str_alg (algorithm);
	if (jwt_libjwt_algorithm == JWT_ALG_INVAL) return false;

	jwt_libjwt_private_key = jwt_libjwt_key_copy (private_key, private_size);
	jwt_libjwt_private_size = (int) private_size;

	jwt_libjwt_public_key = jwt_libjwt_key_copy (public_key, public_size);
	jwt_libjwt_public_size = (int) public_size;

	const bool ready = jwt_libjwt_private_key && jwt_libjwt_public_key && jwt_libjwt_signs ();
	if (!ready) jwt_backend_end ();

	return ready;

}

// the claims of a token that was decoded: its algorithm and its times against the clock, and the claim
static bool jwt_libjwt_validate (jwt_t *jwt) {

	bool valid = false;

	jwt_valid_t *jwt_valid = NULL;
	if (!jwt_valid_new (&jwt_valid, jwt_libjwt_algorithm)) {
		(void) jwt_valid_set_headers (jwt_valid, JWT_LIBJWT_HEADERS);
		(void) jwt_valid_set_now (jwt_valid, time (NULL));

		valid = (jwt_validate (jwt, jwt_valid) == JWT_VALIDATION_SUCCESS);

		jwt_valid_free (jwt_valid);
	}

	const char *id = valid ? jwt_get_grant (jwt, JWT_LIBJWT_CLAIM) : NULL;

	return id && !strcmp (id, JWT_LIBJWT_CLAIM_VALUE);

}

bool jwt_backend_verify (const char *token, const size_t length) {

	(void) length;

	bool valid = false;

	jwt_t *jwt = NULL;
	if (!jwt_decode (&jwt, token, jwt_libjwt_public_key, jwt_libjwt_public_size)) {
		valid = jwt_libjwt_validate (jwt);
	}

	jwt_free (jwt);

	return valid;

}
