/**
 * @file cerver/http/websockets/handshake.h
 * @brief The opening handshake of RFC 6455: the keys, the accept and the tokens of the headers.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_HANDSHAKE_H_
#define _CERVER_HTTP_WEBSOCKETS_HANDSHAKE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup http_websockets_handshake Handshake
 * @ingroup http_websockets
 * @brief The values and the checks of the upgrade of a request to a WebSocket connection.
 * @{
 */

/** @brief The only version of the protocol (RFC 6455 section 4.1). */
#define HTTP_WEB_SOCKET_VERSION						"13"

/** @brief The token of the Upgrade header. */
#define HTTP_WEB_SOCKET_UPGRADE_TOKEN				"websocket"

/** @brief The token of the Connection header. */
#define HTTP_WEB_SOCKET_CONNECTION_TOKEN			"Upgrade"

/** @brief Appended to the key of the client to compute the accept of the server. */
#define HTTP_WEB_SOCKET_GUID						"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/** @brief The random bytes of the key of the client. */
#define HTTP_WEB_SOCKET_KEY_BYTES					16

/** @brief The length of the key of the client: 16 bytes in base64. */
#define HTTP_WEB_SOCKET_KEY_LENGTH					24

/** @brief The size of a key, with its terminator. */
#define HTTP_WEB_SOCKET_KEY_SIZE					(HTTP_WEB_SOCKET_KEY_LENGTH + 1)

/** @brief The length of the accept: a SHA-1 (20 bytes) in base64. */
#define HTTP_WEB_SOCKET_ACCEPT_LENGTH				28

/** @brief The size of an accept, with its terminator. */
#define HTTP_WEB_SOCKET_ACCEPT_SIZE					(HTTP_WEB_SOCKET_ACCEPT_LENGTH + 1)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns whether a key is the base64 of 16 bytes, in the only form that they have (with its padding and
 * the unused bits in 0).
 *
 * @param[in] key the key, it can be NULL
 * @param[in] length the length of the key
 * @return true if it is valid
 */
CERVER_PUBLIC bool http_web_socket_key_is_valid (
	const char *key, const size_t length
);

/**
 * @brief Writes a new random key, with the random bytes of the kernel.
 *
 * @param[out] key where the key is written, #HTTP_WEB_SOCKET_KEY_SIZE bytes
 * @return true on success
 */
CERVER_PUBLIC bool http_web_socket_key_generate (char *key);

/**
 * @brief Writes the Sec-WebSocket-Accept of a key.
 *
 * @param[in] key the key
 * @param[in] length the length of the key, it must be #HTTP_WEB_SOCKET_KEY_LENGTH
 * @param[out] accept where the accept is written, #HTTP_WEB_SOCKET_ACCEPT_SIZE bytes
 * @return true on success
 */
CERVER_PUBLIC bool http_web_socket_accept_compute (
	const char *key, const size_t length, char *accept
);

/**
 * @brief Returns whether a header value, a list separated by commas (RFC 9110 section 5.6.1), has a token, without
 * regard to case.
 *
 * @param[in] value the value of the header
 * @param[in] length the length of the value
 * @param[in] token the token
 * @return true if the list has the token
 */
CERVER_PUBLIC bool http_web_socket_header_has_token (
	const char *value, const size_t length, const char *token
);

/**
 * @brief Returns the first subprotocol of the server that the client offered in its Sec-WebSocket-Protocol value.
 *
 * @param[in] offered the value of the header of the client
 * @param[in] length the length of the value
 * @param[in] protocols the subprotocols of the server, in its order of preference
 * @param[in] n_protocols the number of subprotocols
 * @return the subprotocol (one of protocols), NULL when there is none
 */
CERVER_PUBLIC const char *http_web_socket_protocol_select (
	const char *offered, const size_t length,
	const char * const *protocols, const unsigned int n_protocols
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
