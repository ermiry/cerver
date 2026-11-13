#ifndef _CERVER_HTTP_WEBSOCKETS_INTERNAL_H_
#define _CERVER_HTTP_WEBSOCKETS_INTERNAL_H_

// what only cerver knows of its WebSocket connections: the fields of a connection,
// of the handshake of an upgrade, of the registry of an HTTP cerver and of a client,
// and the methods that only cerver calls
// this header is not installed, the ones of include/ only have the types and their API

#include <stdbool.h>
#include <stddef.h>

#include <pthread.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/connection.h"
#include "cerver/network.h"
#include "cerver/socket.h"

#include "cerver/http/websockets/client.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/message.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/websocket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _HttpCerver;
struct _HttpEndpoint;
struct _HttpReceive;
struct CerverReceive;

#pragma region connection

// what the server learned in the request of an upgrade, until its connection starts
typedef struct HttpWebSocketHandshake {

	char accept[HTTP_WEB_SOCKET_ACCEPT_SIZE];

	// a copy of the protocol of the response, NULL for none
	char *protocol;

	bool compression;
	HttpWebSocketDeflateParameters deflate;

	// a place among the connections of the cerver was taken for it
	bool reserved;

	void *data;
	void (*delete_data)(void *);

	void *decoded_data;
	void (*delete_decoded_data)(void *);

	// the bytes that arrived after the request, the first frames of the client
	u8 *pending;
	size_t pending_size;

} HttpWebSocketHandshake;

// a WebSocket connection, of the server or of a client
struct _HttpWebSocket {

	u64 id;

	HttpWebSocketRole role;

	// HttpWebSocketState, read and written atomically
	unsigned int state;

	// the thread of the connection, the registry and the application hold references
	unsigned int references;

	// the connection of the server, that owns the socket, NULL for a client
	Connection *connection;
	struct _Cerver *cerver;

	Socket *socket;

	char ip[CONNECTION_IP_SIZE];

	struct _HttpCerver *http_cerver;
	const struct _HttpEndpoint *endpoint;
	struct _HttpWebSocketClient *client;

	// the resolved settings of the endpoint (or the ones of the client)
	HttpWebSocketSettings settings;

	String *path;
	char *protocol;

	// the query of the request of the upgrade as it was sent, NULL when it had none
	String *query;

	void *data;
	void (*delete_data)(void *);

	void *decoded_data;
	void (*delete_decoded_data)(void *);

	// the compression, NULL when it was not negotiated
	HttpWebSocketDeflate *deflate;

	HttpWebSocketReceiver receiver;

	u8 *buffer;
	size_t buffer_size;

	u8 *pending;
	size_t pending_size;

	// wakes up the thread of the connection (an eventfd)
	int wake_fd;

	// held during each frame, and during a whole data message
	pthread_mutex_t send_mutex;
	pthread_mutex_t message_mutex;

	// the closing handshake and the flags that other threads set
	pthread_mutex_t mutex;

	// the message that is being streamed (http_web_socket_send_begin ())
	bool streaming;
	u8 stream_type;
	bool stream_started;
	bool stream_compressed;

	// the bytes of the pieces of the stream, for the stats of its message
	size_t stream_size;

	bool close_sent;
	bool close_received;
	struct timespec close_deadline;

	// the code and the reason of the end, that on_close reports
	u16 close_code;
	char close_reason[HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE + 1];
	size_t close_reason_length;

	// the cerver is going away (1001)
	bool going_away;

	// the connection must end now, without the closing handshake
	bool aborted;

	// the first error, reported by the thread of the connection
	HttpWebSocketError error;

	struct timespec last_received;
	bool ping_sent;
	struct timespec pong_deadline;
	u64 n_pings;

	// the masks of a client
	u8 masks[HTTP_WEB_SOCKET_MASKS_SIZE];
	size_t masks_position;
	u8 *scratch;

	// the rooms, protected by the mutex of the registry
	char rooms[HTTP_WEB_SOCKET_MAX_ROOMS][HTTP_WEB_SOCKET_ROOM_NAME_SIZE];
	unsigned int n_rooms;

	// the list of the registry
	struct _HttpWebSocket *previous;
	struct _HttpWebSocket *next;
	bool registered;

	// the data messages and their bytes without compression, the same that its cerver counts
	u64 n_messages_received;
	u64 n_messages_sent;
	u64 n_bytes_received;
	u64 n_bytes_sent;

};

// a close can be sent: its code is one that an endpoint sends and its reason fits and is UTF-8
CERVER_HIDDEN bool http_web_socket_close_is_valid (const u16 code, const char *reason);

// creates a connection with its settings, its socket is taken by the caller
CERVER_HIDDEN HttpWebSocket *http_web_socket_create (
	const HttpWebSocketRole role, const HttpWebSocketSettings *settings
);

// sets the compression that was negotiated
// returns 0 on success, 1 on error
CERVER_HIDDEN u8 http_web_socket_set_compression (
	HttpWebSocket *web_socket,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
);

// the handshake ended, the connection can send from now on (only from CONNECTING)
CERVER_HIDDEN void http_web_socket_opened (HttpWebSocket *web_socket);

// a connection that opened and will never run: it is closed with the send mutex,
// so a send of another thread ends before its socket is released and no other one starts
CERVER_HIDDEN void http_web_socket_closed (HttpWebSocket *web_socket);

// the bytes that arrived with the handshake, the connection takes them
CERVER_HIDDEN void http_web_socket_set_pending (
	HttpWebSocket *web_socket, u8 *pending, const size_t pending_size
);

// runs the connection in the current thread until it ends,
// it calls every method, and it closes the socket at the end
CERVER_HIDDEN void http_web_socket_run (HttpWebSocket *web_socket);

// wakes up the thread of the connection
CERVER_HIDDEN void http_web_socket_wake (HttpWebSocket *web_socket);

// the cerver is going away, the connection is closed with 1001
CERVER_HIDDEN void http_web_socket_go_away (HttpWebSocket *web_socket);

// ends the connection now, without waiting for the closing handshake
CERVER_HIDDEN void http_web_socket_abort (HttpWebSocket *web_socket);

// the timeout of a send that is the one of its connection, resolved when the message can be sent
#define HTTP_WEB_SOCKET_SEND_TIMEOUT_CONNECTION		0

// sends a whole message with a deadline of timeout milliseconds, for a broadcast,
// or with the one of the connection (HTTP_WEB_SOCKET_SEND_TIMEOUT_CONNECTION)
// a send that does not end in time fails the connection,
// one that the connection does not take because it is closing only returns its error
CERVER_HIDDEN u8 http_web_socket_send_timeout (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size,
	const unsigned int timeout
);

// starts the connection of the server of an upgraded request in its own thread,
// it takes its connection, when it returns true the caller must not drop it
CERVER_HIDDEN bool http_web_socket_start (
	struct CerverReceive *cr, struct _HttpReceive *http_receive
);

CERVER_HIDDEN void http_web_socket_handshake_delete (HttpWebSocketHandshake *handshake);

#pragma endregion

#pragma region registry

// the WebSocket connections of an HTTP cerver, and their stats
typedef struct _HttpWebSocketRegistry {

	pthread_mutex_t mutex;

	HttpWebSocket *first;

	// the connections that are registered
	unsigned int count;

	// the connections plus the upgrades that have a place
	unsigned int reserved;

	unsigned int max_connections;

	unsigned int broadcast_timeout;

	// the cerver is going away, no more connections are accepted
	bool stopping;

	u64 next_id;

	// stats, read and written atomically
	u64 n_opened;
	u64 n_closed;
	u64 n_rejected[HTTP_WEB_SOCKET_REJECTIONS];
	u64 n_messages_received;
	u64 n_messages_sent;
	u64 n_bytes_received;
	u64 n_bytes_sent;
	u64 n_slow;
	u64 n_errors;
	u64 n_closes[HTTP_WEB_SOCKET_CLOSE_CODES_SIZE];
	u64 n_closes_other;

} HttpWebSocketRegistry;

CERVER_HIDDEN HttpWebSocketRegistry *http_web_socket_registry_create (void);

CERVER_HIDDEN void http_web_socket_registry_delete (HttpWebSocketRegistry *registry);

// takes a place for an upgrade, false when the cerver has its most connections or it is stopping
CERVER_HIDDEN bool http_web_socket_registry_reserve (HttpWebSocketRegistry *registry);

// gives back the place of an upgrade that did not start
CERVER_HIDDEN void http_web_socket_registry_unreserve (HttpWebSocketRegistry *registry);

// adds a connection with its place already taken, and sets its id
// returns 0 on success, 1 if the cerver is stopping
CERVER_HIDDEN u8 http_web_socket_registry_add (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
);

// removes the connection, and gives back its place
CERVER_HIDDEN void http_web_socket_registry_remove (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
);

// closes every connection with 1001, and accepts no more
CERVER_HIDDEN void http_web_socket_registry_go_away (HttpWebSocketRegistry *registry);

// ends every connection now
CERVER_HIDDEN void http_web_socket_registry_abort (HttpWebSocketRegistry *registry);

CERVER_HIDDEN void http_web_socket_registry_count_rejection (
	HttpWebSocketRegistry *registry, const HttpWebSocketRejection rejection
);

CERVER_HIDDEN void http_web_socket_registry_count_close (
	HttpWebSocketRegistry *registry, const u16 code
);

#pragma endregion

#pragma region client

// what the client takes from the response of the server
typedef struct HttpWebSocketClientResponse {

	int status;

	// the response was parsed until its empty line
	bool complete;

	// a header was repeated, too long or the response was not valid HTTP
	bool invalid;

	char upgrade[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	char connection[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	char accept[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	char protocol[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	char extensions[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];

	bool has_protocol;
	bool has_extensions;

	// the bytes of the response until its empty line, the frames of the server can follow them
	size_t size;

} HttpWebSocketClientResponse;

// the results of http_web_socket_client_response_parse ()
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE	0
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL		1
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID		2

// parses the response of a handshake, it can be called again with more bytes
// returns HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE, _PARTIAL when it needs more bytes
// and _INVALID when it is not valid (invalid is set), also without its end
// in HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE bytes
CERVER_PUBLIC u8 http_web_socket_client_response_parse (
	const char *data, const size_t size,
	HttpWebSocketClientResponse *response
);

// the checks of RFC 6455 section 4.1 and of the extensions that were offered:
// a 101, the tokens, the accept of the key, a protocol that was offered and permessage-deflate
// protocol (optional) is set to the one of the server, compression to the parameters of the extension
// returns true when the connection can start
CERVER_PUBLIC bool http_web_socket_client_response_check (
	const HttpWebSocketClientResponse *response, const char *key,
	char * const *protocols, const unsigned int n_protocols,
	const HttpWebSocketDeflateOptions *options,
	const char **protocol, bool *compression, HttpWebSocketDeflateParameters *parameters
);

struct _HttpWebSocketClient {

	char host[HTTP_WEB_SOCKET_CLIENT_HOST_SIZE];
	u16 port;
	char path[HTTP_WEB_SOCKET_CLIENT_PATH_SIZE];

	// the extra headers of the request, "Name: value"
	DoubleList *headers;

	// the methods, the limits and the timeouts of the connection,
	// the protocols and the compression that are offered
	HttpWebSocketSettings settings;

	unsigned int connect_timeout;
	unsigned int handshake_timeout;

	bool reconnect;
	unsigned int reconnect_min;
	unsigned int reconnect_max;

	pthread_mutex_t mutex;

	// the connection of now, NULL between connections
	HttpWebSocket *web_socket;

	// wakes up the wait between reconnections (an eventfd)
	int wake_fd;

	// http_web_socket_client_close () or delete (), there are no more reconnections
	bool stopping;

	pthread_t thread;
	bool thread_started;

	// only one of the threads that wait for the client joins its thread
	pthread_mutex_t join_mutex;

	// the connections that were opened
	unsigned int n_connections;

	// the attempts that failed in a row, with the connections that ended before they were stable
	unsigned int n_failures;

	// a connection that is still open at this time lasted the longest wait between reconnections:
	// it was stable, and the failures before it are forgotten
	struct timespec stable_deadline;

	// the address of the last connection
	char ip[NETWORK_ADDRESS_STRING_SIZE];

	// the data of the application, every connection of the client has it since it is created
	void *data;

};

// the milliseconds to wait before the attempt that follows n_failures:
// min doubled for each failure, never above max nor above the timeout that a poll () takes,
// with a part of it taken from random so the clients of a server do not come back together
CERVER_PRIVATE unsigned int http_web_socket_client_backoff_delay (
	const unsigned int min, const unsigned int max,
	const unsigned int n_failures, const unsigned int random
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
