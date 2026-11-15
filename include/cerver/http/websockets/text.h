/**
 * @file cerver/http/websockets/text.h
 * @brief The incremental UTF-8 validation of the text messages (RFC 3629).
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_TEXT_H_
#define _CERVER_HTTP_WEBSOCKETS_TEXT_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_websockets_text Text
 * @ingroup http_websockets
 * @brief Validating a text message that can come in many fragments or reads.
 * @{
 */

/**
 * @brief The state of the UTF-8 validation of a text message, that keeps a code point split between fragments or
 * reads.
 */
typedef struct HttpWebSocketText {

	/** @brief The continuation bytes that the current code point still needs. */
	u8 remaining;

	/**
	 * @brief The lower bound of the next continuation byte, narrower after some lead bytes to reject the overlong
	 * forms, the surrogates and the code points above U+10FFFF.
	 */
	u8 lower;
	/** @brief The upper bound of the next continuation byte. */
	u8 upper;

} HttpWebSocketText;

/**
 * @brief Starts the validation of a new text.
 *
 * @param[out] text the state, it can be NULL
 */
CERVER_PUBLIC void http_web_socket_text_init (HttpWebSocketText *text);

/**
 * @brief Validates the next bytes of a text; a code point can continue in the next call.
 *
 * @param[in,out] text the state
 * @param[in] data the bytes, NULL only when size is 0
 * @param[in] size the number of bytes
 * @return false at the first byte that can not be part of valid UTF-8 (so a text fails as soon as it can not be
 * completed), or for a NULL argument
 */
CERVER_PUBLIC bool http_web_socket_text_validate (
	HttpWebSocketText *text, const void *data, const size_t size
);

/**
 * @brief Returns whether the text ends with a complete code point.
 *
 * @param[in] text the state
 * @return true when no code point is waiting for its continuation bytes; false for a NULL state
 */
CERVER_PUBLIC bool http_web_socket_text_complete (const HttpWebSocketText *text);

/**
 * @brief Returns whether some bytes are a whole valid UTF-8 text.
 *
 * @param[in] data the bytes
 * @param[in] size the number of bytes
 * @return true when they are valid and complete
 */
CERVER_PUBLIC bool http_web_socket_text_check (const void *data, const size_t size);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
