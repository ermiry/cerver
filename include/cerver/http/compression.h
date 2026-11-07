#ifndef _CERVER_HTTP_COMPRESSION_H_
#define _CERVER_HTTP_COMPRESSION_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

// the smallest body that is compressed, a smaller one is sent as it is
#define HTTP_COMPRESSION_DEFAULT_MIN_SIZE		(1 << 10)

// the zlib compression level, from 1 (fastest) to 9 (smallest)
#define HTTP_COMPRESSION_DEFAULT_LEVEL			6
#define HTTP_COMPRESSION_MIN_LEVEL				1
#define HTTP_COMPRESSION_MAX_LEVEL				9

// the only content coding that cerver uses
#define HTTP_COMPRESSION_ENCODING				"gzip"

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

// returns true if cerver was compiled with zlib (ZLIB=1),
// without it, the compression settings are accepted but nothing is compressed
CERVER_EXPORT bool http_compression_available (void);

// returns true if the Accept-Encoding value accepts gzip,
// by name or with "*", and with a quality value bigger than 0
CERVER_PUBLIC bool http_compression_accepts_gzip (
	const char *accept_encoding, const size_t length
);

// returns true if a body of the content type is worth compressing:
// text, JSON, JavaScript, XML and SVG
CERVER_PUBLIC bool http_compression_is_compressible (
	const char *content_type, const size_t length
);

// compresses the data with gzip into memory of the arena,
// zlib's own memory is also taken from the arena, so nothing is allocated with malloc ()
// returns true on success, and sets the compressed data and its length
CERVER_PRIVATE bool http_compression_gzip (
	struct Arena *arena,
	const void *data, const size_t length, const int level,
	void **compressed, size_t *compressed_length
);

#ifdef __cplusplus
}
#endif

#endif
