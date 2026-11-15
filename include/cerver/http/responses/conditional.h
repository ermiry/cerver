#ifndef _CERVER_HTTP_CONDITIONAL_H_
#define _CERVER_HTTP_CONDITIONAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <sys/stat.h>

#include "cerver/config.h"

// the longest entity tag that cerver generates and its '\0',
// a weak one of a file is W/"<size>-<mtime>" and a strong one of a body is "<hash>-gzip"
#define HTTP_ETAG_SIZE					48

// the suffix of the entity tag of a compressed body,
// a compressed representation is a different one (RFC 9110 section 8.8.3.3)
#define HTTP_ETAG_COMPRESSED_SUFFIX		"-gzip"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpRequest;

// writes the strong entity tag of a body, the FNV-1a of its bytes,
// with the compressed suffix if the body is sent compressed
// etag must have at least HTTP_ETAG_SIZE bytes
// returns the length of the entity tag, 0 on error
CERVER_PUBLIC size_t http_etag_from_data (
	const void *data, const size_t length,
	const bool compressed, char *etag
);

// writes the weak entity tag of a file, from its size and its modification time
// etag must have at least HTTP_ETAG_SIZE bytes
// returns the length of the entity tag, 0 on error
CERVER_PUBLIC size_t http_etag_from_file (
	const struct stat *file_status, char *etag
);

// returns true if the If-None-Match value ("*" or a list of entity tags)
// matches the entity tag with the weak comparison (RFC 9110 section 13.1.2)
CERVER_PUBLIC bool http_etag_none_match (
	const char *if_none_match, const size_t if_none_match_length,
	const char *etag, const size_t etag_length
);

// returns true if a GET or a HEAD request can be answered with 304 Not Modified:
// its If-None-Match matches the entity tag, or it has no If-None-Match
// and its If-Modified-Since is not older than the last modification
// an empty entity tag or a last modification of 0 are not used
CERVER_PUBLIC bool http_conditional_not_modified (
	const struct _HttpRequest *request,
	const char *etag, const size_t etag_length,
	const time_t last_modified
);

#ifdef __cplusplus
}
#endif

#endif
