#ifndef _CERVER_HTTP_WEBSOCKETS_HANDSHAKE_H_
#define _CERVER_HTTP_WEBSOCKETS_HANDSHAKE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

// the only version of the protocol (RFC 6455 section 4.1)
#define HTTP_WEB_SOCKET_VERSION						"13"

// the token of the Upgrade header
#define HTTP_WEB_SOCKET_UPGRADE_TOKEN				"websocket"

// the token of the Connection header
#define HTTP_WEB_SOCKET_CONNECTION_TOKEN			"Upgrade"

// appended to the key of the client to compute the accept of the server
#define HTTP_WEB_SOCKET_GUID						"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// the key of the client is 16 random bytes in base64
#define HTTP_WEB_SOCKET_KEY_BYTES					16
#define HTTP_WEB_SOCKET_KEY_LENGTH					24
#define HTTP_WEB_SOCKET_KEY_SIZE					(HTTP_WEB_SOCKET_KEY_LENGTH + 1)

// the accept is a SHA-1 (20 bytes) in base64
#define HTTP_WEB_SOCKET_ACCEPT_LENGTH				28
#define HTTP_WEB_SOCKET_ACCEPT_SIZE					(HTTP_WEB_SOCKET_ACCEPT_LENGTH + 1)

#ifdef __cplusplus
extern "C" {
#endif

// returns true when the key is the base64 of 16 bytes
CERVER_PUBLIC bool http_web_socket_key_is_valid (
	const char *key, const size_t length
);

// writes a new random key in key, that has HTTP_WEB_SOCKET_KEY_SIZE bytes
// returns true on success
CERVER_PUBLIC bool http_web_socket_key_generate (char *key);

// writes the Sec-WebSocket-Accept of the key in accept, that has HTTP_WEB_SOCKET_ACCEPT_SIZE bytes
// returns true on success
CERVER_PUBLIC bool http_web_socket_accept_compute (
	const char *key, const size_t length, char *accept
);

// returns true when a header value, a list separated by commas (RFC 9110 section 5.6.1),
// has the token, without regard to case
CERVER_PUBLIC bool http_web_socket_header_has_token (
	const char *value, const size_t length, const char *token
);

// returns the first protocol of the server that the client offered
// in its Sec-WebSocket-Protocol value, NULL when there is none
CERVER_PUBLIC const char *http_web_socket_protocol_select (
	const char *offered, const size_t length,
	const char * const *protocols, const unsigned int n_protocols
);

#ifdef __cplusplus
}
#endif

#endif
