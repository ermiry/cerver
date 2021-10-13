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

#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#define CONNECTION_NAME_SIZE						64
#define CONNECTION_IP_SIZE							64

#define CONNECTION_DEFAULT_NAME						"no-name"

#define CONNECTION_DEFAULT_PROTOCOL					PROTOCOL_TCP
#define CONNECTION_DEFAULT_USE_IPV6					false

// used for connection with exponential backoff (secs)
#define CONNECTION_DEFAULT_MAX_SLEEP				60

#define CONNECTION_DEFAULT_MAX_AUTH_TRIES			2
#define CONNECTION_DEFAULT_BAD_PACKETS				4

#define CONNECTION_DEFAULT_RECEIVE_FLAGS			0
#define CONNECTION_DEFAULT_RECEIVE_BUFFER_SIZE		4096

#define CONNECTION_DEFAULT_UPDATE_TIMEOUT			2

#define CONNECTION_DEFAULT_RECEIVE_PACKETS			true

#define CONNECTION_DEFAULT_USE_SEND_QUEUE			false
#define CONNECTION_DEFAULT_SEND_FLAGS				0

#define CONNECTION_DEFAULT_ATTEMPT_RECONNECT		false
#define CONNECTION_DEFAULT_RECONNECT_WAIT_TIME		30

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

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

};

typedef struct _ConnectionStats ConnectionStats;

CERVER_PUBLIC ConnectionStats *connection_stats_new (void);

CERVER_PUBLIC void connection_stats_print (
	struct _Connection *connection
);

// a connection from a client
struct _Connection {

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

	pthread_t connection_thread_id;
	time_t connected_timestamp;             // when the connection started

	struct _CerverReport *cerver_report;    // info about the cerver we are connecting to

	u32 max_sleep;
	bool active;
	bool updating;

	u8 auth_tries;                          // remaining attempts to authenticate
	u8 bad_packets;                         // number of bad packets before being disconnected

	int receive_flags;						// flags passed to recv ()
	u32 receive_packet_buffer_size;         // read packets into a buffer of this size in client_receive ()
	
	ReceiveHandle receive_handle;

	pthread_t request_thread_id;

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

	bool use_send_queue;
	int send_flags;
	pthread_t send_thread_id;
	JobQueue *send_queue;

	bool authenticated;                     // the connection has been authenticated to the cerver
	void *auth_data;                        // maybe auth credentials
	size_t auth_data_size;
	Action delete_auth_data;                // destroys the auth data when the connection ends
	bool admin_auth;                        // attempt to connect as an admin
	struct _Packet *auth_packet;

	bool attempt_reconnect;
	unsigned int reconnect_wait_time;
	pthread_t reconnect_thread_id;

	ConnectionStats *stats;

	pthread_cond_t *cond;
	pthread_mutex_t *mutex;

};

typedef struct _Connection Connection;

CERVER_PUBLIC Connection *connection_new (void);

CERVER_PUBLIC void connection_delete (void *connection_ptr);

CERVER_PUBLIC Connection *connection_create_empty (void);

CERVER_PUBLIC Connection *connection_create_complete (void);

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
// a place to safely store the request response,
// like when using client_connection_request_to_cerver ()
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

typedef struct ConnectionCustomReceiveData {

	struct _Client *client;
	struct _Connection *connection;
	void *args;

} ConnectionCustomReceiveData;

// sets a custom receive method to handle incomming packets in the connection
// a reference to the client and connection will be passed to the action
// as a ConnectionCustomReceiveData structure
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
CERVER_PUBLIC void connection_set_send_queue (
	Connection *connection, int flags
);

// sets the connection auth data to send whenever the cerver requires authentication
// and a method to destroy it once the connection has ended,
// if delete_auth_data is NULL, the auth data won't be deleted
CERVER_PUBLIC void connection_set_auth_data (
	Connection *connection,
	void *auth_data, size_t auth_data_size, Action delete_auth_data,
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

// sets the ability to attempt a reconnection
// if the connection has been stopped
// ability to set the default time (secs) to wait before connecting
CERVER_PUBLIC void connection_set_attempt_reconnect (
	Connection *connection, unsigned int wait_time
);

// sets up the new connection values
CERVER_PRIVATE unsigned int connection_init (Connection *connection);

// connects to the specified address (ip and port)
// sets connection's state based on result
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_connect (Connection *connection);

// creates a detachable thread to attempt a reconnection
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_reconnect (Connection *connection);

// starts a connection
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_start (Connection *connection);

// resets connection values
CERVER_PRIVATE void connection_reset (Connection *connection);

// ends a client connection
CERVER_PRIVATE void connection_end (Connection *connection);

CERVER_PRIVATE void connection_drop (
	struct _Cerver *cerver, Connection *connection
);

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

CERVER_PUBLIC void connection_send_packet (
	Connection *connection, Packet *packet
);

#ifdef __cplusplus
}
#endif

#endif