/**
 * @file cerver/http/headers/header.h
 * @brief A header with its value in a fixed buffer, like the headers of a part of a multipart body.
 */

#ifndef _CERVER_HTTP_HEADERS_HEADER_H_
#define _CERVER_HTTP_HEADERS_HEADER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_headers_header Header
 * @ingroup http_headers
 * @brief A header in a fixed buffer.
 * @{
 */

/** @brief The size of the value of a header, with its terminator. */
#define HTTP_HEADER_VALUE_SIZE			256

/**
 * @brief The size of a temporary header.
 *
 * @note cerver does not use it.
 */
#define HTTP_HEADER_TEMP_SIZE			128
/**
 * @brief The size of an actual header.
 *
 * @note cerver does not use it.
 */
#define HTTP_HEADER_ACTUAL_SIZE			128
/**
 * @brief A header with its value in a fixed buffer.
 */
struct _HttpHeader {

	/** @brief The length of the value. */
	int len;
	/** @brief The value. */
	char value[HTTP_HEADER_VALUE_SIZE];

};

/** @brief A header in a fixed buffer, see struct _HttpHeader. */
typedef struct _HttpHeader HttpHeader;

/**
 * @brief Sets the length and every byte of the value of a header to zero.
 *
 * @param[out] header the header, it must not be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_header_reset (HttpHeader *header);

/**
 * @brief Sets the value of a header from a format.
 *
 * @param[out] header the header, it must not be NULL
 * @param[in] format the printf () format of the value
 * @param[in] ... the arguments of the format
 * @note A value that does not fit is truncated and its length is the one that was kept; a format that fails leaves
 * it empty. For the internal methods of cerver.
 */
CERVER_PRIVATE void http_header_set (
	HttpHeader *header, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Prints the length and the value of a header to stdout.
 *
 * @param[in] header the header, it must not be NULL
 */
CERVER_PUBLIC void http_header_print (
	const HttpHeader *header
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
