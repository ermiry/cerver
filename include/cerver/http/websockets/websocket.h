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

	u64 n_messages_received;
	u64 n_messages_sent;
	u64 n_bytes_received;
	u64 n_bytes_sent;

};

typedef struct _HttpWebSocket HttpWebSocket;

#pragma region public

CERVER_EXPORT u64 http_web_socket_get_id (const HttpWebSocket *web_socket);

CERVER_EXPORT HttpWebSocketState http_web_socket_get_state (const HttpWebSocket *web_socket);

CERVER_EXPORT HttpWebSocketRole http_web_socket_get_role (const HttpWebSocket *web_socket);

// the address of the client (resolved with the trusted proxies) or of the server
CERVER_EXPORT const char *http_web_socket_get_ip (const HttpWebSocket *web_socket);

// the path of the request of the upgrade
CERVER_EXPORT const String *http_web_socket_get_path (const HttpWebSocket *web_socket);

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

#pragma region internal

// creates a connection with its settings, its socket is taken by the caller
CERVER_PRIVATE HttpWebSocket *http_web_socket_create (
	const HttpWebSocketRole role, const HttpWebSocketSettings *settings
);

// sets the compression that was negotiated
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_web_socket_set_compression (
	HttpWebSocket *web_socket,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
);

// the handshake ended, the connection can send from now on (only from CONNECTING)
CERVER_PRIVATE void http_web_socket_opened (HttpWebSocket *web_socket);

// the bytes that arrived with the handshake, the connection takes them
CERVER_PRIVATE void http_web_socket_set_pending (
	HttpWebSocket *web_socket, u8 *pending, const size_t pending_size
);

// runs the connection in the current thread until it ends,
// it calls every method, and it closes the socket at the end
CERVER_PRIVATE void http_web_socket_run (HttpWebSocket *web_socket);

// wakes up the thread of the connection
CERVER_PRIVATE void http_web_socket_wake (HttpWebSocket *web_socket);

// the cerver is going away, the connection is closed with 1001
CERVER_PRIVATE void http_web_socket_go_away (HttpWebSocket *web_socket);

// ends the connection now, without waiting for the closing handshake
CERVER_PRIVATE void http_web_socket_abort (HttpWebSocket *web_socket);

// sends a whole message with a deadline of timeout milliseconds, for a broadcast
// a send that does not end in time fails the connection
CERVER_PRIVATE u8 http_web_socket_send_timeout (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size,
	const unsigned int timeout
);

// starts the connection of the server of an upgraded request in its own thread,
// it takes its connection, when it returns true the caller must not drop it
CERVER_PRIVATE bool http_web_socket_start (
	struct CerverReceive *cr, struct _HttpReceive *http_receive
);

CERVER_PRIVATE void http_web_socket_handshake_delete (HttpWebSocketHandshake *handshake);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
