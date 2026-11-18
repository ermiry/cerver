/**
 * @file cerver/http/websockets/registry.h
 * @brief The WebSocket connections of an HTTP cerver: their limit, their rooms, their broadcasts and their stats.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_REGISTRY_H_
#define _CERVER_HTTP_WEBSOCKETS_REGISTRY_H_

#include <stdbool.h>
#include <stddef.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/websocket.h"

/**
 * @defgroup http_websockets_registry Registry
 * @ingroup http_websockets
 * @brief The WebSocket connections of an HTTP cerver (CerverHttp.web_sockets), a struct that only cerver knows:
 * its limit, its rooms and its stats are reached with the methods of this group.
 *
 * Only the connections of the server are in the registry; the ones of a client are not in any room.
 * @{
 */

/** @brief The default most WebSocket connections of an HTTP cerver; an upgrade above it is answered with 503. */
#define HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS			(1 << 10)

/** @brief The default milliseconds that a broadcast waits for each connection; a slower one fails. */
#define HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT		(1 << 8)

/** @brief The first close code that is counted on its own; the codes from 1000 to 1015 are counted one by one, the others together. */
#define HTTP_WEB_SOCKET_CLOSE_CODES_FIRST				1000

/** @brief The number of close codes that are counted on their own. */
#define HTTP_WEB_SOCKET_CLOSE_CODES_SIZE				16

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpEndpoint;

/**
 * @brief The X-macro table of the reasons why an upgrade was rejected: the number, the name, the string and the
 * description (with the status) of each one.
 */
#define HTTP_WEB_SOCKET_REJECTION_MAP(XX)																		\
	XX(0,	VERSION,		Version,		The Sec-WebSocket-Version was not 13 (426))						\
	XX(1,	BAD_REQUEST,	Bad Request,	The request of the upgrade was not valid (400))					\
	XX(2,	UPGRADE,		Upgrade,		The request had no upgrade to WebSocket (426))					\
	XX(3,	ORIGIN,			Origin,			The origin was not in the whitelist (403))						\
	XX(4,	LIMIT,			Limit,			The cerver had its most connections (503))						\
	XX(5,	APPLICATION,	Application,	on_upgrade () rejected it)

/**
 * @brief The reason why an upgrade was rejected, one `HTTP_WEB_SOCKET_REJECTION_<name>` for each row of
 * #HTTP_WEB_SOCKET_REJECTION_MAP.
 */
typedef enum HttpWebSocketRejection {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_REJECTION_##name = num,
	HTTP_WEB_SOCKET_REJECTION_MAP (XX)
	#undef XX

} HttpWebSocketRejection;

/** @brief The number of rejection reasons. */
#define HTTP_WEB_SOCKET_REJECTIONS		6

/**
 * @brief Returns the string of a rejection reason.
 *
 * @param[in] rejection the reason
 * @return the string of its row in #HTTP_WEB_SOCKET_REJECTION_MAP, or "Undefined" for a value out of the table; a
 * static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_rejection_to_string (const HttpWebSocketRejection rejection);

/**
 * @brief Sets the most WebSocket connections of an HTTP cerver.
 *
 * @param[in,out] http_cerver the HTTP cerver
 * @param[in] max_connections the connections, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS (the default)
 */
CERVER_EXPORT void http_cerver_set_web_socket_max_connections (
	struct _HttpCerver *http_cerver, const unsigned int max_connections
);

/**
 * @brief Sets the milliseconds that a broadcast waits for each connection.
 *
 * @param[in,out] http_cerver the HTTP cerver
 * @param[in] broadcast_timeout the milliseconds, 0 for #HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT (the default)
 */
CERVER_EXPORT void http_cerver_set_web_socket_broadcast_timeout_milliseconds (
	struct _HttpCerver *http_cerver, const unsigned int broadcast_timeout
);

/**
 * @brief Returns the WebSocket connections that are open, of a room or of every one.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] room the room, NULL for every connection
 * @return the number of connections, 0 for a NULL cerver
 */
CERVER_EXPORT unsigned int http_cerver_web_socket_count (
	struct _HttpCerver *http_cerver, const char *room
);

/**
 * @brief A copy of the counters of the WebSocket connections of an HTTP cerver, the ones of its metrics.
 */
typedef struct HttpWebSocketStats {

	/** @brief The connections that are open. */
	u64 n_connections;

	/** @brief The connections that were opened. */
	u64 n_opened;
	/** @brief The connections that ended. */
	u64 n_closed;

	/** @brief The upgrades that were rejected, by their HttpWebSocketRejection. */
	u64 n_rejected[HTTP_WEB_SOCKET_REJECTIONS];

	/** @brief The data messages that were received. */
	u64 n_messages_received;
	/** @brief The data messages that were sent. */
	u64 n_messages_sent;
	/** @brief The bytes of the received messages, without compression. */
	u64 n_bytes_received;
	/** @brief The bytes of the sent messages, without compression. */
	u64 n_bytes_sent;

	/** @brief The connections that failed because a send did not end in time. */
	u64 n_slow;

	/** @brief The connections that ended with an error. */
	u64 n_errors;

	/**
	 * @brief The connections that ended with each close code from #HTTP_WEB_SOCKET_CLOSE_CODES_FIRST (1006 without a
	 * close).
	 */
	u64 n_closes[HTTP_WEB_SOCKET_CLOSE_CODES_SIZE];
	/** @brief The connections that ended with any other close code. */
	u64 n_closes_other;

} HttpWebSocketStats;

/**
 * @brief Copies the stats of the WebSocket connections of an HTTP cerver.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[out] stats where the stats are copied
 * @retval 0 the stats were copied
 * @retval 1 a NULL argument or a cerver without a registry
 */
CERVER_EXPORT u8 http_cerver_web_socket_get_stats (
	const struct _HttpCerver *http_cerver, HttpWebSocketStats *stats
);

/**
 * @brief Copies the stats of one connection, of the server or of a client: its messages and its bytes; the other
 * values are the ones of a cerver and stay in zero.
 *
 * @param[in] web_socket the connection
 * @param[out] stats where the stats are copied
 * @retval 0 the stats were copied
 * @retval 1 a NULL argument
 */
CERVER_EXPORT u8 http_web_socket_get_stats (
	const HttpWebSocket *web_socket, HttpWebSocketStats *stats
);

/**
 * @brief Adds a connection to a room; a connection is in #HTTP_WEB_SOCKET_MAX_ROOMS rooms or less.
 *
 * @param[in,out] web_socket the connection of the server
 * @param[in] room the name, shorter than #HTTP_WEB_SOCKET_ROOM_NAME_SIZE
 * @retval 0 the connection is in the room (also when it already was)
 * @retval 1 a NULL argument, a connection of a client or that is not registered, a name that is too long, or the
 * connection has its most rooms
 */
CERVER_EXPORT u8 http_web_socket_join (HttpWebSocket *web_socket, const char *room);

/**
 * @brief Removes a connection from a room.
 *
 * @param[in,out] web_socket the connection
 * @param[in] room the name
 * @retval 0 the connection left the room
 * @retval 1 it was not in the room, or a NULL argument
 */
CERVER_EXPORT u8 http_web_socket_leave (HttpWebSocket *web_socket, const char *room);

/**
 * @brief Returns whether a connection is in a room.
 *
 * @param[in] web_socket the connection
 * @param[in] room the name
 * @return true when the connection is in the room
 */
CERVER_EXPORT bool http_web_socket_in_room (HttpWebSocket *web_socket, const char *room);

/**
 * @brief Sends a message to every connection of a room, or to every one, but one.
 *
 * A connection that is too slow (the broadcast timeout) fails without stopping the others.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] room the room, NULL for every connection
 * @param[in] except a connection that does not receive it, it can be NULL
 * @param[in] type HTTP_WEB_SOCKET_OPCODE_TEXT or HTTP_WEB_SOCKET_OPCODE_BINARY
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @return the connections that received it
 */
CERVER_EXPORT unsigned int http_cerver_web_socket_broadcast (
	struct _HttpCerver *http_cerver, const char *room, const HttpWebSocket *except,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

/**
 * @brief Sends a message to every connection of an endpoint, like http_cerver_web_socket_broadcast ().
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] endpoint the endpoint
 * @param[in] type HTTP_WEB_SOCKET_OPCODE_TEXT or HTTP_WEB_SOCKET_OPCODE_BINARY
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @return the connections that received it, 0 for a NULL endpoint
 */
CERVER_EXPORT unsigned int http_endpoint_web_socket_broadcast (
	struct _HttpCerver *http_cerver, const struct _HttpEndpoint *endpoint,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

/**
 * @brief Calls a method with every connection of a room (or every one), outside of the mutex of the registry, with
 * a reference to each one that is released after its call.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] room the room, NULL for every connection
 * @param[in] method the method
 * @param[in] args the arguments of the method
 */
CERVER_EXPORT void http_cerver_web_socket_for_each (
	struct _HttpCerver *http_cerver, const char *room,
	void (*method)(HttpWebSocket *web_socket, void *args), void *args
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
