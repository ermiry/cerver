/**
 * @file cerver/http/responses/compression.h
 * @brief The gzip compression of the bodies of the HTTP responses.
 */

#ifndef _CERVER_HTTP_COMPRESSION_H_
#define _CERVER_HTTP_COMPRESSION_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

/**
 * @defgroup http_responses_compression Compression
 * @ingroup http_responses
 * @brief The gzip content coding of the responses, compiled with zlib (ZLIB=1).
 * @{
 */

/** @brief The smallest body that is compressed, a smaller one is sent as it is. */
#define HTTP_COMPRESSION_DEFAULT_MIN_SIZE		(1 << 10)

/** @brief The default compression level of zlib. */
#define HTTP_COMPRESSION_DEFAULT_LEVEL			6

/** @brief The fastest compression level of zlib. */
#define HTTP_COMPRESSION_MIN_LEVEL				1

/** @brief The smallest compression level of zlib. */
#define HTTP_COMPRESSION_MAX_LEVEL				9

/** @brief The only content coding that cerver uses. */
#define HTTP_COMPRESSION_ENCODING				"gzip"

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

/**
 * @brief Returns whether cerver was compiled with zlib (ZLIB=1).
 *
 * @return true with zlib; without it, the compression settings are accepted but nothing is compressed
 */
CERVER_EXPORT bool cerver_http_compression_available (void);

/**
 * @brief Returns whether an Accept-Encoding value accepts gzip.
 *
 * gzip (or x-gzip) is accepted by name or with "*", with a quality value bigger than 0; the quality of the name
 * wins over the one of "*".
 *
 * @param[in] accept_encoding the value, it can be NULL
 * @param[in] length the length of the value
 * @return true if gzip is accepted
 */
CERVER_PUBLIC bool cerver_http_compression_accepts_gzip (
	const char *accept_encoding, const size_t length
);

/**
 * @brief Returns whether a body of a content type is worth compressing: text, JSON, JavaScript, XML and SVG.
 *
 * The parameters of the content type (`; charset=...`) are ignored.
 *
 * @param[in] content_type the content type, it can be NULL
 * @param[in] length the length of the content type
 * @return true if it is compressible
 */
CERVER_PUBLIC bool cerver_http_compression_is_compressible (
	const char *content_type, const size_t length
);

/**
 * @brief Compresses the data with gzip into memory of the arena.
 *
 * The memory of zlib is also taken from the arena, so nothing is allocated with malloc ().
 *
 * @param[in,out] arena the arena of the request
 * @param[in] data the data
 * @param[in] length the length of the data, from 1 to UINT32_MAX
 * @param[in] level the compression level of zlib
 * @param[out] compressed the compressed data, in the arena
 * @param[out] compressed_length the length of the compressed data
 * @return true on success; false for a NULL argument, a length out of range, a failure of zlib, or when cerver was
 * compiled without zlib
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool cerver_http_compression_gzip (
	struct Arena *arena,
	const void *data, const size_t length, const int level,
	void **compressed, size_t *compressed_length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
