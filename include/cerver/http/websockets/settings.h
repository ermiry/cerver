/**
 * @file cerver/http/websockets/settings.h
 * @brief The errors, the upgrade, the handlers and the settings of the WebSocket connections of an endpoint.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_SETTINGS_H_
#define _CERVER_HTTP_WEBSOCKETS_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/responses/status.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"

/**
 * @defgroup http_websockets_settings Settings
 * @ingroup http_websockets
 * @brief What an endpoint decides about its WebSocket connections and the methods that handle them.
 * @{
 */

/** @brief The milliseconds of a second. */
#define HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND				1000

/** @brief The default most bytes of a message, after it was decompressed. */
#define HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE					(1 << 20)

/** @brief The default milliseconds without receiving anything before a ping, 0 disables it. */
#define HTTP_WEB_SOCKET_DEFAULT_PING_INTERVAL				((1 << 5) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

/** @brief The default milliseconds to answer a ping; the connection is closed when it is not answered. */
#define HTTP_WEB_SOCKET_DEFAULT_PONG_TIMEOUT				((1 << 4) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

/** @brief The default milliseconds that wait for the close frame of the peer after sending one. */
#define HTTP_WEB_SOCKET_DEFAULT_CLOSE_TIMEOUT				((1 << 2) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

/** @brief The most subprotocols of an endpoint. */
#define HTTP_WEB_SOCKET_MAX_PROTOCOLS						(1 << 4)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpHeaders;
struct _HttpReceive;
struct _HttpRequest;
struct _HttpWebSocket;

/**
 * @brief The X-macro table of the errors of a WebSocket connection: the number, the name, the string and the
 * description of each one.
 */
#define HTTP_WEB_SOCKET_ERROR_MAP(XX)																	\
	XX(0,	NONE,			None,			No error)													\
	XX(1,	PROTOCOL,		Protocol,		The peer broke the protocol)								\
	XX(2,	RECEIVE,		Receive,		A receive failed)											\
	XX(3,	SEND,			Send,			A send failed or did not end in time)						\
	XX(4,	PONG_TIMEOUT,	Pong Timeout,	The peer did not answer a ping in time)					\
	XX(5,	CLOSE_TIMEOUT,	Close Timeout,	The peer did not answer the close frame in time)			\
	XX(6,	MEMORY,			Memory,			There was not enough memory)								\
	XX(7,	HANDSHAKE,		Handshake,		The response of the server was not a valid upgrade)		\
	XX(8,	CONNECT,		Connect,		The client could not connect to the server)

/**
 * @brief The error of a WebSocket connection, one `HTTP_WEB_SOCKET_ERROR_<name>` for each row of
 * #HTTP_WEB_SOCKET_ERROR_MAP.
 */
typedef enum HttpWebSocketError {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_ERROR_##name = num,
	HTTP_WEB_SOCKET_ERROR_MAP (XX)
	#undef XX

} HttpWebSocketError;

/**
 * @brief Returns the string of an error.
 *
 * @param[in] error the error
 * @return the string of its row in #HTTP_WEB_SOCKET_ERROR_MAP, or the one of HTTP_WEB_SOCKET_ERROR_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_error_to_string (const HttpWebSocketError error);

/**
 * @brief Returns the description of an error.
 *
 * @param[in] error the error
 * @return the description of its row in #HTTP_WEB_SOCKET_ERROR_MAP, or the one of HTTP_WEB_SOCKET_ERROR_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_error_description (const HttpWebSocketError error);

/**
 * @brief What on_upgrade () decides about a connection, before its 101.
 */
typedef struct HttpWebSocketUpgrade {

	/**
	 * @brief The subprotocol of the response: already the first one of the endpoint that the client offered; it
	 * can be changed to another one that the client offered, or to NULL for none.
	 */
	const char *protocol;

	/** @brief The data of the connection, deleted with #delete_data when it ends. */
	void *data;
	/** @brief Releases the data of the connection. */
	void (*delete_data)(void *);

	/**
	 * @brief The connection takes the decoded data of the request (the grants of its bearer JWT):
	 * http_web_socket_get_decoded_data () returns it, and it is deleted with the connection.
	 */
	bool adopt_decoded_data;

	/** @brief The status of the response when the upgrade is rejected, 403 by default. */
	CerverHttpStatus status;

	/**
	 * @brief The headers that the 101 adds after the ones of the handshake; they are of cerver,
	 * http_web_socket_upgrade_add_header () adds one.
	 */
	struct _HttpHeaders *headers;

} HttpWebSocketUpgrade;

/**
 * @brief Adds a header to the 101 of the connection that on_upgrade () accepts, like a Set-Cookie.
 *
 * A name can be added many times and they are sent in the order they were added. The headers that cerver writes in
 * a 101 can not be added: Server, Upgrade, Connection, Sec-WebSocket-Accept, Sec-WebSocket-Protocol (it is the
 * protocol of the upgrade), Sec-WebSocket-Extensions, Content-Length and Transfer-Encoding.
 *
 * @param[in,out] upgrade the upgrade of on_upgrade ()
 * @param[in] name the name, a token
 * @param[in] value the value, it can not have the end of a line
 * @retval 0 the header was added
 * @retval 1 a NULL argument, an upgrade without headers, a reserved header, or a name or a value that is not valid
 */
CERVER_EXPORT u8 http_web_socket_upgrade_add_header (
	HttpWebSocketUpgrade *upgrade, const char *name, const char *value
);

/**
 * @brief The methods of the connections of an endpoint.
 *
 * @threadsafety All of them but on_upgrade are serialized by connection: in order and never two at the same time,
 * and the ones of different connections run at the same time. The thread that runs them is not part of the API
 * (today it is the one of the connection, with the event loop it is one of a pool), so nothing of a connection is
 * kept in the variables of a thread: its place is http_web_socket_set_data ().
 */
typedef struct HttpWebSocketHandlers {

	/**
	 * @brief Decides about the upgrade, in the thread of the request, after the authentication and the before
	 * middlewares; it returns true to accept the connection, false to reject it with the status of the upgrade.
	 *
	 * It can answer the request itself and return false.
	 *
	 * @since 3.0b-12 it takes the context of the request, instead of its receive and its request.
	 */
	bool (*on_upgrade)(
		struct _HttpReceive *context,
		HttpWebSocketUpgrade *upgrade
	);

	/** @brief The connection is open, it can already send. */
	void (*on_open)(struct _HttpWebSocket *web_socket);

	/** @brief A whole message; its data is valid only during the call. */
	void (*on_message)(
		struct _HttpWebSocket *web_socket,
		HttpWebSocketOpcode type, const void *data, size_t size
	);

	/**
	 * @brief A piece of a message, instead of on_message when it is set; the last piece is a call with last as true
	 * that can be empty.
	 */
	void (*on_fragment)(
		struct _HttpWebSocket *web_socket,
		HttpWebSocketOpcode type, const void *data, size_t size,
		bool first, bool last
	);

	/** @brief A ping, that cerver already answered. */
	void (*on_ping)(struct _HttpWebSocket *web_socket, const void *data, size_t size);

	/** @brief A pong. */
	void (*on_pong)(struct _HttpWebSocket *web_socket, const void *data, size_t size);

	/**
	 * @brief The connection ended, always once and last, with the code of the close frames (1006 when it ended
	 * without them).
	 */
	void (*on_close)(
		struct _HttpWebSocket *web_socket,
		u16 code, const char *reason, size_t length
	);

	/** @brief The connection failed, before its on_close. */
	void (*on_error)(struct _HttpWebSocket *web_socket, HttpWebSocketError error);

} HttpWebSocketHandlers;

/** @brief A setting that a group passes to its subgroups and endpoints: the handlers. */
#define HTTP_WEB_SOCKET_SETTING_HANDLERS			(1 << 0)
/** @brief A setting that a group passes to its subgroups and endpoints: the max message. */
#define HTTP_WEB_SOCKET_SETTING_MAX_MESSAGE			(1 << 1)
/** @brief A setting that a group passes to its subgroups and endpoints: the subprotocols. */
#define HTTP_WEB_SOCKET_SETTING_PROTOCOLS			(1 << 2)
/** @brief A setting that a group passes to its subgroups and endpoints: the compression. */
#define HTTP_WEB_SOCKET_SETTING_COMPRESSION			(1 << 3)
/** @brief A setting that a group passes to its subgroups and endpoints: the check of the Origin. */
#define HTTP_WEB_SOCKET_SETTING_ORIGIN				(1 << 4)
/** @brief A setting that a group passes to its subgroups and endpoints: the pings. */
#define HTTP_WEB_SOCKET_SETTING_PING				(1 << 5)
/** @brief A setting that a group passes to its subgroups and endpoints: the close timeout. */
#define HTTP_WEB_SOCKET_SETTING_CLOSE_TIMEOUT		(1 << 6)
/** @brief A setting that a group passes to its subgroups and endpoints: the send timeout. */
#define HTTP_WEB_SOCKET_SETTING_SEND_TIMEOUT		(1 << 7)

/**
 * @brief The settings of the WebSocket connections of a group or an endpoint.
 */
typedef struct HttpWebSocketSettings {

	/** @brief The HTTP_WEB_SOCKET_SETTING_* flags of the settings that were set. */
	unsigned int set;

	/** @brief The methods of the connections. */
	HttpWebSocketHandlers handlers;

	/** @brief The most bytes of a message, also of each frame. */
	size_t max_message;

	/** @brief The subprotocols, in the order of preference of the server, owned by the group or the endpoint that set them. */
	char **protocols;
	/** @brief The number of subprotocols. */
	unsigned int n_protocols;

	/** @brief The options of permessage-deflate. */
	HttpWebSocketDeflateOptions compression;

	/** @brief The Origin of the upgrade must be in the origins whitelist of the cerver (403 if not). */
	bool origin_check;

	/** @brief The milliseconds without receiving anything before a ping, 0 disables the pings. */
	unsigned int ping_interval;
	/** @brief The milliseconds to answer a ping. */
	unsigned int pong_timeout;

	/** @brief The milliseconds that wait for the close frame of the peer. */
	unsigned int close_timeout;

	/** @brief The milliseconds of each send, 0 uses the send timeout of the HTTP cerver. */
	unsigned int send_timeout;

} HttpWebSocketSettings;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
