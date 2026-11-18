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
#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/events/internal.h"
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

struct _HttpWebSocketRoom;

/**
 * @internal
 * @brief The place of a connection in a room: a node of the list of the members of the room and of the list of the
 * rooms of the connection, protected by the mutex of the registry.
 */
typedef struct HttpWebSocketMembership {

	/** @internal @brief The room. */
	struct _HttpWebSocketRoom *room;
	/** @internal @brief The connection. */
	struct _HttpWebSocket *web_socket;

	/** @internal @brief The previous member of the room. */
	struct HttpWebSocketMembership *previous;
	/** @internal @brief The next member of the room. */
	struct HttpWebSocketMembership *next;

	/** @internal @brief The next room of the connection. */
	struct HttpWebSocketMembership *next_room;

} HttpWebSocketMembership;

/**
 * @internal
 * @brief A room of the index of the registry, created with its first member and released with its last one.
 */
typedef struct _HttpWebSocketRoom {

	/** @internal @brief A copy of the name. */
	char *name;
	/** @internal @brief The bytes of the name, the key of the index. */
	size_t length;

	/** @internal @brief The first member. */
	HttpWebSocketMembership *first;
	/** @internal @brief The members. */
	unsigned int count;

} HttpWebSocketRoom;

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

/** @internal @brief The first bytes of the queue of a connection, that it keeps while it lives. */
#define HTTP_WEB_SOCKET_QUEUE_INITIAL_SIZE			(1 << 12)

/** @internal @brief The buffers of a connection above this size go back to their first size after their use (W7). */
#define HTTP_WEB_SOCKET_SHRINK_SIZE					(1 << 16)

/**
 * @internal
 * @brief How a connection reads, writes and ends its stream; a socket today, TLS later (A24).
 *
 * Every method can be called with the stream already ended: it fails without touching the file descriptor.
 */
typedef struct HttpWebSocketTransport {

	/** @internal @brief A recv () without waiting: the bytes, 0 at the end, -1 with errno. */
	ssize_t (*read) (struct _HttpWebSocket *web_socket, void *buffer, size_t size);

	/** @internal @brief A sendmsg () without waiting: the bytes that the stream took, -1 with errno. */
	ssize_t (*write) (struct _HttpWebSocket *web_socket, const struct iovec *parts, int n_parts);

	/** @internal @brief Ends the writes (SHUT_WR) of the stream, its reads stay open. */
	void (*end_writes) (struct _HttpWebSocket *web_socket);

	/** @internal @brief Ends both directions, a read or a write that waits ends now. */
	void (*shutdown) (struct _HttpWebSocket *web_socket);

	/** @internal @brief The file descriptor that a poller watches, -1 without one. */
	int (*fd) (const struct _HttpWebSocket *web_socket);

	/**
	 * @internal
	 * @brief Whether a read gives bytes without the fd being readable (TLS: a record that was decrypted already).
	 * @since 2.1b-22
	 */
	bool (*pending) (const struct _HttpWebSocket *web_socket);

	/**
	 * @internal
	 * @brief The bytes that the transport keeps to send (TLS: the encrypted ones that the socket did not take);
	 * they count in the queue, so its limit and its backpressure see them.
	 * @since 2.1b-22
	 */
	size_t (*waiting) (const struct _HttpWebSocket *web_socket);

	/**
	 * @internal
	 * @brief Sends what the transport keeps, without waiting: the bytes that still wait, -1 with errno.
	 * @since 2.1b-22
	 */
	ssize_t (*flush) (struct _HttpWebSocket *web_socket);

} HttpWebSocketTransport;

/** @internal @brief The transport of the connections over a socket. */
CERVER_HIDDEN const HttpWebSocketTransport http_web_socket_socket_transport;

/**
 * @internal
 * @brief The transport of the connections over TLS: a write encrypts every byte into the output of the TLS of the
 * socket (it takes them all), and the encrypted bytes that the socket does not take wait there.
 * @since 2.1b-22
 */
CERVER_HIDDEN const HttpWebSocketTransport http_web_socket_tls_transport;

/** @internal @brief What an event of a connection that its pool runs is (http/websockets/loop.c). */
typedef enum HttpWebSocketEventType {

	/** @internal @brief on_open (). */
	HTTP_WEB_SOCKET_EVENT_OPEN			= 0,
	/** @internal @brief on_message () with a whole message. */
	HTTP_WEB_SOCKET_EVENT_MESSAGE		= 1,
	/** @internal @brief on_fragment () with a piece of a message. */
	HTTP_WEB_SOCKET_EVENT_FRAGMENT		= 2,
	/** @internal @brief on_ping (), the loop already answered it. */
	HTTP_WEB_SOCKET_EVENT_PING			= 3,
	/** @internal @brief on_pong (). */
	HTTP_WEB_SOCKET_EVENT_PONG			= 4,
	/** @internal @brief on_drain (). */
	HTTP_WEB_SOCKET_EVENT_DRAIN			= 5,
	/** @internal @brief on_error () and on_close (), the last ones. */
	HTTP_WEB_SOCKET_EVENT_END			= 6

} HttpWebSocketEventType;

/** @internal @brief An event of a connection, its payload is in the data of its batch. */
typedef struct HttpWebSocketEvent {

	/** @internal @brief What the event is. */
	HttpWebSocketEventType type;
	/** @internal @brief The opcode of a message or of a fragment. */
	HttpWebSocketOpcode opcode;
	/** @internal @brief The first piece of a message. */
	bool first;
	/** @internal @brief The last piece of a message. */
	bool last;
	/** @internal @brief Where its payload starts in the data of the batch. */
	size_t offset;
	/** @internal @brief The bytes of its payload. */
	size_t size;

} HttpWebSocketEvent;

/**
 * @internal
 * @brief The events that the loop read for a connection, that one job of its pool runs in order; the loop writes
 * them and the job reads them, never at the same time.
 */
typedef struct HttpWebSocketBatch {

	/** @internal @brief The events. */
	HttpWebSocketEvent *events;
	/** @internal @brief The events in the batch. */
	unsigned int n_events;
	/** @internal @brief The places of #events. */
	unsigned int capacity;

	/** @internal @brief The payloads of the events. */
	u8 *data;
	/** @internal @brief The bytes in #data. */
	size_t size;
	/** @internal @brief The size of #data. */
	size_t data_capacity;

} HttpWebSocketBatch;

/** @internal @brief Another thread asks the loop of a connection to adopt it. */
#define HTTP_WEB_SOCKET_REQUEST_ADOPT				(1u << 0)
/** @internal @brief Another thread left bytes in the queue of a connection. */
#define HTTP_WEB_SOCKET_REQUEST_OUTPUT				(1u << 1)
/** @internal @brief The job of a connection ended. */
#define HTTP_WEB_SOCKET_REQUEST_JOB_DONE			(1u << 2)
/** @internal @brief The pool did not run the job of a connection, the loop runs it. */
#define HTTP_WEB_SOCKET_REQUEST_JOB_REJECTED		(1u << 3)
/** @internal @brief The timers of a connection changed (a close that another thread sent). */
#define HTTP_WEB_SOCKET_REQUEST_TIMERS				(1u << 4)
/** @internal @brief The cerver goes away (1001). */
#define HTTP_WEB_SOCKET_REQUEST_GO_AWAY				(1u << 5)
/** @internal @brief The connection ends now, without its closing handshake. */
#define HTTP_WEB_SOCKET_REQUEST_ABORT				(1u << 6)

/** @internal @brief A park whose connection is owned by its loop or by a thread of its pool. */
#define HTTP_WEB_SOCKET_PARK_TAKEN					0
/** @internal @brief A park whose connection waits in the poller of its pool, the first one that takes it owns it. */
#define HTTP_WEB_SOCKET_PARK_PARKED					1

/** @internal @brief What a thread of the pool read after a job: nothing that ends the connection. */
#define HTTP_WEB_SOCKET_READ_END_NONE				0
/** @internal @brief The frames that it read ended the connection (a close or a failure of the peer). */
#define HTTP_WEB_SOCKET_READ_END_FRAMES				1
/** @internal @brief The peer ended its side of the TCP connection. */
#define HTTP_WEB_SOCKET_READ_END_PEER				2
/** @internal @brief A read failed. */
#define HTTP_WEB_SOCKET_READ_END_FAILED				3

struct _HttpWebSocket;

/**
 * @internal
 * @brief Where a connection of a loop waits for its next frames in the poller of its pool, between two jobs: the
 * threads of the pool read it when they have nothing else, like the connections that are kept (F4b).
 *
 * A park lives in a list of its loop and is released with it, never with its connection: a thread that got its
 * report from the poller after the connection was taken (and maybe released) only finds it taken, or parked by
 * another connection that it reads in vain.
 */
typedef struct HttpWebSocketPark {

	/** @internal @brief The source of the socket in the poller of the pool, its owner is the park. */
	EventSource source;

	/** @internal @brief Protects the state, the connection and the arming of the source. */
	pthread_mutex_t mutex;
	/** @internal @brief #HTTP_WEB_SOCKET_PARK_TAKEN or #HTTP_WEB_SOCKET_PARK_PARKED. */
	unsigned int state;
	/** @internal @brief The connection, NULL when the park is free. */
	struct _HttpWebSocket *web_socket;

	/** @internal @brief The next free park of the loop. */
	struct HttpWebSocketPark *next;
	/** @internal @brief The next park of the loop, free or not. */
	struct HttpWebSocketPark *all_next;

} HttpWebSocketPark;

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

	/** @internal @brief The compression, NULL when it was not negotiated or after the end of the connection (W7). */
	HttpWebSocketDeflate *deflate;
	/** @internal @brief The compression was negotiated, it stays after zlib is released. */
	bool compressed;

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
	/** @internal @brief How the connection reads and writes, NULL until it has a stream. */
	const HttpWebSocketTransport *transport;

	/**
	 * @internal
	 * @brief The frames that wait to be written, from #queue_start to #queue_end, protected by #send_mutex; a send
	 * writes what the stream takes and leaves the rest here, and the owner of the connection (its loop or its
	 * thread) writes it.
	 */
	u8 *queue;
	/** @internal @brief The size of #queue. */
	size_t queue_capacity;
	/** @internal @brief The first byte that waits. */
	size_t queue_start;
	/** @internal @brief The end of the bytes that wait. */
	size_t queue_end;
	/** @internal @brief The bytes that wait, atomic: http_web_socket_get_buffered_size () reads it without the mutex. */
	size_t queue_size;
	/** @internal @brief The most bytes of data frames that wait, resolved when the connection starts. */
	size_t queue_max;
	/** @internal @brief What a send does with a message that does not fit. */
	HttpWebSocketOverflow overflow;
	/** @internal @brief A send went over half of the queue: on_drain () runs when it is empty. */
	bool queue_backpressure;
	/** @internal @brief The queue became empty after #queue_backpressure: on_drain () has to run. */
	bool queue_drained;
	/** @internal @brief When the queue fails if it writes nothing more: its send timeout after its last write, or after its first byte. */
	struct timespec queue_deadline;
	/** @internal @brief The writes of the stream were ended (SHUT_WR). */
	bool writes_ended;

	/** @internal @brief The loop that owns the connection, NULL with a thread (CERVER_HANDLER_TYPE_THREADS and the clients). */
	EventLoop *loop;
	/** @internal @brief Its source in the poller of its loop. */
	EventSource source;
	/** @internal @brief Its timer in the wheel of its loop. */
	EventTimer timer;
	/** @internal @brief The lane of its loop whose pool runs its jobs. */
	unsigned int lane;
	/** @internal @brief The HTTP_WEB_SOCKET_REQUEST_* flags that other threads gave the loop, atomic. */
	unsigned int loop_requests;
	/** @internal @brief The next connection with requests for the loop, with the mutex of the completed connections. */
	struct _HttpWebSocket *loop_next;
	/** @internal @brief The next connection whose job waits for a place in its pool, only the loop uses it. */
	struct _HttpWebSocket *waiting_next;
	/** @internal @brief The job of the connection waits for a place in its pool. */
	bool waiting;
	/** @internal @brief The events of the next job. */
	HttpWebSocketBatch batch;
	/** @internal @brief The events that the current job runs, the loop fills #batch meanwhile. */
	HttpWebSocketBatch running;
	/** @internal @brief A job of the pool runs its events: the loop does not read the connection. */
	bool job;
	/** @internal @brief on_open () ran and the connection is in the registry. */
	bool opened;
	/** @internal @brief The loop reads the connection. */
	bool reading;
	/** @internal @brief The connection ends: the loop writes its queue, ends its writes and drains its peer. */
	bool ending;
	/** @internal @brief The loop reads and discards what the peer sends after the writes ended. */
	bool draining;
	/** @internal @brief The bytes that were drained. */
	size_t drained;
	/** @internal @brief The end of the drain, in the milliseconds of the loop. */
	u64 drain_deadline;
	/** @internal @brief The last events (on_error (), on_close ()) were given to its pool: nothing else runs. */
	bool finished;
	/** @internal @brief The loop released the connection: the requests that still come are ignored. */
	bool released;
	/** @internal @brief The place of the handshake is still the one of the connection, it is given back if it is not registered. */
	bool reserved;

	/** @internal @brief Where it waits in the poller of its pool between its jobs, NULL when it can not. */
	HttpWebSocketPark *park;
	/**
	 * @internal @brief Its queue was empty again while a job had it: its on_drain () goes after the job (loop); atomic,
	 * the thread of the job reads it before it parks the connection.
	 */
	bool drain_pending;
	/** @internal @brief What a thread of the pool read that ends it, a `HTTP_WEB_SOCKET_READ_END_*`. */
	unsigned int read_end;

	/** @internal @brief The rooms, protected by the mutex of the registry; kept while on_close () runs. */
	HttpWebSocketMembership *rooms;
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

/**
 * @internal
 * @brief The milliseconds of each send of a connection: the ones of its settings, of its HTTP cerver, of its cerver
 * or the default.
 *
 * @param[in] web_socket the connection
 * @return the milliseconds
 */
CERVER_HIDDEN unsigned int http_web_socket_send_milliseconds (const HttpWebSocket *web_socket);

/**
 * @internal
 * @brief The connection fails: it keeps its first error and ends without its closing handshake.
 *
 * @param[in,out] web_socket the connection
 * @param[in] error the error, HTTP_WEB_SOCKET_ERROR_SEND also counts a slow peer
 */
CERVER_HIDDEN void http_web_socket_fail (HttpWebSocket *web_socket, const HttpWebSocketError error);

/**
 * @internal
 * @brief Keeps the first error of a connection, the one that on_error () reports.
 *
 * @param[in,out] web_socket the connection
 * @param[in] error the error
 */
CERVER_HIDDEN void http_web_socket_set_error (HttpWebSocket *web_socket, const HttpWebSocketError error);

/**
 * @internal
 * @brief The next 4 random bytes of the masks of a client, renewed when they run out.
 *
 * @param[in,out] web_socket the connection of a client
 * @param[out] mask where the mask is written, #HTTP_WEB_SOCKET_MASK_SIZE bytes
 */
CERVER_HIDDEN void http_web_socket_next_mask (HttpWebSocket *web_socket, u8 *mask);

/**
 * @internal
 * @brief Tells the owner of a connection (its loop or its thread) that something of the connection changed: its
 * queue, its timers, a go away or an abort.
 *
 * @param[in,out] web_socket the connection
 * @param[in] requests the HTTP_WEB_SOCKET_REQUEST_* flags, for a loop
 */
CERVER_HIDDEN void http_web_socket_notify (HttpWebSocket *web_socket, const unsigned int requests);

/**
 * @internal
 * @brief Sends the close frame once (into the queue), the send mutex must be held.
 *
 * @param[in,out] web_socket the connection
 * @param[in] code the code, 1005 sends a close frame without a payload
 * @param[in] reason the reason, it can be NULL
 * @param[in] length the bytes of the reason
 * @retval 0 the close frame was taken now
 * @retval 1 it was already sent, or the connection could not take it
 */
CERVER_HIDDEN u8 http_web_socket_close_frame_locked (
	HttpWebSocket *web_socket, const u16 code, const char *reason, const size_t length
);

/**
 * @internal
 * @brief Starts the connection of the server of an upgraded request; it takes its connection.
 *
 * With a loop (CERVER_HANDLER_TYPE_EVENTS) the connection goes to that loop, that reads it and writes its queue,
 * and its methods run in jobs of the pool of its endpoint; without one it runs in its own thread.
 *
 * @param[in,out] cr the receive of the connection
 * @param[in,out] http_receive the receive of the request
 * @param[in,out] loop the loop of the connection, NULL for a thread
 * @return true when it started, then the caller must not drop the connection
 */
CERVER_HIDDEN bool http_web_socket_start (
	struct CerverReceive *cr, struct _HttpReceive *http_receive, EventLoop *loop
);

/**
 * @internal
 * @brief A connection reads and writes a socket: its transport, its queue and, with its own thread, its buffer.
 *
 * @param[in,out] web_socket the connection
 * @param[in] socket the socket, the connection does not own it
 * @param[in] thread the connection runs in its own thread, that reads in a buffer of its own
 * @retval 0 the connection has its stream
 * @retval 1 no memory
 */
CERVER_HIDDEN u8 http_web_socket_stream_set (HttpWebSocket *web_socket, Socket *socket, const bool thread);

/**
 * @internal
 * @brief The connection that ended leaves its registry (or gives its place back) and its TCP connection is
 * dropped, by its owner.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_stream_end (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Returns whether the peer can still be sending after the close of cerver, that it did not answer.
 *
 * @param[in] web_socket the connection
 * @return true when the connection drains its peer after its writes ended
 */
CERVER_HIDDEN bool http_web_socket_drain_needed (const HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Gives a new connection of the server to a loop, from the thread of the pool that answered its upgrade.
 *
 * @param[in,out] web_socket the connection, the loop takes its reference
 * @param[in,out] loop the loop
 * @return true when the loop took it
 */
CERVER_HIDDEN bool http_web_socket_loop_adopt (HttpWebSocket *web_socket, EventLoop *loop);

/**
 * @internal
 * @brief Releases the events of a batch.
 *
 * @param[in,out] batch the batch
 */
CERVER_HIDDEN void http_web_socket_batch_delete (HttpWebSocketBatch *batch);

/**
 * @internal
 * @brief Resolves the size and the policy of the queue of a connection, from its settings or from its cerver.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_queue_setup (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Releases the queue of a connection.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_queue_delete (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Sends a frame: it writes what the stream takes without waiting when nothing waits before it, and leaves
 * the rest in the queue for the owner of the connection. The send mutex must be held.
 *
 * A whole message that does not fit takes the policy of the queue; the control frames and the pieces of a stream
 * always fit. A frame larger than the queue fits only when the queue is empty.
 *
 * @param[in,out] web_socket the connection
 * @param[in] fin the last frame of its message
 * @param[in] rsv1 a compressed message
 * @param[in] opcode the opcode
 * @param[in] payload the payload
 * @param[in] size the bytes of the payload
 * @param[in] exempt a frame that never takes the policy: a control frame or a piece of a stream
 * @return HTTP_WEB_SOCKET_SEND_QUEUED or HTTP_WEB_SOCKET_SEND_BACKPRESSURE when it was taken, the other results when
 * it was not
 */
CERVER_HIDDEN HttpWebSocketSendResult http_web_socket_queue_frame (
	HttpWebSocket *web_socket,
	const bool fin, const bool rsv1, const u8 opcode,
	const u8 *payload, const size_t size, const bool exempt
);

/**
 * @internal
 * @brief Writes what the stream takes of the queue, without waiting; the send mutex must be held.
 *
 * @param[in,out] web_socket the connection
 * @retval 0 the stream took what it could
 * @retval 1 a write failed, the connection fails
 */
CERVER_HIDDEN u8 http_web_socket_queue_flush (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Returns whether nothing waits in the queue, with the send mutex held or by the owner of the connection.
 *
 * @param[in] web_socket the connection
 * @return true when the queue is empty
 */
CERVER_HIDDEN bool http_web_socket_queue_is_empty (const HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Returns the milliseconds until the queue fails for not writing a byte in its send timeout.
 *
 * @param[in] web_socket the connection
 * @return the milliseconds, 0 when it is over, -1 with an empty queue
 */
CERVER_HIDDEN int http_web_socket_queue_remaining (const HttpWebSocket *web_socket);

/**
 * @internal
 * @brief The buffers of a connection that grew above #HTTP_WEB_SOCKET_SHRINK_SIZE go back to their first size
 * (W7, S10); the owner of the connection calls it when they are empty.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_queue_shrink (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Adds an event to the next job of a connection of a loop, with a copy of its payload; the loop calls it
 * while it parses what it read.
 *
 * @param[in,out] web_socket the connection
 * @param[in] type the event
 * @param[in] opcode the opcode of a message or of a fragment
 * @param[in] payload the payload, it can be NULL with 0
 * @param[in] size the bytes of the payload
 * @param[in] first the first piece of a message
 * @param[in] last the last piece of a message
 * @retval 0 the event was added
 * @retval 1 no memory
 */
CERVER_HIDDEN u8 http_web_socket_loop_event (
	HttpWebSocket *web_socket, const HttpWebSocketEventType type, const HttpWebSocketOpcode opcode,
	const u8 *payload, const size_t size, const bool first, const bool last
);

/**
 * @internal
 * @brief The receiver of a connection: what it parsed goes to its methods (a thread) or to its next job (a loop).
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_receiver_setup (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Parses bytes that the connection received.
 *
 * @param[in,out] web_socket the connection
 * @param[in] data the bytes
 * @param[in] size the number of bytes
 * @return true when the connection ends
 */
CERVER_HIDDEN bool http_web_socket_receive_bytes (HttpWebSocket *web_socket, u8 *data, const size_t size);

/**
 * @internal
 * @brief The next ping is sent after the ping interval without receiving anything.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_alive (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief The code and the reason that on_close () reports.
 *
 * @param[in,out] web_socket the connection
 * @param[in] code the code
 * @param[in] reason the reason, it can be NULL
 * @param[in] length the bytes of the reason
 */
CERVER_HIDDEN void http_web_socket_set_close (
	HttpWebSocket *web_socket, const u16 code, const char *reason, const size_t length
);

/**
 * @internal
 * @brief The timers of a connection that expired: the closing handshake, the pong of a ping and the next ping, and
 * the send timeout of its queue.
 *
 * @param[in,out] web_socket the connection
 * @param[in] pings false while the methods run and the connection is not read (a loop): its pings wait
 * @return true when the connection ends
 */
CERVER_HIDDEN bool http_web_socket_timers (HttpWebSocket *web_socket, const bool pings);

/**
 * @internal
 * @brief The milliseconds until the next timer of a connection, -1 without one.
 *
 * @param[in] web_socket the connection
 * @param[in] pings false while its pings wait
 * @return the milliseconds
 */
CERVER_HIDDEN int http_web_socket_next_timeout (const HttpWebSocket *web_socket, const bool pings);

/**
 * @internal
 * @brief Counts the end of a connection and takes it out of its registry, before its last methods.
 *
 * @param[in,out] web_socket the connection
 * @return the first error of the connection
 */
CERVER_HIDDEN HttpWebSocketError http_web_socket_ended (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Runs the last methods of a connection, on_error () and on_close ().
 *
 * @param[in,out] web_socket the connection
 * @param[in] error the first error of the connection
 */
CERVER_HIDDEN void http_web_socket_closed_methods (HttpWebSocket *web_socket, const HttpWebSocketError error);

/**
 * @internal
 * @brief Puts a connection whose on_open () ran in its registry; a cerver that is going away sends it 1001.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_register (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief Gives the loop of a connection some requests from another thread; the loop handles them in its next step.
 *
 * @param[in,out] web_socket the connection, of a loop
 * @param[in] requests the HTTP_WEB_SOCKET_REQUEST_* flags
 */
CERVER_HIDDEN void http_web_socket_loop_request (HttpWebSocket *web_socket, const unsigned int requests);

/**
 * @internal
 * @brief The loop handles the requests of a connection that other threads gave it; it releases the reference of
 * the request.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_loop_requests (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief The poller of the loop reported the socket of a connection.
 *
 * @param[in,out] web_socket the connection
 * @param[in] ready the EVENT_READY_* flags
 */
CERVER_HIDDEN void http_web_socket_loop_ready (HttpWebSocket *web_socket, const unsigned int ready);

/**
 * @internal
 * @brief The timer of a connection expired in the wheel of its loop.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_loop_timeout (HttpWebSocket *web_socket);

/**
 * @internal
 * @brief The jobs of the connections of a loop that did not find a place in their pools try again.
 *
 * @param[in,out] loop the loop
 */
CERVER_HIDDEN void http_web_socket_loop_retry (EventLoop *loop);

/**
 * @internal
 * @brief A thread of a pool got the report of a park: it owns its connection when the connection still waits there.
 *
 * @param[in,out] park the park of the report
 * @return the connection, to read with http_web_socket_park_work (); NULL when it was taken before
 */
CERVER_HIDDEN HttpWebSocket *http_web_socket_park_take (HttpWebSocketPark *park);

/**
 * @internal
 * @brief The work of a thread of a pool that took a connection from its park: it reads it, runs its methods, and
 * parks it again or gives it back to its loop.
 *
 * @param[in,out] web_socket_ptr the connection
 */
CERVER_HIDDEN void http_web_socket_park_work (void *web_socket_ptr);

/**
 * @internal
 * @brief A loop found the report of a park in the poller of a pool whose threads do not wait: the connection goes
 * back to its loop, that reads it.
 *
 * @param[in,out] park the park of the report
 */
CERVER_HIDDEN void http_web_socket_park_rescued (HttpWebSocketPark *park);

/**
 * @internal
 * @brief Releases the parks of a loop, after the threads of its pools ended.
 *
 * @param[in,out] loop the loop
 */
CERVER_HIDDEN void http_web_socket_parks_delete (EventLoop *loop);

/**
 * @internal
 * @brief Releases the requests that a loop did not handle, when it is deleted.
 *
 * @param[in,out] web_socket the first connection of the list
 */
CERVER_HIDDEN void http_web_socket_loop_discard (HttpWebSocket *web_socket);

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

	/** @internal @brief The rooms by their names, each one with its members (A14). */
	struct Htab *rooms;

	/** @internal @brief The connections that are registered. */
	unsigned int count;

	/** @internal @brief The connections plus the upgrades that have a place. */
	unsigned int reserved;

	/** @internal @brief The most connections. */
	unsigned int max_connections;

	/** @internal @brief The size of the queue of the connections whose endpoint did not set one. */
	size_t max_queue;
	/** @internal @brief The policy of the queue of the connections whose endpoint did not set one. */
	HttpWebSocketOverflow overflow;

	/** @internal @brief The cerver is going away, no more connections are accepted. */
	bool stopping;

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
	/** @internal @brief The messages that a queue without a place did not take. */
	u64 n_dropped;
	/** @internal @brief The times that a queue was empty again after its backpressure (on_drain ()). */
	u64 n_drains;
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
 * @brief Returns the bytes that the queues of the connections of the registry have now.
 *
 * @param[in] registry the registry
 * @return the bytes
 */
CERVER_HIDDEN size_t http_web_socket_registry_queued (HttpWebSocketRegistry *registry);

/**
 * @internal
 * @brief Removes a connection from every one of its rooms, after its on_close () (A14); it can run more than once.
 *
 * @param[in,out] web_socket the connection
 */
CERVER_HIDDEN void http_web_socket_rooms_clear (HttpWebSocket *web_socket);

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

/**
 * @internal
 * @brief The X-macro table of the results of http_web_socket_client_response_parse (): the number, the name, the
 * string and the description of each one.
 *
 * @since 2.1b-21 (they were three defines)
 */
#define HTTP_WEB_SOCKET_CLIENT_RESPONSE_MAP(XX)																\
	XX(0,	COMPLETE,	Complete,	The response is complete)												\
	XX(1,	PARTIAL,	Partial,	It needs more bytes)													\
	XX(2,	INVALID,	Invalid,	The response is not valid)

/**
 * @internal
 * @brief The result of http_web_socket_client_response_parse (), one `HTTP_WEB_SOCKET_CLIENT_RESPONSE_<name>` for
 * each row of #HTTP_WEB_SOCKET_CLIENT_RESPONSE_MAP.
 */
typedef enum HttpWebSocketClientResponseResult {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_CLIENT_RESPONSE_##name = num,
	HTTP_WEB_SOCKET_CLIENT_RESPONSE_MAP (XX)
	#undef XX

} HttpWebSocketClientResponseResult;

/**
 * @internal
 * @brief Returns the string of a result of http_web_socket_client_response_parse ().
 *
 * @param[in] result the result
 * @return the string of its row in #HTTP_WEB_SOCKET_CLIENT_RESPONSE_MAP, or "Undefined" for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_client_response_result_to_string (
	const HttpWebSocketClientResponseResult result
);

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
CERVER_PUBLIC HttpWebSocketClientResponseResult http_web_socket_client_response_parse (
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

	/** @internal @brief The url is a wss:// one: the client connects with TLS. */
	bool secure;
	/** @internal @brief The TLS of the client, made with the default settings for a wss:// url without one. */
	struct _ClientTls *tls;

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
	/**
	 * @internal @brief The connection of now opened after the close of the client, which did not find it: its thread
	 * closes it (#mutex, with #web_socket).
	 */
	bool stopped_before_open;

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
	/** @internal @brief Releases the data when the client is deleted, or when another data takes its place. */
	void (*delete_data)(void *);

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
