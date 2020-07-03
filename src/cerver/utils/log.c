#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static char *log_get_msg_type (LogType type) {

	char *retval = (char *) calloc (16, sizeof (char));

	if (retval) {
		switch (type) {
			case LOG_ERROR: strcpy (retval, "[ERROR]"); break;
			case LOG_WARNING: strcpy (retval, "[WARNING]"); break;
			case LOG_SUCCESS: strcpy (retval, "[SUCCESS]"); break;
			case LOG_DEBUG: strcpy (retval, "[DEBUG]"); break;
			case LOG_TEST: strcpy (retval, "[TEST]"); break;

			case LOG_CERVER: strcpy (retval, "[CERVER]"); break;
			case LOG_CLIENT: strcpy (retval, "[CLIENT]"); break;

			case LOG_REQ: strcpy (retval, "[REQ]"); break;
			case LOG_FILE: strcpy (retval, "[FILE]"); break;
			case LOG_PACKET: strcpy (retval, "[PACKET]"); break;
			case LOG_PLAYER: strcpy (retval, "[PLAYER]"); break;
			case LOG_GAME: strcpy (retval, "[GAME]"); break;

			case LOG_HANDLER: strcpy (retval, "[HANDLER]"); break;
			case LOG_ADMIN: strcpy (retval, "[ADMIN]"); break;

			default: break;
		}
	}

	return retval;

}

void cerver_log_msg (FILE *__restrict __stream, LogType first_type, LogType second_type,
	const char *msg) {

	if (__stream && msg) {
		char *first = log_get_msg_type (first_type);
		if (first) {
			char *message = NULL;

			if (second_type != LOG_NO_TYPE) {
				char *second = log_get_msg_type (second_type);
				if (second) {
					switch (first_type) {
						case LOG_DEBUG:
						case LOG_TEST:
							message = c_string_create ("%s: %s\n", second, msg);
							break;

						default: 
							message = c_string_create ("%s%s: %s\n", first, second, msg);
							break;
					}

					free (second);
				}
			}

			else {
				switch (first_type) {
					case LOG_DEBUG:
					case LOG_TEST:
						break;

					default: 
						message = c_string_create ("%s: %s\n", first, msg);
						break;
				}
			}

			if (message) {
				// log messages with color
				switch (first_type) {
					case LOG_DEBUG: 
						if (second_type != LOG_NO_TYPE) fprintf (__stream, LOG_COLOR_MAGENTA "%s" LOG_COLOR_RESET "%s", first, message); 
						else fprintf (__stream, LOG_COLOR_MAGENTA "%s: " LOG_COLOR_RESET "%s", first, msg); 
						break;
					
					case LOG_TEST: 
						if (second_type != LOG_NO_TYPE) fprintf (__stream, LOG_COLOR_CYAN "%s" LOG_COLOR_RESET "%s", first, message); 
						else fprintf (__stream, LOG_COLOR_CYAN "%s: " LOG_COLOR_RESET "%s", first, msg);
						break;

					case LOG_ERROR: fprintf (__stream, LOG_COLOR_RED "%s" LOG_COLOR_RESET, message); break;
					case LOG_WARNING: fprintf (__stream, LOG_COLOR_YELLOW "%s" LOG_COLOR_RESET, message); break;
					case LOG_SUCCESS: fprintf (__stream, LOG_COLOR_GREEN "%s" LOG_COLOR_RESET, message); break;

					case LOG_CERVER: fprintf (__stream, LOG_COLOR_BLUE "%s" LOG_COLOR_RESET, message); break;

					default: fprintf (__stream, "%s", message); break;
				}

				free (message);
			}

			free (first);
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