#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdarg.h>

#include <ctype.h>

#include "cerver/files.h"

#include "cerver/http/http.h"
#include "cerver/http/multipart.h"

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
		multi_part->n_reads = 0;
		multi_part->total_wrote = 0;

		multi_part->saved_filename_len = 0;
		(void) memset (multi_part->saved_filename, 0, HTTP_MULTI_PART_SAVED_FILENAME_SIZE);

		multi_part->moved_file = false;

		multi_part->value_len = 0;
		(void) memset (multi_part->value, 0, HTTP_MULTI_PART_VALUE_SIZE);
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

		dlist_reset (multi_part->params);

		multi_part->name = NULL;
		// multi_part->filename = NULL;

		multi_part->filename_len = 0;
		(void) memset (multi_part->filename, 0, HTTP_MULTI_PART_FILENAME_SIZE);

		multi_part->generated_filename_len = 0;
		(void) memset (multi_part->generated_filename, 0, HTTP_MULTI_PART_GENERATED_FILENAME_SIZE);

		multi_part->fd = -1;
		multi_part->n_reads = 0;
		multi_part->total_wrote = 0;

		multi_part->saved_filename_len = 0;
		(void) memset (multi_part->saved_filename, 0, HTTP_MULTI_PART_SAVED_FILENAME_SIZE);

		multi_part->moved_file = false;

		multi_part->value_len = 0;
		(void) memset (multi_part->value, 0, HTTP_MULTI_PART_VALUE_SIZE);
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

void http_multi_part_set_generated_filename (
	const MultiPart *multi_part, const char *format, ...
) {

	if (multi_part && format) {
		va_list args;
		va_start (args, format);

		MultiPart *mpart = (MultiPart *) multi_part;

		mpart->generated_filename_len = vsnprintf (
			mpart->generated_filename,
			HTTP_MULTI_PART_GENERATED_FILENAME_SIZE - 1,
			format, args
		);

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

#pragma region parser

#define LF		10
#define CR		13

// static void multipart_log(const char * format, ...) {
// 	#ifdef DEBUG_MULTIPART
// 	va_list args;
// 	va_start(args, format);

// 	fprintf(stderr, "[HTTP_MULTIPART_PARSER] %s:%d: ", __FILE__, __LINE__);
// 	vfprintf(stderr, format, args);
// 	fprintf(stderr, "\n");
// 	#endif
// }

#define NOTIFY_CB(FOR)                                     \
do {                                                       \
	if (p->settings->on_##FOR) {                           \
		if (p->settings->on_##FOR(p) != 0) {               \
			return i;                                      \
		}                                                  \
	}                                                      \
} while (0)

#define EMIT_DATA_CB(FOR, ptr, len)                        \
do {                                                       \
	if (p->settings->on_##FOR) {                           \
		if (p->settings->on_##FOR(p, ptr, len) != 0) {     \
			return i;                                      \
		}                                                  \
	}                                                      \
} while (0)

enum state {
	s_uninitialized = 1,
	s_start,
	s_start_boundary,
	s_header_field_start,
	s_header_field,
	s_headers_almost_done,
	s_header_value_start,
	s_header_value,
	s_header_value_almost_done,
	s_part_data_start,
	s_part_data,
	s_part_data_almost_boundary,
	s_part_data_boundary,
	s_part_data_almost_end,
	s_part_data_end,
	s_part_data_final_hyphen,
	s_end
};

multipart_parser *multipart_parser_init (
	const char *boundary, const multipart_parser_settings *settings
) {

	multipart_parser *p = (multipart_parser *) malloc (
		sizeof (multipart_parser) + strlen (boundary) + strlen (boundary) + 9
	);

	if (p) {
		(void) strcpy (p->multipart_boundary, boundary);
		p->boundary_length = strlen (boundary);

		p->lookbehind = (p->multipart_boundary + p->boundary_length + 1);

		p->index = 0;
		p->state = s_start;
		p->settings = settings;
	}

	return p;

}

void multipart_parser_free (multipart_parser *p) {

	if (p) free (p);

}

size_t multipart_parser_execute (
	multipart_parser *p, const char *buf, size_t len
) {

	size_t i = 0;
	size_t mark = 0;
	char c = 0, cl = 0;
	int is_last = 0;

	while (i < len) {
		c = buf[i];
		is_last = (i == (len - 1));
		switch (p->state) {
			case s_start:
				// multipart_log("s_start");
				p->index = 0;
				p->state = s_start_boundary;

			/* fallthrough */
			case s_start_boundary:
				// multipart_log("s_start_boundary");
				if (p->index == p->boundary_length) {
					if (c != CR) {
						return i;
					}
					p->index++;
					break;
				}

				else if (p->index == (p->boundary_length + 1)) {
					if (c != LF) {
						return i;
					}
					p->index = 0;
					NOTIFY_CB (part_data_begin);
					p->state = s_header_field_start;
					break;
				}

				if (c != p->multipart_boundary[p->index]) {
					return i;
				}
				p->index++;
				break;

			case s_header_field_start:
				// multipart_log("s_header_field_start");
				mark = i;
				p->state = s_header_field;

			/* fallthrough */
			case s_header_field:
				// multipart_log("s_header_field");
				if (c == CR) {
					p->state = s_headers_almost_done;
					break;
				}

				if (c == ':') {
					EMIT_DATA_CB (header_field, buf + mark, i - mark);
					p->state = s_header_value_start;
					break;
				}

				cl = tolower (c);
				if ((c != '-') && (cl < 'a' || cl > 'z')) {
					// multipart_log("invalid character in header name");
					return i;
				}
				if (is_last) EMIT_DATA_CB (header_field, buf + mark, (i - mark) + 1);
				break;

			case s_headers_almost_done:
				// multipart_log("s_headers_almost_done");
				if (c != LF) {
					return i;
				}

				p->state = s_part_data_start;
				break;

			case s_header_value_start:
				// multipart_log("s_header_value_start");
				if (c == ' ') {
					break;
				}

				mark = i;
				p->state = s_header_value;

			/* fallthrough */
			case s_header_value:
				// multipart_log("s_header_value");
				if (c == CR) {
					EMIT_DATA_CB (header_value, buf + mark, i - mark);
					p->state = s_header_value_almost_done;
					break;
				}

				if (is_last) EMIT_DATA_CB (header_value, buf + mark, (i - mark) + 1);
				break;

			case s_header_value_almost_done:
				// multipart_log("s_header_value_almost_done");
				if (c != LF) {
					return i;
				}

				p->state = s_header_field_start;
				break;

			case s_part_data_start:
				// multipart_log("s_part_data_start");
				NOTIFY_CB (headers_complete);
				mark = i;
				p->state = s_part_data;

			/* fallthrough */
			case s_part_data:
				// multipart_log("s_part_data");
				if (c == CR) {
					EMIT_DATA_CB (part_data, buf + mark, i - mark);
					mark = i;
					p->state = s_part_data_almost_boundary;
					p->lookbehind[0] = CR;
					break;
				}
				if (is_last) EMIT_DATA_CB (part_data, buf + mark, (i - mark) + 1);
				break;

			case s_part_data_almost_boundary:
				// multipart_log("s_part_data_almost_boundary");
				if (c == LF) {
					p->state = s_part_data_boundary;
					p->lookbehind[1] = LF;
					p->index = 0;
					break;
				}
				EMIT_DATA_CB (part_data, p->lookbehind, 1);
				p->state = s_part_data;
				mark = i --;
				break;

			case s_part_data_boundary:
				// multipart_log("s_part_data_boundary");
				if (p->multipart_boundary[p->index] != c) {
					EMIT_DATA_CB (part_data, p->lookbehind, 2 + p->index);
					p->state = s_part_data;
					mark = i --;
					break;
				}
				p->lookbehind[2 + p->index] = c;
				if ((++ p->index) == p->boundary_length) {
					NOTIFY_CB (part_data_end);
					p->state = s_part_data_almost_end;
				}
				break;

			case s_part_data_almost_end:
				// multipart_log("s_part_data_almost_end");
				if (c == '-') {
					p->state = s_part_data_final_hyphen;
					break;
				}
				if (c == CR) {
					p->state = s_part_data_end;
					break;
				}
				return i;

			case s_part_data_final_hyphen:
				// multipart_log("s_part_data_final_hyphen");
				if (c == '-') {
					NOTIFY_CB (body_end);
					p->state = s_end;
					break;
				}
				return i;

			case s_part_data_end:
				// multipart_log("s_part_data_end");
				if (c == LF) {
					p->state = s_header_field_start;
					NOTIFY_CB (part_data_begin);
					break;
				}
				return i;

			case s_end:
				// multipart_log ("s_end: %02X", (int) c);
				break;

			default:
				// multipart_log ("Multipart parser unrecoverable error");
				return 0;
		}

		++i;
	}

	return len;

}

#pragma endregion
