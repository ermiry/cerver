#ifndef _CERVER_HTTP_WEBSOCKETS_CLIENT_H_
#define _CERVER_HTTP_WEBSOCKETS_CLIENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/network.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/websocket.h"

// the scheme of the URLs, wss:// arrives with TLS
#define HTTP_WEB_SOCKET_CLIENT_SCHEME				"ws://"
#define HTTP_WEB_SOCKET_CLIENT_SECURE_SCHEME		"wss://"
#define HTTP_WEB_SOCKET_CLIENT_DEFAULT_PORT			80

// the bytes of the host, the path and each value of the response that is checked
#define HTTP_WEB_SOCKET_CLIENT_HOST_SIZE			(1 << 8)
#define HTTP_WEB_SOCKET_CLIENT_PATH_SIZE			(1 << 11)
#define HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE			(1 << 8)

// the most bytes of the response of the handshake
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE		(1 << 13)

// the most bytes of the request of the handshake
#define HTTP_WEB_SOCKET_CLIENT_REQUEST_SIZE			(1 << 13)

// milliseconds of the connect () of each address and of the handshake
#define HTTP_WEB_SOCKET_CLIENT_CONNECT_TIMEOUT		((1 << 3) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)
#define HTTP_WEB_SOCKET_CLIENT_HANDSHAKE_TIMEOUT	((1 << 3) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

// milliseconds of the first and the last wait between reconnections, doubled after each failure
#define HTTP_WEB_SOCKET_CLIENT_RECONNECT_MIN		(1 << 8)
#define HTTP_WEB_SOCKET_CLIENT_RECONNECT_MAX		(1 << 15)

#ifdef __cplusplus
extern "C" {
#endif

#pragma region client

// a WebSocket client: its url, its settings and the connection that it has now
// its fields are not part of the API, the methods of this header are the way to a client
typedef struct _HttpWebSocketClient HttpWebSocketClient;

CERVER_EXPORT HttpWebSocketClient *http_web_socket_client_create (void);

// closes the connection with 1000, waits for its thread and releases the client
// it is the last call of a client, from a thread that is not running one of its methods:
// a method (on_open, on_message, on_close, on_error) that deletes its client only closes it
CERVER_EXPORT void http_web_socket_client_delete (HttpWebSocketClient *client);

// ws://host[:port][/path][?query], the host can be an IPv6 in brackets
// returns 0 on success, 1 on a URL that is not valid (wss:// is not supported yet)
CERVER_EXPORT u8 http_web_socket_client_set_url (
	HttpWebSocketClient *client, const char *url
);

// adds a header to the request of the handshake, like an Authorization
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_client_add_header (
	HttpWebSocketClient *client, const char *name, const char *value
);

// the subprotocols that are offered, in order of preference
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_client_set_protocols (
	HttpWebSocketClient *client, const char * const *protocols, const unsigned int n_protocols
);

CERVER_EXPORT void http_web_socket_client_set_handlers (
	HttpWebSocketClient *client, const HttpWebSocketHandlers *handlers
);

// the data that every connection of the client has (http_web_socket_get_data ()),
// it is not owned by the client, and it is set before the connection exists
CERVER_EXPORT void http_web_socket_client_set_data (HttpWebSocketClient *client, void *data);

CERVER_EXPORT void *http_web_socket_client_get_data (const HttpWebSocketClient *client);

// offers permessage-deflate with the default options
CERVER_EXPORT void http_web_socket_client_set_compression (
	HttpWebSocketClient *client, const bool compression
);

CERVER_EXPORT void http_web_socket_client_set_compression_options (
	HttpWebSocketClient *client, const HttpWebSocketDeflateOptions *options
);

CERVER_EXPORT void http_web_socket_client_set_max_message (
	HttpWebSocketClient *client, const size_t max_message
);

CERVER_EXPORT void http_web_socket_client_set_ping_milliseconds (
	HttpWebSocketClient *client, const unsigned int ping_interval, const unsigned int pong_timeout
);

CERVER_EXPORT void http_web_socket_client_set_close_timeout_milliseconds (
	HttpWebSocketClient *client, const unsigned int close_timeout
);

CERVER_EXPORT void http_web_socket_client_set_send_timeout_milliseconds (
	HttpWebSocketClient *client, const unsigned int send_timeout
);

// the milliseconds of the connect () of each address and of the handshake, 0 keeps the defaults
CERVER_EXPORT void http_web_socket_client_set_timeouts_milliseconds (
	HttpWebSocketClient *client, const unsigned int connect_timeout, const unsigned int handshake_timeout
);

// reconnects when the connection ends without http_web_socket_client_close (),
// waiting from min to max milliseconds (doubled after each failure, with jitter, never above max)
// a connection that ends before it lasted max milliseconds counts as a failure,
// so a server that accepts and closes is not reconnected without a wait
CERVER_EXPORT void http_web_socket_client_set_reconnect_milliseconds (
	HttpWebSocketClient *client, const bool reconnect,
	const unsigned int min, const unsigned int max
);

// connects in the current thread and returns after the handshake,
// the connection runs in its own thread, where every method is called
// returns 0 on success, 1 on error (on_error and on_close were called, also when
// the connection opened and its thread could not be created)
CERVER_EXPORT u8 http_web_socket_client_connect (HttpWebSocketClient *client);

// connects in the thread of the connection, the methods report the result
// returns 0 when the thread started, 1 on error
CERVER_EXPORT u8 http_web_socket_client_connect_async (HttpWebSocketClient *client);

// the connection of now with a reference (http_web_socket_release ()), NULL between connections
CERVER_EXPORT HttpWebSocket *http_web_socket_client_get_web_socket (HttpWebSocketClient *client);

// sends a whole message with the connection of now
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_client_send (
	HttpWebSocketClient *client,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

CERVER_EXPORT u8 http_web_socket_client_send_text (
	HttpWebSocketClient *client, const char *text, const size_t length
);

CERVER_EXPORT u8 http_web_socket_client_send_binary (
	HttpWebSocketClient *client, const void *data, const size_t size
);

// stops the reconnections and starts the closing handshake of the connection of now
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_client_close (
	HttpWebSocketClient *client, const u16 code, const char *reason
);

// waits until the thread of the client ends, after a close or without reconnections
// a method of the client does not wait: it runs in that thread
CERVER_EXPORT void http_web_socket_client_wait (HttpWebSocketClient *client);

// the connections that were opened
CERVER_EXPORT unsigned int http_web_socket_client_get_connections (const HttpWebSocketClient *client);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
