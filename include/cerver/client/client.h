#ifndef _CERVER_CLIENT_CLIENT_H_
#define _CERVER_CLIENT_CLIENT_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"
#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

#define CLIENT_NAME_SIZE							64
#define CLIENT_FILES_MAX_PATHS       				32

#define CLIENT_DEFAULT_NAME							"no-name"

#define CLIENT_DEFAULT_MAX_RECEIVED_PACKET_SIZE		MAX_UDP_PACKET_SIZE
#define CLIENT_DEFAULT_MAX_BAD_PACKETS				16

// the pieces in which client_connection_get_next_packet () reads a packet
#define CLIENT_NEXT_PACKET_BUFFER_SIZE				(1 << 12)

// the deleted clients that are kept to be reused, a cerver creates one per connection
#define CLIENT_RECYCLE_LIMIT						FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _PacketsPerType;
struct _Handler;

struct FileHeader;

struct _ClientEvent;
struct _ClientError;

#pragma region stats

struct _ClientStats {

	time_t threshold_time;			// every time we want to reset the client's stats

	u64 n_receives_done;			// n calls to recv ()

	u64 total_bytes_received;		// total amount of bytes received from this client
	u64 total_bytes_sent;			// total amount of bytes that have been sent to the client (all of its connections)

	u64 n_packets_received;			// total number of packets received from this client (packet header + data)
	u64 n_packets_sent;				// total number of packets sent to this client (all connections)

	// point to the storage below, so the stats of a client are not extra allocations
	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

	struct _PacketsPerType received_packets_storage;
	struct _PacketsPerType sent_packets_storage;

};

typedef struct _ClientStats ClientStats;

CERVER_PUBLIC void client_stats_print (
	struct _Client *client
);

struct _ClientFileStats {

	u64 n_files_requests;				// n requests to get a file
	u64 n_success_files_requests;		// fulfilled requests
	u64 n_bad_files_requests;			// bad requests
	u64 n_files_sent;					// n files sent
	u64 n_bad_files_sent;				// n files that failed to send
	u64 n_bytes_sent;					// total bytes sent

	u64 n_files_upload_requests;		// n requests to upload a file
	u64 n_success_files_uploaded;		// n files received
	u64 n_bad_files_upload_requests;	// bad requests to upload files
	u64 n_bad_files_received;			// files that failed to be received
	u64 n_bytes_received;				// total bytes received

};

typedef struct _ClientFileStats ClientFileStats;

CERVER_PUBLIC void client_file_stats_print (
	struct _Client *client
);

#pragma endregion

#pragma region main

#define CLIENT_MAX_EVENTS				32
#define CLIENT_MAX_ERRORS				32

#define CLIENT_CONNECTIONS_STATUS_MAP(XX)									\
	XX(0,	NONE,		None, 		Undefined)								\
	XX(1,	ERROR,		Error, 		Failed to remove connection)			\
	XX(2,	ONE,		One,		At least one active connection)			\
	XX(3,	DROPPED,	Dropped,	Removed due to no connections left)

typedef enum ClientConnectionsStatus {

	#define XX(num, name, string, description) CLIENT_CONNECTIONS_STATUS_##name = num,
	CLIENT_CONNECTIONS_STATUS_MAP (XX)
	#undef XX

} ClientConnectionsStatus;

CERVER_PUBLIC const char *client_connections_status_to_string (
	const ClientConnectionsStatus status
);

CERVER_PUBLIC const char *client_connections_status_description (
	const ClientConnectionsStatus status
);

// anyone that connects to the cerver
struct _Client {

	// generated using connection values
	u64 id;
	time_t connected_timestamp;

	char name[CLIENT_NAME_SIZE];

	DoubleList *connections;

	// multiple connections can be associated with the same client using the same session id
	// it is set and read with the session lock, use client_get_session_id ()
	String *session_id;
	pthread_mutex_t session_lock;

	time_t last_activity;	// the last time the client sent / receive data

	bool drop_client;		// client failed to authenticate

	void *data;
	Action delete_data;

	// used when the client connects to another server
	bool running;
	time_t time_started;
	u64 uptime;

	// custom packet handlers
	unsigned int num_handlers_alive;       // handlers currently alive
	unsigned int num_handlers_working;     // handlers currently working
	pthread_mutex_t *handlers_lock;
	struct _Handler *app_packet_handler;
	struct _Handler *app_error_packet_handler;
	struct _Handler *custom_packet_handler;

	size_t max_received_packet_size;

	bool check_packets;              // enable / disbale packet checking
	u8 max_bad_packets;              // bad packets of a connection before it is ended, 0 disables it

	// general client lock
	// it also protects the connections list against the update threads that remove their connections
	pthread_mutex_t *lock;

	// the connections' update threads that are alive, client_teardown () waits for them
	unsigned int n_update_threads;
	pthread_cond_t updates_cond;

	// the async connect and request threads that use the client,
	// client_teardown () waits for them before it ends the connections
	unsigned int n_async_threads;

	// set by client_teardown (), it cancels the connect backoff of its connections
	bool teardown_requested;

	// the events and errors actions that run in their own threads,
	// client_teardown () waits for them before the client is released
	ThreadGroup event_threads;

	struct _ClientEvent *events[CLIENT_MAX_EVENTS];
	struct _ClientError *errors[CLIENT_MAX_ERRORS];

	// files
	unsigned int n_paths;
	String *paths[CLIENT_FILES_MAX_PATHS];

	// default path where received files will be placed
	String *uploads_path;

	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	);

	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	);

	ClientFileStats *file_stats;

	ClientStats *stats;

	// client_create () points the values above to these ones,
	// so a client is not extra allocations, they are kept while the client is recycled
	ClientFileStats file_stats_storage;
	ClientStats stats_storage;
	pthread_mutex_t lock_storage;

	// the next deleted client that waits to be reused
	struct _Client *recycle_next;

};

typedef struct _Client Client;

CERVER_PUBLIC Client *client_new (void);

// completely deletes a client and all of its data
CERVER_PUBLIC void client_delete (void *ptr);

// used in data structures that require a delete function
// but the client needs to stay alive
CERVER_PUBLIC void client_delete_dummy (void *ptr);

// creates a new client and inits its values
CERVER_PUBLIC Client *client_create (void);

// creates a new client and registers a new connection
CERVER_PUBLIC Client *client_create_with_connection (
	struct _Cerver *cerver,
	const i32 sock_fd, const struct sockaddr_storage *address
);

// sets the client's name
CERVER_EXPORT void client_set_name (
	Client *client, const char *name
);

// sets the client's session id
CERVER_PUBLIC u8 client_set_session_id (
	Client *client, const char *session_id
);

// copies the client's session id into session_id, cut to size - 1 bytes and NUL terminated,
// it can be called from any thread while the client exists
// returns the number of bytes copied, 0 if the client does not have a session id
CERVER_EXPORT size_t client_get_session_id (
	const Client *client, char *session_id, const size_t size
);

// returns the client's app data
CERVER_EXPORT void *client_get_data (Client *client);

// sets client's data and a way to destroy it
// deletes the previous data of the client
CERVER_EXPORT void client_set_data (
	Client *client, void *data, Action delete_data
);

// sets customs PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packet types handlers
CERVER_EXPORT void client_set_app_handlers (
	Client *client,
	struct _Handler *app_handler, struct _Handler *app_error_handler
);

// sets a PACKET_TYPE_CUSTOM packet type handler
CERVER_EXPORT void client_set_custom_handler (
	Client *client, struct _Handler *custom_handler
);

// only handle packets with size <= max_received_packet_size
// if the packet is bigger it will be considered a bad packet 
CERVER_EXPORT void client_set_max_received_packet_size (
	Client *client, size_t max_received_packet_size
);

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
CERVER_EXPORT void client_set_check_packets (
	Client *client, bool check_packets
);

// sets how many bad packets a connection can receive before the client ends it,
// a bad packet fails packet_check () or has an unknown type
// 0 disables the limit, the default is CLIENT_DEFAULT_MAX_BAD_PACKETS
CERVER_EXPORT void client_set_max_bad_packets (
	Client *client, u8 max_bad_packets
);

// compare clients based on their client ids
CERVER_PUBLIC int client_comparator_client_id (
	const void *a, const void *b
);

// compare clients based on their session ids
CERVER_PUBLIC int client_comparator_session_id (
	const void *a, const void *b
);

// closes all client connections
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_disconnect (Client *client);

// the client got disconnected from the cerver, so correctly clear our data
CERVER_EXPORT void client_got_disconnected (Client *client);

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
CERVER_EXPORT void client_drop (
	struct _Cerver *cerver, Client *client
);

// adds a new connection to the end of the client to the client's connection list
// without adding it to any other structure
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_add (
	Client *client, struct _Connection *connection
);

// removes the connection from the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_remove (
	Client *client, struct _Connection *connection
);

// closes the connection & then removes it from the client & finally deletes the connection
// moves the socket to the cerver's socket pool
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_drop (
	struct _Cerver *cerver,
	Client *client, struct _Connection *connection
);

// removes the connection from the client referred to by the sock fd by calling client_connection_drop ()
// and also remove the client & connection from the cerver's structures when needed
// also checks if there is another active connection in the client, if not it will be dropped
// returns the resulting status after the operation
CERVER_PUBLIC ClientConnectionsStatus client_remove_connection_with_sock_fd (
	struct _Cerver *cerver,
	Client *client, const i32 sock_fd
);

// registers all the active connections from a client to the cerver's structures (like maps)
// returns 0 on success registering at least one, 1 if all connections failed
CERVER_PRIVATE u8 client_register_connections_to_cerver (
	struct _Cerver *cerver, Client *client
);

// unregiters all the active connections from a client from the cerver's structures (like maps)
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
CERVER_PRIVATE u8 client_unregister_connections_from_cerver (
	struct _Cerver *cerver, Client *client
);

// registers all the active connections from a client to the cerver's poll
// returns 0 on success registering at least one, 1 if all connections failed
CERVER_PRIVATE u8 client_register_connections_to_cerver_poll (
	struct _Cerver *cerver, Client *client
);

// unregisters all the active connections from a client from the cerver's poll
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
CERVER_PRIVATE u8 client_unregister_connections_from_cerver_poll (
	struct _Cerver *cerver, Client *client
);

// removes the client from cerver data structures, not taking into account its connections
CERVER_PRIVATE Client *client_remove_from_cerver (
	struct _Cerver *cerver, Client *client
);

// registers a client to the cerver --> add it to cerver's structures
// on error the client is not registered and the caller releases it with its connections
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 client_register_to_cerver (
	struct _Cerver *cerver, Client *client
);

// unregisters a client from a cerver -- removes it from cerver's structures
CERVER_PRIVATE Client *client_unregister_from_cerver (
	struct _Cerver *cerver, Client *client
);

// gets the client associated with a sock fd using the client-sock fd map
CERVER_PUBLIC Client *client_get_with_sock_fd (
	struct _Cerver *cerver, i32 sock_fd
);

// searches the avl tree to get the client associated with the session id
// the cerver must support sessions
CERVER_PUBLIC Client *client_get_with_session_id (
	struct _Cerver *cerver, const char *session_id
);

// broadcast a packet to all clients inside an avl structure
CERVER_PUBLIC void client_broadcast_to_all_avl (
	AVLNode *node,
	struct _Cerver *cerver,
	struct _Packet *packet
);

#pragma endregion


#pragma region client

/*** Use these to connect/disconnect a client to/from another server ***/

typedef struct ClientConnection {

	pthread_t connection_thread_id;
	struct _Client *client;
	struct _Connection *connection;

} ClientConnection;

CERVER_PRIVATE void client_connection_aux_delete (
	ClientConnection *cc
);

// returns true while the client is running, it is read by its update threads
CERVER_PUBLIC bool client_is_running (const Client *client);

// true when client_teardown () was called, the connections stop their connect backoff
CERVER_PRIVATE bool client_teardown_is_requested (const Client *client);

// completes the end of a connection that its update thread owns,
// called by the update thread when its receive loop has ended
CERVER_PRIVATE void client_connection_end_deferred (
	Client *client, struct _Connection *connection
);

// an update thread of the client is about to be created (started is true)
// or it failed to be created (started is false)
CERVER_PRIVATE void client_update_thread_started (
	Client *client, struct _Connection *connection, const bool started
);

// an update thread of the client has ended and it does not use the client anymore
CERVER_PRIVATE void client_update_thread_ended (Client *client);

// creates a new connection that is ready to connect and registers it to the client
CERVER_EXPORT struct _Connection *client_connection_create (
	Client *client,
	const char *ip_address, u16 port,
	Protocol protocol, bool use_ipv6
);

// registers an existing connection to a client
// retuns 0 on success, 1 on error
CERVER_EXPORT int client_connection_register (
	Client *client, struct _Connection *connection
);

// unregister an exitsing connection from the client
// returns 0 on success, 1 on error or if the connection does not belong to the client
CERVER_EXPORT int client_connection_unregister (
	Client *client, struct _Connection *connection
);

// performs a receive in the connection's socket
// to get a complete packet & handle it
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int client_connection_get_next_packet (
	Client *client, struct _Connection *connection
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
