/**
 * @file cerver/crypto/random.h
 * @brief The random bytes of the kernel.
 */

#ifndef _CERVER_CRYPTO_RANDOM_H_
#define _CERVER_CRYPTO_RANDOM_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup crypto Crypto
 * @brief The cryptography that cerver uses: random bytes, hashes and the cleansing of secrets.
 */

/**
 * @defgroup crypto_random Random
 * @ingroup crypto
 * @brief The random bytes of the kernel, with getrandom ().
 * @{
 */

/**
 * @brief Fills a buffer with random bytes of the kernel (getrandom () from the urandom pool).
 *
 * It retries an interrupted call and a partial read; it blocks only until the pool of the kernel is initialized.
 *
 * @param[out] buffer the buffer
 * @param[in] size the bytes to fill
 * @return true when the whole buffer was filled, false for a NULL buffer or when getrandom () failed
 * @threadsafety Safe from any thread.
 */
CERVER_PUBLIC bool cerver_crypto_random_bytes (
	void *buffer, const size_t size
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
