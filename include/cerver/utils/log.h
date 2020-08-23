#ifndef _CERVER_UTILS_LOG_H_
#define _CERVER_UTILS_LOG_H_

#include <stdio.h>

#include "cerver/config.h"

#define LOG_COLOR_RED       "\x1b[31m"
#define LOG_COLOR_GREEN     "\x1b[32m"
#define LOG_COLOR_YELLOW    "\x1b[33m"
#define LOG_COLOR_BLUE      "\x1b[34m"
#define LOG_COLOR_MAGENTA   "\x1b[35m"
#define LOG_COLOR_CYAN      "\x1b[36m"
#define LOG_COLOR_RESET     "\x1b[0m"

typedef enum LogType {

	LOG_NO_TYPE             = 0,

	LOG_ERROR               = 1,
	LOG_WARNING             = 2,
	LOG_SUCCESS             = 3,
	LOG_DEBUG               = 4,
	LOG_TEST                = 5,

	LOG_CERVER				= 6,
	LOG_CLIENT				= 7,

	LOG_REQ					= 8,
	LOG_PACKET				= 9,
	LOG_FILE				= 10,
	LOG_GAME				= 11,
	LOG_PLAYER				= 12,

	LOG_HANDLER				= 13,
	LOG_ADMIN				= 14,

	LOG_EVENT				= 15

} LogType;

CERVER_PUBLIC void cerver_log_msg (FILE *__restrict __stream, LogType first_type, LogType second_type,
	const char *msg);

// prints a red error message to stderr
CERVER_PUBLIC void cerver_log_error (const char *msg);

// prints a yellow warning message to stderr
CERVER_PUBLIC void cerver_log_warning (const char *msg);

// prints a green success message to stdout
CERVER_PUBLIC void cerver_log_success (const char *msg);

// prints a debug message to stdout
CERVER_PUBLIC void cerver_log_debug (const char *msg);

#endif