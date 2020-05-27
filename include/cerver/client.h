#ifndef _CERVER_CLIENT_H_
#define _CERVER_CLIENT_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/packets.h"
#include "cerver/connection.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dllist.h"

struct _Cerver;
struct _Packet;
struct _PacketsPerType;
struct _Connection;

struct _ClientStats {

    time_t client_threshold_time;           // every time we want to reset the client's stats
    u64 total_bytes_received;               // total amount of bytes received from this client
    u64 total_bytes_sent;                   // total amount of bytes that have been sent to the client (all of its connections)
    u64 n_packets_received;                 // total number of packets received from this client (packet header + data)
    u64 n_packets_send;                     // total number of packets sent to this client (all connections)

    struct _PacketsPerType *received_packets;
    struct _PacketsPerType *sent_packets;

};

typedef struct _ClientStats ClientStats;

// anyone that connects to the cerver
struct _Client {

    // generated using connection values
    u64 id;
    time_t connected_timestamp;

    DoubleList *connections;

    // multiple connections can be associated with the same client using the same session id
    estring *session_id;

    bool drop_client;        // client failed to authenticate

    void *data;
    Action delete_data;

    // used when the client connects to another server
    bool running;
    time_t time_started;
    u64 uptime;

    // custom packet handlers
    Action app_packet_handler;
    Action app_error_packet_handler;
    Action custom_packet_handler;

    ClientStats *stats;

};

typedef struct _Client Client;

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

// sets the client's session id
extern void client_set_session_id (Client *client, const char *session_id);

// returns the client's app data
extern void *client_get_data (Client *client);

// sets client's data and a way to destroy it
// deletes the previous data of the client
extern void client_set_data (Client *client, void *data, Action delete_data);

// sets the client packet handlers
extern void client_set_handlers (Client *client, 
    Action app_handler, Action app_error_handler, Action custom_handler);

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
extern u8 client_add_connection (Client *client, Connection *connection);

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

/*** Use these to connect/disconnect a client to/from another server ***/

typedef struct ClientConnection {

    struct _Client *client;
    struct _Connection *connection;

} ClientConnection;

extern void client_connection_aux_delete (ClientConnection *cc);

// creates a new connection that is ready to connect and registers it to the client
extern struct _Connection *client_connection_create (Client *client,
    const char *ip_address, u16 port, Protocol protocol, bool use_ipv6);

// this is a blocking method and ONLY works for cerver packets
// connects the client connection and makes a first request to the cerver
// then listen for packets until the target one is received, 
// then it returns the packet data as it is
// returns 0 on success, 1 on error
extern int client_connection_request_to_cerver (Client *client, struct _Connection *connection, 
    struct _Packet *request_packet);

// starts a client connection -- used to connect a client to another server
// returns only after a success or failed connection
// returns 0 on success, 1 on error
extern int client_connection_start (Client *client, struct _Connection *connection);

// starts the client connection async -- creates a new thread to handle how to connect with server
// returns 0 on success, 1 on error
extern int client_connection_start_async (Client *client, Connection *connection);

// terminates and destroy a connection registered to a client
// that is connected to a cerver
// returns 0 on success, 1 on error
extern int client_connection_end (Client *client, struct _Connection *connection);

// stop any on going connection and process then, destroys the client
extern u8 client_teardown (Client *client);

// receives incoming data from the socket and handles cerver packets
extern void client_receive (Client *client, Connection *connection);

#endif