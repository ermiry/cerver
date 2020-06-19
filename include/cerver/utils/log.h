#ifndef _CERVER_UTILS_LOG_H_
#define _CERVER_UTILS_LOG_H_

#include <stdio.h>

#define COLOR_RED       "\x1b[31m"
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_BLUE      "\x1b[34m"
#define COLOR_MAGENTA   "\x1b[35m"
#define COLOR_CYAN      "\x1b[36m"
#define COLOR_RESET     "\x1b[0m"

typedef enum LogMsgType {

	LOG_NO_TYPE             = 0,

	LOG_ERROR               = 1,
	LOG_WARNING             = 2,
	LOG_SUCCESS             = 3,
	LOG_DEBUG               = 4,
	LOG_TEST                = 5,

	LOG_CERVER,
	LOG_CLIENT,

	LOG_REQ,
	LOG_PACKET,
	LOG_FILE,
	LOG_GAME,
	LOG_PLAYER,

} LogMsgType;

extern void cerver_log_msg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
	const char *msg);

// prints a red error message to stderr
extern void cerver_log_error (const char *msg);

// prints a yellow warning message to stderr
extern void cerver_log_warning (const char *msg);

// prints a green success message to stdout
extern void cerver_log_success (const char *msg);

// prints a debug message to stdout
extern void cerver_log_debug (const char *msg);

#endif