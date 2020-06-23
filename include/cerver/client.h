#ifndef _CERVER_CLIENT_H_
#define _CERVER_CLIENT_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dllist.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/packets.h"
#include "cerver/connection.h"
#include "cerver/handler.h"

#include "cerver/utils/log.h"

struct _Cerver;
struct _Client;
struct _Packet;
struct _PacketsPerType;
struct _Connection;
struct _Handler;

struct _ClientStats {

    time_t threshold_time;                  // every time we want to reset the client's stats

    u64 n_receives_done;                    // n calls to recv ()

    u64 total_bytes_received;               // total amount of bytes received from this client
    u64 total_bytes_sent;                   // total amount of bytes that have been sent to the client (all of its connections)

    u64 n_packets_received;                 // total number of packets received from this client (packet header + data)
    u64 n_packets_sent;                     // total number of packets sent to this client (all connections)

    struct _PacketsPerType *received_packets;
    struct _PacketsPerType *sent_packets;

};

typedef struct _ClientStats ClientStats;

extern void client_stats_print (struct _Client *client);

// anyone that connects to the cerver
struct _Client {

    // generated using connection values
    u64 id;
    time_t connected_timestamp;
    
    // 16/06/2020 - abiility to add a name to a client
    estring *name;

    DoubleList *connections;

    // multiple connections can be associated with the same client using the same session id
    estring *session_id;

    time_t last_activity;   // the last time the client sent / receive data

    bool drop_client;        // client failed to authenticate

    void *data;
    Action delete_data;

    // used when the client connects to another server
    bool running;
    time_t time_started;
    u64 uptime;

    // 16/06/2020 - custom packet handlers
    volatile unsigned int num_handlers_alive;       // handlers currently alive
    volatile unsigned int num_handlers_working;     // handlers currently working
    pthread_mutex_t *handlers_lock;
    struct _Handler *app_packet_handler;
    struct _Handler *app_error_packet_handler;
    struct _Handler *custom_packet_handler;

    // 17/06/2020 - general client lock
    pthread_mutex_t *lock;

    ClientStats *stats;

};

typedef struct _Client Client;

#pragma region main

extern Client *client_new (void);

// completely deletes a client and all of its data
extern void client_delete (void *ptr);

// used in data structures that require a delete function, but the client needs to stay alive
extern void client_delete_dummy (void *ptr);

// creates a new client and inits its values
extern Client *client_create (void);

// creates a new client and registers a new connection
extern Client *client_create_with_connection (struct _Cerver *cerver, 
    const i32 sock_fd, const struct sockaddr_storage address);

// sets the client's name
extern void client_set_name (Client *client, const char *name);

// this methods is primarily used for logging
// returns the client's name directly (if any) & should NOT be deleted, if not
// returns a newly allocated string with the clients id that should be deleted after use
extern char *client_get_identifier (Client *client, bool *is_name);

// sets the client's session id
extern void client_set_session_id (Client *client, const char *session_id);

// returns the client's app data
extern void *client_get_data (Client *client);

// sets client's data and a way to destroy it
// deletes the previous data of the client
extern void client_set_data (Client *client, void *data, Action delete_data);

// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
extern void client_set_app_handlers (Client *client, 
    struct _Handler *app_handler, struct _Handler *app_error_handler);

// sets a CUSTOM_PACKET packet type handler
extern void client_set_custom_handler (Client *client, struct _Handler *custom_handler);

// compare clients based on their client ids
extern int client_comparator_client_id (const void *a, const void *b);

// compare clients based on their session ids
extern int client_comparator_session_id (const void *a, const void *b);

// closes all client connections
// returns 0 on success, 1 on error
extern u8 client_disconnect (Client *client);

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
extern void client_drop (struct _Cerver *cerver, Client *client);

// adds a new connection to the end of the client to the client's connection list
// without adding it to any other structure
// returns 0 on success, 1 on error
extern u8 client_add_connection (Client *client, struct _Connection *connection);

// removes the connection from the client
// and also checks if there is another active connection in the client, if not it will be dropped
// returns 0 on success, 1 on error
extern u8 client_remove_connection (struct _Cerver *cerver, Client *client, struct _Connection *connection);

// removes the connection from the client referred to by the sock fd
// and also checks if there is another active connection in the client, if not it will be dropped
// returns 0 on success, 1 on error
extern u8 client_remove_connection_by_sock_fd (struct _Cerver *cerver, Client *client, i32 sock_fd);

// registers all the active connections from a client to the cerver's structures (like maps)
// returns 0 on success registering at least one, 1 if all connections failed
extern u8 client_register_connections_to_cerver (struct _Cerver *cerver, Client *client);

// unregiters all the active connections from a client from the cerver's structures (like maps)
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
extern u8 client_unregister_connections_from_cerver (struct _Cerver *cerver, Client *client);

// registers all the active connections from a client to the cerver's poll
// returns 0 on success registering at least one, 1 if all connections failed
extern u8 client_register_connections_to_cerver_poll (struct _Cerver *cerver, Client *client);

// unregisters all the active connections from a client from the cerver's poll 
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
extern u8 client_unregister_connections_from_cerver_poll (struct _Cerver *cerver, Client *client);

// 07/06/2020
// removes the client from cerver data structures, not taking into account its connections
extern Client *client_remove_from_cerver (struct _Cerver *cerver, Client *client);

// registers a client to the cerver --> add it to cerver's structures
// returns 0 on success, 1 on error
extern u8 client_register_to_cerver (struct _Cerver *cerver, Client *client);

// unregisters a client from a cerver -- removes it from cerver's structures
extern Client *client_unregister_from_cerver (struct _Cerver *cerver, Client *client);

// gets the client associated with a sock fd using the client-sock fd map
extern Client *client_get_by_sock_fd (struct _Cerver *cerver, i32 sock_fd);

// searches the avl tree to get the client associated with the session id
// the cerver must support sessions
extern Client *client_get_by_session_id (struct _Cerver *cerver, char *session_id);

// broadcast a packet to all clients inside an avl structure
extern void client_broadcast_to_all_avl (AVLNode *node, struct _Cerver *cerver, 
    struct _Packet *packet);

#pragma endregion

/*** Use these to connect/disconnect a client to/from another server ***/

#pragma region client

typedef struct ClientConnection {

    pthread_t connection_thread_id;
    struct _Client *client;
    struct _Connection *connection;

} ClientConnection;

extern void client_connection_aux_delete (ClientConnection *cc);

// creates a new connection that is ready to connect and registers it to the client
extern struct _Connection *client_connection_create (Client *client,
    const char *ip_address, u16 port, Protocol protocol, bool use_ipv6);

/*** connect ***/

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
extern unsigned int client_connect (Client *client, struct _Connection *connection);

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet 
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
extern unsigned int client_connect_to_cerver (Client *client, struct _Connection *connection);

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
extern unsigned int client_connect_async (Client *client, struct _Connection *connection);

/*** requests ***/

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
extern unsigned int client_request_to_cerver (Client *client, struct _Connection *connection, struct _Packet *request);

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this method will NOT block
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// returns 0 on success request, 1 on error
extern unsigned int client_request_to_cerver_async (Client *client, struct _Connection *connection, struct _Packet *request);

/*** start ***/

// after a client connection successfully connects to a server, 
// it will start the connection's update thread to enable the connection to
// receive & handle packets in a dedicated thread
// returns 0 on success, 1 on error
extern int client_connection_start (Client *client, struct _Connection *connection);

// connects a client connection to a server
// and after a success connection, it will start the connection (create update thread for receiving messages)
// this is a blocking method, returns only after a success or failed connection
// returns 0 on success, 1 on error
extern int client_connect_and_start (Client *client, struct _Connection *connection);

// connects a client connection to a server in a new thread to avoid blocking the calling thread,
// and after a success connection, it will start the connection (create update thread for receiving messages)
// returns 0 on success creating connection thread, 1 on error
extern u8 client_connect_and_start_async (Client *client, struct _Connection *connection);

/*** end ***/

// terminates the connection & closes the socket
// but does NOT destroy the current connection
// returns 0 on success, 1 on error
extern int client_connection_close (Client *client, struct _Connection *connection);

// terminates and destroys a connection registered to a client
// that is connected to a cerver
// returns 0 on success, 1 on error
extern int client_connection_end (Client *client, struct _Connection *connection);

// stop any on going connection and process and destroys the client
// returns 0 on success, 1 on error
extern u8 client_teardown (Client *client);

// calls client_teardown () in a new thread as handlers might need time to stop
// that will cause the calling thread to wait at least a second
// returns 0 on success creating thread, 1 on error
extern u8 client_teardown_async (Client *client);

/*** update ***/

// receives incoming data from the socket
// returns 0 on success handle, 1 if any error ocurred and must likely the connection was ended
extern unsigned int client_receive (Client *client, struct _Connection *connection);

#pragma endregion

#endif