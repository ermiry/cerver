#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static char *log_get_msg_type (LogMsgType type) {

	char temp[16] = { 0 };

	switch (type) {
		case LOG_ERROR: strcpy (temp, "[ERROR]"); break;
		case LOG_WARNING: strcpy (temp, "[WARNING]"); break;
		case LOG_SUCCESS: strcpy (temp, "[SUCCESS]"); break;
		case LOG_DEBUG: strcpy (temp, "[DEBUG]"); break;
		case LOG_TEST: strcpy (temp, "[TEST]"); break;

		case LOG_CERVER: strcpy (temp, "[CERVER]"); break;
		case LOG_CLIENT: strcpy (temp, "[CLIENT]"); break;

		case LOG_REQ: strcpy (temp, "[REQ]"); break;
		case LOG_FILE: strcpy (temp, "[FILE]"); break;
		case LOG_PACKET: strcpy (temp, "[PACKET]"); break;
		case LOG_PLAYER: strcpy (temp, "[PLAYER]"); break;
		case LOG_GAME: strcpy (temp, "[GAME]"); break;

		default: break;
	}

	char *retval = (char *) calloc (strlen (temp) + 1, sizeof (temp));
	if (retval) strcpy (retval, temp);

	return retval;

}

void cerver_log_msg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
	const char *msg) {

	char *first = log_get_msg_type (firstType);
	char *second = NULL;
	char *message = NULL;

	if (secondType != 0) {
		second = log_get_msg_type (secondType);

		if (firstType == LOG_DEBUG)
			message = c_string_create ("%s: %s\n", second, msg);
		
		else message = c_string_create ("%s%s: %s\n", first, second, msg);
	}

	else if (firstType != LOG_DEBUG)
		message = c_string_create ("%s: %s\n", first, msg);

	// log messages with color
	switch (firstType) {
		case LOG_DEBUG: 
			fprintf (__stream, COLOR_MAGENTA "%s: " COLOR_RESET "%s\n", first, msg); break;
		
		case LOG_TEST:
			fprintf (__stream, COLOR_CYAN "%s: " COLOR_RESET "%s\n", first, msg); break;

		case LOG_ERROR: fprintf (__stream, COLOR_RED "%s" COLOR_RESET, message); break;
		case LOG_WARNING: fprintf (__stream, COLOR_YELLOW "%s" COLOR_RESET, message); break;
		case LOG_SUCCESS: fprintf (__stream, COLOR_GREEN "%s" COLOR_RESET, message); break;

		case LOG_CERVER: fprintf (__stream, COLOR_BLUE "%s" COLOR_RESET, message); break;

		default: fprintf (__stream, "%s", message); break;
	}

	if (message) free (message);

}

// prints a red error message to stderr
void cerver_log_error (const char *msg) {

	if (msg) fprintf (stderr, COLOR_RED "[ERROR]: " "%s\n" COLOR_RESET, msg);

}

// prints a yellow warning message to stderr
void cerver_log_warning (const char *msg) {

	if (msg) fprintf (stderr, COLOR_YELLOW "[WARNING]: " "%s\n" COLOR_RESET, msg);

}

// prints a green success message to stdout
void cerver_log_success (const char *msg) {

	if (msg) fprintf (stdout, COLOR_GREEN "[SUCCESS]: " "%s\n" COLOR_RESET, msg);

}

// prints a debug message to stdout
void cerver_log_debug (const char *msg) {

	if (msg) fprintf (stdout, COLOR_MAGENTA "[DEBUG]: " COLOR_RESET "%s\n", msg);

}