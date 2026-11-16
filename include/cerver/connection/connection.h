/**
 * @file cerver/connection/connection.h
 * @brief The Connection: a socket of a client to a cerver, its defaults, its states and its setters.
 */

#ifndef _CERVER_CONNECTION_CONNECTION_H_
#define _CERVER_CONNECTION_CONNECTION_H_

#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/connection/stats.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

/**
 * @defgroup connection Connection
 * @brief A connection of a client: the socket that it uses to talk with a cerver, from the side of the cerver or
 * of the client.
 *
 * In a cerver, each accepted socket is a connection of a client; in the client side, a client creates its
 * connections with client_connection_create () and connects them to a cerver.
 */

/**
 * @defgroup connection_defaults Defaults
 * @ingroup connection
 * @brief The values that a new connection starts with.
 * @{
 */

/** @brief The size of the name of a connection, with its terminator. */
#define CONNECTION_NAME_SIZE						64

/** @brief The size of the ip of a connection, with its terminator. */
#define CONNECTION_IP_SIZE							64

/** @brief The name of a connection created with connection_create_empty (). */
#define CONNECTION_DEFAULT_NAME						"no-name"

/** @brief The default protocol of a connection. */
#define CONNECTION_DEFAULT_PROTOCOL					PROTOCOL_TCP

/** @brief A new connection uses IPv4 by default. */
#define CONNECTION_DEFAULT_USE_IPV6					false

/**
 * @brief The default max seconds of the exponential backoff between two rounds of connection attempts: when the
 * next wait would be longer, connection_connect () gives up.
 */
#define CONNECTION_DEFAULT_MAX_SLEEP				60

/** @brief The seconds of the first wait of the backoff, that doubles after each failed round. */
#define CONNECTION_FIRST_RETRY_SECONDS				2

/** @brief The default milliseconds that a connect () to each resolved address can take. */
#define CONNECTION_DEFAULT_CONNECT_TIMEOUT			4096

/** @brief How often, in milliseconds, the wait of a connect () checks if the connection was ended. */
#define CONNECTION_CONNECT_POLL_INTERVAL			128

/** @brief The default authentication attempts of a connection. */
#define CONNECTION_DEFAULT_MAX_AUTH_TRIES			2

/**
 * @brief The bad packets of a connection.
 *
 * @note cerver does not use this value: the limits are the ones of the cerver (cerver_set_max_bad_packets ()).
 * @deprecated It goes away in the 3.0.
 */
#define CONNECTION_DEFAULT_BAD_PACKETS				4

/** @brief The default flags that are passed to recv (). */
#define CONNECTION_DEFAULT_RECEIVE_FLAGS			0

/** @brief The default size in bytes of the buffer that the update thread of a connection receives into. */
#define CONNECTION_DEFAULT_RECEIVE_BUFFER_SIZE		4096

/** @brief The size of the token that an authenticated UDP connection sends after the packet in every datagram. */
#define CONNECTION_DATAGRAM_TOKEN_SIZE				16

/** @brief The default SO_RCVTIMEO in seconds of the socket of a connection in its update thread. */
#define CONNECTION_DEFAULT_UPDATE_TIMEOUT			2

/** @brief A new connection receives packets by default. */
#define CONNECTION_DEFAULT_RECEIVE_PACKETS			true

/** @brief A new connection sends its packets directly, without a send queue, by default. */
#define CONNECTION_DEFAULT_USE_SEND_QUEUE			false

/** @brief The default flags of the sends of the send queue. */
#define CONNECTION_DEFAULT_SEND_FLAGS				0

/** @brief A new connection does not reconnect when the cerver ends it by default. */
#define CONNECTION_DEFAULT_ATTEMPT_RECONNECT		false

/** @brief The default seconds between two reconnection attempts. */
#define CONNECTION_DEFAULT_RECONNECT_WAIT_TIME		30

/** @brief The deleted connections that are kept to be reused. */
#define CONNECTION_RECYCLE_LIMIT					FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _Socket;
struct _Cerver;
struct _CerverReport;
struct _Client;
struct _Connection;
struct _PacketsPerType;
struct _AdminCerver;
struct _Lobby;

/**
 * @defgroup connection_main Connection
 * @ingroup connection
 * @brief The struct of a connection, its states and its setters.
 *
 * The setters ignore a NULL connection.
 * @{
 */

/**
 * @brief The X-macro table of the states of a connection: the number, the name, the string and the description
 * of each one.
 */
#define CONNECTION_STATE_MAP(XX)														\
	XX(0,	NONE,			None, 			(Undefined))								\
	XX(1,	CONNECTING,		Connecting, 	(Performing connection))					\
	XX(2,	READY,			Ready, 			(Connected and ready))						\
	XX(3,	IDLE,			Idle,			(Available for work))						\
	XX(4,	WORKING,		Working,		(Currently working))						\
	XX(5,	BUSY,			Busy,			(Busy to work))								\
	XX(6,	UNAVAILABLE,	Unavailable,	(Not available for work))					\
	XX(7,	DISCONNECTING,	Disconnecting,	(In the process of ending the connection))	\
	XX(8,	DISCONNECTED,	Disconnected,	(Connection has been ended))

/**
 * @brief The state of a connection, one `CONNECTION_STATE_<name>` for each row of #CONNECTION_STATE_MAP.
 */
typedef enum ConnectionState {

	#define XX(num, name, string, description) CONNECTION_STATE_##name = num,
	CONNECTION_STATE_MAP (XX)
	#undef XX

} ConnectionState;

/**
 * @brief Returns the string of a state.
 *
 * @param[in] state the state
 * @return the string of its row in #CONNECTION_STATE_MAP, or the one of CONNECTION_STATE_NONE for a value out of
 * the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *connection_state_string (
	const ConnectionState state
);

/**
 * @brief Returns the description of a state.
 *
 * @param[in] state the state
 * @return the description of its row in #CONNECTION_STATE_MAP, between parentheses, or the one of
 * CONNECTION_STATE_NONE for a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *connection_state_description (
	const ConnectionState state
);

/**
 * @brief A connection of a client.
 *
 * The fields are read by the parts of cerver; an application sets them with the setters.
 */
struct _Connection {

	/** @brief The client that owns the connection. */
	struct _Client *client;

	/** @brief The name of the connection, that also names its update thread. */
	char name[CONNECTION_NAME_SIZE];

	/** @brief The socket of the connection, that the connection owns. */
	struct _Socket *socket;
	/** @brief The port of the peer. */
	u16 port;
	/** @brief The protocol of the connection. */
	Protocol protocol;
	/** @brief The connection uses IPv6. */
	bool use_ipv6;

	/** @brief The ip of the peer, as a string. */
	char ip[CONNECTION_IP_SIZE];
	/** @brief The address of the peer. */
	struct sockaddr_storage address;

	/** @brief The state of the connection, read with connection_get_state (). */
	ConnectionState state;
	/** @brief Protects #state. */
	pthread_mutex_t *state_mutex;

	/** @brief When the connection started. */
	time_t connected_timestamp;

	/** @brief The info of the cerver that the connection connected to, set with __atomic. */
	struct _CerverReport *cerver_report;

	/** @brief The max seconds of the backoff of connection_connect (). */
	u32 max_sleep;

	/** @brief The milliseconds that a connect () to each resolved address can take. */
	u32 connect_timeout;

	/**
	 * @brief The socket of the connection is connected (or accepted) and open.
	 *
	 * @threadsafety Read and written with __atomic: the end of a connection takes it, so two threads that end the
	 * same connection (a client teardown and its reconnection thread) never both end it.
	 */
	bool active;

	/** @brief The connection has a thread in connection_update (), set before the thread is created. */
	bool updating;

	/**
	 * @brief The end of the connection was asked for.
	 *
	 * The end belongs to the first thread that asks for it; #updating, #ending and #end_deferred are protected by
	 * #mutex.
	 */
	bool ending;
	/** @brief The end was deferred, the update thread completes it when its receive loop ends. */
	bool end_deferred;

	/**
	 * @brief The socket was shut down to wake up its receive, set with __atomic, so an UDP recv () of 0 bytes is
	 * told apart from an empty datagram.
	 */
	bool shut_down;

	/** @brief The authentication attempts that remain. */
	u8 auth_tries;
	/** @brief The bad packets that the connection sent. */
	u8 bad_packets;

	/** @brief The flags that are passed to recv (). */
	int receive_flags;
	/** @brief The size in bytes of the buffer that the update thread receives into. */
	u32 receive_packet_buffer_size;

	/** @brief The state of the receive of the packets of the connection. */
	ReceiveHandle receive_handle;

	/** @brief The update thread of the connection. */
	pthread_t update_thread_id;
	/** @brief The SO_RCVTIMEO in seconds of the socket in the update thread. */
	u32 update_timeout;

	/** @brief A place to store the response of a request, like the ones of client_connection_request_to_cerver (). */
	void *received_data;
	/** @brief The size of #received_data. */
	size_t received_data_size;
	/** @brief Releases #received_data when the connection is deleted or reset. */
	Action received_data_delete;

	/** @brief The connection receives packets in its update thread. */
	bool receive_packets;

	/** @brief A custom method that handles the received buffers instead of the cerver packets. */
	u8 (*custom_receive) (
		void *custom_data_ptr,
		char *buffer, const size_t buffer_size
	);
	/** @brief The arguments of the custom receive method. */
	void *custom_receive_args;
	/** @brief Releases the arguments of the custom receive method when the connection is deleted. */
	void (*custom_receive_args_delete)(void *);

	/** @brief The packets of the send queue are sent by a dedicated thread, joined when the connection is destroyed. */
	bool use_send_queue;
	/** @brief The flags of the sends of the send queue. */
	int send_flags;

	/** @brief The seconds that a send to a full socket waits for space, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT. */
	u32 send_timeout;
	/** @brief The send thread is running, set with __atomic. */
	bool send_thread_running;
	/** @brief The send thread. */
	pthread_t send_thread_id;
	/** @brief The queue of the packets to send. */
	JobQueue *send_queue;

	/** @brief The connection has authenticated to the cerver. */
	bool authenticated;
	/** @brief The credentials that the connection sends when the cerver asks for them. */
	void *auth_data;
	/** @brief The size of #auth_data. */
	size_t auth_data_size;
	/** @brief Releases #auth_data, NULL to keep it. */
	Action delete_auth_data;
	/** @brief The connection authenticates as an admin. */
	bool admin_auth;
	/** @brief The auth data is an encoded object. */
	bool auth_serialized;
	/** @brief The auth data is an encoded session token. */
	bool auth_token;
	/** @brief The packet of the authentication, made from the auth data. */
	struct _Packet *auth_packet;

	/**
	 * @brief The token of an authenticated UDP connection, it goes after the packet in every datagram.
	 *
	 * #datagram_token_set is set with __atomic after the token, so a sender reads it complete.
	 */
	u8 datagram_token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	/** @brief The connection has a datagram token. */
	bool datagram_token_set;

	/** @brief The route that a load balancer gave to the connection of a client, 0 without one. */
	u32 route_id;

	/** @brief The lobby of the player of the client, NULL outside a lobby; written with the lock of the game cerver. */
	struct _Lobby *lobby;

	/**
	 * @brief The update thread reconnects when the cerver ends the connection, waiting #reconnect_wait_time
	 * seconds between each attempt.
	 */
	bool attempt_reconnect;
	/** @brief The seconds between two reconnection attempts. */
	unsigned int reconnect_wait_time;
	/** @brief The attempts since the connection was lost. */
	unsigned int reconnect_attempts;

	/** @brief The stats of the connection. */
	ConnectionStats *stats;

	/** @brief Signaled to wake up the waits of the connection (its backoff, its reconnection). */
	pthread_cond_t *cond;
	/** @brief Protects the end of the connection and its waits. */
	pthread_mutex_t *mutex;

	/** @brief The storage that #stats points to, kept while the connection is recycled. */
	ConnectionStats stats_storage;

	/** @brief The storage that #state_mutex points to, initialized once. */
	pthread_mutex_t state_mutex_storage;
	/** @brief The storage that #cond points to, initialized once. */
	pthread_cond_t cond_storage;
	/** @brief The storage that #mutex points to, initialized once. */
	pthread_mutex_t mutex_storage;

	/** @brief The next deleted connection that waits to be reused. */
	struct _Connection *recycle_next;

};

/** @brief A connection, see struct _Connection. */
typedef struct _Connection Connection;

/**
 * @brief Allocates a connection with the default values, without a socket, stats or synchronization.
 *
 * It reuses a deleted connection when there is one.
 *
 * @return the new connection, NULL if it could not be allocated
 */
CERVER_PUBLIC Connection *connection_new (void);

/**
 * @brief Deletes a connection: it ends it if it is active, and releases its socket, its report, its received
 * data, its custom receive arguments, its auth data and its send queue.
 *
 * The connection is kept to be reused, up to #CONNECTION_RECYCLE_LIMIT.
 *
 * @param[in] connection_ptr the connection, it can be NULL
 */
CERVER_PUBLIC void connection_delete (void *connection_ptr);

/**
 * @brief Creates a connection with the default name, an empty socket, its state mutex and its stats.
 *
 * @return the new connection, released with connection_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Connection *connection_create_empty (void);

/**
 * @brief Creates an empty connection with its synchronization values, ready to connect to a cerver.
 *
 * @return the new connection, released with connection_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Connection *connection_create_complete (void);

/**
 * @brief Sets the condition and the mutex that a connection uses to connect to a cerver, the ones that it has
 * embedded.
 *
 * @param[in,out] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_set_synchronization (Connection *connection);

/**
 * @brief Creates the connection of an accepted socket, with the ip and the port of its address.
 *
 * @param[in] sock_fd the accepted socket, the connection owns it from now on
 * @param[in] address the address of the peer
 * @param[in] protocol the protocol
 * @return the new connection, released with connection_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Connection *connection_create (
	const i32 sock_fd, const struct sockaddr_storage *address,
	const Protocol protocol
);

/**
 * @brief Compares two connections by the fds of their sockets.
 *
 * @param[in] a the first connection
 * @param[in] b the second connection
 * @return -1, 0 or 1 as the fd of a is smaller, equal or bigger than the one of b; -1 when a connection or its
 * socket is NULL
 */
CERVER_PUBLIC int connection_comparator (
	const void *a, const void *b
);

/**
 * @brief Sets the name of the connection, replacing the one it had.
 *
 * @param[in,out] connection the connection
 * @param[in] name the name, truncated to CONNECTION_NAME_SIZE - 1 characters; NULL keeps the current one
 */
CERVER_PUBLIC void connection_set_name (
	Connection *connection, const char *name
);

/**
 * @brief Sets the ip and the port of the connection from its address (from where the client connects).
 *
 * @param[in,out] connection the connection
 */
CERVER_PUBLIC void connection_get_values (Connection *connection);

/**
 * @brief Sets the network values of a connection that will connect to a cerver.
 *
 * It also marks the connection as not active.
 *
 * @param[in,out] connection the connection
 * @param[in] ip_address the ip or the name of the cerver, truncated to CONNECTION_IP_SIZE - 1 characters; NULL
 * keeps the current one
 * @param[in] port the port
 * @param[in] protocol the protocol
 * @param[in] use_ipv6 true to use IPv6
 */
CERVER_PUBLIC void connection_set_values (
	Connection *connection,
	const char *ip_address, u16 port, Protocol protocol, bool use_ipv6
);

/**
 * @brief Returns the state of the connection.
 *
 * @param[in] connection the connection
 * @return the state, CONNECTION_STATE_NONE for a NULL connection or one without its state mutex
 * @threadsafety Safe from any thread, it reads the state with its mutex.
 */
CERVER_PUBLIC ConnectionState connection_get_state (
	Connection *connection
);

/**
 * @brief Sets the state of the connection, with its mutex.
 *
 * @param[in,out] connection the connection, one without its state mutex is not changed
 * @param[in] state the state
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_set_state (
	Connection *connection, const ConnectionState state
);

/**
 * @brief Sets the max seconds of the exponential backoff of connection_connect ().
 *
 * The waits between the rounds of attempts start at #CONNECTION_FIRST_RETRY_SECONDS and double; when the next one
 * would be longer than max_sleep, the connection gives up.
 *
 * @param[in,out] connection the connection
 * @param[in] max_sleep the seconds, the default is #CONNECTION_DEFAULT_MAX_SLEEP
 */
CERVER_EXPORT void connection_set_max_sleep (
	Connection *connection, u32 max_sleep
);

/**
 * @brief Sets how long a connect () to each address of the cerver can take; after it, the next resolved address
 * is tried.
 *
 * @param[in,out] connection the connection
 * @param[in] timeout the milliseconds, the default is #CONNECTION_DEFAULT_CONNECT_TIMEOUT
 */
CERVER_EXPORT void connection_set_connect_timeout (
	Connection *connection, u32 timeout
);

/**
 * @brief Sets whether the connection receives packets: when it does, a thread handles its incoming packets.
 *
 * @param[in,out] connection the connection
 * @param[in] receive true to receive, the default is #CONNECTION_DEFAULT_RECEIVE_PACKETS
 */
CERVER_EXPORT void connection_set_receive (
	Connection *connection, bool receive
);

/**
 * @brief Sets the flags that are passed to recv () when the connection receives its packets.
 *
 * @param[in,out] connection the connection
 * @param[in] flags the flags, the default is #CONNECTION_DEFAULT_RECEIVE_FLAGS
 */
CERVER_EXPORT void connection_set_receive_flags (
	Connection *connection, int flags
);

/**
 * @brief Sets the size of the buffer that the update thread of the connection receives into.
 *
 * @param[in,out] connection the connection
 * @param[in] size the size in bytes, 0 keeps the current one; the default is
 * #CONNECTION_DEFAULT_RECEIVE_BUFFER_SIZE
 */
CERVER_EXPORT void connection_set_receive_buffer_size (
	Connection *connection, u32 size
);

/**
 * @brief Sets the received data of the connection: a place to store the response of a request, like the ones of
 * client_connection_request_to_cerver ().
 *
 * @param[in,out] connection the connection
 * @param[in] data the data
 * @param[in] data_size the size of the data
 * @param[in] data_delete releases the data when the connection is deleted or reset, NULL to keep it
 * @note Different data releases the data that was already set with its own delete method; the same data again is
 * kept.
 */
CERVER_EXPORT void connection_set_received_data (
	Connection *connection,
	void *data, size_t data_size, Action data_delete
);

/**
 * @brief Sets the SO_RCVTIMEO of the socket of the connection: how long a recv () blocks waiting for new data.
 *
 * @param[in,out] connection the connection
 * @param[in] timeout the seconds, the default is #CONNECTION_DEFAULT_UPDATE_TIMEOUT
 * @note It only has effect in connection_update (), when the update thread starts.
 */
CERVER_EXPORT void connection_set_update_timeout (
	Connection *connection, u32 timeout
);

/**
 * @brief Sets how long a send waits for space in a full non blocking socket before it fails.
 *
 * @param[in,out] connection the connection
 * @param[in] timeout the seconds, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT
 */
CERVER_EXPORT void connection_set_send_timeout (
	Connection *connection, u32 timeout
);

/**
 * @brief What a custom receive method gets: the client, the connection and the arguments of the method.
 */
typedef struct ConnectionCustomReceiveData {

	/** @brief The client of the connection. */
	struct _Client *client;
	/** @brief The connection. */
	struct _Connection *connection;
	/** @brief The arguments that were set with the method. */
	void *args;

} ConnectionCustomReceiveData;

/**
 * @brief Sets a custom method that handles the buffers that the connection receives, instead of the cerver
 * packets.
 *
 * The method gets a ConnectionCustomReceiveData with the client, the connection and the arguments; setting a
 * method also makes the connection receive packets.
 *
 * @param[in,out] connection the connection
 * @param[in] custom_receive the method, it returns 0 on success and 1 on error; NULL to use the cerver packets
 * @param[in] args the arguments of the method
 * @param[in] args_delete releases the arguments when the connection is deleted, NULL to keep them
 */
CERVER_PUBLIC void connection_set_custom_receive (
	Connection *connection,
	u8 (*custom_receive) (
		void *custom_data_ptr,
		char *buffer, const size_t buffer_size
	),
	void *args, void (*args_delete)(void *)
);

/**
 * @brief Enables the send queue of the connection: connection_send_packet () queues its packets, and a dedicated
 * thread, created when the connection starts and joined when it is destroyed, sends them.
 *
 * @param[in,out] connection the connection, one that already has a send queue keeps it and its flags
 * @param[in] flags the flags of the sends
 */
CERVER_PUBLIC void connection_set_send_queue (
	Connection *connection, int flags
);

/**
 * @brief Enables the reconnection of the connection when the cerver ends it.
 *
 * The update thread waits wait_time seconds before each attempt, until it connects again or the client ends the
 * connection.
 *
 * @param[in,out] connection the connection
 * @param[in] wait_time the seconds between two attempts
 */
CERVER_PUBLIC void connection_set_attempt_reconnect (
	Connection *connection, unsigned int wait_time
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
