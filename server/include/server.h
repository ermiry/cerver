#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

#define EXIT_FAILURE    1

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef unsigned char asciiChar;

extern void die (char *msg);

/*** SEVER VALUES ***/

#include <sys/time.h>
#include <time.h>

typedef struct timespec Cptime;

typedef enum RequestType {

    REQ_GET_FILE = 1,
    POST_SEND_FILE,
    
    REQ_CREATE_LOBBY = 3

} RequestType;

#define CLIENT_REQ_TYPE_SIZE     8

extern i32 server;

/*** SERVER FUNCS ***/

#include "utils/config.h"

extern u32 initServer (Config *, u8);
extern void listenForConnections (void);
extern u8 teardown (void);

#endif