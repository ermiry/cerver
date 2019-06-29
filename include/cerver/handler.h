#ifndef _CERVER_HANDLER_H_
#define _CERVER_HANDLER_H_

#include <stdlib.h>

#include "cerver/types/types.h"
#include "cerver/cerver.h"

struct _Cerver;
struct _Client;
struct _Connection;

typedef struct CerverReceive {

    Cerver *cerver;
    i32 sock_fd;
    bool on_hold;

} CerverReceive;

extern CerverReceive *cerver_receive_new (struct _Cerver *cerver, 
    i32 sock_fd, bool on_hold);

extern void cerver_receive_delete (void *ptr);

// receive all incoming data from the socket
extern void cerver_receive (void *ptr);

// sends back a test packet to the client!
extern void cerver_test_packet_handler (Packet *packet);

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