/**
 * @file crypto/internal.h
 * @internal
 * @brief What the parts of the crypto module share and never export: the struct of a key and how it is created
 * from a key of the provider.
 */

#ifndef _CERVER_CRYPTO_INTERNAL_H_
#define _CERVER_CRYPTO_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/crypto/key.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief A key (cerver/crypto/key.h): a secret, or a key of the provider with what it is.
 */
struct CerverCryptoKey {

	/** @internal @brief The type of the key. */
	CerverCryptoKeyType type;
	/** @internal @brief A secret and a private key sign; a public key only verifies. */
	bool is_private;
	/** @internal @brief The bits of the key. */
	unsigned int bits;
	/** @internal @brief The curve of an EC key, CERVER_CRYPTO_CURVE_NONE for the others. */
	CerverCryptoCurve curve;

	/** @internal @brief The bytes of a secret, of the key; NULL for the other types. */
	u8 *secret;
	/** @internal @brief The bytes of #secret. */
	size_t secret_size;

	/** @internal @brief The key of the provider (crypto/provider.h); NULL for a secret. */
	void *provider;

};

/**
 * @internal
 * @brief Creates a key around a key of the provider, that it takes: the provider key is released when it fails.
 *
 * @param[in,out] provider_key the key of the provider, NULL when the provider could not create it
 * @param[in] is_private whether it is a private key
 * @param[out] error why the key was not created: CERVER_CRYPTO_KEY_ERROR_FORMAT for a NULL provider key,
 * CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED for one of a type or of a curve that cerver does not use,
 * CERVER_CRYPTO_KEY_ERROR_MEMORY without memory; it can be NULL
 * @return the key, NULL on error
 */
CERVER_HIDDEN CerverCryptoKey *crypto_key_adopt (
	void *provider_key, const bool is_private,
	CerverCryptoKeyError *error
);

/**
 * @internal
 * @brief Sets the error of the creation of a key.
 *
 * @param[out] error where it is set, it can be NULL
 * @param[in] code the error
 */
CERVER_HIDDEN void crypto_key_error_set (
	CerverCryptoKeyError *error, const CerverCryptoKeyError code
);

#ifdef __cplusplus
}
#endif

#endif
