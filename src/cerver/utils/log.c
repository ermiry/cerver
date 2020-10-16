#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>

#include "cerver/collections/pool.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static Pool *log_pool = NULL;

#pragma region types

static const char *log_get_msg_type (LogType type) {

	switch (type) {
		#define XX(num, name, string) case LOG_TYPE_##name: return #string;
		LOG_TYPE_MAP(XX)
		#undef XX

		default: return log_get_msg_type (LOG_TYPE_NONE);
	}

}

#pragma endregion

#pragma region internal

typedef struct {

	char header[LOG_HEADER_SIZE];
	char message[LOG_MESSAGE_SIZE];

} CerverLog;

static void *cerver_log_new (void) {

	CerverLog *log = (CerverLog *) malloc (sizeof (CerverLog));
	if (log) {
		memset (log->header, 0, LOG_HEADER_SIZE);
		memset (log->message, 0, LOG_MESSAGE_SIZE);
	}

	return log;

}

static void cerver_log_delete (void *cerver_log_ptr) {

	if (cerver_log_ptr) free (cerver_log_ptr);

}

static void cerver_log_header_create (
	char *header,
	LogType first_type, LogType second_type
) {

	const char *first = log_get_msg_type (first_type);
	if (second_type != LOG_TYPE_NONE) {
		(void) snprintf (
			header, LOG_HEADER_SIZE, 
			"%s%s", 
			first, log_get_msg_type (second_type)
		);
	}

	else {
		(void) snprintf (
			header, LOG_HEADER_SIZE,
			"%s",
			first
		);
	}

}

static FILE *cerver_log_get_stream (LogType first_type) {

	FILE *retval = stdout;

	switch (first_type) {
		case LOG_TYPE_ERROR:
		case LOG_TYPE_WARNING:
			retval = stderr;
			break;

		default: break;
	}

	return retval;

}

static void cerver_log_internal (
	FILE *__restrict __stream,
	LogType first_type, LogType second_type,
	const char *format, va_list args
) {

	CerverLog *log = (CerverLog *) pool_pop (log_pool);
	if (log) {
		cerver_log_header_create (log->header, first_type, second_type);
		(void) vsnprintf (log->message, LOG_MESSAGE_SIZE, format, args);

		switch (first_type) {
			case LOG_TYPE_DEBUG: fprintf (__stream, LOG_COLOR_MAGENTA "%s: " LOG_COLOR_RESET "%s\n", log->header, log->message); break;
			
			case LOG_TYPE_TEST: fprintf (__stream, LOG_COLOR_CYAN "%s: " LOG_COLOR_RESET "%s\n", log->header, log->message); break;

			case LOG_TYPE_ERROR: fprintf (__stream, LOG_COLOR_RED "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;
			case LOG_TYPE_WARNING: fprintf (__stream, LOG_COLOR_YELLOW "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;
			case LOG_TYPE_SUCCESS: fprintf (__stream, LOG_COLOR_GREEN "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;

			case LOG_TYPE_CERVER: fprintf (__stream, LOG_COLOR_BLUE "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;

			case LOG_TYPE_EVENT: fprintf (__stream, LOG_COLOR_MAGENTA "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;

			default: fprintf (__stream, "%s: %s\n", log->header, log->message); break;
		}

		pool_push (log_pool, log);
	}

}

#pragma endregion

#pragma region public

void cerver_log (
	LogType first_type, LogType second_type,
	const char *format, ...
) {

	if (format) {
		va_list args = { 0 };
		va_start (args, format);

		cerver_log_internal (
			cerver_log_get_stream (first_type),
			first_type, second_type,
			format, args
		);

		va_end (args);
	}

}

void cerver_log_msg (
	FILE *__restrict __stream, 
	LogType first_type, LogType second_type,
	const char *msg
) {

	if (__stream && msg) {
		va_list args = { 0 };

		cerver_log_internal (
			__stream,
			first_type, second_type,
			msg, args
		);
	}

}

// prints a red error message to stderr
void cerver_log_error (const char *msg, ...) {

	if (msg) {
		va_list args = { 0 };
		va_start (args, msg);

		cerver_log_internal (
			stderr,
			LOG_TYPE_ERROR, LOG_TYPE_NONE,
			msg, args
		);

		va_end (args);
	}

}

// prints a yellow warning message to stderr
void cerver_log_warning (const char *msg, ...) {

	if (msg) {
		va_list args = { 0 };
		va_start (args, msg);

		cerver_log_internal (
			stderr,
			LOG_TYPE_WARNING, LOG_TYPE_NONE,
			msg, args
		);

		va_end (args);
	}

}

// prints a green success message to stdout
void cerver_log_success (const char *msg, ...) {

	if (msg) {
		va_list args = { 0 };
		va_start (args, msg);

		cerver_log_internal (
			stdout,
			LOG_TYPE_SUCCESS, LOG_TYPE_NONE,
			msg, args
		);

		va_end (args);
	}

}

// prints a debug message to stdout
void cerver_log_debug (const char *msg, ...) {

	if (msg) {
		va_list args = { 0 };
		va_start (args, msg);

		cerver_log_internal (
			stdout,
			LOG_TYPE_DEBUG, LOG_TYPE_NONE,
			msg, args
		);

		va_end (args);
	}

}

#pragma endregion

#pragma region main

void cerver_log_init (void) {

	if (!log_pool) {
		log_pool = pool_create (cerver_log_delete);
		pool_init (log_pool, cerver_log_new, LOG_POOL_INIT);
	}

}

void cerver_log_end (void) {

	pool_delete (log_pool);
	log_pool = NULL;

}

#pragma endregion