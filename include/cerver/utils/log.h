#ifndef _CERVER_UTILS_LOG_H_
#define _CERVER_UTILS_LOG_H_

#include <stdio.h>
#include <stdbool.h>

#include "cerver/config.h"

#define LOG_DEFAULT_PATH		"/var/log/cerver"

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

#define LOG_DEFAULT_UPDATE_INTERVAL			1

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
	XX(8, 	CONNECTION, [CONNECTION])			\
	XX(9, 	HANDLER, 	[HANDLER])				\
	XX(10, 	ADMIN, 		[ADMIN])				\
	XX(11, 	EVENT, 		[EVENT])				\
	XX(12, 	PACKET, 	[PACKET])				\
	XX(13, 	REQ, 		[REQ])					\
	XX(14, 	FILE, 		[FILE])					\
	XX(15, 	HTTP, 		[HTTP])					\
	XX(16, 	GAME, 		[GAME])					\
	XX(17, 	PLAYER, 	[PLAYER)				\

typedef enum LogType {

	#define XX(num, name, string) LOG_TYPE_##name = num,
	LOG_TYPE_MAP (XX)
	#undef XX
	
} LogType;

#pragma endregion

#pragma region configuration

typedef enum LogOutputType {

	LOG_OUTPUT_TYPE_NONE		= 0,
	LOG_OUTPUT_TYPE_STD			= 1,	// log to stdout & stderr
	LOG_OUTPUT_TYPE_FILE		= 2,	// log to a file
	LOG_OUTPUT_TYPE_BOTH		= 3		// log to both std output and file

} LogOutputType;

// returns the current log output type
CERVER_EXPORT LogOutputType cerver_log_get_output_type (void);

// sets the log output type to use
CERVER_EXPORT void cerver_log_set_output_type (LogOutputType type);

// sets the path where logs files will be stored
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int cerver_log_set_path (const char *pathname);

// sets the interval in secs which will be used to sync the contents of the log file to disk
// the default values is 1 second
CERVER_EXPORT void cerver_log_set_update_interval (unsigned int interval);

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

// creates and prints a message of custom types
// based on the first type, the message can be printed with colors to stdout
CERVER_PUBLIC void cerver_log (
	LogType first_type, LogType second_type,
	const char *format, ...
);

// prints a message with no type, effectively making this a custom printf ()
CERVER_PUBLIC void cerver_log_msg (const char *msg, ...);

// prints a red error message to stderr
CERVER_PUBLIC void cerver_log_error (const char *msg, ...);

// prints a yellow warning message to stderr
CERVER_PUBLIC void cerver_log_warning (const char *msg, ...);

// prints a green success message to stdout
CERVER_PUBLIC void cerver_log_success (const char *msg, ...);

// prints a debug message to stdout
CERVER_PUBLIC void cerver_log_debug (const char *msg, ...);

// prints a line break, equivalent to printf ("\n")
CERVER_PUBLIC void cerver_log_line_break (void);

#pragma endregion

#pragma region main

CERVER_PRIVATE void cerver_log_init (void);

CERVER_PRIVATE void cerver_log_end (void);

#pragma endregion

#endif