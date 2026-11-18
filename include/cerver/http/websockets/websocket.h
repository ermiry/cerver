/**
 * @file cerver/http/websockets/websocket.h
 * @brief A WebSocket connection (RFC 6455), of the server or of a client: its values, its data and how it sends.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_WEBSOCKET_H_
#define _CERVER_HTTP_WEBSOCKETS_WEBSOCKET_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/network/socket.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/message.h"
#include "cerver/http/websockets/settings.h"

/**
 * @defgroup http_websockets WebSockets
 * @ingroup http
 * @brief The WebSocket connections of an HTTP cerver and of the clients: the handshake, the frames, the messages,
 * the compression, the registry and the rooms.
 */

/**
 * @defgroup http_websockets_connection Connection
 * @ingroup http_websockets
 * @brief A WebSocket connection and how it sends.
 * @{
 */

/** @brief The bytes of each recv () of a connection. */
#define HTTP_WEB_SOCKET_RECEIVE_BUFFER_SIZE			(1 << 14)

/** @brief The random bytes that a client takes its masks from, renewed when they run out. */
#define HTTP_WEB_SOCKET_MASKS_SIZE					(1 << 8)

/** @brief The payload that a client masks at once before sending it. */
#define HTTP_WEB_SOCKET_SEND_CHUNK_SIZE				(1 << 14)

/** @brief The most rooms of a connection. */
#define HTTP_WEB_SOCKET_MAX_ROOMS					(1 << 4)

/** @brief The size of the name of a room, with its terminator. */
#define HTTP_WEB_SOCKET_ROOM_NAME_SIZE				(1 << 6)

/** @brief The bytes of the payload of the pings that cerver sends. */
#define HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE			8

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _HttpCerver;
struct _HttpEndpoint;
struct _HttpReceive;
struct _HttpRequest;
struct _HttpWebSocketClient;
struct CerverReceive;

/**
 * @brief The X-macro table of the states of a connection: the number, the name, the string and the description of
 * each one.
 */
#define HTTP_WEB_SOCKET_STATE_MAP(XX)																\
	XX(0,	CONNECTING,		Connecting,		The client has not finished its handshake)			\
	XX(1,	OPEN,			Open,			The connection sends and receives messages)			\
	XX(2,	CLOSING,		Closing,		A close frame was sent or received)					\
	XX(3,	CLOSED,			Closed,			The connection ended)

/**
 * @brief The state of a connection, one `HTTP_WEB_SOCKET_STATE_<name>` for each row of #HTTP_WEB_SOCKET_STATE_MAP.
 */
typedef enum HttpWebSocketState {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_STATE_##name = num,
	HTTP_WEB_SOCKET_STATE_MAP (XX)
	#undef XX

} HttpWebSocketState;

/**
 * @brief Returns the string of a state.
 *
 * @param[in] state the state
 * @return the string of its row in #HTTP_WEB_SOCKET_STATE_MAP, or the one of HTTP_WEB_SOCKET_STATE_CLOSED for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_state_to_string (const HttpWebSocketState state);

/**
 * @brief A WebSocket connection, of the server or of a client.
 *
 * Its fields are not part of the API: they change with the event loop and with TLS; the methods of this header and
 * of cerver/http/websockets/registry.h are the way to a connection.
 */
typedef struct _HttpWebSocket HttpWebSocket;

/**
 * @brief Returns the id of a connection.
 *
 * @param[in] web_socket the connection
 * @return the id, 0 for a NULL connection
 */
CERVER_EXPORT u64 http_web_socket_get_id (const HttpWebSocket *web_socket);

/**
 * @brief Returns the state of a connection.
 *
 * @param[in] web_socket the connection
 * @return the state, HTTP_WEB_SOCKET_STATE_CLOSED for a NULL connection
 * @threadsafety Safe from any thread, it is read with __atomic.
 */
CERVER_EXPORT HttpWebSocketState http_web_socket_get_state (const HttpWebSocket *web_socket);

/**
 * @brief Returns the side of a connection.
 *
 * @param[in] web_socket the connection
 * @return the role, HTTP_WEB_SOCKET_ROLE_SERVER for a NULL connection
 */
CERVER_EXPORT HttpWebSocketRole http_web_socket_get_role (const HttpWebSocket *web_socket);

/**
 * @brief Returns the address of the client (resolved with the trusted proxies) or of the server.
 *
 * @param[in] web_socket the connection
 * @return the address, NULL for a NULL connection
 */
CERVER_EXPORT const char *http_web_socket_get_ip (const HttpWebSocket *web_socket);

/**
 * @brief Returns the path of the request of the upgrade.
 *
 * @param[in] web_socket the connection
 * @return the path, NULL for a NULL connection
 */
CERVER_EXPORT const String *http_web_socket_get_path (const HttpWebSocket *web_socket);

/**
 * @brief Returns the query of the request of the upgrade as it was sent (without its ?, with its escapes).
 *
 * @param[in] web_socket the connection
 * @return the query, NULL when it had none; the connection of a client keeps its query in its path
 */
CERVER_EXPORT const String *http_web_socket_get_query (const HttpWebSocket *web_socket);

/**
 * @brief Returns the subprotocol that was agreed.
 *
 * @param[in] web_socket the connection
 * @return the subprotocol, NULL for none
 */
CERVER_EXPORT const char *http_web_socket_get_protocol (const HttpWebSocket *web_socket);

/**
 * @brief Returns whether permessage-deflate was negotiated.
 *
 * @param[in] web_socket the connection
 * @return true when the connection is compressed
 */
CERVER_EXPORT bool http_web_socket_is_compressed (const HttpWebSocket *web_socket);

/**
 * @brief Returns the HTTP cerver of a connection of the server.
 *
 * @param[in] web_socket the connection
 * @return the HTTP cerver, NULL for a NULL connection or one of a client
 */
CERVER_EXPORT struct _HttpCerver *http_web_socket_get_http_cerver (const HttpWebSocket *web_socket);

/**
 * @brief Returns the endpoint of a connection of the server.
 *
 * @param[in] web_socket the connection
 * @return the endpoint, NULL for a NULL connection or one of a client
 */
CERVER_EXPORT const struct _HttpEndpoint *http_web_socket_get_endpoint (const HttpWebSocket *web_socket);

/**
 * @brief Returns the custom data of the endpoint of a connection (cerver_http_endpoint_set_custom_data (), or the one that
 * it inherits).
 *
 * @param[in] web_socket the connection
 * @return the custom data, NULL without one
 */
CERVER_EXPORT void *http_web_socket_get_endpoint_data (const HttpWebSocket *web_socket);

/**
 * @brief Returns the decoded data that the connection adopted in on_upgrade ().
 *
 * @param[in] web_socket the connection
 * @return the decoded data, NULL without one
 */
CERVER_EXPORT void *http_web_socket_get_decoded_data (const HttpWebSocket *web_socket);

/**
 * @brief Sets the data of a connection; the previous one is deleted (unless it is the same data).
 *
 * @param[in,out] web_socket the connection
 * @param[in] data the data
 * @param[in] delete_data releases the data when the connection is released, NULL to keep it
 */
CERVER_EXPORT void http_web_socket_set_data (
	HttpWebSocket *web_socket, void *data, void (*delete_data)(void *)
);

/**
 * @brief Returns the data of a connection.
 *
 * @param[in] web_socket the connection
 * @return the data, NULL without one
 */
CERVER_EXPORT void *http_web_socket_get_data (const HttpWebSocket *web_socket);

/**
 * @brief Takes a reference, so the connection stays valid outside of its methods; a send of a connection that
 * ended returns an error.
 *
 * @param[in,out] web_socket the connection, it can be NULL
 * @return the connection
 * @threadsafety Safe from any thread, the references are counted with __atomic.
 */
CERVER_EXPORT HttpWebSocket *http_web_socket_retain (HttpWebSocket *web_socket);

/**
 * @brief Releases a reference; the last one deletes the connection.
 *
 * @param[in] web_socket the connection, it can be NULL
 * @threadsafety Safe from any thread, the references are counted with __atomic.
 */
CERVER_EXPORT void http_web_socket_release (HttpWebSocket *web_socket);

/**
 * @brief Sends a whole message, compressed when it was negotiated, with the send timeout of the connection.
 *
 * @param[in,out] web_socket the connection
 * @param[in] type HTTP_WEB_SOCKET_OPCODE_TEXT or HTTP_WEB_SOCKET_OPCODE_BINARY
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @retval 0 the message was sent
 * @retval 1 it was not sent (a NULL argument, a connection that is not open, a send that failed)
 * @threadsafety It can be called from any thread, the messages are never mixed.
 */
CERVER_EXPORT u8 http_web_socket_send (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

/**
 * @brief Sends a text message, like http_web_socket_send ().
 *
 * @param[in,out] web_socket the connection
 * @param[in] text the text, in UTF-8
 * @param[in] length the bytes of the text
 * @retval 0 the message was sent
 * @retval 1 it was not sent
 */
CERVER_EXPORT u8 http_web_socket_send_text (
	HttpWebSocket *web_socket, const char *text, const size_t length
);

/**
 * @brief Sends a binary message, like http_web_socket_send ().
 *
 * @param[in,out] web_socket the connection
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @retval 0 the message was sent
 * @retval 1 it was not sent
 */
CERVER_EXPORT u8 http_web_socket_send_binary (
	HttpWebSocket *web_socket, const void *data, const size_t size
);

/**
 * @brief Starts a message that is sent in pieces, until http_web_socket_send_end ().
 *
 * The other messages of the connection wait for it, the control frames do not. A stream that does not begin (the
 * connection is closing or it has ended) holds nothing: it must not be ended, and its pieces and its end return an
 * error. A stream that began is always ended with http_web_socket_send_end (), also when a piece failed: a piece
 * that the connection did not take because it is closing is an error that does not abort it.
 *
 * @param[in,out] web_socket the connection
 * @param[in] type HTTP_WEB_SOCKET_OPCODE_TEXT or HTTP_WEB_SOCKET_OPCODE_BINARY
 * @retval 0 the stream began
 * @retval 1 it did not begin
 * @note The stream holds the mutex of the messages of the connection from its begin to its end, so its thread owns
 * it: a piece or an end from another thread returns 1 and changes nothing (since 3.0b-4; it unlocked the mutex of
 * the owner).
 */
CERVER_EXPORT u8 http_web_socket_send_begin (
	HttpWebSocket *web_socket, const HttpWebSocketOpcode type
);

/**
 * @brief Sends the next piece of a message started with http_web_socket_send_begin (); a text can be split
 * anywhere, also inside a code point.
 *
 * @param[in,out] web_socket the connection
 * @param[in] data the piece, NULL only when size is 0
 * @param[in] size the bytes of the piece
 * @retval 0 the piece was sent (or kept by the compressor)
 * @retval 1 a stream that did not begin, a thread that did not begin it, or the piece was not sent
 */
CERVER_EXPORT u8 http_web_socket_send_write (
	HttpWebSocket *web_socket, const void *data, const size_t size
);

/**
 * @brief Ends a message started with http_web_socket_send_begin (); the other messages can be sent again.
 *
 * @param[in,out] web_socket the connection
 * @retval 0 the end of the message was sent
 * @retval 1 a stream that did not begin, a thread that did not begin it (the stream goes on), or the end was not
 * sent (the stream is ended anyway)
 */
CERVER_EXPORT u8 http_web_socket_send_end (HttpWebSocket *web_socket);

/**
 * @brief Sends a ping, that the peer answers with a pong.
 *
 * @param[in,out] web_socket the connection
 * @param[in] data the payload, NULL only when size is 0
 * @param[in] size the bytes of the payload, up to #HTTP_WEB_SOCKET_CONTROL_MAX_SIZE
 * @retval 0 the ping was sent
 * @retval 1 a NULL argument, a connection that is not open, or it was not sent
 */
CERVER_EXPORT u8 http_web_socket_ping (
	HttpWebSocket *web_socket, const void *data, const size_t size
);

/**
 * @brief Starts the closing handshake; the connection ends when the peer answers, or after the close timeout.
 *
 * @param[in,out] web_socket the connection
 * @param[in] code a code that can be sent (http_web_socket_close_code_is_valid ())
 * @param[in] reason the reason, NULL or up to #HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE bytes in UTF-8
 * @retval 0 the close frame was sent, the connection was open or closing
 * @retval 1 a NULL connection, a code or a reason that is not valid, or a connection that is not open
 */
CERVER_EXPORT u8 http_web_socket_close (
	HttpWebSocket *web_socket, const u16 code, const char *reason
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
