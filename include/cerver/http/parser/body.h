/**
 * @file cerver/http/parser/body.h
 * @brief The body of a message: its Content-Length, its chunks with their extensions and their trailers, or the end
 * of the connection.
 */

#ifndef _CERVER_HTTP_PARSER_BODY_H_
#define _CERVER_HTTP_PARSER_BODY_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/parser/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_parser_body Body
 * @ingroup http_parser
 * @brief Reads the body of a message in any piece.
 * @{
 */

/**
 * @brief Reads the body of a message, after its head.
 *
 * Each call gives at most one piece of the body, a view of the buffer: the caller calls again with the bytes after
 * the consumed ones until the result is #CERVER_HTTP_PARSER_RESULT_MORE (the buffer was read) or
 * #CERVER_HTTP_PARSER_RESULT_COMPLETE. A message without a body is complete at the first call. After the message,
 * the parser waits for the head of the next one (cerver_http_parser_reset () is not needed); an upgrade
 * (parser->upgrade) ends with its message.
 *
 * @param[in,out] parser the parser
 * @param[in] buffer the bytes that follow the ones that were consumed
 * @param[in] length the bytes of the buffer
 * @param[out] consumed the bytes of the buffer that the call read
 * @param[out] data the piece of the body, NULL without one
 * @param[out] data_length the bytes of the piece
 * @retval CERVER_HTTP_PARSER_RESULT_BODY a piece of the body
 * @retval CERVER_HTTP_PARSER_RESULT_MORE the buffer was read, the body needs more bytes
 * @retval CERVER_HTTP_PARSER_RESULT_COMPLETE the message is complete
 * @retval CERVER_HTTP_PARSER_RESULT_ERROR the body is not valid, the error of the parser says why
 * @since 3.0b-9
 */
CERVER_EXPORT CerverHttpParserResult cerver_http_parser_body (
	CerverHttpParser *parser,
	const char *buffer, const size_t length, size_t *consumed,
	const char **data, size_t *data_length
);

/**
 * @brief Tells a parser that its connection was closed.
 *
 * @param[in,out] parser the parser
 * @retval CERVER_HTTP_PARSER_RESULT_COMPLETE the body of a response that ends with the connection is complete
 * @retval CERVER_HTTP_PARSER_RESULT_MORE the parser was waiting for the head of a message that did not start
 * @retval CERVER_HTTP_PARSER_RESULT_ERROR a message was not complete (`CERVER_HTTP_PARSER_ERROR_INCOMPLETE`)
 * @since 3.0b-9
 */
CERVER_EXPORT CerverHttpParserResult cerver_http_parser_finish (
	CerverHttpParser *parser
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
