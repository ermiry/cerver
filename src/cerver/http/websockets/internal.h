/**
 * @file http/websockets/internal.h
 * @internal
 * @brief What only cerver knows of its WebSocket connections: the fields of a connection, of the handshake of an
 * upgrade, of the registry of an HTTP cerver and of a client, and the methods that only cerver calls.
 *
 * This header is not installed, the ones of include/ only have the types and their API.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_INTERNAL_H_
#define _CERVER_HTTP_WEBSOCKETS_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"

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

/**
 * @internal
 * @brief Sets the default settings, with nothing set.
 *
 * @param[out] settings the settings
 */
CERVER_HIDDEN void http_web_socket_settings_init (HttpWebSocketSettings *settings);

/**
 * @internal
 * @brief Releases what the settings own (their subprotocols).
 *
 * @param[in,out] settings the settings
 */
CERVER_HIDDEN void http_web_socket_settings_end (HttpWebSocketSettings *settings);

/**
 * @internal
 * @brief Takes from a source the settings that were not set in the resolved ones; the resolved settings only keep
 * references.
 *
 * @param[in,out] resolved the settings that inherit
 * @param[in] source the settings of the group
 */
CERVER_HIDDEN void http_web_socket_settings_inherit (
	HttpWebSocketSettings *resolved, const HttpWebSocketSettings *source
);

/**
 * @internal
 * @brief Sets the handlers of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] handlers the handlers, copied; NULL changes nothing
 */
CERVER_HIDDEN void http_web_socket_settings_set_handlers (
	HttpWebSocketSettings *settings, const HttpWebSocketHandlers *handlers
);

/**
 * @internal
 * @brief Sets the max message of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] max_message the bytes, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE
 */
CERVER_HIDDEN void http_web_socket_settings_set_max_message (
	HttpWebSocketSettings *settings, const size_t max_message
);

/**
 * @internal
 * @brief Copies the subprotocols, at most #HTTP_WEB_SOCKET_MAX_PROTOCOLS; a list that is rejected does not change
 * the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] protocols the subprotocols
 * @param[in] n_protocols the number of subprotocols
 * @retval 0 the subprotocols were set
 * @retval 1 a list that is not valid or no memory
 */
CERVER_HIDDEN u8 http_web_socket_settings_set_protocols (
	HttpWebSocketSettings *settings,
	const char * const *protocols, const unsigned int n_protocols
);

/**
 * @internal
 * @brief Sets the options of permessage-deflate of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] compression the options, copied; NULL changes nothing
 */
CERVER_HIDDEN void http_web_socket_settings_set_compression (
	HttpWebSocketSettings *settings, const HttpWebSocketDeflateOptions *compression
);

/**
 * @internal
 * @brief Sets the check of the Origin of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] origin_check true to check it
 */
CERVER_HIDDEN void http_web_socket_settings_set_origin_check (
	HttpWebSocketSettings *settings, const bool origin_check
);

/**
 * @internal
 * @brief Sets the pings of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] ping_interval the milliseconds before a ping, 0 disables them
 * @param[in] pong_timeout the milliseconds to wait for the pong
 */
CERVER_HIDDEN void http_web_socket_settings_set_ping_milliseconds (
	HttpWebSocketSettings *settings,
	const unsigned int ping_interval, const unsigned int pong_timeout
);

/**
 * @internal
 * @brief Sets the close timeout of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] close_timeout the milliseconds
 */
CERVER_HIDDEN void http_web_socket_settings_set_close_timeout_milliseconds (
	HttpWebSocketSettings *settings, const unsigned int close_timeout
);

/**
 * @internal
 * @brief Sets the send timeout of the settings.
 *
 * @param[in,out] settings the settings
 * @param[in] send_timeout the milliseconds, 0 uses the send timeout of the HTTP cerver
 */
CERVER_HIDDEN void http_web_socket_settings_set_send_timeout_milliseconds (
	HttpWebSocketSettings *settings, const unsigned int send_timeout
);

/**
 * @internal
 * @brief What the server learned in the request of an upgrade, until its connection starts.
 */
typedef struct HttpWebSocketHandshake {

	/** @internal @brief The Sec-WebSocket-Accept of the response. */
	char accept[HTTP_WEB_SOCKET_ACCEPT_SIZE];

	/** @internal @brief A copy of the subprotocol of the response, NULL for none. */
	char *protocol;

	/** @internal @brief permessage-deflate was agreed. */
	bool compression;
	/** @internal @brief The parameters of permessage-deflate. */
	HttpWebSocketDeflateParameters deflate;

	/** @internal @brief A place among the connections of the cerver was taken for it. */
	bool reserved;

	/** @internal @brief The data of the connection, from on_upgrade (). */
	void *data;
	/** @internal @brief Releases the data of the connection. */
	void (*delete_data)(void *);

	/** @internal @brief The decoded data of the request that the connection adopts. */
	void *decoded_data;
	/** @internal @brief Releases the decoded data. */
	void (*delete_decoded_data)(void *);

	/** @internal @brief The bytes that arrived after the request, the first frames of the client. */
	u8 *pending;
	/** @internal @brief The number of pending bytes. */
	size_t pending_size;

} HttpWebSocketHandshake;

/**
 * @internal
 * @brief A WebSocket connection, of the server or of a client.
 */
struct _HttpWebSocket {

	/** @internal @brief The id of the connection in its registry. */
	u64 id;

	/** @internal @brief The side of the connection. */
	HttpWebSocketRole role;

	/** @internal @brief The HttpWebSocketState, read and written atomically. */
	unsigned int state;

	/** @internal @brief The references: the thread of the connection, the registry and the application hold them. */
	unsigned int references;

	/** @internal @brief The connection of the server, that owns the socket, NULL for a client. */
	Connection *connection;
	/** @internal @brief The cerver of the connection of the server. */
	struct _Cerver *cerver;

	/** @internal @brief The socket. */
	Socket *socket;

	/** @internal @brief The address of the peer. */
	char ip[CONNECTION_IP_SIZE];

	/** @internal @brief The HTTP cerver of the connection of the server, NULL for a client. */
	struct _HttpCerver *http_cerver;
	/** @internal @brief The endpoint of the connection of the server. */
	const struct _HttpEndpoint *endpoint;
	/** @internal @brief The client of the connection of a client. */
	struct _HttpWebSocketClient *client;

	/** @internal @brief The resolved settings of the endpoint (or the ones of the client). */
	HttpWebSocketSettings settings;

	/** @internal @brief The path of the request of the upgrade. */
	String *path;
	/** @internal @brief The subprotocol that was agreed, NULL for none. */
	char *protocol;

	/** @internal @brief The query of the request of the upgrade as it was sent, NULL when it had none. */
	String *query;

	/** @internal @brief The data of the connection. */
	void *data;
	/** @internal @brief Releases the data of the connection. */
	void (*delete_data)(void *);

	/** @internal @brief The decoded data that the connection adopted. */
	void *decoded_data;
	/** @internal @brief Releases the decoded data. */
	void (*delete_decoded_data)(void *);

	/** @internal @brief The compression, NULL when it was not negotiated. */
	HttpWebSocketDeflate *deflate;

	/** @internal @brief The receiver of the messages. */
	HttpWebSocketReceiver receiver;

	/** @internal @brief The buffer of each recv (). */
	u8 *buffer;
	/** @internal @brief The size of the buffer. */
	size_t buffer_size;

	/** @internal @brief The bytes that arrived with the handshake. */
	u8 *pending;
	/** @internal @brief The number of pending bytes. */
	size_t pending_size;

	/** @internal @brief Wakes up the thread of the connection (an eventfd). */
	int wake_fd;

	/** @internal @brief Held during each frame. */
	pthread_mutex_t send_mutex;
	/** @internal @brief Held during a whole data message. */
	pthread_mutex_t message_mutex;

	/** @internal @brief Protects the closing handshake and the flags that other threads set. */
	pthread_mutex_t mutex;

	/** @internal @brief A message is being streamed (http_web_socket_send_begin ()). */
	bool streaming;
	/**
	 * @internal @brief The thread that began the stream and holds #message_mutex: only it writes and ends the stream
	 * (another one unlocked a mutex that it did not hold).
	 */
	pthread_t stream_owner;
	/** @internal @brief The opcode of the stream. */
	u8 stream_type;
	/** @internal @brief The first frame of the stream was sent. */
	bool stream_started;
	/** @internal @brief The stream is compressed. */
	bool stream_compressed;

	/** @internal @brief The bytes of the pieces of the stream, for the stats of its message. */
	size_t stream_size;

	/** @internal @brief A close frame was sent. */
	bool close_sent;
	/** @internal @brief A close frame was received. */
	bool close_received;
	/** @internal @brief When the closing handshake times out. */
	struct timespec close_deadline;

	/** @internal @brief The code of the end, that on_close reports. */
	u16 close_code;
	/** @internal @brief The reason of the end. */
	char close_reason[HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE + 1];
	/** @internal @brief The length of the reason. */
	size_t close_reason_length;

	/** @internal @brief The cerver is going away (1001). */
	bool going_away;

	/** @internal @brief The connection must end now, without the closing handshake. */
	bool aborted;

	/** @internal @brief The first error, reported by the thread of the connection. */
	HttpWebSocketError error;

	/** @internal @brief When the connection last received something. */
	struct timespec last_received;
	/** @internal @brief A ping is waiting for its pong. */
	bool ping_sent;
	/** @internal @brief When the pong times out. */
	struct timespec pong_deadline;
	/** @internal @brief The pings that were sent. */
	u64 n_pings;

	/** @internal @brief The random bytes of the masks of a client. */
	u8 masks[HTTP_WEB_SOCKET_MASKS_SIZE];
	/** @internal @brief The next mask in #masks. */
	size_t masks_position;
	/** @internal @brief The buffer where a client masks its payloads. */
	u8 *scratch;

	/** @internal @brief The rooms, protected by the mutex of the registry. */
	char rooms[HTTP_WEB_SOCKET_MAX_ROOMS][HTTP_WEB_SOCKET_ROOM_NAME_SIZE];
	/** @internal @brief The number of rooms. */
	unsigned int n_rooms;

	/** @internal @brief The previous connection of the list of the registry. */
	struct _HttpWebSocket *previous;
	/** @internal @brief The next connection of the list of the registry. */
	struct _HttpWebSocket *next;
	/** @internal @brief The connection is in the registry. */
	bool registered;

	/** @internal @brief The data messages that were received, the same that its cerver counts. */
	u64 n_messages_received;
	/** @internal @brief The data messages that were sent. */
	u64 n_messages_sent;
	/** @internal @brief The bytes of the received messages, without compression. */
	u64 n_bytes_received;
	/** @internal @brief The bytes of the sent messages, without compression. */
	u64 n_bytes_sent;

};

/**
 * @internal
 * @brief Returns whether a close can be sent: its code is one that an endpoint sends, and its reason fits and is
 * UTF-8.
 *
 * @param[in] code the code
 * @param[in] reason the reason, NULL for none
 * @return true if it can be sent
 */
CERVER_HIDDEN bool http_web_socket_close_is_valid (const u16 code, const char *reason);

/**
 * @internal
 * @brief Creates a connection with its settings; its socket is taken by the caller.
 *
 * @param[in] role the role of the connection
 * @param[in] settings the settings
 * @return the new connection, NULL if it could not be created
 */
CERVER_HIDDEN HttpWebSocket *http_web_socket_create (
	const HttpWebSocketRole role, const HttpWebSocketSettings *settings
);

/**
 * @internal
 * @brief Sets the compression that was negotiated.
 *
 * @param[in,out] web_socket the connection
 * @param[in] parameters the parameters that were agreed
 * @param[in] options the options
 * @retval 0 the compression was set
 * @retval 1 it could not be created
 */
CERVER_HIDDEN u8 http_web_socket_set_compression (
	HttpWebSocket *web_socket,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
);

/**
 * @internal
 * @brief The handshake ended: the connection can send from now on (only from CONNECTING).
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_opened (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Closes a connection that opened and will never run.
 *
 * It is closed with the send mutex, so a send of another thread ends before its socket is released and no other
 * one starts.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_closed (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Gives the connection the bytes that arrived with the handshake.
 *
 * @param[in,out] web_socket the connection
 * @param[in] pending the bytes, the connection takes them
 * @param[in] pending_size the number of bytes
 */
CERVER_HIDDEN void http_web_socket_set_pending (
	HttpWebSocket *web_socket, u8 *pending, const size_t pending_size
);

/**
 * @internal
 * @brief Runs the connection in the current thread until it ends: it calls every method, and it closes the socket
 * at the end.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_run (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Wakes up the thread of the connection.
 *
 * @param[in] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_wake (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief The cerver is going away: the connection is closed with 1001.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_go_away (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Ends the connection now, without waiting for the closing handshake.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_abort (HttpWebSocket *web_socket);

/** @internal @brief The timeout of a send that is the one of its connection, resolved when the message can be sent. */
#define HTTP_WEB_SOCKET_SEND_TIMEOUT_CONNECTION		0

/**
 * @internal
 * @brief Sends a whole message with a deadline of some milliseconds (for a broadcast), or with the one of the
 * connection.
 *
 * A send that does not end in time fails the connection; one that the connection does not take because it is
 * closing only returns its error.
 *
 * @param[in,out] web_socket the connection
 * @param[in] type the opcode
 * @param[in] data the payload
 * @param[in] size the bytes of the payload
 * @param[in] timeout the milliseconds, or #HTTP_WEB_SOCKET_SEND_TIMEOUT_CONNECTION
 * @retval 0 the message was sent
 * @retval 1 it was not sent
 */
CERVER_HIDDEN u8 http_web_socket_send_timeout (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size,
	const unsigned int timeout
);

/**
 * @internal
 * @brief Starts the connection of the server of an upgraded request in its own thread; it takes its connection.
 *
 * @param[in,out] cr the receive of the connection
 * @param[in,out] http_receive the receive of the request
 * @return true when it started, then the caller must not drop the connection
 */
CERVER_HIDDEN bool http_web_socket_start (
	struct CerverReceive *cr, struct _HttpReceive *http_receive
);

/**
 * @internal
 * @brief Releases the handshake of an upgrade.
 *
 * @param[in] handshake the handshake, it can be NULL
 */
CERVER_HIDDEN void http_web_socket_handshake_delete (HttpWebSocketHandshake *handshake);

/**
 * @internal
 * @brief The WebSocket connections of an HTTP cerver, and their stats.
 */
typedef struct _HttpWebSocketRegistry {

	/** @internal @brief Protects the list, the counts and the rooms. */
	pthread_mutex_t mutex;

	/** @internal @brief The first connection of the list. */
	HttpWebSocket *first;

	/** @internal @brief The connections that are registered. */
	unsigned int count;

	/** @internal @brief The connections plus the upgrades that have a place. */
	unsigned int reserved;

	/** @internal @brief The most connections. */
	unsigned int max_connections;

	/** @internal @brief The milliseconds that a broadcast waits for each connection. */
	unsigned int broadcast_timeout;

	/** @internal @brief The cerver is going away, no more connections are accepted. */
	bool stopping;

	/** @internal @brief The id of the next connection. */
	u64 next_id;

	/** @internal @brief The connections that were opened, read and written atomically like the stats below. */
	u64 n_opened;
	/** @internal @brief The connections that ended. */
	u64 n_closed;
	/** @internal @brief The upgrades that were rejected, by reason. */
	u64 n_rejected[HTTP_WEB_SOCKET_REJECTIONS];
	/** @internal @brief The data messages that were received. */
	u64 n_messages_received;
	/** @internal @brief The data messages that were sent. */
	u64 n_messages_sent;
	/** @internal @brief The bytes of the received messages. */
	u64 n_bytes_received;
	/** @internal @brief The bytes of the sent messages. */
	u64 n_bytes_sent;
	/** @internal @brief The connections that failed because a send did not end in time. */
	u64 n_slow;
	/** @internal @brief The connections that ended with an error. */
	u64 n_errors;
	/** @internal @brief The connections that ended with each close code from #HTTP_WEB_SOCKET_CLOSE_CODES_FIRST. */
	u64 n_closes[HTTP_WEB_SOCKET_CLOSE_CODES_SIZE];
	/** @internal @brief The connections that ended with any other close code. */
	u64 n_closes_other;

} HttpWebSocketRegistry;

/**
 * @internal
 * @brief Creates an empty registry.
 *
 * @return the new registry, NULL if it could not be allocated
 */
CERVER_HIDDEN HttpWebSocketRegistry *http_web_socket_registry_create (void);

/**
 * @internal
 * @brief Releases a registry.
 *
 * @param[in] registry the registry, it can be NULL
 */
CERVER_HIDDEN void http_web_socket_registry_delete (HttpWebSocketRegistry *registry);

/**
 * @internal
 * @brief Takes a place for an upgrade.
 *
 * @param[in,out] registry the registry
 * @return false when the cerver has its most connections or it is stopping
 */
CERVER_HIDDEN bool http_web_socket_registry_reserve (HttpWebSocketRegistry *registry);

/**
 * @internal
 * @brief Gives back the place of an upgrade that did not start.
 *
 * @param[in,out] registry the registry
 */
CERVER_HIDDEN void http_web_socket_registry_unreserve (HttpWebSocketRegistry *registry);

/**
 * @internal
 * @brief Adds a connection whose place was already taken, and sets its id.
 *
 * @param[in,out] registry the registry
 * @param[in,out] web_socket the connection
 * @retval 0 the connection was added
 * @retval 1 the cerver is stopping
 */
CERVER_HIDDEN u8 http_web_socket_registry_add (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
);

/**
 * @internal
 * @brief Removes a connection, and gives back its place.
 *
 * @param[in,out] registry the registry
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_registry_remove (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
);

/**
 * @internal
 * @brief Closes every connection with 1001, and accepts no more.
 *
 * @param[in,out] registry the registry
 */
CERVER_HIDDEN void http_web_socket_registry_go_away (HttpWebSocketRegistry *registry);

/**
 * @internal
 * @brief Ends every connection now.
 *
 * @param[in,out] registry the registry
 */
CERVER_HIDDEN void http_web_socket_registry_abort (HttpWebSocketRegistry *registry);

/**
 * @internal
 * @brief Counts an upgrade that was rejected.
 *
 * @param[in,out] registry the registry
 * @param[in] rejection the reason
 */
CERVER_HIDDEN void http_web_socket_registry_count_rejection (
	HttpWebSocketRegistry *registry, const HttpWebSocketRejection rejection
);

/**
 * @internal
 * @brief Counts the close code of a connection that ended.
 *
 * @param[in,out] registry the registry
 * @param[in] code the close code
 */
CERVER_HIDDEN void http_web_socket_registry_count_close (
	HttpWebSocketRegistry *registry, const u16 code
);

/**
 * @internal
 * @brief What the client takes from the response of the server.
 */
typedef struct HttpWebSocketClientResponse {

	/** @internal @brief The status of the response. */
	int status;

	/** @internal @brief The response was parsed until its empty line. */
	bool complete;

	/** @internal @brief A header was repeated, too long, or the response was not valid HTTP. */
	bool invalid;

	/** @internal @brief The value of the Upgrade header. */
	char upgrade[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	/** @internal @brief The value of the Connection header. */
	char connection[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	/** @internal @brief The value of the Sec-WebSocket-Accept header. */
	char accept[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	/** @internal @brief The value of the Sec-WebSocket-Protocol header. */
	char protocol[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	/** @internal @brief The value of the Sec-WebSocket-Extensions header. */
	char extensions[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];

	/** @internal @brief The response had a Sec-WebSocket-Protocol. */
	bool has_protocol;
	/** @internal @brief The response had a Sec-WebSocket-Extensions. */
	bool has_extensions;

	/** @internal @brief The bytes of the response until its empty line, the frames of the server can follow them. */
	size_t size;

} HttpWebSocketClientResponse;

/** @internal @brief A result of http_web_socket_client_response_parse (): the response is complete. */
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE	0
/** @internal @brief A result of http_web_socket_client_response_parse (): it needs more bytes. */
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL		1
/** @internal @brief A result of http_web_socket_client_response_parse (): the response is not valid. */
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID		2

/**
 * @internal
 * @brief Parses the response of a handshake; it can be called again with more bytes.
 *
 * @param[in] data the bytes of the response
 * @param[in] size the number of bytes
 * @param[out] response what the client takes from the response
 * @retval HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE the response is complete
 * @retval HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL it needs more bytes
 * @retval HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID it is not valid (invalid is set), also without its end in
 * #HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE bytes
 */
CERVER_PUBLIC u8 http_web_socket_client_response_parse (
	const char *data, const size_t size,
	HttpWebSocketClientResponse *response
);

/**
 * @internal
 * @brief Checks a response with the rules of RFC 6455 section 4.1 and of the extensions that were offered: a 101,
 * the tokens, the accept of the key, a subprotocol that was offered and permessage-deflate.
 *
 * @param[in] response the response
 * @param[in] key the key of the request
 * @param[in] protocols the subprotocols that were offered
 * @param[in] n_protocols the number of subprotocols
 * @param[in] options the options of the compression
 * @param[out] protocol the subprotocol of the server, it can be NULL
 * @param[out] compression true when permessage-deflate was agreed
 * @param[out] parameters the parameters of the extension
 * @return true when the connection can start
 */
CERVER_PUBLIC bool http_web_socket_client_response_check (
	const HttpWebSocketClientResponse *response, const char *key,
	char * const *protocols, const unsigned int n_protocols,
	const HttpWebSocketDeflateOptions *options,
	const char **protocol, bool *compression, HttpWebSocketDeflateParameters *parameters
);

/**
 * @internal
 * @brief A WebSocket client.
 */
struct _HttpWebSocketClient {

	/** @internal @brief The host of the url. */
	char host[HTTP_WEB_SOCKET_CLIENT_HOST_SIZE];
	/** @internal @brief The port of the url. */
	u16 port;
	/** @internal @brief The path and the query of the url. */
	char path[HTTP_WEB_SOCKET_CLIENT_PATH_SIZE];

	/** @internal @brief The extra headers of the request, "Name: value". */
	DoubleList *headers;

	/** @internal @brief The methods, the limits and the timeouts of the connection, the subprotocols and the compression that are offered. */
	HttpWebSocketSettings settings;

	/** @internal @brief The milliseconds of each connect (). */
	unsigned int connect_timeout;
	/** @internal @brief The milliseconds of the handshake. */
	unsigned int handshake_timeout;

	/** @internal @brief The client reconnects. */
	bool reconnect;
	/** @internal @brief The first wait between reconnections. */
	unsigned int reconnect_min;
	/** @internal @brief The longest wait between reconnections. */
	unsigned int reconnect_max;

	/** @internal @brief Protects the connection of now. */
	pthread_mutex_t mutex;

	/** @internal @brief The connection of now, NULL between connections. */
	HttpWebSocket *web_socket;

	/** @internal @brief Wakes up the wait between reconnections (an eventfd). */
	int wake_fd;

	/** @internal @brief http_web_socket_client_close () or delete (): there are no more reconnections. */
	bool stopping;

	/** @internal @brief The thread of the client. */
	pthread_t thread;
	/** @internal @brief The thread was started and not joined. */
	bool thread_started;

	/** @internal @brief Only one of the threads that wait for the client joins its thread. */
	pthread_mutex_t join_mutex;

	/** @internal @brief The connections that were opened. */
	unsigned int n_connections;

	/** @internal @brief The attempts that failed in a row, with the connections that ended before they were stable. */
	unsigned int n_failures;

	/**
	 * @internal
	 * @brief A connection that is still open at this time lasted the longest wait between reconnections: it was
	 * stable, and the failures before it are forgotten.
	 */
	struct timespec stable_deadline;

	/** @internal @brief The address of the last connection. */
	char ip[NETWORK_ADDRESS_STRING_SIZE];

	/** @internal @brief The data of the application, every connection of the client has it since it is created. */
	void *data;

};

/**
 * @internal
 * @brief Returns the milliseconds to wait before the attempt that follows some failures.
 *
 * min doubled for each failure, never above max nor above the timeout that a poll () takes, with a part of it taken
 * from random so the clients of a server do not come back together.
 *
 * @param[in] min the first wait
 * @param[in] max the longest wait
 * @param[in] n_failures the failures in a row
 * @param[in] random a random value
 * @return the milliseconds
 */
CERVER_PRIVATE unsigned int http_web_socket_client_backoff_delay (
	const unsigned int min, const unsigned int max,
	const unsigned int n_failures, const unsigned int random
);

#ifdef __cplusplus
}
#endif

#endif
