#ifndef _CERVER_HANDLER_H_
#define _CERVER_HANDLER_H_

#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/client.h"
#include "cerver/packets.h"
#include "cerver/receive.h"

#include "cerver/threads/jobs.h"

#include "cerver/game/lobby.h"

#define RECEIVE_PACKET_BUFFER_SIZE      8192

struct _Socket;
struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;
struct _Packet;
struct _Admin;

#pragma region handler

typedef enum HandlerType {

    HANDLER_TYPE_NONE         = 0,

    HANDLER_TYPE_CERVER       = 1,
    HANDLER_TYPE_CLIENT       = 2,
    HANDLER_TYPE_ADMIN        = 3,

} HandlerType;

// the strcuture that will be passed to the handler
typedef struct HandlerData {

    int handler_id;

    void *data;                     // handler's own data
    struct _Packet *packet;         // the packet to handle

} HandlerData;

struct _Handler {

    HandlerType type;
    int unique_id;                  // added every time a new handler gets created

    int id;
    pthread_t thread_id;

    // unique handler data
    // will be passed to jobs alongside any job specific data as the args
    void *data;
    
    // must return a newly allocated handler unique data
    // will be executed when the handler starts
    void *(*data_create) (void *args);
    void *data_create_args;

    // called at the end of the handler to delete the handler's data
    // if no method is set, it won't be deleted
    Action data_delete;

    // the method that this handler will execute to handle packets
    Action handler;

    // 27/05/2020 - used to avoid pushing job to the queue and instead handle
    // the packet directly in the same thread
    // this option is set to false as default
    // pros - inmediate handle with no delays
    //      - handler method can be called from multiple threads
    // neutral - data create and delete will be executed every time
    // cons - calling thread will be busy until handler method is done
    bool direct_handle;

    // the jobs (packets) that are waiting to be handled - passed as args to the handler method
    JobQueue *job_queue;

    struct _Cerver *cerver;     // the cerver this handler belongs to
    struct _Client *client;     // the client this handler belongs to

};

typedef struct _Handler Handler;

CERVER_PRIVATE void handler_delete (void *handler_ptr);

// creates a new handler
// handler method is your actual app packet handler
CERVER_EXPORT Handler *handler_create (Action handler_method);

// creates a new handler that will be used for cerver's multiple app handlers configuration
// it should be registered to the cerver before it starts
// the user is responsible for setting the unique id, which will be used to match
// incoming packets
// handler method is your actual app packet handler
CERVER_EXPORT Handler *handler_create_with_id (int id, Action handler_method);

// sets the handler's data directly
// this data will be passed to the handler method using a HandlerData structure
CERVER_EXPORT void handler_set_data (Handler *handler, void *data);

// set a method to create the handler's data before it starts handling any packet
// this data will be passed to the handler method using a HandlerData structure
CERVER_EXPORT void handler_set_data_create (Handler *handler, 
    void *(*data_create) (void *args), void *data_create_args);

// set the method to be used to delete the handler's data
CERVER_EXPORT void handler_set_data_delete (Handler *handler, Action data_delete);

// used to avoid pushing job to the queue and instead handle
// the packet directly in the same thread
// pros     - inmediate handle with no delays
//          - handler method can be called from multiple threads
// neutral  - data create and delete will be executed every time
// cons     - calling thread will be busy until handler method is done
CERVER_EXPORT void handler_set_direct_handle (Handler *handler, bool direct_handle);

// starts the new handler by creating a dedicated thread for it
// called by internal cerver methods
CERVER_PRIVATE int handler_start (Handler *handler);

#pragma endregion

#pragma region handlers

// sends back a test packet to the client!
CERVER_PRIVATE void cerver_test_packet_handler (struct _Packet *packet);

#pragma endregion

#pragma region sock receive

struct _SockReceive {

    struct _Packet *spare_packet;
    size_t missing_packet;

    void *header;
    char *header_end;
    // unsigned int curr_header_pos;
    unsigned int remaining_header;
    bool complete_header;

};

typedef struct _SockReceive SockReceive;

CERVER_PRIVATE SockReceive *sock_receive_new (void);

CERVER_PRIVATE void sock_receive_delete (void *sock_receive_ptr);

#pragma endregion

#pragma region receive

// the default timeout when handling a connection in dedicated thread
#define DEFAULT_SOCKET_RECV_TIMEOUT         5

typedef struct ReceiveHandle {

    ReceiveType type;

    struct _Cerver *cerver;

    struct _Socket *socket;
    struct _Connection *connection;
    struct _Client *client;
    struct _Admin *admin;

    struct _Lobby *lobby;

    char *buffer;
    size_t buffer_size;

} ReceiveHandle;

CERVER_PRIVATE void receive_handle_delete (void *receive_ptr);

// default cerver receive handler
CERVER_PRIVATE void cerver_receive_handle_buffer (void *receive_ptr);

typedef struct CerverReceive {

    ReceiveType type;

    struct _Cerver *cerver;

    struct _Socket *socket;
    struct _Connection *connection;
    struct _Client *client;
    struct _Admin *admin;

    struct _Lobby *lobby;

} CerverReceive;

CERVER_PRIVATE void cerver_receive_delete (void *ptr);

CERVER_PRIVATE CerverReceive *cerver_receive_create (ReceiveType receive_type, struct _Cerver *cerver, const i32 sock_fd);

CERVER_PRIVATE CerverReceive *cerver_receive_create_full (ReceiveType receive_type, 
    struct _Cerver *cerver, 
    struct _Client *client, struct _Connection *connection
);

CERVER_PRIVATE void cerver_switch_receive_handle_failed (CerverReceive *cr);

// receive all incoming data from the socket
CERVER_PRIVATE void cerver_receive (void *ptr);

#pragma endregion

#pragma region poll

// reallocs main cerver poll fds
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 cerver_realloc_main_poll_fds (struct _Cerver *cerver);

// get a free index in the main cerver poll strcuture
CERVER_PRIVATE i32 cerver_poll_get_free_idx (struct _Cerver *cerver);

// get the idx of the connection sock fd in the cerver poll fds
CERVER_PRIVATE i32 cerver_poll_get_idx_by_sock_fd (struct _Cerver *cerver, i32 sock_fd);

// regsiters a client connection to the cerver's mains poll structure
// and maps the sock fd to the client
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 cerver_poll_register_connection (struct _Cerver *cerver, struct _Connection *connection);

// removes a sock fd from the cerver's main poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 cerver_poll_unregister_sock_fd (struct _Cerver *cerver, const i32 sock_fd);

// unregsiters a client connection from the cerver's main poll structure
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 cerver_poll_unregister_connection (struct _Cerver *cerver, struct _Connection *connection);

// server poll loop to handle events in the registered socket's fds
CERVER_PRIVATE u8 cerver_poll (struct _Cerver *cerver);

#pragma endregion

#pragma region threads

// handle new connections in dedicated threads
CERVER_PRIVATE u8 cerver_threads (struct _Cerver *cerver);

#pragma endregion

#endif