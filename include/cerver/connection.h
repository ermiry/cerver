#ifndef _CERVER_CONNECTION_H_
#define _CERVER_CONNECTION_H_

#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/socket.h"

#include "cerver/collections/freelist.h"

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

struct _ConnectionStats {

	time_t threshold_time;                  // every time we want to reset the connection's stats

	u64 n_receives_done;                    // n calls to recv ()

	u64 total_bytes_received;               // total amount of bytes received from this connection
	u64 total_bytes_sent;                   // total amount of bytes that have been sent to the connection

	u64 n_packets_received;                 // total number of packets received from this connection (packet header + data)
	u64 n_packets_sent;                     // total number of packets sent to this connection

	// point to the storage below, so the stats of a connection are not extra allocations
	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

	struct _PacketsPerType received_packets_storage;
	struct _PacketsPerType sent_packets_storage;

};

typedef struct _ConnectionStats ConnectionStats;

CERVER_PUBLIC ConnectionStats *connection_stats_new (void);

CERVER_PUBLIC void connection_stats_print (
	struct _Connection *connection
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

// pushes a packet to the connection's send queue,
// the send thread sends it and deletes it
CERVER_PUBLIC void connection_send_packet (
	Connection *connection, struct _Packet *packet
);

// sets the connection auth data to send whenever the cerver requires authentication
// and a method to destroy it once the connection has ended,
// if delete_auth_data is NULL, the auth data won't be deleted
CERVER_PUBLIC void connection_set_auth_data (
	Connection *connection,
	void *auth_data, size_t auth_data_size, Action delete_auth_data,
	bool admin_auth
);

// sets the credentials as an object of the auth schema of the cerver (cerver_set_auth_schema ()),
// the object is encoded now, so it can be released after this
// returns 0 on success, 1 if the object can not be encoded
CERVER_PUBLIC u8 connection_set_auth_object (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	bool admin_auth
);

// sets the session token that a cerver with sessions gave to the client,
// to authenticate the connection with it instead of the credentials
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 connection_set_auth_token (
	Connection *connection, const char *token,
	bool admin_auth
);

// removes the connection auth data using the connection's delete_auth_data method
// if not such method, the data won't be deleted
// the connection's auth data & delete method will be equal to NULL
CERVER_PUBLIC void connection_remove_auth_data (
	Connection *connection
);

// generates the connection auth packet to be send to the server
// this is also generated automatically whenever the cerver ask for authentication
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 connection_generate_auth_packet (
	Connection *connection
);

// sets the token that an authenticated UDP connection sends after the packet in every datagram
CERVER_PRIVATE void connection_set_datagram_token (
	Connection *connection, const u8 *token
);

// copies the datagram token of the connection into token
// returns true if the connection has one
CERVER_PRIVATE bool connection_get_datagram_token (
	const Connection *connection, u8 *token
);

// the connection sends its datagrams without a token again
CERVER_PRIVATE void connection_clear_datagram_token (Connection *connection);

// returns true if the connection sends a token in its datagrams (it authenticated)
CERVER_PRIVATE bool connection_has_datagram_token (const Connection *connection);

// sets the ability to attempt a reconnection when the cerver ends the connection
// the update thread waits wait_time secs before each attempt,
// until it connects again or the connection is ended by the client
CERVER_PUBLIC void connection_set_attempt_reconnect (
	Connection *connection, unsigned int wait_time
);

// sets up the new connection values
CERVER_PRIVATE u8 connection_init (Connection *connection);

// starts a connection -> connects to the specified ip and port
// sets connection's state based on result
// returns 0 on success, 1 on error
CERVER_PRIVATE int connection_connect (Connection *connection);

// asks the update thread of a connection with attempt reconnect
// to close its socket and to connect again
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_reconnect (Connection *connection);

// starts the threads of a connection that is connected,
// like the send queue thread
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_start (Connection *connection);

// waits until the send thread ends, the connection must not be active
CERVER_PRIVATE void connection_stop_send (Connection *connection);

// resets connection values to be able to connect it again,
// the connection must not have an update thread
CERVER_PRIVATE void connection_reset (Connection *connection);

// ends a client connection
CERVER_PRIVATE void connection_end (Connection *connection);

CERVER_PRIVATE void connection_drop (
	struct _Cerver *cerver, Connection *connection
);

// asks the cerver to drop the connection, it can be called from any packet handler:
// the socket is shut down (the packets that were already sent still arrive),
// and the receive loop ends and removes the connection when the handler returns,
// like when the client closes it, so the connection must not be used after this call
// returns 0 on success, 1 on error
CERVER_EXPORT u8 connection_drop_request (Connection *connection);

// gets the connection from the on hold connections map in cerver
CERVER_PRIVATE Connection *connection_get_by_sock_fd_from_on_hold (
	struct _Cerver *cerver, const i32 sock_fd
);

// gets the connection from the client by its sock fd
CERVER_PRIVATE Connection *connection_get_by_sock_fd_from_client (
	struct _Client *client, const i32 sock_fd
);

// gets the connection from the admin cerver by its sock fd
CERVER_PRIVATE Connection *connection_get_by_sock_fd_from_admin (
	struct _AdminCerver *admin_cerver, const i32 sock_fd
);

// checks if the connection belongs to the client
CERVER_PRIVATE bool connection_check_owner (
	struct _Client *client, Connection *connection
);

// registers a new connection to a client without adding it to the cerver poll
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_register_to_client (
	struct _Client *client, Connection *connection
);

// registers the client connection to the cerver's strcutures (like maps)
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_register_to_cerver (
	struct _Cerver *cerver,
	struct _Client *client, Connection *connection
);

// unregister the client connection from the cerver's structures (like maps)
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_unregister_from_cerver (
	struct _Cerver *cerver, Connection *connection
);

// wrapper function for easy access
// registers a client connection to the cerver poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_register_to_cerver_poll (
	struct _Cerver *cerver, Connection *connection
);

// wrapper function for easy access
// unregisters a client connection from the cerver poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_unregister_from_cerver_poll (
	struct _Cerver *cerver, Connection *connection
);

// first adds the client connection to the cerver's poll array, and upon success,
// adds the connection to the cerver's structures
// this method is equivalent to call connection_register_to_cerver_poll () & connection_register_to_cerver
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_add_to_cerver (
	struct _Cerver *cerver,
	struct _Client *client,
	Connection *connection
);

// removes the connection's sock fd from the cerver's poll array and then removes the connection
// from the cerver's structures
// this method is equivalent to call connection_unregister_from_cerver_poll () & connection_unregister_from_cerver ()
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_remove_from_cerver (
	struct _Cerver *cerver, Connection *connection
);

// starts listening and receiving data in the connection sock
// the connection's updating value must be set before creating the thread
CERVER_PRIVATE void *connection_update (void *ptr);

// returns true if the current thread is the update thread of the connection,
// like a packet handler with direct handle that runs in it
CERVER_PRIVATE bool connection_is_update_thread (
	const Connection *connection
);

// wakes up the update thread of the connection that is blocked in recv (),
// the socket is shut down but it is only closed by connection_end (),
// so its fd can not be reused while the update thread uses it
// the connection's mutex must be held
CERVER_PRIVATE void connection_wake_update (Connection *connection);

// wakes up the connection that waits in the backoff of connection_connect (),
// it stops waiting if its client was torn down
CERVER_PRIVATE void connection_wake_connect (Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
