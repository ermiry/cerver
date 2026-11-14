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

#define CONNECTION_NAME_SIZE						64
#define CONNECTION_IP_SIZE							64

#define CONNECTION_DEFAULT_NAME						"no-name"

#define CONNECTION_DEFAULT_PROTOCOL					PROTOCOL_TCP
#define CONNECTION_DEFAULT_USE_IPV6					false

// used for connection with exponential backoff (secs)
#define CONNECTION_DEFAULT_MAX_SLEEP				60
#define CONNECTION_FIRST_RETRY_SECONDS				2

// how long a connect () to each resolved address can take (ms),
// and how often the wait checks if the connection was ended
#define CONNECTION_DEFAULT_CONNECT_TIMEOUT			4096
#define CONNECTION_CONNECT_POLL_INTERVAL			128

#define CONNECTION_DEFAULT_MAX_AUTH_TRIES			2
#define CONNECTION_DEFAULT_BAD_PACKETS				4

#define CONNECTION_DEFAULT_RECEIVE_FLAGS			0
#define CONNECTION_DEFAULT_RECEIVE_BUFFER_SIZE		4096

// an authenticated UDP connection sends this token after the packet in every datagram
#define CONNECTION_DATAGRAM_TOKEN_SIZE				16

#define CONNECTION_DEFAULT_UPDATE_TIMEOUT			2

#define CONNECTION_DEFAULT_RECEIVE_PACKETS			true

#define CONNECTION_DEFAULT_USE_SEND_QUEUE			false
#define CONNECTION_DEFAULT_SEND_FLAGS				0

#define CONNECTION_DEFAULT_ATTEMPT_RECONNECT		false
#define CONNECTION_DEFAULT_RECONNECT_WAIT_TIME		30

// the deleted connections that are kept to be reused
#define CONNECTION_RECYCLE_LIMIT					FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

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

typedef enum ConnectionState {

	#define XX(num, name, string, description) CONNECTION_STATE_##name = num,
	CONNECTION_STATE_MAP (XX)
	#undef XX

} ConnectionState;

CERVER_PUBLIC const char *connection_state_string (
	const ConnectionState state
);

CERVER_PUBLIC const char *connection_state_description (
	const ConnectionState state
);

// a connection from a client
struct _Connection {

	// the client that owns the connection
	struct _Client *client;

	char name[CONNECTION_NAME_SIZE];

	struct _Socket *socket;
	u16 port;
	Protocol protocol;
	bool use_ipv6;

	char ip[CONNECTION_IP_SIZE];
	struct sockaddr_storage address;

	ConnectionState state;
	pthread_mutex_t *state_mutex;

	time_t connected_timestamp;             // when the connection started

	struct _CerverReport *cerver_report;    // info about the cerver we are connecting to

	u32 max_sleep;

	// how long a connect () to each resolved address can take, in milliseconds
	u32 connect_timeout;

	bool active;

	// the connection has a thread in connection_update (), set before the thread is created
	bool updating;

	// the end of the connection belongs to the first thread that asks for it,
	// a deferred end is completed by the update thread when its receive loop ends
	// the three values are protected by the connection's mutex
	bool ending;
	bool end_deferred;

	// the socket was shut down to wake up its receive, set with __atomic,
	// so an UDP recv () of 0 bytes is told apart from an empty datagram
	bool shut_down;

	u8 auth_tries;                          // remaining attempts to authenticate
	u8 bad_packets;                         // number of bad packets before being disconnected

	int receive_flags;                      // the flags that are passed to recv ()
	u32 receive_packet_buffer_size;         // read packets into a buffer of this size in client_receive ()
	
	ReceiveHandle receive_handle;

	pthread_t update_thread_id;
	u32 update_timeout;

	// a place to safely store the request response
	// like when using client_connection_request_to_cerver ()
	void *received_data;
	size_t received_data_size;
	Action received_data_delete;

	bool receive_packets;                   // set if the connection will receive packets or not (default true)
	
	// custom receive method to handle incomming packets in the connection
	u8 (*custom_receive) (
		void *custom_data_ptr,
		char *buffer, const size_t buffer_size
	);
	void *custom_receive_args;              		// arguments to be passed to the custom receive method
	void (*custom_receive_args_delete)(void *);		// method to delete the arguments when the connection gets deleted

	// the packets of the send queue are sent by a dedicated thread
	// that is joined when the connection is destroyed
	bool use_send_queue;
	int send_flags;

	// the secs that a send to a full socket waits for space, 0 uses CERVER_DEFAULT_SEND_TIMEOUT
	u32 send_timeout;
	bool send_thread_running;
	pthread_t send_thread_id;
	JobQueue *send_queue;

	bool authenticated;                     // the connection has been authenticated to the cerver
	void *auth_data;                        // maybe auth credentials
	size_t auth_data_size;
	Action delete_auth_data;                // destroys the auth data when the connection ends
	bool admin_auth;                        // attempt to connect as an admin
	bool auth_serialized;                   // the auth data is an encoded object
	bool auth_token;                        // the auth data is an encoded session token
	struct _Packet *auth_packet;

	// the token of an authenticated UDP connection, it goes after the packet in every datagram,
	// the flag is set with __atomic after the token, so a sender reads it complete
	u8 datagram_token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	bool datagram_token_set;

	// the route that a load balancer gave to the connection of a client, 0 without one
	u32 route_id;

	// the lobby of the player of the client, NULL outside a lobby,
	// written with the lock of the game cerver (lobby.h)
	struct _Lobby *lobby;

	// the update thread reconnects when the cerver ends the connection,
	// it waits reconnect_wait_time secs between each attempt
	bool attempt_reconnect;
	unsigned int reconnect_wait_time;
	unsigned int reconnect_attempts;		// attempts since the connection was lost

	ConnectionStats *stats;

	pthread_cond_t *cond;
	pthread_mutex_t *mutex;

	// the pointers above point to these values,
	// they are initialized once and kept while the connection is recycled
	ConnectionStats stats_storage;

	pthread_mutex_t state_mutex_storage;
	pthread_cond_t cond_storage;
	pthread_mutex_t mutex_storage;

	// the next deleted connection that waits to be reused
	struct _Connection *recycle_next;

};

typedef struct _Connection Connection;

CERVER_PUBLIC Connection *connection_new (void);

CERVER_PUBLIC void connection_delete (void *connection_ptr);

CERVER_PUBLIC Connection *connection_create_empty (void);

// creates an empty connection with its synchronization values,
// ready to connect to a cerver
CERVER_PUBLIC Connection *connection_create_complete (void);

// sets the cond and the mutex that a connection uses to connect to a cerver
CERVER_PRIVATE void connection_set_synchronization (Connection *connection);

// creates a new client connection with the specified values
CERVER_PUBLIC Connection *connection_create (
	const i32 sock_fd, const struct sockaddr_storage *address,
	const Protocol protocol
);

// compare two connections by their socket fds
CERVER_PUBLIC int connection_comparator (
	const void *a, const void *b
);

// sets the connection's name, if it had a name before, it will be replaced
CERVER_PUBLIC void connection_set_name (
	Connection *connection, const char *name
);

// get from where the client is connecting
CERVER_PUBLIC void connection_get_values (Connection *connection);

// sets the connection's newtwork values
CERVER_PUBLIC void connection_set_values (
	Connection *connection,
	const char *ip_address, u16 port, Protocol protocol, bool use_ipv6
);

CERVER_PUBLIC ConnectionState connection_get_state (
	Connection *connection
);

CERVER_PRIVATE void connection_set_state (
	Connection *connection, const ConnectionState state
);

// sets the connection max sleep (wait time) to try to connect to the cerver
CERVER_EXPORT void connection_set_max_sleep (
	Connection *connection, u32 max_sleep
);

// sets how long a connect () to each address of the cerver can take, in milliseconds,
// after it the next resolved address is tried (default CONNECTION_DEFAULT_CONNECT_TIMEOUT)
CERVER_EXPORT void connection_set_connect_timeout (
	Connection *connection, u32 timeout
);

// sets if the connection will receive packets or not (default true)
// if true, a new thread is created that handled incoming packets
CERVER_EXPORT void connection_set_receive (
	Connection *connection, bool receive
);

// sets the flags to be passed to recv () when handling packets
CERVER_EXPORT void connection_set_receive_flags (
	Connection *connection, int flags
);

// read packets into a buffer of this size in client_receive ()
// by default the value RECEIVE_PACKET_BUFFER_SIZE is used
CERVER_EXPORT void connection_set_receive_buffer_size (
	Connection *connection, u32 size
);

// sets the connection received data
// 01/01/2020 - a place to safely store the request response, like when using client_connection_request_to_cerver ()
CERVER_EXPORT void connection_set_received_data (
	Connection *connection,
	void *data, size_t data_size, Action data_delete
);

// sets the timeout (in secs) the connection's socket will have
// this refers to the time the socket will block waiting for new data to araive
// note that this only has effect in connection_update ()
CERVER_EXPORT void connection_set_update_timeout (
	Connection *connection, u32 timeout
);

// sets how many secs a send waits for space in a full non blocking socket
// before it fails, 0 uses CERVER_DEFAULT_SEND_TIMEOUT
CERVER_EXPORT void connection_set_send_timeout (
	Connection *connection, u32 timeout
);

typedef struct ConnectionCustomReceiveData {

	struct _Client *client;
	struct _Connection *connection;
	void *args;

} ConnectionCustomReceiveData;

// sets a custom receive method to handle incomming packets in the connection
// a reference to the client and connection will be passed to the action as a ConnectionCustomReceiveData structure
// alongside the arguments passed to this method
// the method must return 0 on success & 1 on error
CERVER_PUBLIC void connection_set_custom_receive (
	Connection *connection, 
	u8 (*custom_receive) (
		void *custom_data_ptr,
		char *buffer, const size_t buffer_size
	),
	void *args, void (*args_delete)(void *)
);

// enables the ability to send packets using the connection's queue
// a dedicated thread will be created to send queued packets
// when the connection starts, and it is joined when it is destroyed
CERVER_PUBLIC void connection_set_send_queue (
	Connection *connection, int flags
);

// sets the ability to attempt a reconnection when the cerver ends the connection
// the update thread waits wait_time secs before each attempt,
// until it connects again or the connection is ended by the client
CERVER_PUBLIC void connection_set_attempt_reconnect (
	Connection *connection, unsigned int wait_time
);

#ifdef __cplusplus
}
#endif

#endif
