/**
 * @file cerver/jwt/key.h
 * @brief The keys of the tokens: a key of the provider of the cryptography with the algorithm of its tokens.
 */

#ifndef _CERVER_JWT_KEY_H_
#define _CERVER_JWT_KEY_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/crypto/key.h"

#include "cerver/jwt/jwt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup jwt_key Key
 * @ingroup jwt
 * @brief Creating the key that signs or verifies the tokens of one algorithm.
 *
 * The algorithm is given with the key and it must be one that the key can do (RFC 7518 section 3): HS256, HS384
 * and HS512 take a secret with at least the bytes of their digest (32, 48 and 64); RS256, RS384, RS512, PS256,
 * PS384 and PS512 take a key of RSA of at least 2048 bits; ES256 takes a key of P-256, ES384 one of P-384 and
 * ES512 one of P-521; EdDSA takes a key of Ed25519 or of Ed448. A secret and a private key sign and verify; a
 * public key only verifies.
 * @{
 */

/**
 * @brief Creates the key of the tokens of an algorithm from a key of the provider of the cryptography, that it
 * takes: the key of the provider is released with it, and also when this fails.
 *
 * @param[in] algorithm the algorithm of the tokens of the key
 * @param[in,out] key the key of the provider (cerver/crypto/key.h)
 * @param[out] error why the key was not created, CERVER_JWT_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_jwt_key_delete (); NULL with CERVER_JWT_ERROR_ARGUMENTS for a NULL key,
 * with CERVER_JWT_ERROR_ALGORITHM for CERVER_JWT_ALGORITHM_NONE and for a value out of its table, with
 * CERVER_JWT_ERROR_KEY for a key that the algorithm does not take (another type, another curve, a secret
 * shorter than the digest, a key of RSA of less than 2048 bits) and with CERVER_JWT_ERROR_MEMORY without memory
 * @warning A secret that was created from the text of a PEM is taken: only cerver_jwt_key_create_secret () sees
 * the bytes of the secret and refuses it.
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtKey *cerver_jwt_key_create (
	const CerverJwtAlgorithm algorithm, CerverCryptoKey *key,
	CerverJwtError *error
);

/**
 * @brief Creates the key of the tokens of HS256, HS384 or HS512 from the bytes of a secret.
 *
 * @param[in] algorithm CERVER_JWT_ALGORITHM_HS256, CERVER_JWT_ALGORITHM_HS384 or CERVER_JWT_ALGORITHM_HS512
 * @param[in] secret the bytes of the secret, copied
 * @param[in] size how many: at least the bytes of the digest of the algorithm (32, 48 or 64)
 * @param[out] error why the key was not created, CERVER_JWT_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_jwt_key_delete (); NULL with CERVER_JWT_ERROR_ARGUMENTS for a NULL or an
 * empty secret or one over #CERVER_CRYPTO_SECRET_SIZE_MAX, with CERVER_JWT_ERROR_ALGORITHM for what is not an
 * algorithm, with CERVER_JWT_ERROR_KEY for an algorithm that does not take a secret, for a secret that is too
 * short and for one that has the start of a PEM (`-----BEGIN`), and with CERVER_JWT_ERROR_MEMORY without memory
 * @note The text of a PEM is never a secret: a public key that is used as the secret of an HMAC lets anyone
 * that has it sign tokens.
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtKey *cerver_jwt_key_create_secret (
	const CerverJwtAlgorithm algorithm,
	const void *secret, const size_t size,
	CerverJwtError *error
);

/**
 * @brief Creates the key of the tokens of an algorithm from the PEM of a public or of a private key
 * (cerver_crypto_key_create_pem ()).
 *
 * @param[in] algorithm the algorithm of the tokens of the key
 * @param[in] pem the text of the PEM
 * @param[in] size its bytes
 * @param[in] is_private true to read a private key, that signs and verifies; false to read a public one
 * @param[out] error why the key was not created, CERVER_JWT_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_jwt_key_delete (); NULL with CERVER_JWT_ERROR_ARGUMENTS for a NULL or an
 * empty PEM, with CERVER_JWT_ERROR_ALGORITHM for what is not an algorithm, with CERVER_JWT_ERROR_KEY for a text
 * that is not the PEM of a key of that kind (one with a passphrase is never decrypted) and for a key that the
 * algorithm does not take, and with CERVER_JWT_ERROR_MEMORY without memory
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtKey *cerver_jwt_key_create_pem (
	const CerverJwtAlgorithm algorithm,
	const void *pem, const size_t size, const bool is_private,
	CerverJwtError *error
);

/**
 * @brief Creates the key of the tokens of an algorithm from a JWK (RFC 7517, cerver_crypto_key_create_jwk ()).
 *
 * The algorithm is the one of the call: the `alg` of the JWK is not read. A JWK that says that its key is not
 * for signatures (its `use`, its `key_ops`) is not a key.
 *
 * @param[in] algorithm the algorithm of the tokens of the key
 * @param[in] jwk the text of the JWK
 * @param[in] size its bytes
 * @param[out] error why the key was not created, CERVER_JWT_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_jwt_key_delete (); NULL with CERVER_JWT_ERROR_ARGUMENTS for a NULL or an
 * empty text, with CERVER_JWT_ERROR_ALGORITHM for what is not an algorithm, with CERVER_JWT_ERROR_KEY for a text
 * that is not the JWK of a key and for a key that the algorithm does not take, and with CERVER_JWT_ERROR_MEMORY
 * without memory
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtKey *cerver_jwt_key_create_jwk (
	const CerverJwtAlgorithm algorithm,
	const char *jwk, const size_t size,
	CerverJwtError *error
);

/**
 * @brief Releases a key and its key of the provider.
 *
 * @param[in,out] key the key, it can be NULL
 * @since 3.0b-8
 */
CERVER_EXPORT void cerver_jwt_key_delete (
	CerverJwtKey *key
);

/**
 * @brief Returns the algorithm of the tokens of a key.
 *
 * @param[in] key the key
 * @return its algorithm; CERVER_JWT_ALGORITHM_NONE for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtAlgorithm cerver_jwt_key_algorithm (
	const CerverJwtKey *key
);

/**
 * @brief Returns whether a key can sign.
 *
 * @param[in] key the key
 * @return true for a secret and for a private key, that also verify; false for a public key and for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT bool cerver_jwt_key_is_private (
	const CerverJwtKey *key
);

/**
 * @brief Returns the key of the provider of the cryptography of a key.
 *
 * @param[in] key the key
 * @return its key, that belongs to it; NULL for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT const CerverCryptoKey *cerver_jwt_key_crypto (
	const CerverJwtKey *key
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
