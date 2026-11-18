/**
 * @file cerver/http/requests/url.h
 * @brief The percent-encoding of the urls.
 */

#ifndef _CERVER_HTTP_URL_H_
#define _CERVER_HTTP_URL_H_

#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_url Url
 * @ingroup http_requests
 * @brief The encoding and the decoding of the urls.
 * @{
 */

/**
 * @brief Returns the url-encoded version of a string.
 *
 * The letters, the digits and "-_.~" are kept, a space is a '+', and any other byte is a "%XX".
 *
 * @param[in] str the string
 * @return a new string, released with free (); NULL for a NULL string or if it could not be allocated
 */
CERVER_PUBLIC char *cerver_http_url_encode (const char *str);

/**
 * @brief Returns the url-decoded version of a string.
 *
 * A "%XX" is its byte and a '+' is a space; an incomplete or invalid escape is copied as it is.
 *
 * @param[in] str the string
 * @return a new string, released with free (); NULL for a NULL string or if it could not be allocated
 */
CERVER_PUBLIC char *cerver_http_url_decode (const char *str);

/**
 * @brief Percent-decodes a path segment.
 *
 * A '+' is kept (it is a space only in a query), and an incomplete or invalid escape is copied as it is; the result
 * does not end with '\0'.
 *
 * @param[out] destination the buffer, with room for length bytes
 * @param[in] source the segment
 * @param[in] length the bytes of the segment
 * @return the length of the decoded value
 */
CERVER_PUBLIC size_t cerver_http_url_decode_path (
	char *destination, const char *source, const size_t length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
