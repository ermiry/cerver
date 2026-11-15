/**
 * @file cerver/crypto/hash.h
 * @brief The hashes of the provider of the cryptography.
 */

#ifndef _CERVER_CRYPTO_HASH_H_
#define _CERVER_CRYPTO_HASH_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup crypto_hash Hash
 * @ingroup crypto
 * @brief The digests that the provider of the cryptography computes.
 * @{
 */

/** @brief The bytes of a SHA-1 digest. */
#define CERVER_CRYPTO_SHA1_SIZE				20

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Computes the SHA-1 of some bytes, with the provider of the cryptography (OpenSSL).
 *
 * @param[in] data the bytes, NULL only when size is 0
 * @param[in] size the number of bytes
 * @param[out] digest where the #CERVER_CRYPTO_SHA1_SIZE bytes of the digest are written
 * @return true on success, false for a NULL digest, NULL data with a size, or a failure of the provider
 */
CERVER_PUBLIC bool cerver_crypto_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
