#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/crypto/key.h"
#include "cerver/crypto/signature.h"

#include "cerver/jwt/internal.h"
#include "cerver/jwt/jwt.h"
#include "cerver/jwt/key.h"

#include "cerver/utils/base64.h"

// the least bits of the secret of each HMAC, the ones of its digest (RFC 7518 section 3.2)
#define JWT_KEY_SHA256_BITS					256
#define JWT_KEY_SHA384_BITS					384
#define JWT_KEY_SHA512_BITS					512

// the least bits of a key of RSA (RFC 7518 sections 3.3 and 3.5)
#define JWT_KEY_RSA_BITS					2048

// the header of every token, around the string of its algorithm
#define JWT_KEY_HEADER_START				"{\"alg\":\""
#define JWT_KEY_HEADER_END					"\",\"typ\":\"JWT\"}"

// the header of the tokens of the algorithms, whose strings have the same length
#define JWT_KEY_HEADER_SIZE					32

// the first characters of every PEM: the text of a key is never the secret of an HMAC
#define JWT_KEY_PEM_START					"-----BEGIN"

// the key that an algorithm takes
typedef struct JwtKeyRule {

	// the algorithm of the provider that signs its tokens
	CerverCryptoSignature signature;

	// the least bits of its key
	unsigned int bits;

} JwtKeyRule;

// one for each row of CERVER_JWT_ALGORITHM_MAP, in its order; EdDSA has the algorithm of the curve of its key
static const JwtKeyRule jwt_key_rules[CERVER_JWT_ALGORITHMS] = {
	{ CERVER_CRYPTO_SIGNATURE_NONE, 0 },
	{ CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, JWT_KEY_SHA256_BITS },
	{ CERVER_CRYPTO_SIGNATURE_HMAC_SHA384, JWT_KEY_SHA384_BITS },
	{ CERVER_CRYPTO_SIGNATURE_HMAC_SHA512, JWT_KEY_SHA512_BITS },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256, JWT_KEY_RSA_BITS },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA384, JWT_KEY_RSA_BITS },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA512, JWT_KEY_RSA_BITS },
	{ CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256, 0 },
	{ CERVER_CRYPTO_SIGNATURE_ECDSA_P384_SHA384, 0 },
	{ CERVER_CRYPTO_SIGNATURE_ECDSA_P521_SHA512, 0 },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, JWT_KEY_RSA_BITS },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA384, JWT_KEY_RSA_BITS },
	{ CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA512, JWT_KEY_RSA_BITS },
	{ CERVER_CRYPTO_SIGNATURE_NONE, 0 }
};

static void jwt_key_error_set (CerverJwtError *error, const CerverJwtError code) {

	if (error) *error = code;

}

static bool jwt_key_algorithm_is_valid (const CerverJwtAlgorithm algorithm) {

	return (algorithm > CERVER_JWT_ALGORITHM_NONE) && ((unsigned int) algorithm < CERVER_JWT_ALGORITHMS);

}

// EdDSA is the algorithm of the Edwards curve of its key
static CerverCryptoSignature jwt_key_signature_edwards (const CerverCryptoKey *key) {

	CerverCryptoSignature signature = CERVER_CRYPTO_SIGNATURE_NONE;

	switch (cerver_crypto_key_type (key)) {
		case CERVER_CRYPTO_KEY_TYPE_ED25519: signature = CERVER_CRYPTO_SIGNATURE_ED25519; break;
		case CERVER_CRYPTO_KEY_TYPE_ED448: signature = CERVER_CRYPTO_SIGNATURE_ED448; break;

		default: break;
	}

	return signature;

}

// the algorithm of the provider that signs the tokens of an algorithm with a key,
// none when the key is not one that the algorithm takes or it has less bits than it needs
static CerverCryptoSignature jwt_key_signature (
	const CerverJwtAlgorithm algorithm, const CerverCryptoKey *key
) {

	if (algorithm == CERVER_JWT_ALGORITHM_EDDSA) return jwt_key_signature_edwards (key);

	const JwtKeyRule *rule = &jwt_key_rules[algorithm];

	return (
		cerver_crypto_signature_takes (key, rule->signature)
		&& (cerver_crypto_key_bits (key) >= rule->bits)
	) ? rule->signature : CERVER_CRYPTO_SIGNATURE_NONE;

}

// the header of the tokens of the key: the same bytes for every one of them
// returns whether the header of its algorithm fits, as the one of every algorithm of the table does
static bool jwt_key_header_set (CerverJwtKey *key) {

	char header[JWT_KEY_HEADER_SIZE] = { 0 };

	const int length = snprintf (
		header, JWT_KEY_HEADER_SIZE, JWT_KEY_HEADER_START "%s" JWT_KEY_HEADER_END,
		cerver_jwt_algorithm_to_string (key->algorithm)
	);

	const bool fits = (length > 0) && (length < JWT_KEY_HEADER_SIZE);

	key->header_length = fits ?
		base64url_encode (key->header, (const unsigned char *) header, (size_t) length) : 0;

	return fits;

}

static CerverJwtKey *jwt_key_new (
	const CerverJwtAlgorithm algorithm, const CerverCryptoSignature signature, CerverCryptoKey *crypto_key
) {

	CerverJwtKey *key = (CerverJwtKey *) malloc (sizeof (CerverJwtKey));
	if (key) {
		key->key = crypto_key;

		key->algorithm = algorithm;
		key->signature = signature;
		key->signature_size = cerver_crypto_signature_size (crypto_key, signature);

		// a key without the header of its tokens is not one
		if (!jwt_key_header_set (key)) {
			free (key);
			key = NULL;
		}
	}

	return key;

}

void cerver_jwt_key_delete (CerverJwtKey *key) {

	if (key) {
		cerver_crypto_key_delete (key->key);

		free (key);
	}

}

// why a key of the provider with an algorithm is not the key of its tokens
static CerverJwtError jwt_key_check (
	const CerverJwtAlgorithm algorithm, const CerverCryptoKey *key,
	CerverCryptoSignature *signature
) {

	if (!key) return CERVER_JWT_ERROR_ARGUMENTS;

	if (!jwt_key_algorithm_is_valid (algorithm)) return CERVER_JWT_ERROR_ALGORITHM;

	*signature = jwt_key_signature (algorithm, key);

	return (*signature != CERVER_CRYPTO_SIGNATURE_NONE) ? CERVER_JWT_ERROR_NONE : CERVER_JWT_ERROR_KEY;

}

CerverJwtKey *cerver_jwt_key_create (
	const CerverJwtAlgorithm algorithm, CerverCryptoKey *key,
	CerverJwtError *error
) {

	CerverJwtKey *jwt_key = NULL;

	CerverCryptoSignature signature = CERVER_CRYPTO_SIGNATURE_NONE;
	CerverJwtError result = jwt_key_check (algorithm, key, &signature);
	if (result == CERVER_JWT_ERROR_NONE) {
		jwt_key = jwt_key_new (algorithm, signature, key);
		if (!jwt_key) result = CERVER_JWT_ERROR_MEMORY;
	}

	// the key of the provider was taken: without a key of its tokens it has no owner
	if (!jwt_key) cerver_crypto_key_delete (key);

	jwt_key_error_set (error, result);

	return jwt_key;

}

// the error of the tokens for the one of a key of the provider that was not created
static CerverJwtError jwt_key_error_translate (const CerverCryptoKeyError error) {

	CerverJwtError result = CERVER_JWT_ERROR_KEY;

	switch (error) {
		case CERVER_CRYPTO_KEY_ERROR_ARGUMENTS: result = CERVER_JWT_ERROR_ARGUMENTS; break;
		case CERVER_CRYPTO_KEY_ERROR_MEMORY: result = CERVER_JWT_ERROR_MEMORY; break;

		default: break;
	}

	return result;

}

// the key of the tokens of an algorithm with a key of the provider that a method just created, or its error
static CerverJwtKey *jwt_key_adopt (
	const CerverJwtAlgorithm algorithm,
	CerverCryptoKey *key, const CerverCryptoKeyError key_error,
	CerverJwtError *error
) {

	if (!key) {
		jwt_key_error_set (error, jwt_key_error_translate (key_error));
		return NULL;
	}

	return cerver_jwt_key_create (algorithm, key, error);

}

// whether the bytes of a secret have the start of a PEM
static bool jwt_key_secret_is_pem (const void *secret, const size_t size) {

	const size_t start_length = sizeof (JWT_KEY_PEM_START) - 1;
	const char *bytes = (const char *) secret;

	bool found = false;

	for (size_t i = 0; ((i + start_length) <= size) && !found; i++) {
		found = !memcmp (bytes + i, JWT_KEY_PEM_START, start_length);
	}

	return found;

}

CerverJwtKey *cerver_jwt_key_create_secret (
	const CerverJwtAlgorithm algorithm,
	const void *secret, const size_t size,
	CerverJwtError *error
) {

	if (!secret || !size) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_ARGUMENTS);
		return NULL;
	}

	if (!jwt_key_algorithm_is_valid (algorithm)) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_ALGORITHM);
		return NULL;
	}

	if (jwt_key_secret_is_pem (secret, size)) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_KEY);
		return NULL;
	}

	CerverCryptoKeyError key_error = CERVER_CRYPTO_KEY_ERROR_NONE;
	CerverCryptoKey *key = cerver_crypto_key_create_secret (secret, size, &key_error);

	return jwt_key_adopt (algorithm, key, key_error, error);

}

CerverJwtKey *cerver_jwt_key_create_pem (
	const CerverJwtAlgorithm algorithm,
	const void *pem, const size_t size, const bool is_private,
	CerverJwtError *error
) {

	if (!pem || !size) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_ARGUMENTS);
		return NULL;
	}

	if (!jwt_key_algorithm_is_valid (algorithm)) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_ALGORITHM);
		return NULL;
	}

	CerverCryptoKeyError key_error = CERVER_CRYPTO_KEY_ERROR_NONE;
	CerverCryptoKey *key = cerver_crypto_key_create_pem (pem, size, is_private, &key_error);

	return jwt_key_adopt (algorithm, key, key_error, error);

}

CerverJwtKey *cerver_jwt_key_create_jwk (
	const CerverJwtAlgorithm algorithm,
	const char *jwk, const size_t size,
	CerverJwtError *error
) {

	if (!jwk || !size) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_ARGUMENTS);
		return NULL;
	}

	if (!jwt_key_algorithm_is_valid (algorithm)) {
		jwt_key_error_set (error, CERVER_JWT_ERROR_ALGORITHM);
		return NULL;
	}

	CerverCryptoKeyError key_error = CERVER_CRYPTO_KEY_ERROR_NONE;
	CerverCryptoKey *key = cerver_crypto_key_create_jwk (jwk, size, &key_error);

	return jwt_key_adopt (algorithm, key, key_error, error);

}

CerverJwtAlgorithm cerver_jwt_key_algorithm (const CerverJwtKey *key) {

	return key ? key->algorithm : CERVER_JWT_ALGORITHM_NONE;

}

bool cerver_jwt_key_is_private (const CerverJwtKey *key) {

	return key ? cerver_crypto_key_is_private (key->key) : false;

}

const CerverCryptoKey *cerver_jwt_key_crypto (const CerverJwtKey *key) {

	return key ? key->key : NULL;

}
