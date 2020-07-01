#ifndef _CERVER_ERRORS_H_
#define _CERVER_ERRORS_H_

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

struct _Cerver;
struct _Client;
struct _Packet;
struct _Connection;

typedef enum ErrorType {

    ERR_NONE                    = 0,

    // internal server error, like no memory
    ERR_CERVER_ERROR            = 1,   

    ERR_CREATE_LOBBY            = 2,
    ERR_JOIN_LOBBY              = 3,
    ERR_LEAVE_LOBBY             = 4,
    ERR_FIND_LOBBY              = 5,

    ERR_GAME_INIT               = 6,
    ERR_GAME_START              = 7,

    ERR_FAILED_AUTH             = 8,

} ErrorType;

// when a client error happens, it sets the appropaited msg (if any)
// and an event is triggered
typedef struct Error {

    time_t timestamp;
    u32 error_type;
    estring *msg;

} Error;

extern Error *error_new (u32 error_type, const char *msg);

extern void error_delete (void *ptr);

// creates an error packet ready to be sent
extern struct _Packet *error_packet_generate (u32 error_type, const char *msg);

// creates and send a new error packet
// returns 0 on success, 1 on error
extern u8 error_packet_generate_and_send (u32 error_type, const char *msg,
    struct _Cerver *cerver, struct _Client *client, struct _Connection *connection);

// serialized error data
typedef struct SError {

    time_t timestamp;
    u32 error_type;
    char msg[64];

} SError;

#endif