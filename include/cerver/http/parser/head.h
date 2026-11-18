/**
 * @file cerver/http/parser/head.h
 * @brief The head of a message: its start line and its fields.
 */

#ifndef _CERVER_HTTP_PARSER_HEAD_H_
#define _CERVER_HTTP_PARSER_HEAD_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/headers/types.h"
#include "cerver/http/parser/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_parser_head Head
 * @ingroup http_parser
 * @brief Reads the head of a message and asks for its parts.
 * @{
 */

/**
 * @brief Reads the head of a message.
 *
 * The buffer has the bytes of the head from its first one: a head that arrives in pieces is given each time with
 * every byte that arrived before it, and the parser goes on from the last line that it read. The lines are read
 * when they are complete, and a line that is not yet complete is checked against the limits.
 *
 * A request accepts the empty lines before its request line (RFC 9112 section 2.2), the methods of
 * #REQUEST_METHOD_MAP and PRI, the four forms of its target (RFC 9112 section 3.2) and every version HTTP/1.x, with
 * a minor above 1 read as HTTP/1.1 (RFC 9112 section 2.3); HTTP/0.9 and HTTP/2.0 are
 * `CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED`, and any other version needs `HTTP_PARSER_LENIENT_VERSION`. A
 * method that is a token, not longer than the longest one of the table, is
 * `CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN` (since the 3.0b-10); any other one is `CERVER_HTTP_PARSER_ERROR_METHOD`.
 *
 * @param[in,out] parser the parser
 * @param[in] buffer the bytes of the head from its first one, they can be followed by others
 * @param[in] length the bytes of the buffer
 * @param[out] consumed the bytes of the head when it is complete, 0 otherwise
 * @retval CERVER_HTTP_PARSER_RESULT_HEAD the head is complete, in parser->head
 * @retval CERVER_HTTP_PARSER_RESULT_MORE the head needs more bytes
 * @retval CERVER_HTTP_PARSER_RESULT_FIELDS the array of the fields is full
 * @retval CERVER_HTTP_PARSER_RESULT_ERROR the head is not valid, its error says why
 * @since 3.0b-9
 */
CERVER_EXPORT CerverHttpParserResult cerver_http_parser_head (
	CerverHttpParser *parser,
	const char *buffer, const size_t length, size_t *consumed
);

/**
 * @brief Returns the first byte of a span of a head.
 *
 * @param[in] head the head
 * @param[in] span the span
 * @return a pointer in the base of the head; the span is not terminated
 * @since 3.0b-9
 */
CERVER_EXPORT const char *cerver_http_head_at (
	const CerverHttpHead *head, const CerverHttpSpan span
);

/**
 * @brief Returns the first field of a head with a known type.
 *
 * @param[in] head the head
 * @param[in] type the type of the header
 * @return the field, NULL if the head does not have it
 * @since 3.0b-9
 */
CERVER_EXPORT const CerverHttpField *cerver_http_head_field_with_type (
	const CerverHttpHead *head, const CerverHttpHeader type
);

/**
 * @brief Returns the first field of a head with a name (case insensitive).
 *
 * @param[in] head the head
 * @param[in] name the name
 * @param[in] length the bytes of the name
 * @return the field, NULL if the head does not have it
 * @since 3.0b-9
 */
CERVER_EXPORT const CerverHttpField *cerver_http_head_field_with_name (
	const CerverHttpHead *head, const char *name, const size_t length
);

/**
 * @brief Returns the known header of a name (case insensitive).
 *
 * @param[in] name the name, it does not need to be terminated
 * @param[in] length the bytes of the name
 * @return its header of #HTTP_HEADERS_MAP, `HTTP_HEADER_UNDEFINED` for any other one
 * @threadsafety Safe from any thread.
 * @since 3.0b-9
 */
CERVER_EXPORT CerverHttpHeader cerver_http_parser_field_type (
	const char *name, const size_t length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
