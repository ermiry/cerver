#ifndef _CERVER_HTTP_WEBSOCKETS_SETTINGS_H_
#define _CERVER_HTTP_WEBSOCKETS_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/responses/status.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"

#define HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND				1000

// the most bytes of a message, after it was decompressed
#define HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE					(1 << 20)

// a ping is sent after this time without receiving anything, 0 disables it
#define HTTP_WEB_SOCKET_DEFAULT_PING_INTERVAL				((1 << 5) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

// the connection is closed when its ping is not answered in this time
#define HTTP_WEB_SOCKET_DEFAULT_PONG_TIMEOUT				((1 << 4) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

// the time that waits for the close frame of the peer after sending one
#define HTTP_WEB_SOCKET_DEFAULT_CLOSE_TIMEOUT				((1 << 2) * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND)

// the most protocols of an endpoint
#define HTTP_WEB_SOCKET_MAX_PROTOCOLS						(1 << 4)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpHeaders;
struct _HttpReceive;
struct _HttpRequest;
struct _HttpWebSocket;

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

typedef enum HttpWebSocketError {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_ERROR_##name = num,
	HTTP_WEB_SOCKET_ERROR_MAP (XX)
	#undef XX

} HttpWebSocketError;

CERVER_PUBLIC const char *http_web_socket_error_to_string (const HttpWebSocketError error);

CERVER_PUBLIC const char *http_web_socket_error_description (const HttpWebSocketError error);

// what on_upgrade () decides about a connection, before its 101
typedef struct HttpWebSocketUpgrade {

	// the protocol of the response, already the first one of the endpoint that the client offered,
	// it can be changed to another one that the client offered, or to NULL for none
	const char *protocol;

	// the data of the connection, deleted with delete_data when it ends
	void *data;
	void (*delete_data)(void *);

	// the connection takes the decoded data of the request (the grants of its bearer JWT),
	// http_web_socket_get_decoded_data () returns it, and it is deleted with the connection
	bool adopt_decoded_data;

	// the status of the response when the upgrade is rejected, 403 by default
	http_status status;

	// the headers that the 101 adds after the ones of the handshake,
	// they are of cerver: http_web_socket_upgrade_add_header () adds one
	struct _HttpHeaders *headers;

} HttpWebSocketUpgrade;

// adds a header to the 101 of the connection that on_upgrade () accepts, like a Set-Cookie,
// a name can be added many times and they are sent in the order they were added
// the headers that cerver writes in a 101 can not be added: Server, Upgrade, Connection,
// Sec-WebSocket-Accept, Sec-WebSocket-Protocol (it is the protocol of the upgrade),
// Sec-WebSocket-Extensions, Content-Length and Transfer-Encoding
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_socket_upgrade_add_header (
	HttpWebSocketUpgrade *upgrade, const char *name, const char *value
);

// the methods of the connections of an endpoint
// all of them, but on_upgrade, are serialized by connection: in order and never two at the same time,
// and the ones of different connections run at the same time
// the thread that runs them is not part of the API (today it is the one of the connection,
// with the event loop it is one of a pool), so nothing of a connection is kept
// in the variables of a thread: its place is http_web_socket_set_data ()
typedef struct HttpWebSocketHandlers {

	// runs in the thread of the request, after the authentication and the before middlewares,
	// it can answer the request itself and return false
	// returns true to accept the connection, false to reject it with the status of upgrade
	bool (*on_upgrade)(
		const struct _HttpReceive *http_receive,
		const struct _HttpRequest *request,
		HttpWebSocketUpgrade *upgrade
	);

	// the connection is open, it can already send
	void (*on_open)(struct _HttpWebSocket *web_socket);

	// a whole message, its data is valid only during the call
	void (*on_message)(
		struct _HttpWebSocket *web_socket,
		HttpWebSocketOpcode type, const void *data, size_t size
	);

	// a piece of a message, instead of on_message when it is set,
	// the last piece is a call with last as true that can be empty
	void (*on_fragment)(
		struct _HttpWebSocket *web_socket,
		HttpWebSocketOpcode type, const void *data, size_t size,
		bool first, bool last
	);

	// a ping, that cerver already answered
	void (*on_ping)(struct _HttpWebSocket *web_socket, const void *data, size_t size);

	void (*on_pong)(struct _HttpWebSocket *web_socket, const void *data, size_t size);

	// the connection ended, always once and last,
	// with the code of the close frames (1006 when it ended without them)
	void (*on_close)(
		struct _HttpWebSocket *web_socket,
		u16 code, const char *reason, size_t length
	);

	// the connection failed, before its on_close
	void (*on_error)(struct _HttpWebSocket *web_socket, HttpWebSocketError error);

} HttpWebSocketHandlers;

// the settings that a group passes to its subgroups and endpoints
#define HTTP_WEB_SOCKET_SETTING_HANDLERS			(1 << 0)
#define HTTP_WEB_SOCKET_SETTING_MAX_MESSAGE			(1 << 1)
#define HTTP_WEB_SOCKET_SETTING_PROTOCOLS			(1 << 2)
#define HTTP_WEB_SOCKET_SETTING_COMPRESSION			(1 << 3)
#define HTTP_WEB_SOCKET_SETTING_ORIGIN				(1 << 4)
#define HTTP_WEB_SOCKET_SETTING_PING				(1 << 5)
#define HTTP_WEB_SOCKET_SETTING_CLOSE_TIMEOUT		(1 << 6)
#define HTTP_WEB_SOCKET_SETTING_SEND_TIMEOUT		(1 << 7)

typedef struct HttpWebSocketSettings {

	// the settings that were set
	unsigned int set;

	HttpWebSocketHandlers handlers;

	// the most bytes of a message, also of each frame
	size_t max_message;

	// the subprotocols, in the order of preference of the server,
	// owned by the group or the endpoint that set them
	char **protocols;
	unsigned int n_protocols;

	// permessage-deflate
	HttpWebSocketDeflateOptions compression;

	// the Origin of the upgrade must be in the origins whitelist of the cerver (403 if not)
	bool origin_check;

	// milliseconds, a ping_interval of 0 disables the pings
	unsigned int ping_interval;
	unsigned int pong_timeout;

	// milliseconds that waits for the close frame of the peer
	unsigned int close_timeout;

	// milliseconds of each send, 0 uses the send timeout of the HTTP cerver
	unsigned int send_timeout;

} HttpWebSocketSettings;

#ifdef __cplusplus
}
#endif

#endif
