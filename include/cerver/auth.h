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
typedef struct AuthData {

    estring *token;

    void *auth_data;
    size_t auth_data_size;

    // recover data used for authentication
    // after a success auth, it will be added to the client
    // if not, it should be dispossed by user dfined auth method
    // and set to NULL
    void *data;                 
    Action delete_data;         // how to delete the data

} AuthData;

// generates an authentication packet with client auth request
extern struct _Packet *auth_packet_generate (void);

// handles an packet from an on hold connection
extern void on_hold_packet_handler (void *ptr);

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess authentication or it failed to, so it is dropped
// returns 0 on success, 1 on error
extern u8 on_hold_connection (struct _Cerver *cerver, struct _Connection *connection);

// closes the on hold connection and removes it from the cerver
extern void on_hold_connection_drop (const struct _Cerver *cerver, struct _Connection *connection);

// auxiliary structure passed to the user defined auth method
typedef struct AuthPacket {

    struct _Packet *packet;
    AuthData *auth_data;

} AuthPacket;

#pragma region poll

// handles packets from the on hold clients until they authenticate
extern void *on_hold_poll (void *cerver_ptr);

#pragma endregion

#endif