/**
 * @file cerver/crypto/signature.h
 * @brief The signatures of the provider of the cryptography: HMAC, RSA, ECDSA and EdDSA over a message.
 */

#ifndef _CERVER_CRYPTO_SIGNATURE_H_
#define _CERVER_CRYPTO_SIGNATURE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/crypto/key.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup crypto_signature Signature
 * @ingroup crypto
 * @brief Signing a message with a key and verifying its signature.
 *
 * The provider of the cryptography (OpenSSL) computes every signature; cerver only checks that the key is the one
 * of the algorithm and gives each signature the bytes that are sent: the one of an ECDSA is R and S, each one with
 * the size of its curve (IEEE P1363, the one of RFC 7518), not their DER.
 * @{
 */

/**
 * @brief The algorithms of a signature.
 *
 * An HMAC takes a secret; RSA with the padding of PKCS #1 v1.5 or with PSS (its salt has the size of its digest
 * and its mask is MGF1 with the same digest) takes a key of RSA; an ECDSA takes a key of its curve; Ed25519 and
 * Ed448 take a key of their curve. Each row is XX (number, name, string).
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_SIGNATURE_MAP(XX)						\
	XX(0,	NONE,				None)						\
	XX(1,	HMAC_SHA256,		HMAC-SHA256)				\
	XX(2,	HMAC_SHA384,		HMAC-SHA384)				\
	XX(3,	HMAC_SHA512,		HMAC-SHA512)				\
	XX(4,	RSA_PKCS1_SHA256,	RSA-PKCS1-SHA256)			\
	XX(5,	RSA_PKCS1_SHA384,	RSA-PKCS1-SHA384)			\
	XX(6,	RSA_PKCS1_SHA512,	RSA-PKCS1-SHA512)			\
	XX(7,	RSA_PSS_SHA256,		RSA-PSS-SHA256)				\
	XX(8,	RSA_PSS_SHA384,		RSA-PSS-SHA384)				\
	XX(9,	RSA_PSS_SHA512,		RSA-PSS-SHA512)				\
	XX(10,	ECDSA_P256_SHA256,	ECDSA-P256-SHA256)			\
	XX(11,	ECDSA_P384_SHA384,	ECDSA-P384-SHA384)			\
	XX(12,	ECDSA_P521_SHA512,	ECDSA-P521-SHA512)			\
	XX(13,	ED25519,			Ed25519)					\
	XX(14,	ED448,				Ed448)

/**
 * @brief The algorithm of a signature, one `CERVER_CRYPTO_SIGNATURE_<name>` for each row of
 * #CERVER_CRYPTO_SIGNATURE_MAP.
 *
 * @since 3.0b-8
 */
typedef enum CerverCryptoSignature {

	#define XX(num, name, string) CERVER_CRYPTO_SIGNATURE_##name = num,
	CERVER_CRYPTO_SIGNATURE_MAP(XX)
	#undef XX

} CerverCryptoSignature;

/**
 * @brief The number of algorithms of #CERVER_CRYPTO_SIGNATURE_MAP, with CERVER_CRYPTO_SIGNATURE_NONE.
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_SIGNATURES					15

/**
 * @brief The bytes of the largest signature that cerver_crypto_sign () writes: the one of a key of RSA of 2^14
 * bits.
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_SIGNATURE_SIZE_MAX			(1 << 11)

/**
 * @brief Returns the string of an algorithm of a signature.
 *
 * @param[in] algorithm the algorithm
 * @return the string of its row in #CERVER_CRYPTO_SIGNATURE_MAP, or the one of CERVER_CRYPTO_SIGNATURE_NONE for a
 * value out of the table; a static string that must not be freed
 * @since 3.0b-8
 */
CERVER_EXPORT const char *cerver_crypto_signature_to_string (
	const CerverCryptoSignature algorithm
);

/**
 * @brief Returns whether a key is the one that an algorithm takes.
 *
 * @param[in] key the key
 * @param[in] algorithm the algorithm
 * @return true for a secret with an HMAC, a key of RSA with one of the algorithms of RSA, an EC key with the
 * ECDSA of its curve and a key of Ed25519 or Ed448 with its algorithm; false for any other pair, for
 * CERVER_CRYPTO_SIGNATURE_NONE and for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT bool cerver_crypto_signature_takes (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm
);

/**
 * @brief Returns the bytes of the signatures of a key with an algorithm.
 *
 * @param[in] key the key
 * @param[in] algorithm the algorithm
 * @return the bytes of its digest for an HMAC (32, 48 or 64), the bytes of the modulus for RSA, twice the bytes of
 * the curve for an ECDSA (64, 96 or 132), 64 for Ed25519 and 114 for Ed448; 0 when the key is not the one of the
 * algorithm (see cerver_crypto_signature_takes ())
 * @since 3.0b-8
 */
CERVER_EXPORT size_t cerver_crypto_signature_size (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm
);

/**
 * @brief Signs a message.
 *
 * @param[in] key the key: a secret, or a private key of the algorithm
 * @param[in] algorithm the algorithm
 * @param[in] message the bytes of the message, NULL only when its size is 0
 * @param[in] message_size how many
 * @param[out] signature where the signature is written
 * @param[in] capacity the bytes of the buffer, at least cerver_crypto_signature_size ()
 * @param[out] size the bytes of the signature, 0 when it fails
 * @return true when the message was signed; false for a NULL key, signature or size, a key that is not the one of
 * the algorithm or that is public, a buffer that is too small, NULL data with a size, or a failure of the provider
 * @note The S of an ECDSA signature is never in the upper half of the order of its curve: a signature has one
 * form.
 * @threadsafety Safe from any thread, also with the same key.
 * @since 3.0b-8
 */
CERVER_EXPORT bool cerver_crypto_sign (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	u8 *signature, const size_t capacity, size_t *size
);

/**
 * @brief Verifies the signature of a message.
 *
 * @param[in] key the key: a secret, or a public or a private key of the algorithm
 * @param[in] algorithm the algorithm
 * @param[in] message the bytes of the message, NULL only when its size is 0
 * @param[in] message_size how many
 * @param[in] signature the bytes of the signature
 * @param[in] signature_size how many
 * @return true when the signature is the one of the message with the key; false when it is not, for a NULL key or
 * signature, a key that is not the one of the algorithm, NULL data with a size, or a failure of the provider
 * @note An HMAC is compared in constant time. The signature of an ECDSA must have exactly twice the bytes of its
 * curve.
 * @threadsafety Safe from any thread, also with the same key.
 * @since 3.0b-8
 */
CERVER_EXPORT bool cerver_crypto_verify (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
