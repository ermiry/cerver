#ifndef _CERVER_UTILS_LOG_H_
#define _CERVER_UTILS_LOG_H_

#include <stdio.h>
#include <stdbool.h>

#include "cerver/config.h"

#define LOG_POOL_INIT			32

#define LOG_DATETIME_SIZE		32
#define LOG_HEADER_SIZE			32
#define LOG_HEADER_HALF_SIZE	LOG_HEADER_SIZE / 2

#define LOG_MESSAGE_SIZE		4096

#define LOG_COLOR_RED       "\x1b[31m"
#define LOG_COLOR_GREEN     "\x1b[32m"
#define LOG_COLOR_YELLOW    "\x1b[33m"
#define LOG_COLOR_BLUE      "\x1b[34m"
#define LOG_COLOR_MAGENTA   "\x1b[35m"
#define LOG_COLOR_CYAN      "\x1b[36m"
#define LOG_COLOR_RESET     "\x1b[0m"

#pragma region types

#define LOG_TYPE_MAP(XX)						\
	XX(0, 	NONE, 		[NONE])					\
	XX(1, 	ERROR, 		[ERROR])				\
	XX(2, 	WARNING, 	[WARNING])				\
	XX(3, 	SUCCESS, 	[SUCCESS])				\
	XX(4, 	DEBUG, 		[DEBUG])				\
	XX(5, 	TEST, 		[TEST])					\
	XX(6, 	CERVER, 	[CERVER])				\
	XX(7, 	CLIENT, 	[CLIENT])				\
	XX(8, 	HANDLER, 	[HANDLER])				\
	XX(9, 	ADMIN, 		[ADMIN])				\
	XX(10, 	EVENT, 		[EVENT])				\
	XX(11, 	PACKET, 	[PACKET])				\
	XX(12, 	REQ, 		[REQ])					\
	XX(13, 	FILE, 		[FILE])					\
	XX(14, 	HTTP, 		[HTTP])					\
	XX(15, 	GAME, 		[GAME])					\
	XX(16, 	PLAYER, 	[PLAYER)				\

typedef enum LogType {

	#define XX(num, name, string) LOG_TYPE_##name = num,
	LOG_TYPE_MAP (XX)
	#undef XX
	
} LogType;

#pragma endregion

#pragma region configuration

#define LOG_TIME_TYPE_MAP(XX)										\
	XX(0, 	NONE, 		None,		Logs without time)				\
	XX(1, 	TIME, 		Time,		Logs with time)					\
	XX(2, 	DATE, 		Date,		Logs with date)					\
	XX(3, 	BOTH, 		Both,		Logs with date and time)

typedef enum LogTimeType {

	#define XX(num, name, string, description) LOG_TIME_TYPE_##name = num,
	LOG_TIME_TYPE_MAP (XX)
	#undef XX

} LogTimeType;

CERVER_PUBLIC const char *cerver_log_time_type_to_string (LogTimeType type);

CERVER_PUBLIC const char *cerver_log_time_type_description (LogTimeType type);

// returns the current log time configuration
CERVER_EXPORT LogTimeType cerver_log_get_time_config (void);

// sets the log time configuration to be used by log methods
// none: print logs with no dates
// time: 24h time format
// date: day/month/year format
// both: day/month/year - 24h date time format
CERVER_EXPORT void cerver_log_set_time_config (LogTimeType type);

// set if logs datetimes will use local time or not
CERVER_EXPORT void cerver_log_set_local_time (bool value);

#pragma endregion

#pragma region public

CERVER_PUBLIC void cerver_log (
	LogType first_type, LogType second_type,
	const char *format, ...
);

CERVER_PUBLIC void cerver_log_msg (
	FILE *__restrict __stream, 
	LogType first_type, LogType second_type,
	const char *msg
);

// prints a red error message to stderr
CERVER_PUBLIC void cerver_log_error (const char *msg, ...);

// prints a yellow warning message to stderr
CERVER_PUBLIC void cerver_log_warning (const char *msg, ...);

// prints a green success message to stdout
CERVER_PUBLIC void cerver_log_success (const char *msg, ...);

// prints a debug message to stdout
CERVER_PUBLIC void cerver_log_debug (const char *msg, ...);

#pragma endregion

#pragma region main

CERVER_PRIVATE void cerver_log_init (void);

CERVER_PRIVATE void cerver_log_end (void);

#pragma endregion

#endif