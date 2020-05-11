#ifndef _CERVER_HANDLER_H_
#define _CERVER_HANDLER_H_

#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/packets.h"

#include "cerver/threads/jobs.h"

#include "cerver/game/lobby.h"

#define RECEIVE_PACKET_BUFFER_SIZE      8192

struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;
struct _Packet;

struct _Handler {

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

    // the jobs (packets) that are waiting to be handled - passed as args to the handler method
    JobQueue *job_queue;

    struct _Cerver *cerver;      // the cerver this handler belongs to

};

typedef struct _Handler Handler;

extern void handler_delete (void *handler_ptr);

extern Handler *handler_create (int id, Action handler_method);

extern void handler_set_data (Handler *handler, void *data);

extern void handler_set_data_create (Handler *handler, 
    void *(*data_create) (void *args), void *data_create_args);

extern void handler_set_data_delete (Handler *handler, Action data_delete);

// starts the new handler by creating a dedicated thread for it
// called by internal cerver methods
extern int handler_start (Handler *handler);

typedef struct ReceiveHandle {

    struct _Cerver *cerver;
    i32 sock_fd;
    char *buffer;
    size_t buffer_size;
    bool on_hold;
    struct _Lobby *lobby;

} ReceiveHandle;

extern void receive_handle_delete (void *receive_ptr);

// default cerver receive handler
extern void cerver_receive_handle_buffer (void *receive_ptr);

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

extern SockReceive *sock_receive_new (void);

extern void sock_receive_delete (void *sock_receive_ptr);

typedef struct CerverReceive {

    struct _Cerver *cerver;
    i32 sock_fd;
    bool on_hold;
    struct _Lobby *lobby;

} CerverReceive;

extern CerverReceive *cerver_receive_new (struct _Cerver *cerver, 
    i32 sock_fd, bool on_hold, struct _Lobby *lobby);

extern void cerver_receive_delete (void *ptr);

// receive all incoming data from the socket
extern void cerver_receive (void *ptr);

// sends back a test packet to the client!
extern void cerver_test_packet_handler (struct _Packet *packet);

// reallocs main cerver poll fds
// returns 0 on success, 1 on error
extern u8 cerver_realloc_main_poll_fds (struct _Cerver *cerver);

// get a free index in the main cerver poll strcuture
extern i32 cerver_poll_get_free_idx (struct _Cerver *cerver);

// get the idx of the connection sock fd in the cerver poll fds
extern i32 cerver_poll_get_idx_by_sock_fd (struct _Cerver *cerver, i32 sock_fd);

// regsiters a client connection to the cerver's main poll structure
// and maps the sock fd to the client
extern u8 cerver_poll_register_connection (struct _Cerver *cerver, 
    struct _Client *client, struct _Connection *connection);

// unregsiters a client connection from the cerver's main poll structure
// and removes the map from the socket to the client
u8 cerver_poll_unregister_connection (struct _Cerver *cerver, 
    struct _Client *client, struct _Connection *connection);

// server poll loop to handle events in the registered socket's fds
extern u8 cerver_poll (struct _Cerver *cerver);

#endif