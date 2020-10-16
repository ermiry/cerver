#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

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

#define LOG_HEADER_SIZE			32
#define LOG_MESSAGE_SIZE		4096

typedef struct {

	char header[LOG_HEADER_SIZE];
	char message[LOG_MESSAGE_SIZE];

} CerverLog;

static CerverLog *cerver_log_new (void) {

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

#pragma endregion

void cerver_log (
	LogType first_type, LogType second_type,
	const char *format, ...
) {

	if (format) {
		va_list args = { 0 };
		va_start (args, format);

		CerverLog *log = cerver_log_new ();
		cerver_log_header_create (log->header, first_type, second_type);
		(void) vsnprintf (log->message, LOG_MESSAGE_SIZE, format, args);

		switch (first_type) {
			case LOG_TYPE_DEBUG: fprintf (stdout, LOG_COLOR_MAGENTA "%s: " LOG_COLOR_RESET "%s\n", log->header, log->message); break;
			
			case LOG_TYPE_TEST: fprintf (stdout, LOG_COLOR_CYAN "%s: " LOG_COLOR_RESET "%s\n", log->header, log->message); break;

			case LOG_TYPE_ERROR: fprintf (stdout, LOG_COLOR_RED "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;
			case LOG_TYPE_WARNING: fprintf (stdout, LOG_COLOR_YELLOW "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;
			case LOG_TYPE_SUCCESS: fprintf (stdout, LOG_COLOR_GREEN "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;

			case LOG_TYPE_CERVER: fprintf (stdout, LOG_COLOR_BLUE "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;

			case LOG_TYPE_EVENT: fprintf (stdout, LOG_COLOR_MAGENTA "%s: %s\n" LOG_COLOR_RESET, log->header, log->message); break;

			default: fprintf (stdout, "%s: %s\n", log->header, log->message); break;
		}

		cerver_log_delete (log);

		va_end (args);
	}

}

void cerver_log_msg (
	FILE *__restrict __stream, 
	LogType first_type, LogType second_type,
	const char *msg
) {

	if (__stream && msg) {
		const char *first = log_get_msg_type (first_type);
		if (first) {
			char *message = NULL;

			if (second_type != LOG_TYPE_NONE) {
				const char *second = log_get_msg_type (second_type);
				if (second) {
					switch (first_type) {
						case LOG_TYPE_DEBUG:
						case LOG_TYPE_TEST:
							message = c_string_create ("%s: %s\n", second, msg);
							break;

						default: 
							message = c_string_create ("%s%s: %s\n", first, second, msg);
							break;
					}
				}
			}

			else {
				switch (first_type) {
					case LOG_TYPE_DEBUG:
					case LOG_TYPE_TEST:
						break;

					default: 
						message = c_string_create ("%s: %s\n", first, msg);
						break;
				}
			}

			if (message) {
				switch (first_type) {
					case LOG_TYPE_DEBUG: fprintf (__stream, LOG_COLOR_MAGENTA "%s" LOG_COLOR_RESET "%s", first, message); break;
					
					case LOG_TYPE_TEST: fprintf (__stream, LOG_COLOR_CYAN "%s" LOG_COLOR_RESET "%s", first, message); break;

					case LOG_TYPE_ERROR: fprintf (__stream, LOG_COLOR_RED "%s" LOG_COLOR_RESET, message); break;
					case LOG_TYPE_WARNING: fprintf (__stream, LOG_COLOR_YELLOW "%s" LOG_COLOR_RESET, message); break;
					case LOG_TYPE_SUCCESS: fprintf (__stream, LOG_COLOR_GREEN "%s" LOG_COLOR_RESET, message); break;

					case LOG_TYPE_CERVER: fprintf (__stream, LOG_COLOR_BLUE "%s" LOG_COLOR_RESET, message); break;

					case LOG_TYPE_EVENT: fprintf (__stream, LOG_COLOR_MAGENTA "%s" LOG_COLOR_RESET, message); break;

					default: fprintf (__stream, "%s", message); break;
				}

				free (message);
			}

			else {
				switch (first_type) {
					case LOG_TYPE_DEBUG: 
						fprintf (__stream, LOG_COLOR_MAGENTA "%s: " LOG_COLOR_RESET "%s\n", first, msg); 
						break;
					
					case LOG_TYPE_TEST: 
						fprintf (__stream, LOG_COLOR_CYAN "%s: " LOG_COLOR_RESET "%s\n", first, msg);
						break;

					default: break;
				}
			}
		}
	}

}

// prints a red error message to stderr
void cerver_log_error (const char *msg) {

	if (msg) fprintf (stderr, LOG_COLOR_RED "[ERROR]: " "%s\n" LOG_COLOR_RESET, msg);

}

// prints a yellow warning message to stderr
void cerver_log_warning (const char *msg) {

	if (msg) fprintf (stderr, LOG_COLOR_YELLOW "[WARNING]: " "%s\n" LOG_COLOR_RESET, msg);

}

// prints a green success message to stdout
void cerver_log_success (const char *msg) {

	if (msg) fprintf (stdout, LOG_COLOR_GREEN "[SUCCESS]: " "%s\n" LOG_COLOR_RESET, msg);

}

// prints a debug message to stdout
void cerver_log_debug (const char *msg) {

	if (msg) fprintf (stdout, LOG_COLOR_MAGENTA "[DEBUG]: " LOG_COLOR_RESET "%s\n", msg);

}