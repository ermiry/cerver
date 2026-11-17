#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "cerver/files/files.h"

#include "cerver/http/core/http.h"
#include "cerver/http/core/pairs.h"
#include "cerver/http/headers/header.h"
#include "cerver/http/requests/multipart.h"

#pragma region parts

void *http_multi_part_new (void) {

	MultiPart *multi_part = (MultiPart *) malloc (sizeof (MultiPart));
	if (multi_part) {
		multi_part->type = MULTI_PART_TYPE_NONE;

		multi_part->next_header = MULTI_PART_HEADER_INVALID;

		(void) memset (
			multi_part->headers, 0, sizeof (HttpHeader) * MULTI_PART_HEADERS_SIZE
		);

		(void) memset (&multi_part->temp_header, 0, sizeof (HttpHeader));

		multi_part->params = dlist_init (key_value_pair_delete, NULL);

		multi_part->name = NULL;
		// multi_part->filename = NULL;

		multi_part->filename_len = 0;
		(void) memset (multi_part->filename, 0, HTTP_MULTI_PART_FILENAME_SIZE);

		multi_part->generated_filename_len = 0;
		(void) memset (multi_part->generated_filename, 0, HTTP_MULTI_PART_GENERATED_FILENAME_SIZE);

		multi_part->fd = -1;
		multi_part->discard = false;
		multi_part->n_reads = 0;
		multi_part->total_wrote = 0;

		multi_part->saved_filename_len = 0;
		(void) memset (multi_part->saved_filename, 0, HTTP_MULTI_PART_SAVED_FILENAME_SIZE);

		multi_part->moved_file = false;

		multi_part->value_len = 0;
		(void) memset (multi_part->value, 0, HTTP_MULTI_PART_VALUE_SIZE);

		multi_part->error = 0;
	}

	return multi_part;

}

void http_multi_part_delete (void *multi_part_ptr) {

	if (multi_part_ptr) {
		MultiPart *multi_part = (MultiPart *) multi_part_ptr;

		dlist_delete (multi_part->params);

		free (multi_part_ptr);
	}

}

void http_multi_part_reset (MultiPart *multi_part) {

	if (multi_part) {
		multi_part->type = MULTI_PART_TYPE_NONE;

		multi_part->next_header = MULTI_PART_HEADER_INVALID;

		(void) memset (
			multi_part->headers, 0, sizeof (HttpHeader) * MULTI_PART_HEADERS_SIZE
		);

		(void) memset (&multi_part->temp_header, 0, sizeof (HttpHeader));

		dlist_reset (multi_part->params);

		multi_part->name = NULL;
		// multi_part->filename = NULL;

		multi_part->filename_len = 0;
		(void) memset (multi_part->filename, 0, HTTP_MULTI_PART_FILENAME_SIZE);

		multi_part->generated_filename_len = 0;
		(void) memset (multi_part->generated_filename, 0, HTTP_MULTI_PART_GENERATED_FILENAME_SIZE);

		// a request that was rejected while receiving a file leaves it open
		if (multi_part->fd != -1) (void) close (multi_part->fd);

		multi_part->fd = -1;
		multi_part->discard = false;
		multi_part->n_reads = 0;
		multi_part->total_wrote = 0;

		multi_part->saved_filename_len = 0;
		(void) memset (multi_part->saved_filename, 0, HTTP_MULTI_PART_SAVED_FILENAME_SIZE);

		multi_part->moved_file = false;

		multi_part->value_len = 0;
		(void) memset (multi_part->value, 0, HTTP_MULTI_PART_VALUE_SIZE);

		multi_part->error = 0;
	}

}

static bool http_multi_part_name_is_not_empty (const MultiPart *multi_part) {

	bool result = false;

	if (multi_part->name) {
		if (multi_part->name->str && strlen (multi_part->name->str)) {
			result = true;
		}
	}

	return result;

}

bool http_multi_part_is_not_empty (const MultiPart *multi_part) {

	bool result = false;

	switch (multi_part->type) {
		case MULTI_PART_TYPE_FILE: {
			result |= http_multi_part_name_is_not_empty (multi_part);

			result |= (multi_part->filename_len && strlen (multi_part->filename));

			result |= (multi_part->saved_filename_len && strlen (multi_part->saved_filename));
		} break;

		case MULTI_PART_TYPE_VALUE: {
			result |= http_multi_part_name_is_not_empty (multi_part);

			result |= (multi_part->value_len && strlen (multi_part->value));
		} break;

		default: break;
	}

	return result;

}

const MultiPartType http_multi_part_get_type (
	const MultiPart *multi_part
) {

	return multi_part->type;

}

bool http_multi_part_is_file (
	const MultiPart *multi_part
) {

	return (multi_part->type == MULTI_PART_TYPE_FILE);

}

bool http_multi_part_is_value (
	const MultiPart *multi_part
) {

	return (multi_part->type == MULTI_PART_TYPE_VALUE);

}

const String *http_multi_part_get_name (
	const MultiPart *multi_part
) {

	return multi_part->name;

}

const char *http_multi_part_get_filename (
	const MultiPart *multi_part
) {

	return multi_part->filename;

}

const int http_multi_part_get_filename_len (
	const MultiPart *multi_part
) {

	return multi_part->filename_len;

}

const char *http_multi_part_get_generated_filename (
	const MultiPart *multi_part
) {

	return multi_part->generated_filename;

}

const int http_multi_part_get_generated_filename_len (
	const MultiPart *multi_part
) {

	return multi_part->generated_filename_len;

}

// a filename that does not fit is truncated, its length is the one that was kept,
// and a format that fails leaves it empty
void http_multi_part_set_generated_filename (
	const MultiPart *multi_part, const char *format, ...
) {

	if (multi_part && format) {
		va_list args;
		va_start (args, format);

		MultiPart *mpart = (MultiPart *) multi_part;

		if (vsnprintf (
			mpart->generated_filename,
			HTTP_MULTI_PART_GENERATED_FILENAME_SIZE - 1,
			format, args
		) < 0) {
			mpart->generated_filename[0] = '\0';
		}

		mpart->generated_filename_len = (int) strlen (mpart->generated_filename);

		va_end (args);
	}

}

const u32 http_multi_part_get_n_reads (
	const MultiPart *multi_part
) {

	return multi_part->n_reads;

}

const u32 http_multi_part_get_total_wrote (
	const MultiPart *multi_part
) {

	return multi_part->total_wrote;

}

const int http_multi_part_get_error (
	const MultiPart *multi_part
) {

	return multi_part->error;

}

const char *http_multi_part_get_saved_filename (
	const MultiPart *multi_part
) {

	return multi_part->saved_filename;

}

const int http_multi_part_get_saved_filename_len (
	const MultiPart *multi_part
) {

	return multi_part->saved_filename_len;

}

const bool http_multi_part_get_moved_file (
	const MultiPart *multi_part
) {

	return multi_part->moved_file;

}

const char *http_multi_part_get_value (
	const MultiPart *multi_part
) {

	return multi_part->value;

}

const int http_multi_part_get_value_len (
	const MultiPart *multi_part
) {

	return multi_part->value_len;

}

// moves multi-part's saved file to the destination path
// returns 0 on success, 1 on error
unsigned int http_multi_part_move_file (
	MultiPart *mpart, const char *destination_path
) {

	unsigned int retval = 1;

	if (mpart && destination_path) {
		if (!file_move_to (mpart->saved_filename, destination_path)) {
			mpart->moved_file = true;

			retval = 0;
		}
	}

	return retval;

}

// works like http_multi_part_move_file ()
// but with the ability to generate dest on the fly
unsigned int http_multi_part_move_file_generate_destination (
	MultiPart *mpart, const char *format, ...
) {

	unsigned int retval = 1;

	if (mpart && format) {
		va_list args;
		va_start (args, format);

		char destination[HTTP_MULTI_PART_GENERATED_DESTINATION_SIZE] = { 0 };
		(void) vsnprintf (
			destination,
			HTTP_MULTI_PART_GENERATED_DESTINATION_SIZE - 1,
			format, args
		);

		if (!file_move_to (mpart->saved_filename, destination)) {
			mpart->moved_file = true;

			retval = 0;
		}

		va_end (args);
	}

	return retval;

}

void http_multi_part_headers_print (const MultiPart *mpart) {

	if (mpart) {
		const char *null = "NULL";
		const HttpHeader *header = NULL;
		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++) {
			header = &mpart->headers[i];

			switch (i) {
				case MULTI_PART_HEADER_CONTENT_DISPOSITION:
					cerver_log_msg ("Content-Disposition: %s", (header->len > 0) ? header->value : null);
					break;
				case MULTI_PART_HEADER_CONTENT_LENGTH:
					cerver_log_msg ("Content-Length: %s", (header->len > 0) ? header->value : null);
					break;
				case MULTI_PART_HEADER_CONTENT_TYPE:
					cerver_log_msg ("Content-Type: %s", (header->len > 0) ? header->value : null);
					break;

				default: break;
			}
		}
	}

}

void http_multi_part_print (const MultiPart *mpart) {

	if (mpart) {
		if (mpart->type == MULTI_PART_TYPE_FILE) {
			(void) printf (
				"FILE: %s - %s -> %s\n",
				mpart->name->str, mpart->filename, mpart->saved_filename
			);
		}

		else if (mpart->type == MULTI_PART_TYPE_VALUE) {
			(void) printf (
				"VALUE: %s - %s\n",
				mpart->name->str, mpart->value
			);
		}
	}

}

#pragma endregion
