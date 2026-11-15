/**
 * @file cerver/http/headers/header.h
 * @brief A header with its value in a fixed buffer: the headers of a response, of the responses of a cerver and of a
 * part of a multipart body.
 */

#ifndef _CERVER_HTTP_HEADERS_HEADER_H_
#define _CERVER_HTTP_HEADERS_HEADER_H_

#include <stdarg.h>
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

/** @brief The size of the value of a header, with its terminator; for a response it is the whole line. */
#define HTTP_HEADER_VALUE_SIZE			256

/**
 * @brief The size of a temporary header.
 *
 * @note cerver does not use it.
 */
#define HTTP_HEADER_TEMP_SIZE			128

/** @brief The size of the value of a response header that is formatted, with its terminator. */
#define HTTP_HEADER_ACTUAL_SIZE			128

/**
 * @brief A header with its value in a fixed buffer.
 */
struct _HttpHeader {

	/** @brief The length of the value. */
	int len;
	/** @brief The value; for a response, the line `Name: value` and its CRLF. */
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
 * @note A value that does not fit is truncated and its length is the one that was kept. For the internal methods
 * of cerver.
 */
CERVER_PRIVATE void http_header_set (
	HttpHeader *header, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Sets the line of a header of a response, `Name: value` and its CRLF, with the name of its type.
 *
 * @param[out] header the header, it must not be NULL
 * @param[in] type the type of the header
 * @param[in] actual_header the value
 * @retval 0 the line was set
 * @retval 1 the line does not fit in #HTTP_HEADER_VALUE_SIZE, then the header keeps what it had
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_header_set_with_type (
	HttpHeader *header, const http_header type,
	const char *actual_header
);

/**
 * @brief Works like http_response_header_set_with_type () with a value that is formatted.
 *
 * @param[out] header the header, it must not be NULL
 * @param[in] type the type of the header
 * @param[in] format the printf () format of the value
 * @param[in] args the arguments of the format
 * @retval 0 the line was set
 * @retval 1 the value does not fit in #HTTP_HEADER_ACTUAL_SIZE, or the line in #HTTP_HEADER_VALUE_SIZE
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_header_set_with_type_and_args (
	HttpHeader *header, const http_header type,
	const char *format, va_list args
) CERVER_PRINTF_FORMAT (3, 0);

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
