#ifndef _CERVER_HTTP_WEBSOCKETS_REGISTRY_H_
#define _CERVER_HTTP_WEBSOCKETS_REGISTRY_H_

#include <stdbool.h>
#include <stddef.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/websocket.h"

// the most WebSocket connections of an HTTP cerver, an upgrade above it is answered with 503
#define HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS			(1 << 10)

// milliseconds that a broadcast waits for each connection, a slower one fails
#define HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT		(1 << 8)

// the close codes from 1000 to 1015 are counted one by one, the others together
#define HTTP_WEB_SOCKET_CLOSE_CODES_FIRST				1000
#define HTTP_WEB_SOCKET_CLOSE_CODES_SIZE				16

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpEndpoint;

#define HTTP_WEB_SOCKET_REJECTION_MAP(XX)																		\
	XX(0,	VERSION,		Version,		The Sec-WebSocket-Version was not 13 (426))						\
	XX(1,	BAD_REQUEST,	Bad Request,	The request of the upgrade was not valid (400))					\
	XX(2,	UPGRADE,		Upgrade,		The request had no upgrade to WebSocket (426))					\
	XX(3,	ORIGIN,			Origin,			The origin was not in the whitelist (403))						\
	XX(4,	LIMIT,			Limit,			The cerver had its most connections (503))						\
	XX(5,	APPLICATION,	Application,	on_upgrade () rejected it)

typedef enum HttpWebSocketRejection {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_REJECTION_##name = num,
	HTTP_WEB_SOCKET_REJECTION_MAP (XX)
	#undef XX

} HttpWebSocketRejection;

#define HTTP_WEB_SOCKET_REJECTIONS		6

CERVER_PUBLIC const char *http_web_socket_rejection_to_string (const HttpWebSocketRejection rejection);

// the WebSocket connections of an HTTP cerver (HttpCerver.web_sockets) are a struct _HttpWebSocketRegistry
// that only cerver knows: its limit, its rooms and its stats are reached with the methods of this header

#pragma region public

// sets the most WebSocket connections, HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS by default
CERVER_EXPORT void http_cerver_set_web_socket_max_connections (
	struct _HttpCerver *http_cerver, const unsigned int max_connections
);

// sets the milliseconds that a broadcast waits for each connection,
// HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT by default
CERVER_EXPORT void http_cerver_set_web_socket_broadcast_timeout_milliseconds (
	struct _HttpCerver *http_cerver, const unsigned int broadcast_timeout
);

// the WebSocket connections that are open, of a room or of every one with room NULL
CERVER_EXPORT unsigned int http_cerver_web_socket_count (
	struct _HttpCerver *http_cerver, const char *room
);

// a copy of the counters of the WebSocket connections of an HTTP cerver, the ones of its metrics
typedef struct HttpWebSocketStats {

	// the connections that are open
	u64 n_connections;

	// the connections that were opened and the ones that ended
	u64 n_opened;
	u64 n_closed;

	// the upgrades that were rejected, by their HttpWebSocketRejection
	u64 n_rejected[HTTP_WEB_SOCKET_REJECTIONS];

	// the data messages, and their bytes without compression
	u64 n_messages_received;
	u64 n_messages_sent;
	u64 n_bytes_received;
	u64 n_bytes_sent;

	// the connections that failed because a send did not end in time
	u64 n_slow;

	// the connections that ended with an error
	u64 n_errors;

	// the connections that ended with each close code from HTTP_WEB_SOCKET_CLOSE_CODES_FIRST
	// (1006 without a close), and with any other one
	u64 n_closes[HTTP_WEB_SOCKET_CLOSE_CODES_SIZE];
	u64 n_closes_other;

} HttpWebSocketStats;

// copies the stats of the WebSocket connections of the HTTP cerver
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_web_socket_get_stats (
	const struct _HttpCerver *http_cerver, HttpWebSocketStats *stats
);

// copies the stats of one connection, of the server or of a client:
// its messages and its bytes, the other values are the ones of a cerver and stay in zero
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_get_stats (
	const HttpWebSocket *web_socket, HttpWebSocketStats *stats
);

// adds the connection to a room, a connection is in HTTP_WEB_SOCKET_MAX_ROOMS rooms or less
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_join (HttpWebSocket *web_socket, const char *room);

// returns 0 on success, 1 if it was not in the room
CERVER_EXPORT u8 http_web_socket_leave (HttpWebSocket *web_socket, const char *room);

// returns true when the connection is in the room
CERVER_EXPORT bool http_web_socket_in_room (HttpWebSocket *web_socket, const char *room);

// sends a message to every connection of the room, or to every one with room NULL,
// but except (that can be NULL), a connection that is too slow fails without stopping the others
// returns the connections that received it
CERVER_EXPORT unsigned int http_cerver_web_socket_broadcast (
	struct _HttpCerver *http_cerver, const char *room, const HttpWebSocket *except,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

// sends a message to every connection of the endpoint
CERVER_EXPORT unsigned int http_endpoint_web_socket_broadcast (
	struct _HttpCerver *http_cerver, const struct _HttpEndpoint *endpoint,
	const HttpWebSocketOpcode type, const void *data, const size_t size
);

// calls the method with every connection of the room (or every one with room NULL),
// outside of the mutex of the registry, with a reference to each one
CERVER_EXPORT void http_cerver_web_socket_for_each (
	struct _HttpCerver *http_cerver, const char *room,
	void (*method)(HttpWebSocket *web_socket, void *args), void *args
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
