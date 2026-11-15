/**
 * @file cerver/utils/sha256.h
 * @brief The SHA-256 of sha-2 (thirdparty.md), for the session ids and the lobbies.
 */

#ifndef _CERVER_UTILS_SHA256_H_
#define _CERVER_UTILS_SHA256_H_

#include <stddef.h>
#include <stdint.h>

#include "cerver/config.h"

/**
 * @defgroup utils_sha256 SHA-256
 * @ingroup utils
 * @brief The SHA-256 of some bytes, as bytes or as hex characters.
 * @{
 */

/** @brief The size of the string that sha256_generate_output () allocates, more than the 65 bytes it needs. */
#define SHA256_STRING_LEN		80

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Computes the SHA-256 of some bytes.
 *
 * @param[out] hash the 32 bytes of the digest
 * @param[in] input the bytes
 * @param[in] len the number of bytes
 */
CERVER_PUBLIC void sha256_calc (
	uint8_t hash[32], const void *input, size_t len
);

/**
 * @brief Writes the 32 bytes of a digest as 64 lowercase hex characters, with an ending null byte.
 *
 * @param[out] string the string, at least 65 bytes
 * @param[in] hash the digest
 */
CERVER_PUBLIC void sha256_hash_to_string (
	char string[], const uint8_t hash[32]
);

/**
 * @brief Writes the SHA-256 of some bytes as hex characters.
 *
 * @param[out] output the string, at least 65 bytes
 * @param[in] input the bytes
 * @param[in] inlen the number of bytes
 */
CERVER_PUBLIC void sha256_generate (
	char *output, const char *input, const size_t inlen
);

/**
 * @brief Returns the SHA-256 of some bytes as hex characters.
 *
 * @param[in] input the bytes
 * @param[in] inlen the number of bytes
 * @return a string of #SHA256_STRING_LEN bytes allocated with calloc (), released with free (); NULL if it could not
 * be allocated
 */
CERVER_PUBLIC char *sha256_generate_output (
	const char *input, const size_t inlen
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
