#ifndef _CERVER_AUTH_H_
#define _CERVER_AUTH_H_

#include <stdlib.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/packets.h"
#include "cerver/cerver.h"
#include "cerver/client.h"

#define DEFAULT_AUTH_TRIES              3

struct _Cerver;
struct _Connection;

// the auth data stripped from the packet
struct _AuthData {

    estring *token;

    void *auth_data;
    size_t auth_data_size;

    // recover data used for authentication
    // after a success auth, it will be added to the client
    // if not, it should be dispossed by user defined auth method
    // and set to NULL
    void *data;                 
    Action delete_data;         // how to delete the data

};

typedef struct _AuthData AuthData;

// auxiliary structure passed to the user defined auth method
struct _AuthMethod {

    struct _Packet *packet;         // the original packet
    AuthData *auth_data;            // the stripped auth data from the packet

    // a user message that can be sent to the connection when teh auth has failed
    // in a generated ERR_FAILED_AUTH packet
    estring *error_message;

};

typedef struct _AuthMethod AuthMethod;

#pragma region handler

// handles an packet from an on hold connection
extern void on_hold_packet_handler (void *packet_ptr);

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess or failed authentication
// returns 0 on success, 1 on error
extern u8 on_hold_connection (struct _Cerver *cerver, struct _Connection *connection);

// closes the on hold connection and removes it from the cerver
extern void on_hold_connection_drop (const struct _Cerver *cerver, struct _Connection *connection);

#pragma endregion

#pragma region poll

// removed a sock fd from the cerver's on hold poll array
// returns 0 on success, 1 on error
extern u8 on_hold_poll_unregister_sock_fd (struct _Cerver *cerver, const i32 sock_fd);

// handles packets from the on hold clients until they authenticate
extern void *on_hold_poll (void *cerver_ptr);

#pragma endregion

#endif