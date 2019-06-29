#ifndef _CERVER_SESSIONS_H_
#define _CERVER_SESSIONS_H_

#include "cerver/types/types.h"
#include "cerver/network.h"

// auxiliary struct that is passed to cerver session id generator
typedef struct SessionData {

    Packet *packet;
    AuthData *auth_data;
    Client *client;

} SessionData;

extern SessionData *session_data_new (Packet *packet, AuthData *auth_data, Client *client);

extern void session_data_delete (void *ptr);

// create a unique session id for each client based on the current time
extern void *session_default_generate_id (const void *session_data);

// serialized session id - token
typedef struct SToken {

    char token[64];

} SToken;

#endif