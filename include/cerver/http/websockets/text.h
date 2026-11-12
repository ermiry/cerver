#ifndef _CERVER_HTTP_WEBSOCKETS_TEXT_H_
#define _CERVER_HTTP_WEBSOCKETS_TEXT_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

// the state of the UTF-8 validation of a text message (RFC 3629),
// that keeps a code point split between fragments or reads
typedef struct HttpWebSocketText {

	// the continuation bytes that the current code point still needs
	u8 remaining;

	// the range of the next continuation byte,
	// narrower after some lead bytes to reject overlong forms,
	// surrogates and code points above U+10FFFF
	u8 lower;
	u8 upper;

} HttpWebSocketText;

// starts the validation of a new text
CERVER_PUBLIC void http_web_socket_text_init (HttpWebSocketText *text);

// validates the next bytes of the text, a code point can continue in the next call
// returns false at the first byte that can not be part of valid UTF-8,
// so a text fails as soon as it can not be completed
CERVER_PUBLIC bool http_web_socket_text_validate (
	HttpWebSocketText *text, const void *data, const size_t size
);

// returns true when the text ends with a complete code point
CERVER_PUBLIC bool http_web_socket_text_complete (const HttpWebSocketText *text);

// returns true when the bytes are a whole valid UTF-8 text
CERVER_PUBLIC bool http_web_socket_text_check (const void *data, const size_t size);

#ifdef __cplusplus
}
#endif

#endif
