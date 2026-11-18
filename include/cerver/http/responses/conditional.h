/**
 * @file cerver/http/responses/conditional.h
 * @brief The entity tags and the conditional requests (304 Not Modified).
 */

#ifndef _CERVER_HTTP_CONDITIONAL_H_
#define _CERVER_HTTP_CONDITIONAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <sys/stat.h>

#include "cerver/config.h"

/**
 * @defgroup http_responses_conditional Conditional
 * @ingroup http_responses
 * @brief The entity tags of the bodies and the files, and the checks of If-None-Match and If-Modified-Since.
 * @{
 */

/**
 * @brief The size of the longest entity tag that cerver generates, with its terminator.
 *
 * A weak one of a file is `W/"<size>-<mtime>"` and a strong one of a body is `"<hash>-gzip"`.
 */
#define HTTP_ETAG_SIZE					48

/**
 * @brief The suffix of the entity tag of a compressed body: a compressed representation is a different one
 * (RFC 9110 section 8.8.3.3).
 */
#define HTTP_ETAG_COMPRESSED_SUFFIX		"-gzip"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpRequest;

/**
 * @brief Writes the strong entity tag of a body: the FNV-1a of its bytes, with the compressed suffix if the body
 * is sent compressed.
 *
 * @param[in] data the body, it can be NULL only when length is 0
 * @param[in] length the length of the body
 * @param[in] compressed true if the body is sent compressed
 * @param[out] etag where the entity tag is written, at least #HTTP_ETAG_SIZE bytes
 * @return the length of the entity tag, 0 on error
 */
CERVER_PUBLIC size_t cerver_http_etag_from_data (
	const void *data, const size_t length,
	const bool compressed, char *etag
);

/**
 * @brief Writes the weak entity tag of a file, from its size and its modification time.
 *
 * @param[in] file_status the status of the file
 * @param[out] etag where the entity tag is written, at least #HTTP_ETAG_SIZE bytes
 * @return the length of the entity tag, 0 on error
 */
CERVER_PUBLIC size_t cerver_http_etag_from_file (
	const struct stat *file_status, char *etag
);

/**
 * @brief Returns whether an If-None-Match value ("*" or a list of entity tags) matches an entity tag, with the weak
 * comparison (RFC 9110 section 13.1.2).
 *
 * @param[in] if_none_match the value of the header
 * @param[in] if_none_match_length the length of the value
 * @param[in] etag the entity tag
 * @param[in] etag_length the length of the entity tag
 * @return true if it matches; false for a NULL argument or an empty entity tag
 */
CERVER_PUBLIC bool cerver_http_etag_none_match (
	const char *if_none_match, const size_t if_none_match_length,
	const char *etag, const size_t etag_length
);

/**
 * @brief Returns whether a GET or a HEAD request can be answered with 304 Not Modified.
 *
 * Its If-None-Match matches the entity tag, or it has no If-None-Match and its If-Modified-Since (an IMF-fixdate) is
 * not older than the last modification.
 *
 * @param[in] request the request
 * @param[in] etag the entity tag of the response, an empty one is not used
 * @param[in] etag_length the length of the entity tag
 * @param[in] last_modified the last modification of the response, 0 is not used
 * @return true if the response can be 304; false for a NULL request or another method
 */
CERVER_PUBLIC bool cerver_http_conditional_not_modified (
	const struct _HttpRequest *request,
	const char *etag, const size_t etag_length,
	const time_t last_modified
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
