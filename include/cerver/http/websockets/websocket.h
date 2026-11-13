#ifndef _CERVER_HTTP_WEBSOCKETS_WEBSOCKET_H_
#define _CERVER_HTTP_WEBSOCKETS_WEBSOCKET_H_

#include <stdbool.h>
#include <stddef.h>

#include <pthread.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"
#include "cerver/connection.h"
#include "cerver/socket.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/message.h"
#include "cerver/http/websockets/settings.h"

// the bytes of each recv () of a connection
#define HTTP_WEB_SOCKET_RECEIVE_BUFFER_SIZE			(1 << 14)

// the random bytes that a client takes its masks from, renewed when they run out
#define HTTP_WEB_SOCKET_MASKS_SIZE					(1 << 8)

// the payload that a client masks at once before sending it
#define HTTP_WEB_SOCKET_SEND_CHUNK_SIZE				(1 << 14)

// the rooms of a connection, and the bytes of each name with its '\0'
#define HTTP_WEB_SOCKET_MAX_ROOMS					(1 << 4)
#define HTTP_WEB_SOCKET_ROOM_NAME_SIZE				(1 << 6)

// the bytes of the payload of the pings that cerver sends
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

#define HTTP_WEB_SOCKET_STATE_MAP(XX)																\
	XX(0,	CONNECTING,		Connecting,		The client has not finished its handshake)			\
	XX(1,	OPEN,			Open,			The connection sends and receives messages)			\
	XX(2,	CLOSING,		Closing,		A close frame was sent or received)					\
	XX(3,	CLOSED,			Closed,			The connection ended)

typedef enum HttpWebSocketState {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_STATE_##name = num,
	HTTP_WEB_SOCKET_STATE_MAP (XX)
	#undef XX

} HttpWebSocketState;

CERVER_PUBLIC const char *http_web_socket_state_to_string (const HttpWebSocketState state);

// a WebSocket connection, of the server or of a client
// its fields are not part of the API: they change with the event loop and with TLS,
// the methods of this header and of registry.h are the way to a connection
typedef struct _HttpWebSocket HttpWebSocket;

#pragma region public

CERVER_EXPORT u64 http_web_socket_get_id (const HttpWebSocket *web_socket);

CERVER_EXPORT HttpWebSocketState http_web_socket_get_state (const HttpWebSocket *web_socket);

CERVER_EXPORT HttpWebSocketRole http_web_socket_get_role (const HttpWebSocket *web_socket);

// the address of the client (resolved with the trusted proxies) or of the server
CERVER_EXPORT const char *http_web_socket_get_ip (const HttpWebSocket *web_socket);

// the path of the request of the upgrade
CERVER_EXPORT const String *http_web_socket_get_path (const HttpWebSocket *web_socket);

// the query of the request of the upgrade as it was sent (without its ?, with its escapes),
// NULL when it had none; the connection of a client keeps its query in its path
CERVER_EXPORT const String *http_web_socket_get_query (const HttpWebSocket *web_socket);

// the subprotocol that was agreed, NULL for none
CERVER_EXPORT const char *http_web_socket_get_protocol (const HttpWebSocket *web_socket);

// returns true when permessage-deflate was negotiated
CERVER_EXPORT bool http_web_socket_is_compressed (const HttpWebSocket *web_socket);

CERVER_EXPORT struct _HttpCerver *http_web_socket_get_http_cerver (const HttpWebSocket *web_socket);

CERVER_EXPORT const struct _HttpEndpoint *http_web_socket_get_endpoint (const HttpWebSocket *web_socket);

// the custom data of the endpoint (http_endpoint_set_custom_data ())
CERVER_EXPORT void *http_web_socket_get_endpoint_data (const HttpWebSocket *web_socket);

// the decoded data that the connection adopted in on_upgrade ()
CERVER_EXPORT void *http_web_socket_get_decoded_data (const HttpWebSocket *web_socket);

// sets the data of the connection, the previous one is deleted,
// it is deleted with delete_data when the connection is released
CERVER_EXPORT void http_web_socket_set_data (
	HttpWebSocket *web_socket, void *data, void (*delete_data)(void *)
);

CERVER_EXPORT void *http_web_socket_get_data (const HttpWebSocket *web_socket);

// takes a reference, so the connection stays valid outside of its methods,
// a send of a connection that ended returns an error
CERVER_EXPORT HttpWebSocket *http_web_socket_retain (HttpWebSocket *web_socket);

// releases a reference, the last one deletes the connection
CERVER_EXPORT void http_web_socket_release (HttpWebSocket *web_socket);

// sends a whole message, compressed when it was negotiated,
// it can be called from any thread, the messages are never mixed
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_send (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

CERVER_EXPORT u8 http_web_socket_send_text (
	HttpWebSocket *web_socket, const char *text, const size_t length
);

CERVER_EXPORT u8 http_web_socket_send_binary (
	HttpWebSocket *web_socket, const void *data, const size_t size
);

// starts a message that is sent in pieces, until http_web_socket_send_end (),
// the other messages of the connection wait for it, the control frames do not
// a stream that does not begin (the connection is closing or it has ended) holds nothing:
// it must not be ended, and its pieces and its end return an error
// a stream that began is always ended with http_web_socket_send_end (), also when a piece failed:
// a piece that the connection did not take because it is closing is an error that does not abort it
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_send_begin (
	HttpWebSocket *web_socket, const HttpWebSocketOpcode type
);

// sends the next piece of the message (a text can be split anywhere, also inside a code point)
CERVER_EXPORT u8 http_web_socket_send_write (
	HttpWebSocket *web_socket, const void *data, const size_t size
);

// ends the message, the other messages can be sent again
CERVER_EXPORT u8 http_web_socket_send_end (HttpWebSocket *web_socket);

// sends a ping, that the peer answers with a pong
CERVER_EXPORT u8 http_web_socket_ping (
	HttpWebSocket *web_socket, const void *data, const size_t size
);

// starts the closing handshake, with a code that can be sent and a reason of 123 bytes or less in UTF-8,
// the connection ends when the peer answers, or after the close timeout
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_close (
	HttpWebSocket *web_socket, const u16 code, const char *reason
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
