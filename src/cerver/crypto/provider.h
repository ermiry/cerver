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

#if !defined (CERVER_CRYPTO_OPENSSL)
#error "cerver needs a provider of the cryptography: CRYPTO=openssl"
#endif

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
