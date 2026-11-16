/**
 * @file crypto/provider.h
 * @internal
 * @brief What the provider of the cryptography implements; the build selects it with CRYPTO (only openssl).
 */

#ifndef _CERVER_CRYPTO_PROVIDER_H_
#define _CERVER_CRYPTO_PROVIDER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/crypto/hash.h"
#include "cerver/crypto/key.h"
#include "cerver/crypto/signature.h"

#if !defined (CERVER_CRYPTO_OPENSSL)
#error "cerver needs a provider of the cryptography: CRYPTO=openssl"
#endif

/** @internal @brief The bytes of the largest HMAC, the one with SHA-512. */
#define CERVER_CRYPTO_MAC_SIZE_MAX				64

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Some bytes of a key, like one of the numbers of a JWK: a big endian integer, a coordinate or a seed.
 */
typedef struct CryptoBytes {

	/** @internal @brief The bytes, NULL for a part that a key does not have. */
	const u8 *bytes;
	/** @internal @brief How many. */
	size_t size;

} CryptoBytes;

/**
 * @internal
 * @brief The numbers of a key of RSA, as a JWK has them (RFC 7518 section 6.3), each one a big endian integer.
 */
typedef struct CryptoRsaParts {

	/** @internal @brief The modulus, `n`. */
	CryptoBytes modulus;
	/** @internal @brief The public exponent, `e`. */
	CryptoBytes exponent;
	/** @internal @brief The private exponent, `d`; without it the key is a public one. */
	CryptoBytes private_exponent;
	/** @internal @brief The first prime factor, `p`. */
	CryptoBytes first_prime;
	/** @internal @brief The second prime factor, `q`. */
	CryptoBytes second_prime;
	/** @internal @brief The exponent of the first factor, `dp`. */
	CryptoBytes first_exponent;
	/** @internal @brief The exponent of the second factor, `dq`. */
	CryptoBytes second_exponent;
	/** @internal @brief The coefficient of the first factor, `qi`. */
	CryptoBytes coefficient;

} CryptoRsaParts;

/**
 * @internal
 * @brief The SHA-1 of the provider of the cryptography; cerver_crypto_sha1 () already checked the arguments.
 *
 * @param[in] data the bytes, NULL is taken as an empty input
 * @param[in] size the number of bytes
 * @param[out] digest where the #CERVER_CRYPTO_SHA1_SIZE bytes of the digest are written
 * @return true on success
 */
CERVER_HIDDEN bool cerver_crypto_provider_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
);

/**
 * @internal
 * @brief Compares two buffers in a time that does not depend on their bytes.
 *
 * @param[in] first a buffer
 * @param[in] second another one
 * @param[in] size the bytes of each one
 * @return true when they have the same bytes
 */
CERVER_HIDDEN bool cerver_crypto_provider_equal (
	const void *first, const void *second, const size_t size
);

/**
 * @internal
 * @brief Reads the key of a PEM.
 *
 * @param[in] pem the text of the PEM
 * @param[in] size its bytes
 * @param[in] is_private true to read a private key, false to read a public one
 * @return the key of the provider, released with cerver_crypto_provider_key_delete (); NULL for a text that is not
 * that PEM (one with a passphrase is never decrypted) and without memory
 */
CERVER_HIDDEN void *cerver_crypto_provider_key_read_pem (
	const void *pem, const size_t size, const bool is_private
);

/**
 * @internal
 * @brief Creates a key of RSA from its numbers.
 *
 * @param[in] parts the numbers: the modulus and the public exponent; with the private exponent a private key, that
 * has the two primes, their exponents and the coefficient or none of them
 * @return the key of the provider, released with cerver_crypto_provider_key_delete (); NULL when they are not a
 * key and without memory
 */
CERVER_HIDDEN void *cerver_crypto_provider_key_create_rsa (
	const CryptoRsaParts *parts
);

/**
 * @internal
 * @brief Creates a key of an elliptic curve from the coordinates of its point.
 *
 * @param[in] curve the curve
 * @param[in] x the first coordinate of the public point, with the bytes of the curve
 * @param[in] y the second one, with the bytes of the curve
 * @param[in] secret the private number, with the bytes of the curve; without bytes the key is a public one
 * @return the key of the provider, released with cerver_crypto_provider_key_delete (); NULL when a number does not
 * have the bytes of the curve, the point is not of the curve, the private number is not the one of the point, and
 * without memory
 */
CERVER_HIDDEN void *cerver_crypto_provider_key_create_curve (
	const CerverCryptoCurve curve,
	const CryptoBytes x, const CryptoBytes y, const CryptoBytes secret
);

/**
 * @internal
 * @brief Creates a key of an Edwards curve from its bytes.
 *
 * @param[in] type CERVER_CRYPTO_KEY_TYPE_ED25519 or CERVER_CRYPTO_KEY_TYPE_ED448
 * @param[in] point the bytes of the public key
 * @param[in] seed the bytes of the private key; without bytes the key is a public one
 * @return the key of the provider, released with cerver_crypto_provider_key_delete (); NULL when the bytes are not
 * a key of that curve (also a private key whose public one is another) and without memory
 */
CERVER_HIDDEN void *cerver_crypto_provider_key_create_edwards (
	const CerverCryptoKeyType type,
	const CryptoBytes point, const CryptoBytes seed
);

/**
 * @internal
 * @brief Releases a key of the provider.
 *
 * @param[in,out] provider_key the key, it can be NULL
 */
CERVER_HIDDEN void cerver_crypto_provider_key_delete (
	void *provider_key
);

/**
 * @internal
 * @brief Tells what a key of the provider is.
 *
 * @param[in] provider_key the key
 * @param[out] type its type
 * @param[out] bits its bits
 * @param[out] curve the curve of an EC key, CERVER_CRYPTO_CURVE_NONE for the others
 * @return true for a key that cerver uses; false for a key of another type or of another curve
 */
CERVER_HIDDEN bool cerver_crypto_provider_key_describe (
	const void *provider_key,
	CerverCryptoKeyType *type, unsigned int *bits, CerverCryptoCurve *curve
);

/**
 * @internal
 * @brief Returns the bytes of the signatures of a key of the provider with an algorithm that takes it.
 *
 * @param[in] provider_key the key
 * @param[in] algorithm an algorithm of RSA, of ECDSA or of EdDSA
 * @return the bytes of a signature; 0 for an HMAC
 */
CERVER_HIDDEN size_t cerver_crypto_provider_signature_size (
	const void *provider_key, const CerverCryptoSignature algorithm
);

/**
 * @internal
 * @brief Computes the HMAC of a message.
 *
 * @param[in] algorithm one of the HMAC algorithms
 * @param[in] secret the bytes of the secret
 * @param[in] secret_size how many
 * @param[in] message the bytes of the message, NULL is taken as an empty one
 * @param[in] message_size how many
 * @param[out] mac where the HMAC is written
 * @param[out] mac_size its bytes
 * @return true on success
 */
CERVER_HIDDEN bool cerver_crypto_provider_mac (
	const CerverCryptoSignature algorithm,
	const u8 *secret, const size_t secret_size,
	const void *message, const size_t message_size,
	u8 mac[CERVER_CRYPTO_MAC_SIZE_MAX], size_t *mac_size
);

/**
 * @internal
 * @brief Signs a message with a private key of the provider; cerver_crypto_sign () already checked the arguments.
 *
 * @param[in] provider_key the key, the one that the algorithm takes
 * @param[in] algorithm an algorithm of RSA, of ECDSA or of EdDSA
 * @param[in] message the bytes of the message, NULL is taken as an empty one
 * @param[in] message_size how many
 * @param[out] signature where the signature is written, with at least the bytes of
 * cerver_crypto_provider_signature_size ()
 * @param[out] size the bytes of the signature
 * @return true on success
 */
CERVER_HIDDEN bool cerver_crypto_provider_sign (
	const void *provider_key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	u8 *signature, size_t *size
);

/**
 * @internal
 * @brief Verifies a signature with a key of the provider; cerver_crypto_verify () already checked the arguments.
 *
 * @param[in] provider_key the key, the one that the algorithm takes
 * @param[in] algorithm an algorithm of RSA, of ECDSA or of EdDSA
 * @param[in] message the bytes of the message, NULL is taken as an empty one
 * @param[in] message_size how many
 * @param[in] signature the bytes of the signature
 * @param[in] signature_size how many
 * @return true when the signature is the one of the message
 */
CERVER_HIDDEN bool cerver_crypto_provider_verify (
	const void *provider_key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
);

#ifdef __cplusplus
}
#endif

#endif
