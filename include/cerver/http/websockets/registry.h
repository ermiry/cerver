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
 * @brief The WebSocket connections of an HTTP cerver (HttpCerver.web_sockets), a struct that only cerver knows:
 * its limit, its rooms and its stats are reached with the methods of this group.
 *
 * Only the connections of the server are in the registry; the ones of a client are not in any room.
 * @{
 */

/** @brief The default most WebSocket connections of an HTTP cerver; an upgrade above it is answered with 503. */
#define HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS			(1 << 10)

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

/** @brief Counts a row of #HTTP_WEB_SOCKET_REJECTION_MAP. */
#define HTTP_WEB_SOCKET_REJECTION_COUNT(num, name, string, description)		+ 1

/**
 * @brief The number of rejection reasons, the rows of #HTTP_WEB_SOCKET_REJECTION_MAP.
 *
 * @since 2.1b-21 (it is counted from the table)
 */
#define HTTP_WEB_SOCKET_REJECTIONS		(0 HTTP_WEB_SOCKET_REJECTION_MAP (HTTP_WEB_SOCKET_REJECTION_COUNT))

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
 * @brief Sets the size and the policy of the queue of the connections whose endpoint (and its groups) did not set
 * them with http_web_socket_settings_set_queue ().
 *
 * @param[in,out] http_cerver the HTTP cerver
 * @param[in] max_queue the bytes, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE (the default)
 * @param[in] overflow what a send does with a message that does not fit, HTTP_WEB_SOCKET_OVERFLOW_DROP by default
 * @since 2.1b-21 (it replaces http_cerver_set_web_socket_broadcast_timeout_milliseconds (): a broadcast does not
 * wait for any connection)
 */
CERVER_EXPORT void http_cerver_set_web_socket_queue (
	struct _HttpCerver *http_cerver, const size_t max_queue, const HttpWebSocketOverflow overflow
);

/**
 * @brief The connections that a broadcast, a count or a for each reach: the ones that match every field that is set.
 *
 * A NULL target, or one whose fields are all NULL, reaches every connection of the registry.
 *
 * @since 2.1b-21
 */
typedef struct HttpWebSocketTarget {

	/** @brief Only the connections of this room, NULL for every room. */
	const char *room;

	/** @brief Only the connections of this endpoint, NULL for every endpoint. */
	const struct _HttpEndpoint *endpoint;

	/** @brief A connection that is not reached, it can be NULL. */
	const HttpWebSocket *except;

} HttpWebSocketTarget;

/**
 * @brief Returns the WebSocket connections that are open, the ones of a target.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] target the connections, NULL for every one
 * @return the number of connections, 0 for a NULL cerver
 * @since 2.1b-21 (it took the room)
 */
CERVER_EXPORT unsigned int http_cerver_web_socket_count (
	struct _HttpCerver *http_cerver, const HttpWebSocketTarget *target
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

	/** @brief The messages that a queue without a place did not take (A5). @since 2.1b-21 */
	u64 n_dropped;
	/** @brief The times that a queue was empty again after its backpressure, each one an on_drain (). @since 2.1b-21 */
	u64 n_drains;
	/** @brief The bytes that the queues of the connections that are open have now, a gauge. @since 2.1b-21 */
	u64 n_queued_bytes;

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
 * @brief Returns the registered connection with an id, the one of http_web_socket_get_id ().
 *
 * The ids are unique in the process, for the connections of the servers and of the clients.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] id the id
 * @return the connection with a reference that is released with http_web_socket_release (), NULL when no registered
 * connection has the id
 * @since 2.1b-21
 */
CERVER_EXPORT HttpWebSocket *http_cerver_web_socket_get_with_id (
	struct _HttpCerver *http_cerver, const u64 id
);

/**
 * @brief The X-macro table of the results of http_web_socket_join (): the number, the name, the string and the
 * description of each one.
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_JOIN_MAP(XX)																			\
	XX(0,	JOINED,			Joined,			The connection joined the room)									\
	XX(1,	ALREADY,		Already,		The connection was already in the room)							\
	XX(2,	INVALID,		Invalid,		A NULL argument or an empty name)								\
	XX(3,	UNREGISTERED,	Unregistered,	The connection is of a client or it is not in the registry)		\
	XX(4,	FAILED,			Failed,			The room could not be allocated)

/**
 * @brief The result of http_web_socket_join (), one `HTTP_WEB_SOCKET_JOIN_<name>` for each row of
 * #HTTP_WEB_SOCKET_JOIN_MAP.
 *
 * @since 2.1b-21
 */
typedef enum HttpWebSocketJoin {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_JOIN_##name = num,
	HTTP_WEB_SOCKET_JOIN_MAP (XX)
	#undef XX

} HttpWebSocketJoin;

/**
 * @brief Returns the string of a result of http_web_socket_join ().
 *
 * @param[in] join the result
 * @return the string of its row in #HTTP_WEB_SOCKET_JOIN_MAP, or "Undefined" for a value out of the table; a static
 * string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_join_to_string (const HttpWebSocketJoin join);

/**
 * @brief Returns the description of a result of http_web_socket_join ().
 *
 * @param[in] join the result
 * @return the description of its row in #HTTP_WEB_SOCKET_JOIN_MAP, or "Undefined" for a value out of the table; a
 * static string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_join_description (const HttpWebSocketJoin join);

/**
 * @brief Adds a connection to a room; a connection can be in any number of rooms, of any length.
 *
 * The rooms are an index of the registry (a room has its members), so a broadcast to a room only goes through its
 * members; a room is created with its first member and removed with its last one. A connection joins in its
 * on_open () too, before it is registered: the broadcasts and the counts reach it once it is (S7).
 *
 * @param[in,out] web_socket the connection of the server
 * @param[in] room the name, a string that is not empty; it is copied
 * @return HTTP_WEB_SOCKET_JOIN_JOINED or HTTP_WEB_SOCKET_JOIN_ALREADY when the connection is in the room, the cause
 * otherwise
 * @since 2.1b-21 (it returned u8, and it had a limit of 16 rooms of 63 bytes)
 */
CERVER_EXPORT HttpWebSocketJoin http_web_socket_join (HttpWebSocket *web_socket, const char *room);

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
 * @brief Removes a connection from every one of its rooms.
 *
 * The rooms of a connection are kept while its on_close () runs, and cerver removes them after it.
 *
 * @param[in,out] web_socket the connection
 * @return the rooms that it left, 0 for a NULL connection
 * @since 2.1b-21
 */
CERVER_EXPORT unsigned int http_web_socket_leave_all (HttpWebSocket *web_socket);

/**
 * @brief Returns whether a connection is in a room.
 *
 * @param[in] web_socket the connection
 * @param[in] room the name
 * @return true when the connection is in the room
 * @since 2.1b-21 (it took a connection that was not const)
 */
CERVER_EXPORT bool http_web_socket_in_room (const HttpWebSocket *web_socket, const char *room);

/**
 * @brief Returns a copy of the names of the rooms of a connection, in no order.
 *
 * @param[in] web_socket the connection
 * @param[out] n_rooms the number of rooms
 * @return the names, an array and its strings in a single allocation that is released with free (); NULL (with 0
 * rooms) when it is in no room, for a NULL argument or when the copy could not be allocated
 * @since 2.1b-21
 */
CERVER_EXPORT char **http_web_socket_get_rooms (const HttpWebSocket *web_socket, unsigned int *n_rooms);

/**
 * @brief Sends a message to every connection of a target.
 *
 * Each connection takes the message in its queue with its own policy and its own compression, so a slow one never
 * makes the others wait.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] target the connections, NULL for every one
 * @param[in] type HTTP_WEB_SOCKET_OPCODE_TEXT or HTTP_WEB_SOCKET_OPCODE_BINARY
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @return the connections that took it in their queues (HTTP_WEB_SOCKET_SEND_QUEUED or
 * HTTP_WEB_SOCKET_SEND_BACKPRESSURE)
 * @since 2.1b-21 (it took the room and the connection that does not receive it, and it waited for each connection
 * up to the broadcast timeout; it replaces http_endpoint_web_socket_broadcast ())
 */
CERVER_EXPORT unsigned int http_cerver_web_socket_broadcast (
	struct _HttpCerver *http_cerver, const HttpWebSocketTarget *target,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

/**
 * @brief Calls a method with every connection of a target, outside of the mutex of the registry, with a reference
 * to each one that is released after its call.
 *
 * @param[in] http_cerver the HTTP cerver
 * @param[in] target the connections, NULL for every one
 * @param[in] method the method
 * @param[in] args the arguments of the method
 * @since 2.1b-21 (it took the room)
 */
CERVER_EXPORT void http_cerver_web_socket_for_each (
	struct _HttpCerver *http_cerver, const HttpWebSocketTarget *target,
	void (*method)(HttpWebSocket *web_socket, void *args), void *args
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
