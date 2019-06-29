#ifndef _CERVER_CLIENT_H_
#define _CERVER_CLIENT_H_

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "network.h"
#include "cerver.h"
#include "client.h"

#include "collections/avl.h"
#include "collections/dllist.h"

struct _Cerver;
struct _Client;

// a connection from a client
struct _Connection {

    // connection values
    i32 sock_fd;
    u16 port;
    String *ip;
    struct sockaddr_storage address;

    time_t raw_time;
    struct tm *time_info;

    // authentication
    u8 auth_tries;           // remaining attemps to authenticate

    bool active;

};

typedef struct _Connection Connection;

extern Connection *client_connection_new (void);
extern void client_connection_delete (void *ptr);

// creates a new lcient connection with the specified values
Connection *client_connection_create (const i32 sock_fd, 
    const struct sockaddr_storage address);

// compare two connections by their socket fds
extern int client_connection_comparator (const void *a, const void *b);

// sets the connection time stamp
extern void client_connection_set_time (Connection *connection);

// get from where the client is connecting
extern void client_connection_get_values (Connection *connection);

// returns connection time stamp in a string that should be deleted
extern const String *client_connection_get_time (const Connection *connection);

// ends a client connection
extern void client_connection_end (Connection *connection);

// registers a new connection to a client
// returns 0 on success, 1 on error
extern u8 client_connection_register (struct _Cerver *cerver, 
    struct _Client *client, Connection *connection);

// unregisters a connection from a client and stops and deletes it
// returns 0 on success, 1 on error
extern u8 client_connection_unregister (struct _Cerver *cerver, 
    struct _Client *client, Connection *connection);

// anyone that connects to the cerver
struct _Client {

    // generated using connection values
    String *client_id;

    DoubleList *connections;

    // multiple connections can be associated with the same client using the same session id
    String *session_id;

    bool drop_client;        // client failed to authenticate

    void *data;
    Action delete_data;

};

typedef struct _Client Client;

extern Client *client_new (void);
extern void client_delete (void *ptr);

// creates a new client and registers a new connection
extern Client *client_create (struct _Cerver *cerver, 
    const i32 sock_fd, const struct sockaddr_storage address);

// sets the client's session id
extern void client_set_session_id (Client *client, const char *session_id);

// sets client's data and a way to destroy it
extern void client_set_data (Client *client, void *data, Action delete_data);

// compare clients based on their client ids
extern int client_comparator_client_id (const void *a, const void *b);

// compare clients based on their session ids
extern int client_comparator_session_id (const void *a, const void *b);

// closes all client connections
// returns 0 on success, 1 on error
extern u8 client_disconnect (Client *client);

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

#endif