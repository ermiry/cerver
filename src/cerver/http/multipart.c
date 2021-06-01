#include <stdio.h>
#include <string.h>

#include <stdarg.h>

#include <ctype.h>

#include "cerver/http/http.h"
#include "cerver/http/multipart.h"

#pragma region parts

MultiPart *http_multi_part_new (void) {

	MultiPart *multi_part = (MultiPart *) malloc (sizeof (MultiPart));
	if (multi_part) {
		multi_part->next_header = MULTI_PART_HEADER_INVALID;
		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++)
			multi_part->headers[i] = NULL;

		multi_part->params = dlist_init (key_value_pair_delete, NULL);

		multi_part->name = NULL;
		// multi_part->filename = NULL;

		multi_part->filename_len = 0;
		(void) memset (multi_part->filename, 0, HTTP_MULTI_PART_FILENAME_SIZE);

		multi_part->generated_filename_len = 0;
		(void) memset (multi_part->generated_filename, 0, HTTP_MULTI_PART_GENERATED_FILENAME_SIZE);

		multi_part->fd = -1;
		multi_part->saved_filename_len = 0;
		(void) memset (multi_part->saved_filename, 0, HTTP_MULTI_PART_SAVED_FILENAME_SIZE);
		multi_part->n_reads = 0;
		multi_part->total_wrote = 0;

		multi_part->value = NULL;
	}

	return multi_part;

}

void http_multi_part_delete (void *multi_part_ptr) {

	if (multi_part_ptr) {
		MultiPart *multi_part = (MultiPart *) multi_part_ptr;

		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++)
			str_delete (multi_part->headers[i]);

		dlist_delete (multi_part->params);

		str_delete (multi_part->value);

		free (multi_part_ptr);
	}

}

void http_multi_part_headers_print (const MultiPart *mpart) {

	if (mpart) {
		const char *null = "NULL";
		String *header = NULL;
		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++) {
			header = mpart->headers[i];

			switch (i) {
				case MULTI_PART_HEADER_CONTENT_DISPOSITION:
					cerver_log_msg ("Content-Disposition: %s", header ? header->str : null);
					break;
				case MULTI_PART_HEADER_CONTENT_LENGTH:
					cerver_log_msg ("Content-Length: %s", header ? header->str : null);
					break;
				case MULTI_PART_HEADER_CONTENT_TYPE:
					cerver_log_msg ("Content-Type: %s", header ? header->str : null);
					break;

				default: break;
			}
		}
	}

}

void http_multi_part_print (const MultiPart *mpart) {

	if (mpart) {
		if (mpart->filename_len) {
			(void) printf (
				"FILE: %s - %s -> %s\n",
				mpart->name->str, mpart->filename, mpart->saved_filename
			);
		}

		else {
			(void) printf (
				"VALUE: %s - %s\n",
				mpart->name->str, mpart->value->str
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
