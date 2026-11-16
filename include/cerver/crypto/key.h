/**
 * @file cerver/crypto/key.h
 * @brief The keys of the provider of the cryptography: a secret, or a public or a private key of RSA, of an elliptic
 * curve or of an Edwards curve.
 */

#ifndef _CERVER_CRYPTO_KEY_H_
#define _CERVER_CRYPTO_KEY_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup crypto_key Key
 * @ingroup crypto
 * @brief The keys that sign and verify (cerver/crypto/signature.h).
 *
 * A key is created once, from a secret, from a PEM or from a JWK, and it is not changed after: any thread can
 * sign and verify with it at the same time. cerver never writes the cryptography: the provider (OpenSSL) reads the
 * keys and computes with them.
 * @{
 */

/**
 * @brief The types of a key: a secret (HMAC), RSA, an elliptic curve (ECDSA) and the two Edwards curves (EdDSA).
 *
 * Each row is XX (number, name, string).
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_KEY_TYPE_MAP(XX)				\
	XX(0,	NONE,		None)						\
	XX(1,	SECRET,		Secret)						\
	XX(2,	RSA,		RSA)						\
	XX(3,	EC,			EC)							\
	XX(4,	ED25519,	Ed25519)					\
	XX(5,	ED448,		Ed448)

/**
 * @brief The type of a key, one `CERVER_CRYPTO_KEY_TYPE_<name>` for each row of #CERVER_CRYPTO_KEY_TYPE_MAP.
 *
 * @since 3.0b-8
 */
typedef enum CerverCryptoKeyType {

	#define XX(num, name, string) CERVER_CRYPTO_KEY_TYPE_##name = num,
	CERVER_CRYPTO_KEY_TYPE_MAP(XX)
	#undef XX

} CerverCryptoKeyType;

/**
 * @brief Returns the string of a type of key.
 *
 * @param[in] type the type
 * @return the string of its row in #CERVER_CRYPTO_KEY_TYPE_MAP, or the one of CERVER_CRYPTO_KEY_TYPE_NONE for a
 * value out of the table; a static string that must not be freed
 * @since 3.0b-8
 */
CERVER_EXPORT const char *cerver_crypto_key_type_to_string (
	const CerverCryptoKeyType type
);

/**
 * @brief The elliptic curves of an EC key, with their names of FIPS 186 (the `crv` of a JWK).
 *
 * Each row is XX (number, name, string).
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_CURVE_MAP(XX)					\
	XX(0,	NONE,		None)						\
	XX(1,	P256,		P-256)						\
	XX(2,	P384,		P-384)						\
	XX(3,	P521,		P-521)

/**
 * @brief The curve of an EC key, one `CERVER_CRYPTO_CURVE_<name>` for each row of #CERVER_CRYPTO_CURVE_MAP.
 *
 * @since 3.0b-8
 */
typedef enum CerverCryptoCurve {

	#define XX(num, name, string) CERVER_CRYPTO_CURVE_##name = num,
	CERVER_CRYPTO_CURVE_MAP(XX)
	#undef XX

} CerverCryptoCurve;

/**
 * @brief Returns the string of a curve.
 *
 * @param[in] curve the curve
 * @return the string of its row in #CERVER_CRYPTO_CURVE_MAP, or the one of CERVER_CRYPTO_CURVE_NONE for a value
 * out of the table; a static string that must not be freed
 * @since 3.0b-8
 */
CERVER_EXPORT const char *cerver_crypto_curve_to_string (
	const CerverCryptoCurve curve
);

/**
 * @brief Why a key was not created.
 *
 * Each row is XX (number, name, string).
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_KEY_ERROR_MAP(XX)														\
	XX(0,	NONE,			None)															\
	XX(1,	ARGUMENTS,		The key has no material)										\
	XX(2,	FORMAT,			The material of the key could not be read)						\
	XX(3,	UNSUPPORTED,	The key is of a type or of a curve that cerver does not use)		\
	XX(4,	MEMORY,			The key could not be allocated)

/**
 * @brief The error of a key, one `CERVER_CRYPTO_KEY_ERROR_<name>` for each row of #CERVER_CRYPTO_KEY_ERROR_MAP.
 *
 * @since 3.0b-8
 */
typedef enum CerverCryptoKeyError {

	#define XX(num, name, string) CERVER_CRYPTO_KEY_ERROR_##name = num,
	CERVER_CRYPTO_KEY_ERROR_MAP(XX)
	#undef XX

} CerverCryptoKeyError;

/**
 * @brief Returns the string of an error of a key.
 *
 * @param[in] error the error
 * @return the string of its row in #CERVER_CRYPTO_KEY_ERROR_MAP, or the one of CERVER_CRYPTO_KEY_ERROR_NONE for a
 * value out of the table; a static string that must not be freed
 * @since 3.0b-8
 */
CERVER_EXPORT const char *cerver_crypto_key_error_to_string (
	const CerverCryptoKeyError error
);

/**
 * @brief The most bytes of a secret.
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_SECRET_SIZE_MAX				(1 << 16)

/**
 * @brief The most bits of a key of RSA: one with more is a CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED.
 *
 * @since 3.0b-8
 */
#define CERVER_CRYPTO_RSA_BITS_MAX					(1 << 14)

/**
 * @brief A key: a secret, or a public or a private key of RSA, of an elliptic curve or of an Edwards curve.
 *
 * It is created with cerver_crypto_key_create_secret (), cerver_crypto_key_create_pem () or
 * cerver_crypto_key_create_jwk (), and released with cerver_crypto_key_delete ().
 *
 * @since 3.0b-8
 */
typedef struct CerverCryptoKey CerverCryptoKey;

/**
 * @brief Creates a key from the bytes of a secret, the key of an HMAC.
 *
 * @param[in] secret the bytes of the secret, copied
 * @param[in] size how many, from 1 to #CERVER_CRYPTO_SECRET_SIZE_MAX
 * @param[out] error why the key was not created, CERVER_CRYPTO_KEY_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_crypto_key_delete (); NULL with CERVER_CRYPTO_KEY_ERROR_ARGUMENTS for a
 * NULL secret, an empty one or one that is too long, and with CERVER_CRYPTO_KEY_ERROR_MEMORY without memory
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverCryptoKey *cerver_crypto_key_create_secret (
	const void *secret, const size_t size,
	CerverCryptoKeyError *error
);

/**
 * @brief Creates a key from a PEM: a public key (`-----BEGIN PUBLIC KEY-----`) or a private one.
 *
 * The key is one of RSA, of the curves P-256, P-384 or P-521, or Ed25519 or Ed448.
 *
 * @param[in] pem the text of the PEM
 * @param[in] size its bytes
 * @param[in] is_private true to read a private key, false to read a public one
 * @param[out] error why the key was not created, CERVER_CRYPTO_KEY_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_crypto_key_delete (); NULL with CERVER_CRYPTO_KEY_ERROR_ARGUMENTS for a
 * NULL or an empty PEM, with CERVER_CRYPTO_KEY_ERROR_FORMAT for a text that is not the PEM of a key of that kind
 * (and when the provider could not read it), with CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED for a key of another type
 * or another curve and for a key of RSA of more than #CERVER_CRYPTO_RSA_BITS_MAX bits, and with
 * CERVER_CRYPTO_KEY_ERROR_MEMORY without memory for the key
 * @note A PEM with a passphrase is never decrypted: it is a CERVER_CRYPTO_KEY_ERROR_FORMAT.
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverCryptoKey *cerver_crypto_key_create_pem (
	const void *pem, const size_t size, const bool is_private,
	CerverCryptoKeyError *error
);

/**
 * @brief Creates a key from a JWK (RFC 7517), the JSON of one key.
 *
 * Its `kty` chooses the key: `oct` is a secret (its `k`), `RSA` a key of RSA (`n` and `e`; with `d` a private one,
 * that also has `p`, `q`, `dp`, `dq` and `qi`), `EC` a key of the curve of its `crv` (`P-256`, `P-384` or `P-521`,
 * with `x` and `y` of the size of the curve; with `d`, of that size, a private one) and `OKP` a key of the Edwards
 * curve of its `crv` (`Ed25519` or `Ed448`, with `x`; with `d` a private one). The provider checks the key: the
 * point of a public key is of its curve and the numbers of a private key agree. A JWK that says what its key is
 * for is taken only when it is for signatures: its `use` is `sig`, and its `key_ops` are an array with `sign`
 * or with `verify`. The other members of the JWK (`alg`, `kid`, …) are not read.
 *
 * @param[in] jwk the text of the JWK, an object without a key more than once
 * @param[in] size its bytes
 * @param[out] error why the key was not created, CERVER_CRYPTO_KEY_ERROR_NONE when it was; it can be NULL
 * @return the key, released with cerver_crypto_key_delete (); NULL with CERVER_CRYPTO_KEY_ERROR_ARGUMENTS for a
 * NULL or an empty text, with CERVER_CRYPTO_KEY_ERROR_FORMAT for a text that is not the JWK of a key (it is not
 * that JSON or it has more than 2^16 bytes, a member is missing, is not a string or is not base64url, a public
 * key of RSA has one of the numbers of a private one, or its values are not a key), with
 * CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED for another `kty` or another `crv`, for a key that is not for signatures
 * (another `use`, or `key_ops` without `sign` and without `verify`) and for a key of RSA with more than two
 * primes (`oth`), and with CERVER_CRYPTO_KEY_ERROR_MEMORY without memory
 * @note A number of a key of RSA with more than 2^11 bytes is not read (CERVER_CRYPTO_KEY_ERROR_FORMAT): the
 * most bits of a key are #CERVER_CRYPTO_RSA_BITS_MAX.
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverCryptoKey *cerver_crypto_key_create_jwk (
	const char *jwk, const size_t size,
	CerverCryptoKeyError *error
);

/**
 * @brief Releases a key; the bytes of a secret are erased first.
 *
 * @param[in,out] key the key, it can be NULL
 * @since 3.0b-8
 */
CERVER_EXPORT void cerver_crypto_key_delete (
	CerverCryptoKey *key
);

/**
 * @brief Returns the type of a key.
 *
 * @param[in] key the key
 * @return its type; CERVER_CRYPTO_KEY_TYPE_NONE for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT CerverCryptoKeyType cerver_crypto_key_type (
	const CerverCryptoKey *key
);

/**
 * @brief Returns whether a key can sign.
 *
 * @param[in] key the key
 * @return true for a secret and for a private key, that also verifies; false for a public key and for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT bool cerver_crypto_key_is_private (
	const CerverCryptoKey *key
);

/**
 * @brief Returns the size of a key in bits.
 *
 * @param[in] key the key
 * @return the bits of a secret, of the modulus of a key of RSA, of the order of the curve of an EC key (256, 384
 * or 521), or of the bytes of a key of an Edwards curve (256 for Ed25519 and 456 for Ed448); 0 for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT unsigned int cerver_crypto_key_bits (
	const CerverCryptoKey *key
);

/**
 * @brief Returns the curve of an EC key.
 *
 * @param[in] key the key
 * @return its curve; CERVER_CRYPTO_CURVE_NONE for a key of another type and for a NULL key
 * @since 3.0b-8
 */
CERVER_EXPORT CerverCryptoCurve cerver_crypto_key_curve (
	const CerverCryptoKey *key
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
