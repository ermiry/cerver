#ifndef _CERVER_ERRORS_H_
#define _CERVER_ERRORS_H_

#include "cerver/types/string.h"
#include "cerver/packets.h"

typedef enum ErrorType {

    ERR_SERVER_ERROR = 0,   // internal server error, like no memory

    ERR_CREATE_LOBBY = 1,
    ERR_JOIN_LOBBY,
    ERR_LEAVE_LOBBY,
    ERR_FIND_LOBBY,

    ERR_GAME_INIT,

    ERR_FAILED_AUTH,

} ErrorType;

// when a client error happens, it sets the appropaited msg (if any)
// and an event is triggered
typedef struct Error {

    // TODO: maybe add time?
    u32 error_type;
    String *msg;

} Error;

extern Error *error_new (ErrorType error_type, const char *msg);
extern void error_delete (void *ptr);

// creates an error packet ready to be sent
extern Packet *error_packet_generate (ErrorType error_type, const char *msg);

// serialized error data
typedef struct SError {

    u32 error_type;
    char msg[64];

} SError;

#endif