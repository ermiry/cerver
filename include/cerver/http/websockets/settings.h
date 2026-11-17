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

/**
 * @brief The default most bytes of the frames that wait in the queue of a connection to be sent.
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE					(1 << 20)

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
	XX(8,	CONNECT,		Connect,		The client could not connect to the server)					\
	XX(9,	TLS,			TLS,			The handshake of TLS of the client failed)

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
 * @return the string of its row in #HTTP_WEB_SOCKET_ERROR_MAP, or "Undefined" for a value out of the table; a static
 * string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_error_to_string (const HttpWebSocketError error);

/**
 * @brief Returns the description of an error.
 *
 * @param[in] error the error
 * @return the description of its row in #HTTP_WEB_SOCKET_ERROR_MAP, or "Undefined" for a value out of the table; a
 * static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_error_description (const HttpWebSocketError error);

/**
 * @brief The X-macro table of the checks of the Origin of an upgrade against the origins whitelist of the cerver:
 * the number, the name, the string and the description of each one.
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_ORIGIN_CHECK_MAP(XX)																	\
	XX(0,	NONE,			None,			Any Origin and a request without one)								\
	XX(1,	REQUIRED,		Required,		The request must have an Origin of the whitelist)					\
	XX(2,	IF_PRESENT,		IfPresent,		An Origin must be one of the whitelist and a request without one passes)

/**
 * @brief How an endpoint checks the Origin of an upgrade, one `HTTP_WEB_SOCKET_ORIGIN_CHECK_<name>` for each row of
 * #HTTP_WEB_SOCKET_ORIGIN_CHECK_MAP; a request that fails it is answered with 403.
 *
 * The Origin is compared byte by byte with each value of the whitelist.
 *
 * @since 2.1b-21 (it was a bool)
 */
typedef enum HttpWebSocketOriginCheck {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_ORIGIN_CHECK_##name = num,
	HTTP_WEB_SOCKET_ORIGIN_CHECK_MAP (XX)
	#undef XX

} HttpWebSocketOriginCheck;

/**
 * @brief Returns the string of a check of the Origin.
 *
 * @param[in] check the check
 * @return the string of its row in #HTTP_WEB_SOCKET_ORIGIN_CHECK_MAP, or "Undefined" for a value out of the table;
 * a static string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_origin_check_to_string (const HttpWebSocketOriginCheck check);

/**
 * @brief Returns the description of a check of the Origin.
 *
 * @param[in] check the check
 * @return the description of its row in #HTTP_WEB_SOCKET_ORIGIN_CHECK_MAP, or "Undefined" for a value out of the
 * table; a static string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_origin_check_description (const HttpWebSocketOriginCheck check);

/**
 * @brief The X-macro table of what a send does when its message does not fit in the queue of its connection: the
 * number, the name, the string and the description of each one.
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_OVERFLOW_MAP(XX)																		\
	XX(0,	DROP,			Drop,			The message is not taken and the send returns that it was dropped)	\
	XX(1,	CLOSE,			Close,			The message is not taken and the connection is closed with 1008)

/**
 * @brief What a send does when its message does not fit in the queue of its connection, one
 * `HTTP_WEB_SOCKET_OVERFLOW_<name>` for each row of #HTTP_WEB_SOCKET_OVERFLOW_MAP.
 *
 * @since 2.1b-21
 */
typedef enum HttpWebSocketOverflow {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_OVERFLOW_##name = num,
	HTTP_WEB_SOCKET_OVERFLOW_MAP (XX)
	#undef XX

} HttpWebSocketOverflow;

/**
 * @brief Returns the string of a policy of the queue.
 *
 * @param[in] overflow the policy
 * @return the string of its row in #HTTP_WEB_SOCKET_OVERFLOW_MAP, or "Undefined" for a value out of the table; a
 * static string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_overflow_to_string (const HttpWebSocketOverflow overflow);

/**
 * @brief Returns the description of a policy of the queue.
 *
 * @param[in] overflow the policy
 * @return the description of its row in #HTTP_WEB_SOCKET_OVERFLOW_MAP, or "Undefined" for a value out of the table;
 * a static string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_overflow_description (const HttpWebSocketOverflow overflow);

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
	http_status status;

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
 * (with CERVER_HANDLER_TYPE_THREADS it is the one of the connection, with CERVER_HANDLER_TYPE_EVENTS one of the pool
 * of its endpoint, and the event loop does not read the connection while one of them runs), so nothing of a
 * connection is kept in the variables of a thread: its place is http_web_socket_set_data ().
 */
typedef struct HttpWebSocketHandlers {

	/**
	 * @brief Decides about the upgrade, in the thread of the request, after the authentication and the before
	 * middlewares; it returns true to accept the connection, false to reject it with the status of the upgrade.
	 *
	 * It can answer the request itself and return false.
	 */
	bool (*on_upgrade)(
		const struct _HttpReceive *http_receive,
		const struct _HttpRequest *request,
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

	/**
	 * @brief The queue of the connection is empty again after a send returned HTTP_WEB_SOCKET_SEND_BACKPRESSURE:
	 * once for each time that the queue went over half of its size.
	 *
	 * @since 2.1b-21
	 */
	void (*on_drain)(struct _HttpWebSocket *web_socket);

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
 * @brief A setting that a group passes to its subgroups and endpoints: the size and the policy of the queue.
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_SETTING_QUEUE				(1 << 8)

/**
 * @brief The settings of the WebSocket connections of a group, an endpoint or a client.
 *
 * They are set with the http_web_socket_settings_set_* () methods, which also mark each one as set: a group passes
 * to its subgroups and endpoints only the settings that it set, and an endpoint keeps the ones that it set. The
 * settings of an endpoint, a group or a client come from http_endpoint_get_web_socket_settings (),
 * http_group_get_web_socket_settings () and http_web_socket_client_get_settings ().
 *
 * @warning A field that is written directly is not marked as set; the settings are read when the cerver or the
 * client starts.
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

	/** @brief How the Origin of the upgrade is checked against the origins whitelist of the cerver (403 if it fails). */
	HttpWebSocketOriginCheck origin_check;

	/** @brief The milliseconds without receiving anything before a ping, 0 disables the pings. */
	unsigned int ping_interval;
	/** @brief The milliseconds to answer a ping. */
	unsigned int pong_timeout;

	/** @brief The milliseconds that wait for the close frame of the peer. */
	unsigned int close_timeout;

	/**
	 * @brief The milliseconds of each send, 0 uses the send timeout of the HTTP cerver; with the event loop, the
	 * milliseconds that the queue can wait without sending a byte.
	 */
	unsigned int send_timeout;

	/** @brief The most bytes of the frames that wait in the queue of a connection, 0 uses the one of the cerver. */
	size_t max_queue;
	/** @brief What a send does when its message does not fit in the queue. */
	HttpWebSocketOverflow overflow;

} HttpWebSocketSettings;

/**
 * @brief Sets the handlers of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] handlers the handlers, copied; NULL changes nothing
 * @note An endpoint takes WebSocket connections with http_endpoint_set_web_socket_handlers (), which also sets its
 * modifier.
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT void http_web_socket_settings_set_handlers (
	HttpWebSocketSettings *settings, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Sets the most bytes of a message (and of each frame); a larger one closes the connection with 1009.
 *
 * @param[in,out] settings the settings
 * @param[in] max_message the bytes, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE (the default)
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT void http_web_socket_settings_set_max_message (
	HttpWebSocketSettings *settings, const size_t max_message
);

/**
 * @brief Copies the subprotocols, in the order of preference of the server, at most
 * #HTTP_WEB_SOCKET_MAX_PROTOCOLS; a list that is rejected does not change the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] protocols the subprotocols, tokens
 * @param[in] n_protocols the number of subprotocols
 * @retval 0 the subprotocols were set
 * @retval 1 a NULL argument, a list that is not valid or no memory
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT u8 http_web_socket_settings_set_protocols (
	HttpWebSocketSettings *settings,
	const char * const *protocols, const unsigned int n_protocols
);

/**
 * @brief Sets the options of permessage-deflate (RFC 7692), used when cerver was built with zlib.
 *
 * @param[in,out] settings the settings
 * @param[in] compression the options, copied (http_web_socket_deflate_options_init () sets the defaults, and its
 * enabled field enables it); NULL changes nothing
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT void http_web_socket_settings_set_compression (
	HttpWebSocketSettings *settings, const HttpWebSocketDeflateOptions *compression
);

/**
 * @brief Enables or disables permessage-deflate (RFC 7692) with the default options, used when cerver was built
 * with zlib.
 *
 * @param[in,out] settings the settings
 * @param[in] enabled true to enable it
 * @since 2.1b-21
 */
CERVER_EXPORT void http_web_socket_settings_enable_compression (
	HttpWebSocketSettings *settings, const bool enabled
);

/**
 * @brief Sets how the Origin of the upgrade is checked.
 *
 * @param[in,out] settings the settings
 * @param[in] origin_check the check, HTTP_WEB_SOCKET_ORIGIN_CHECK_NONE by default
 * @since 2.1b-21 (it was internal and a bool)
 */
CERVER_EXPORT void http_web_socket_settings_set_origin_check (
	HttpWebSocketSettings *settings, const HttpWebSocketOriginCheck origin_check
);

/**
 * @brief Sets the milliseconds without receiving anything before a ping, and the ones to wait for its pong.
 *
 * @param[in,out] settings the settings
 * @param[in] ping_interval the milliseconds before a ping, 0 disables them
 * @param[in] pong_timeout the milliseconds to wait for the pong
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT void http_web_socket_settings_set_ping_milliseconds (
	HttpWebSocketSettings *settings,
	const unsigned int ping_interval, const unsigned int pong_timeout
);

/**
 * @brief Sets the milliseconds that the closing handshake waits for the peer.
 *
 * @param[in,out] settings the settings
 * @param[in] close_timeout the milliseconds
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT void http_web_socket_settings_set_close_timeout_milliseconds (
	HttpWebSocketSettings *settings, const unsigned int close_timeout
);

/**
 * @brief Sets the milliseconds of each send; with the event loop, the ones that the queue can wait without sending
 * a byte before the connection fails.
 *
 * @param[in,out] settings the settings
 * @param[in] send_timeout the milliseconds, 0 uses the send timeout of the HTTP cerver
 * @since 2.1b-21 (it was internal)
 */
CERVER_EXPORT void http_web_socket_settings_set_send_timeout_milliseconds (
	HttpWebSocketSettings *settings, const unsigned int send_timeout
);

/**
 * @brief Sets the most bytes of the frames that wait in the queue of a connection, and what a send does with a
 * message that does not fit.
 *
 * A send writes what the socket takes at once and leaves the rest in the queue, which the owner of the connection
 * writes (its event loop, or its thread). Over half of the queue a send returns HTTP_WEB_SOCKET_SEND_BACKPRESSURE
 * and on_drain () runs when the queue is empty again.
 *
 * @param[in,out] settings the settings
 * @param[in] max_queue the bytes, 0 uses the one of the cerver (http_cerver_set_web_socket_queue (),
 * #HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE by default)
 * @param[in] overflow what a send does with a message that does not fit, HTTP_WEB_SOCKET_OVERFLOW_DROP by default
 * @since 2.1b-21
 */
CERVER_EXPORT void http_web_socket_settings_set_queue (
	HttpWebSocketSettings *settings, const size_t max_queue, const HttpWebSocketOverflow overflow
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
