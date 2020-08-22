#ifndef _CERVER_CLIENT_H_
#define _CERVER_CLIENT_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/connection.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/handler.h"

#include "cerver/utils/log.h"

struct _Cerver;
struct _Client;
struct _Packet;
struct _PacketsPerType;
struct _Connection;
struct _Handler;

#pragma region stats

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

CERVER_PUBLIC void client_stats_print (struct _Client *client);

#pragma endregion

#pragma region main

// anyone that connects to the cerver
struct _Client {

    // generated using connection values
    u64 id;
    time_t connected_timestamp;
    
    // 16/06/2020 - abiility to add a name to a client
    String *name;

    DoubleList *connections;

    // multiple connections can be associated with the same client using the same session id
    String *session_id;

    time_t last_activity;   // the last time the client sent / receive data

    bool drop_client;        // client failed to authenticate

    void *data;
    Action delete_data;

    // used when the client connects to another server
    bool running;
    time_t time_started;
    u64 uptime;

    // 16/06/2020 - custom packet handlers
    unsigned int num_handlers_alive;       // handlers currently alive
    unsigned int num_handlers_working;     // handlers currently working
    pthread_mutex_t *handlers_lock;
    struct _Handler *app_packet_handler;
    struct _Handler *app_error_packet_handler;
    struct _Handler *custom_packet_handler;

    // 17/06/2020 - general client lock
    pthread_mutex_t *lock;

    DoubleList *events;
    DoubleList *errors;

    ClientStats *stats;

};

typedef struct _Client Client;

CERVER_PUBLIC Client *client_new (void);

// completely deletes a client and all of its data
CERVER_PUBLIC void client_delete (void *ptr);

// used in data structures that require a delete function, but the client needs to stay alive
CERVER_PUBLIC void client_delete_dummy (void *ptr);

// creates a new client and inits its values
CERVER_PUBLIC Client *client_create (void);

// creates a new client and registers a new connection
CERVER_PUBLIC Client *client_create_with_connection (struct _Cerver *cerver, 
    const i32 sock_fd, const struct sockaddr_storage address);

// sets the client's name
CERVER_EXPORT void client_set_name (Client *client, const char *name);

// this methods is primarily used for logging
// returns the client's name directly (if any) & should NOT be deleted, if not
// returns a newly allocated string with the clients id that should be deleted after use
CERVER_EXPORT char *client_get_identifier (Client *client, bool *is_name);

// sets the client's session id
CERVER_EXPORT void client_set_session_id (Client *client, const char *session_id);

// returns the client's app data
CERVER_EXPORT void *client_get_data (Client *client);

// sets client's data and a way to destroy it
// deletes the previous data of the client
CERVER_EXPORT void client_set_data (Client *client, void *data, Action delete_data);

// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
CERVER_EXPORT void client_set_app_handlers (Client *client, 
    struct _Handler *app_handler, struct _Handler *app_error_handler);

// sets a CUSTOM_PACKET packet type handler
CERVER_EXPORT void client_set_custom_handler (Client *client, struct _Handler *custom_handler);

// compare clients based on their client ids
CERVER_PUBLIC int client_comparator_client_id (const void *a, const void *b);

// compare clients based on their session ids
CERVER_PUBLIC int client_comparator_session_id (const void *a, const void *b);

// closes all client connections
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_disconnect (Client *client);

// the client got disconnected from the cerver, so correctly clear our data
CERVER_EXPORT void client_got_disconnected (Client *client);

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
CERVER_EXPORT void client_drop (struct _Cerver *cerver, Client *client);

// adds a new connection to the end of the client to the client's connection list
// without adding it to any other structure
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_add (Client *client, struct _Connection *connection);

// removes the connection from the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_remove (Client *client, struct _Connection *connection);

// closes the connection & then removes it from the client & finally deletes the connection
// moves the socket to the cerver's socket pool
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_drop (struct _Cerver *cerver, Client *client, struct _Connection *connection);

// removes the connection from the client referred to by the sock fd by calling client_connection_drop ()
// and also remove the client & connection from the cerver's structures when needed
// also checks if there is another active connection in the client, if not it will be dropped
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 client_remove_connection_by_sock_fd (struct _Cerver *cerver, Client *client, i32 sock_fd);

// registers all the active connections from a client to the cerver's structures (like maps)
// returns 0 on success registering at least one, 1 if all connections failed
CERVER_PRIVATE u8 client_register_connections_to_cerver (struct _Cerver *cerver, Client *client);

// unregiters all the active connections from a client from the cerver's structures (like maps)
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
CERVER_PRIVATE u8 client_unregister_connections_from_cerver (struct _Cerver *cerver, Client *client);

// registers all the active connections from a client to the cerver's poll
// returns 0 on success registering at least one, 1 if all connections failed
CERVER_PRIVATE u8 client_register_connections_to_cerver_poll (struct _Cerver *cerver, Client *client);

// unregisters all the active connections from a client from the cerver's poll 
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
CERVER_PRIVATE u8 client_unregister_connections_from_cerver_poll (struct _Cerver *cerver, Client *client);

// 07/06/2020
// removes the client from cerver data structures, not taking into account its connections
CERVER_PRIVATE Client *client_remove_from_cerver (struct _Cerver *cerver, Client *client);

// registers a client to the cerver --> add it to cerver's structures
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 client_register_to_cerver (struct _Cerver *cerver, Client *client);

// unregisters a client from a cerver -- removes it from cerver's structures
CERVER_PRIVATE Client *client_unregister_from_cerver (struct _Cerver *cerver, Client *client);

// gets the client associated with a sock fd using the client-sock fd map
CERVER_PUBLIC Client *client_get_by_sock_fd (struct _Cerver *cerver, i32 sock_fd);

// searches the avl tree to get the client associated with the session id
// the cerver must support sessions
CERVER_PUBLIC Client *client_get_by_session_id (struct _Cerver *cerver, const char *session_id);

// broadcast a packet to all clients inside an avl structure
CERVER_PUBLIC void client_broadcast_to_all_avl (AVLNode *node, struct _Cerver *cerver, 
    struct _Packet *packet);

#pragma endregion

#pragma region events

typedef enum ClientEventType {

    CLIENT_EVENT_NONE                  = 0, 

    CLIENT_EVENT_CONNECTED,            // connected to cerver
    CLIENT_EVENT_DISCONNECTED,         // disconnected from the cerver, either by the cerver or by losing connection

    CLIENT_EVENT_CONNECTION_FAILED,    // failed to connect to cerver
    CLIENT_EVENT_CONNECTION_CLOSE,     // this happens when a call to a recv () methods returns <= 0, the connection is clossed directly by client

    CLIENT_EVENT_CONNECTION_DATA,      // data has been received, only triggered from client request methods

    CLIENT_EVENT_CERVER_INFO,          // received cerver info from the cerver
    CLIENT_EVENT_CERVER_TEARDOWN,      // the cerver is going to teardown (disconnect happens automatically)
    CLIENT_EVENT_CERVER_STATS,         // received cerver stats
    CLIENT_EVENT_CERVER_GAME_STATS,    // received cerver game stats

    CLIENT_EVENT_AUTH_SENT,            // auth data has been sent to the cerver
    CLIENT_EVENT_SUCCESS_AUTH,         // auth with cerver has been successfull
    CLIENT_EVENT_MAX_AUTH_TRIES,       // maxed out attempts to authenticate to cerver, so try again

    CLIENT_EVENT_LOBBY_CREATE,         // a new lobby was successfully created
    CLIENT_EVENT_LOBBY_JOIN,           // correctly joined a new lobby
    CLIENT_EVENT_LOBBY_LEAVE,          // successfully exited a lobby

    CLIENT_EVENT_LOBBY_START,          // the game in the lobby has started

} ClientEventType;

typedef struct ClientEvent {

    ClientEventType type;         // the event we are waiting to happen
    bool create_thread;                 // create a detachable thread to run action
    bool drop_after_trigger;            // if we only want to trigger the event once

    // the request that triggered the event
    // this is usefull for custom events
    u32 request_type; 
    void *response_data;                // data that came with the response   
    Action delete_response_data;       

    Action action;                      // the action to be triggered
    void *action_args;                  // the action arguments
    Action delete_action_args;          // how to get rid of the data

} ClientEvent;

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated ClientEventData structure will be passed to your method 
// that should be free using the client_event_data_delete () method
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_event_register (
    struct _Client *client, const ClientEventType event_type, 
    Action action, void *action_args, Action delete_action_args, 
    bool create_thread, bool drop_after_trigger
);

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_event_unregister (struct _Client *client, const ClientEventType event_type);

CERVER_PRIVATE void client_event_set_response (
    struct _Client *client, const ClientEventType event_type,
    void *response_data, Action delete_response_data
);

// triggers all the actions that are registred to an event
CERVER_PRIVATE void client_event_trigger (
    const ClientEventType event_type,
    const struct _Client *client, const struct _Connection *connection
);

// structure that is passed to the user registered method
typedef struct ClientEventData {

    const struct _Client *client;
    const struct _Connection *connection;

    void *response_data;                // data that came with the response   
    Action delete_response_data;  

    void *action_args;                  // the action arguments
    Action delete_action_args;

} ClientEventData;

CERVER_PUBLIC void client_event_data_delete (ClientEventData *event_data);

#pragma endregion

#pragma region errors

typedef enum ClientErrorType {

    CLIENT_ERROR_NONE                    = 0,

	CLIENT_ERROR_CERVER_ERROR            = 1, // internal server error, like no memory

	CLIENT_ERROR_FAILED_AUTH             = 2, // we failed to authenticate with the cerver

	CLIENT_ERROR_CREATE_LOBBY            = 3, // failed to create a new game lobby
	CLIENT_ERROR_JOIN_LOBBY              = 4, // a client / player failed to join an existin lobby
	CLIENT_ERROR_LEAVE_LOBBY             = 5, // a player failed to leave from a lobby
	CLIENT_ERROR_FIND_LOBBY              = 6, // failed to find a game lobby for a player

	CLIENT_ERROR_GAME_INIT               = 7, // the game failed to init properly
	CLIENT_ERROR_GAME_START              = 8, // the game failed to start

} ClientErrorType;

typedef struct ClientError {

	ClientErrorType type;
	bool create_thread;                 // create a detachable thread to run action
    bool drop_after_trigger;            // if we only want to trigger the event once

	Action action;                      // the action to be triggered
    void *action_args;                  // the action arguments
    Action delete_action_args;          // how to get rid of the data

} ClientError;

// registers an action to be triggered when the specified error occurs
// if there is an existing action registered to an error, it will be overrided
// a newly allocated ClientErrorData structure will be passed to your method 
// that should be free using the client_error_data_delete () method
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_error_register (
    struct _Client *client, const ClientErrorType error_type,
	Action action, void *action_args, Action delete_action_args, 
    bool create_thread, bool drop_after_trigger
);

// unregisters the action associated with the error types
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_error_unregister (struct _Client *client, const ClientErrorType error_type);

// triggers all the actions that are registred to an error
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 client_error_trigger (
    const ClientErrorType error_type, 
	const struct _Client *client, const struct _Connection *connection, 
	const char *error_message
);

#pragma region data

// structure that is passed to the user registered method
typedef struct ClientErrorData {

    const struct _Client *client;
    const struct _Connection *connection;

    void *action_args;                  // the action arguments set by the user

	String *error_message;

} ClientErrorData;

CERVER_PUBLIC void client_error_data_delete (ClientErrorData *error_data);

#pragma endregion

#pragma region client

/*** Use these to connect/disconnect a client to/from another server ***/

typedef struct ClientConnection {

    pthread_t connection_thread_id;
    struct _Client *client;
    struct _Connection *connection;

} ClientConnection;

CERVER_PRIVATE void client_connection_aux_delete (ClientConnection *cc);

// creates a new connection that is ready to connect and registers it to the client
CERVER_EXPORT struct _Connection *client_connection_create (
    Client *client,
    const char *ip_address, u16 port, 
    Protocol protocol, bool use_ipv6
);

// registers an existing connection to a client
// retuns 0 on success, 1 on error
CERVER_EXPORT int client_connection_register (Client *client, struct _Connection *connection);

// unregister an exitsing connection from the client
// returns 0 on success, 1 on error or if the connection does not belong to the client
CERVER_EXPORT int client_connection_unregister (Client *client, struct _Connection *connection);

// performs a receive in the connection's socket to get a complete packet & handle it
CERVER_EXPORT void client_connection_get_next_packet (Client *client, struct _Connection *connection);

/*** connect ***/

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
CERVER_EXPORT unsigned int client_connect (Client *client, struct _Connection *connection);

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet 
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
CERVER_EXPORT unsigned int client_connect_to_cerver (Client *client, struct _Connection *connection);

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
CERVER_EXPORT unsigned int client_connect_async (Client *client, struct _Connection *connection);

/*** requests ***/

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
CERVER_EXPORT unsigned int client_request_to_cerver (Client *client, struct _Connection *connection, struct _Packet *request);

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this method will NOT block
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// returns 0 on success request, 1 on error
CERVER_EXPORT unsigned int client_request_to_cerver_async (Client *client, struct _Connection *connection, struct _Packet *request);

/*** start ***/

// after a client connection successfully connects to a server, 
// it will start the connection's update thread to enable the connection to
// receive & handle packets in a dedicated thread
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connection_start (Client *client, struct _Connection *connection);

// connects a client connection to a server
// and after a success connection, it will start the connection (create update thread for receiving messages)
// this is a blocking method, returns only after a success or failed connection
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connect_and_start (Client *client, struct _Connection *connection);

// connects a client connection to a server in a new thread to avoid blocking the calling thread,
// and after a success connection, it will start the connection (create update thread for receiving messages)
// returns 0 on success creating connection thread, 1 on error
CERVER_EXPORT u8 client_connect_and_start_async (Client *client, struct _Connection *connection);

/*** end ***/

// terminates the connection & closes the socket
// but does NOT destroy the current connection
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connection_close (Client *client, struct _Connection *connection);

// terminates and destroys a connection registered to a client
// that is connected to a cerver
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connection_end (Client *client, struct _Connection *connection);

// stop any on going connection and process and destroys the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_teardown (Client *client);

// calls client_teardown () in a new thread as handlers might need time to stop
// that will cause the calling thread to wait at least a second
// returns 0 on success creating thread, 1 on error
CERVER_EXPORT u8 client_teardown_async (Client *client);

/*** update ***/

// receives incoming data from the socket
// returns 0 on success handle, 1 if any error ocurred and must likely the connection was ended
CERVER_PUBLIC unsigned int client_receive (Client *client, struct _Connection *connection);

#pragma endregion

#endif