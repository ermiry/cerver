/**
 * @file cerver/crypto/memory.h
 * @brief The cleansing of the memory of a secret.
 */

#ifndef _CERVER_CRYPTO_MEMORY_H_
#define _CERVER_CRYPTO_MEMORY_H_

#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup crypto_memory Memory
 * @ingroup crypto
 * @brief Erasing a secret before its memory is released.
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

/** @} */

#ifdef __cplusplus
}
#endif

#endif
