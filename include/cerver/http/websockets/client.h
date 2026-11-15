/**
 * @file cerver/http/websockets/client.h
 * @brief A WebSocket client: its url, its settings, its connection and its reconnections.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_CLIENT_H_
#define _CERVER_HTTP_WEBSOCKETS_CLIENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/network/network.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/websocket.h"

/**
 * @defgroup http_websockets_client Client
 * @ingroup http_websockets
 * @brief A client that connects to a WebSocket server and can reconnect.
 *
 * The setters take effect in the next connection and ignore a NULL client.
 * @{
 */

/** @brief The scheme of the URLs of the client. */
#define HTTP_WEB_SOCKET_CLIENT_SCHEME				"ws://"

/** @brief The scheme of the URLs with TLS, not supported yet. */
#define HTTP_WEB_SOCKET_CLIENT_SECURE_SCHEME		"wss://"

/** @brief The port of a url without one. */
#define HTTP_WEB_SOCKET_CLIENT_DEFAULT_PORT			80

/** @brief The size of the host of the url. */
#define HTTP_WEB_SOCKET_CLIENT_HOST_SIZE			(1 << 8)

/** @brief The size of the path (and the query) of the url. */
#define HTTP_WEB_SOCKET_CLIENT_PATH_SIZE			(1 << 11)

/** @brief The size of each value of the response that is checked. */
#define HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE			(1 << 8)

/** @brief The most bytes of the response of the handshake. */
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE		(1 << 13)

/** @brief The most bytes of the request of the handshake. */
#define HTTP_WEB_SOCKET_CLIENT_REQUEST_SIZE			(1 << 13)

/** @brief The default milliseconds of the connect () of each address. */
#define HTTP_WEB_SOCKET_CLIENT_CONNECT_TIMEOUT		((1 << 3) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

/** @brief The default milliseconds of the handshake. */
#define HTTP_WEB_SOCKET_CLIENT_HANDSHAKE_TIMEOUT	((1 << 3) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

/** @brief The default milliseconds of the first wait between reconnections, doubled after each failure. */
#define HTTP_WEB_SOCKET_CLIENT_RECONNECT_MIN		(1 << 8)

/** @brief The default milliseconds of the longest wait between reconnections. */
#define HTTP_WEB_SOCKET_CLIENT_RECONNECT_MAX		(1 << 15)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A WebSocket client: its url, its settings and the connection that it has now.
 *
 * Its fields are not part of the API, the methods of this header are the way to a client.
 */
typedef struct _HttpWebSocketClient HttpWebSocketClient;

/**
 * @brief Creates a client, with the default settings and without reconnections.
 *
 * @return the new client, released with http_web_socket_client_delete (); NULL if it could not be created
 */
CERVER_EXPORT HttpWebSocketClient *http_web_socket_client_create (void);

/**
 * @brief Closes the connection with 1000, waits for its thread and releases the client.
 *
 * It is the last call of a client, from a thread that is not running one of its methods: a method (on_open,
 * on_message, on_close, on_error) that deletes its client only closes it.
 *
 * @param[in] client the client, it can be NULL
 */
CERVER_EXPORT void http_web_socket_client_delete (HttpWebSocketClient *client);

/**
 * @brief Sets the url of the server: `ws://host[:port][/path][?query]`, the host can be an IPv6 in brackets.
 *
 * A url that is rejected in any of its parts does not change the client.
 *
 * @param[in,out] client the client
 * @param[in] url the url
 * @retval 0 the url was set
 * @retval 1 a NULL argument or a url that is not valid (wss:// is not supported yet)
 */
CERVER_EXPORT u8 http_web_socket_client_set_url (
	HttpWebSocketClient *client, const char *url
);

/**
 * @brief Adds a header to the request of the handshake, like an Authorization.
 *
 * @param[in,out] client the client
 * @param[in] name the name, a token that is not one of the headers of the handshake
 * @param[in] value the value, without the end of a line
 * @retval 0 the header was added
 * @retval 1 a NULL argument, a name or a value that is not valid, a header of the handshake, or no memory
 */
CERVER_EXPORT u8 http_web_socket_client_add_header (
	HttpWebSocketClient *client, const char *name, const char *value
);

/**
 * @brief Sets the subprotocols that are offered, in order of preference.
 *
 * @param[in,out] client the client
 * @param[in] protocols the subprotocols, copied
 * @param[in] n_protocols the number of subprotocols, up to #HTTP_WEB_SOCKET_MAX_PROTOCOLS
 * @retval 0 the subprotocols were set
 * @retval 1 a NULL client or a list that is not valid (then the settings do not change)
 */
CERVER_EXPORT u8 http_web_socket_client_set_protocols (
	HttpWebSocketClient *client, const char * const *protocols, const unsigned int n_protocols
);

/**
 * @brief Sets the methods of the connections of the client (on_upgrade is not used).
 *
 * @param[in,out] client the client
 * @param[in] handlers the handlers, copied; NULL changes nothing
 */
CERVER_EXPORT void http_web_socket_client_set_handlers (
	HttpWebSocketClient *client, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Sets the data that every connection of the client has (http_web_socket_get_data ()), already before the
 * connection exists.
 *
 * @param[in,out] client the client
 * @param[in] data the data, the client does not own it
 */
CERVER_EXPORT void http_web_socket_client_set_data (HttpWebSocketClient *client, void *data);

/**
 * @brief Returns the data of a client.
 *
 * @param[in] client the client
 * @return the data, NULL for a NULL client
 */
CERVER_EXPORT void *http_web_socket_client_get_data (const HttpWebSocketClient *client);

/**
 * @brief Offers permessage-deflate with the default options, or stops offering it.
 *
 * @param[in,out] client the client
 * @param[in] compression true to offer it
 */
CERVER_EXPORT void http_web_socket_client_set_compression (
	HttpWebSocketClient *client, const bool compression
);

/**
 * @brief Sets the options of permessage-deflate of the client.
 *
 * @param[in,out] client the client
 * @param[in] options the options, copied
 */
CERVER_EXPORT void http_web_socket_client_set_compression_options (
	HttpWebSocketClient *client, const HttpWebSocketDeflateOptions *options
);

/**
 * @brief Sets the most bytes of a message (and of each frame) that the client receives.
 *
 * @param[in,out] client the client
 * @param[in] max_message the bytes, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE
 */
CERVER_EXPORT void http_web_socket_client_set_max_message (
	HttpWebSocketClient *client, const size_t max_message
);

/**
 * @brief Sets the milliseconds without receiving anything before a ping, and the ones to wait for its pong.
 *
 * @param[in,out] client the client
 * @param[in] ping_interval the milliseconds before a ping, 0 disables them
 * @param[in] pong_timeout the milliseconds to wait for the pong
 */
CERVER_EXPORT void http_web_socket_client_set_ping_milliseconds (
	HttpWebSocketClient *client, const unsigned int ping_interval, const unsigned int pong_timeout
);

/**
 * @brief Sets the milliseconds that the closing handshake waits for the server.
 *
 * @param[in,out] client the client
 * @param[in] close_timeout the milliseconds
 */
CERVER_EXPORT void http_web_socket_client_set_close_timeout_milliseconds (
	HttpWebSocketClient *client, const unsigned int close_timeout
);

/**
 * @brief Sets the milliseconds of each send of the client.
 *
 * @param[in,out] client the client
 * @param[in] send_timeout the milliseconds
 */
CERVER_EXPORT void http_web_socket_client_set_send_timeout_milliseconds (
	HttpWebSocketClient *client, const unsigned int send_timeout
);

/**
 * @brief Sets the milliseconds of the connect () of each address and of the handshake.
 *
 * @param[in,out] client the client
 * @param[in] connect_timeout the milliseconds of each connect (), 0 for #HTTP_WEB_SOCKET_CLIENT_CONNECT_TIMEOUT
 * @param[in] handshake_timeout the milliseconds of the handshake, 0 for #HTTP_WEB_SOCKET_CLIENT_HANDSHAKE_TIMEOUT
 */
CERVER_EXPORT void http_web_socket_client_set_timeouts_milliseconds (
	HttpWebSocketClient *client, const unsigned int connect_timeout, const unsigned int handshake_timeout
);

/**
 * @brief Sets whether the client reconnects when the connection ends without http_web_socket_client_close ().
 *
 * It waits from min to max milliseconds, doubled after each failure, with jitter and never above max; a connection
 * that ends before it lasted max milliseconds counts as a failure, so a server that accepts and closes is not
 * reconnected without a wait.
 *
 * @param[in,out] client the client
 * @param[in] reconnect true to reconnect
 * @param[in] min the first wait, 0 for #HTTP_WEB_SOCKET_CLIENT_RECONNECT_MIN
 * @param[in] max the longest wait, a value below min is min
 */
CERVER_EXPORT void http_web_socket_client_set_reconnect_milliseconds (
	HttpWebSocketClient *client, const bool reconnect,
	const unsigned int min, const unsigned int max
);

/**
 * @brief Connects in the current thread and returns after the handshake; the connection runs in its own thread,
 * where every method is called.
 *
 * @param[in,out] client the client
 * @retval 0 the connection is open
 * @retval 1 it failed: on_error and on_close were called (also when the connection opened and its thread could not
 * be created)
 */
CERVER_EXPORT u8 http_web_socket_client_connect (HttpWebSocketClient *client);

/**
 * @brief Connects in the thread of the connection; the methods report the result.
 *
 * @param[in,out] client the client
 * @retval 0 the thread started
 * @retval 1 a NULL client, a client whose thread is still running, or the thread could not be created
 */
CERVER_EXPORT u8 http_web_socket_client_connect_async (HttpWebSocketClient *client);

/**
 * @brief Returns the connection of now, with a reference.
 *
 * @param[in] client the client
 * @return the connection, released with http_web_socket_release (); NULL between connections
 * @threadsafety Safe from any thread, with the mutex of the client.
 */
CERVER_EXPORT HttpWebSocket *http_web_socket_client_get_web_socket (HttpWebSocketClient *client);

/**
 * @brief Sends a whole message with the connection of now, like http_web_socket_send ().
 *
 * @param[in,out] client the client
 * @param[in] type HTTP_WEB_SOCKET_OPCODE_TEXT or HTTP_WEB_SOCKET_OPCODE_BINARY
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @retval 0 the message was sent
 * @retval 1 there is no connection now, or it was not sent
 */
CERVER_EXPORT u8 http_web_socket_client_send (
	HttpWebSocketClient *client,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

/**
 * @brief Sends a text message with the connection of now.
 *
 * @param[in,out] client the client
 * @param[in] text the text, in UTF-8
 * @param[in] length the bytes of the text
 * @retval 0 the message was sent
 * @retval 1 it was not sent
 */
CERVER_EXPORT u8 http_web_socket_client_send_text (
	HttpWebSocketClient *client, const char *text, const size_t length
);

/**
 * @brief Sends a binary message with the connection of now.
 *
 * @param[in,out] client the client
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @retval 0 the message was sent
 * @retval 1 it was not sent
 */
CERVER_EXPORT u8 http_web_socket_client_send_binary (
	HttpWebSocketClient *client, const void *data, const size_t size
);

/**
 * @brief Stops the reconnections and starts the closing handshake of the connection of now.
 *
 * A close that can not be sent (a code or a reason that is not valid) does not stop the client nor its
 * reconnections.
 *
 * @param[in,out] client the client
 * @param[in] code a code that can be sent
 * @param[in] reason the reason, NULL or up to #HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE bytes in UTF-8
 * @retval 0 the client is stopping (and the close frame was sent if it had a connection)
 * @retval 1 a NULL client, a code or a reason that is not valid, or the close frame was not sent
 */
CERVER_EXPORT u8 http_web_socket_client_close (
	HttpWebSocketClient *client, const u16 code, const char *reason
);

/**
 * @brief Waits until the thread of the client ends, after a close or without reconnections.
 *
 * A method of the client does not wait: it runs in that thread.
 *
 * @param[in,out] client the client
 * @threadsafety Two threads can wait at once, only one of them joins the thread.
 */
CERVER_EXPORT void http_web_socket_client_wait (HttpWebSocketClient *client);

/**
 * @brief Returns the connections that the client opened.
 *
 * @param[in] client the client
 * @return the number of connections, 0 for a NULL client
 */
CERVER_EXPORT unsigned int http_web_socket_client_get_connections (const HttpWebSocketClient *client);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
