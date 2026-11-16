/**
 * @file cerver/crypto/memory.h
 * @brief The cleansing of the memory of a secret and the comparison of two secrets.
 */

#ifndef _CERVER_CRYPTO_MEMORY_H_
#define _CERVER_CRYPTO_MEMORY_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup crypto_memory Memory
 * @ingroup crypto
 * @brief Erasing a secret before its memory is released, and comparing two of them.
 * @{
 */

/**
 * @brief Overwrites the bytes of a buffer with zeros, with a write that the compiler never removes
 * (explicit_bzero ()), for a secret before its memory is released.
 *
 * @param[out] buffer the buffer, it can be NULL
 * @param[in] size the bytes to overwrite
 */
CERVER_PUBLIC void cerver_crypto_cleanse (
	void *buffer, const size_t size
);

/**
 * @brief Compares two buffers of the same size in a time that does not depend on their bytes, by the provider of
 * the cryptography: the comparison of a secret (a token, a digest, a code) against what a client sent.
 *
 * @param[in] first a buffer
 * @param[in] second another one
 * @param[in] size the bytes of each one
 * @return true when they have the same bytes, also for two buffers without bytes; false when they differ and
 * for a NULL buffer with bytes
 *
 * @note The time depends on the size: two secrets of different sizes are compared by their digests, or the
 * sizes are not a secret.
 * @since 3.0b-8
 */
CERVER_PUBLIC bool cerver_crypto_equal (
	const void *first, const void *second, const size_t size
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
