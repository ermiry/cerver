#ifndef _CERVER_LOG_H_
#define _CERVER_LOG_H_

#include <stdio.h>

#include "cerver/cerver.h"

#define COLOR_RED       "\x1b[31m"
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_BLUE      "\x1b[34m"
#define COLOR_MAGENTA   "\x1b[35m"
#define COLOR_CYAN      "\x1b[36m"
#define COLOR_RESET     "\x1b[0m"

typedef enum LogMsgType {

	NO_TYPE = 0,

    ERROR = 1,
    WARNING,
    SUCCESS,
    DEBUG_MSG,
    TEST,

    REQ = 10,
    PACKET,
    FILE_REQ,
    GAME,
    PLAYER,

    SERVER = 100,
    CLIENT,

} LogMsgType;

extern void logMsg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
    const char *msg);

extern void log_newServer (Server *server);

#endif