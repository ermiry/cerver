#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/http/parser/multipart.h"

#define HTTP_MULTIPART_CR				'\r'
#define HTTP_MULTIPART_LF				'\n'
#define HTTP_MULTIPART_COLON			':'
#define HTTP_MULTIPART_HYPHEN			'-'
#define HTTP_MULTIPART_SPACE			' '

// the bit that makes an ASCII letter a lower case one
#define HTTP_MULTIPART_LOWER_BIT		0x20

// the CRLF before the boundary that ends the data of a part
#define HTTP_MULTIPART_LINE_BREAK		2

// where a parser is
typedef enum HttpMultipartState {

	HTTP_MULTIPART_STATE_START					= 1,
	HTTP_MULTIPART_STATE_START_BOUNDARY			= 2,
	HTTP_MULTIPART_STATE_FIELD_START			= 3,
	HTTP_MULTIPART_STATE_FIELD					= 4,
	HTTP_MULTIPART_STATE_HEADERS_ALMOST_DONE	= 5,
	HTTP_MULTIPART_STATE_VALUE_START			= 6,
	HTTP_MULTIPART_STATE_VALUE					= 7,
	HTTP_MULTIPART_STATE_VALUE_ALMOST_DONE		= 8,
	HTTP_MULTIPART_STATE_DATA_START				= 9,
	HTTP_MULTIPART_STATE_DATA					= 10,
	HTTP_MULTIPART_STATE_DATA_ALMOST_BOUNDARY	= 11,
	HTTP_MULTIPART_STATE_DATA_BOUNDARY			= 12,
	HTTP_MULTIPART_STATE_DATA_ALMOST_END		= 13,
	HTTP_MULTIPART_STATE_DATA_END				= 14,
	HTTP_MULTIPART_STATE_FINAL_HYPHEN			= 15,
	HTTP_MULTIPART_STATE_END					= 16

} HttpMultipartState;

// what a byte did: the parser goes to the next one, it reads the same one again, or it stops
typedef enum HttpMultipartStep {

	HTTP_MULTIPART_STEP_NEXT	= 0,
	HTTP_MULTIPART_STEP_AGAIN	= 1,
	HTTP_MULTIPART_STEP_STOP	= 2

} HttpMultipartStep;

bool cerver_http_multipart_init (
	CerverHttpMultipart *multipart,
	const char *boundary, const size_t length,
	const CerverHttpMultipartCallbacks *callbacks, void *data
) {

	bool initialized = false;

	if (multipart && boundary && length && (length < CERVER_HTTP_MULTIPART_BOUNDARY_SIZE)) {
		(void) memset (multipart, 0, sizeof (CerverHttpMultipart));

		(void) memcpy (multipart->boundary, boundary, length);
		multipart->boundary_length = length;

		multipart->state = HTTP_MULTIPART_STATE_START;
		multipart->callbacks = callbacks;
		multipart->data = data;

		initialized = true;
	}

	return initialized;

}

bool cerver_http_multipart_complete (const CerverHttpMultipart *multipart) {

	return multipart->state == HTTP_MULTIPART_STATE_END;

}

#pragma region callbacks

static inline bool http_multipart_data (
	CerverHttpMultipart *multipart, const CerverHttpMultipartData callback,
	const char *at, const size_t length
) {

	return !callback || !callback (multipart, at, length);

}

static inline bool http_multipart_event (
	CerverHttpMultipart *multipart, const CerverHttpMultipartEvent callback
) {

	return !callback || !callback (multipart);

}

#pragma endregion

#pragma region states

// the boundary that starts the body, and the CRLF after it
static HttpMultipartStep http_multipart_start_boundary (CerverHttpMultipart *multipart, const char byte) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	if (multipart->index == multipart->boundary_length) {
		if (byte == HTTP_MULTIPART_CR) multipart->index += 1;
		else step = HTTP_MULTIPART_STEP_STOP;
	}

	else if (multipart->index == (multipart->boundary_length + 1)) {
		if (byte != HTTP_MULTIPART_LF) {
			step = HTTP_MULTIPART_STEP_STOP;
		}

		else {
			multipart->index = 0;
			if (!http_multipart_event (multipart, multipart->callbacks->on_part_data_begin)) step = HTTP_MULTIPART_STEP_STOP;
			multipart->state = HTTP_MULTIPART_STATE_FIELD_START;
		}
	}

	else if (byte != multipart->boundary[multipart->index]) {
		step = HTTP_MULTIPART_STEP_STOP;
	}

	else {
		multipart->index += 1;
	}

	return step;

}

// the name of a header has only letters and '-'
static inline bool http_multipart_field_byte (const char byte) {

	const char lower = (char) (byte | HTTP_MULTIPART_LOWER_BIT);

	return (byte == HTTP_MULTIPART_HYPHEN) || ((lower >= 'a') && (lower <= 'z'));

}

static HttpMultipartStep http_multipart_field (
	CerverHttpMultipart *multipart, const char *buffer, const size_t i, const size_t mark, const bool last
) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;
	const char byte = buffer[i];

	if (byte == HTTP_MULTIPART_CR) {
		multipart->state = HTTP_MULTIPART_STATE_HEADERS_ALMOST_DONE;
	}

	else if (byte == HTTP_MULTIPART_COLON) {
		if (!http_multipart_data (multipart, multipart->callbacks->on_header_field, buffer + mark, i - mark)) {
			step = HTTP_MULTIPART_STEP_STOP;
		}

		else {
			multipart->state = HTTP_MULTIPART_STATE_VALUE_START;
		}
	}

	else if (!http_multipart_field_byte (byte)) {
		step = HTTP_MULTIPART_STEP_STOP;
	}

	else if (last && !http_multipart_data (multipart, multipart->callbacks->on_header_field, buffer + mark, (i - mark) + 1)) {
		step = HTTP_MULTIPART_STEP_STOP;
	}

	return step;

}

static HttpMultipartStep http_multipart_value (
	CerverHttpMultipart *multipart, const char *buffer, const size_t i, const size_t mark, const bool last
) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	if (buffer[i] == HTTP_MULTIPART_CR) {
		if (!http_multipart_data (multipart, multipart->callbacks->on_header_value, buffer + mark, i - mark)) {
			step = HTTP_MULTIPART_STEP_STOP;
		}

		else {
			multipart->state = HTTP_MULTIPART_STATE_VALUE_ALMOST_DONE;
		}
	}

	else if (last && !http_multipart_data (multipart, multipart->callbacks->on_header_value, buffer + mark, (i - mark) + 1)) {
		step = HTTP_MULTIPART_STEP_STOP;
	}

	return step;

}

// a LF after a CR, or the parser stops
static inline HttpMultipartStep http_multipart_line_feed (
	CerverHttpMultipart *multipart, const char byte, const HttpMultipartState next
) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_STOP;

	if (byte == HTTP_MULTIPART_LF) {
		multipart->state = next;
		step = HTTP_MULTIPART_STEP_NEXT;
	}

	return step;

}

// the CR that can start the line break before a boundary
static HttpMultipartStep http_multipart_data_almost_boundary (
	CerverHttpMultipart *multipart, const char byte, size_t *mark, const size_t i
) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	if (byte == HTTP_MULTIPART_LF) {
		multipart->state = HTTP_MULTIPART_STATE_DATA_BOUNDARY;
		multipart->lookbehind[1] = HTTP_MULTIPART_LF;
		multipart->index = 0;
	}

	// the CR was data
	else if (!http_multipart_data (multipart, multipart->callbacks->on_part_data, multipart->lookbehind, 1)) {
		step = HTTP_MULTIPART_STEP_STOP;
	}

	else {
		multipart->state = HTTP_MULTIPART_STATE_DATA;
		*mark = i;
		step = HTTP_MULTIPART_STEP_AGAIN;
	}

	return step;

}

// the bytes of a boundary after the line break, the ones that do not match it were data
static HttpMultipartStep http_multipart_data_boundary (
	CerverHttpMultipart *multipart, const char byte, size_t *mark, const size_t i
) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	if (multipart->boundary[multipart->index] != byte) {
		if (!http_multipart_data (multipart, multipart->callbacks->on_part_data, multipart->lookbehind, HTTP_MULTIPART_LINE_BREAK + multipart->index)) {
			step = HTTP_MULTIPART_STEP_STOP;
		}

		else {
			multipart->state = HTTP_MULTIPART_STATE_DATA;
			*mark = i;
			step = HTTP_MULTIPART_STEP_AGAIN;
		}
	}

	else {
		multipart->lookbehind[HTTP_MULTIPART_LINE_BREAK + multipart->index] = byte;
		multipart->index += 1;

		if (multipart->index == multipart->boundary_length) {
			if (!http_multipart_event (multipart, multipart->callbacks->on_part_data_end)) step = HTTP_MULTIPART_STEP_STOP;
			multipart->state = HTTP_MULTIPART_STATE_DATA_ALMOST_END;
		}
	}

	return step;

}

// after a boundary: "--" ends the body, CRLF starts another part
static HttpMultipartStep http_multipart_data_almost_end (CerverHttpMultipart *multipart, const char byte) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	if (byte == HTTP_MULTIPART_HYPHEN) multipart->state = HTTP_MULTIPART_STATE_FINAL_HYPHEN;
	else if (byte == HTTP_MULTIPART_CR) multipart->state = HTTP_MULTIPART_STATE_DATA_END;
	else step = HTTP_MULTIPART_STEP_STOP;

	return step;

}

static HttpMultipartStep http_multipart_final_hyphen (CerverHttpMultipart *multipart, const char byte) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_STOP;

	if (byte == HTTP_MULTIPART_HYPHEN) {
		step = http_multipart_event (multipart, multipart->callbacks->on_body_end) ?
			HTTP_MULTIPART_STEP_NEXT : HTTP_MULTIPART_STEP_STOP;

		multipart->state = HTTP_MULTIPART_STATE_END;
	}

	return step;

}

static HttpMultipartStep http_multipart_data_end (CerverHttpMultipart *multipart, const char byte) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_STOP;

	if (byte == HTTP_MULTIPART_LF) {
		multipart->state = HTTP_MULTIPART_STATE_FIELD_START;

		step = http_multipart_event (multipart, multipart->callbacks->on_part_data_begin) ?
			HTTP_MULTIPART_STEP_NEXT : HTTP_MULTIPART_STEP_STOP;
	}

	return step;

}

#pragma endregion

#pragma region main

// the data of a part until the next CR, that can start the line break before a boundary
static HttpMultipartStep http_multipart_data_piece (
	CerverHttpMultipart *multipart, const char *buffer, const size_t length, size_t *i, size_t *mark
) {

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	const char *carriage = (const char *) memchr (buffer + *i, HTTP_MULTIPART_CR, length - *i);

	if (!carriage) {
		if (!http_multipart_data (multipart, multipart->callbacks->on_part_data, buffer + *mark, length - *mark)) {
			step = HTTP_MULTIPART_STEP_STOP;
		}

		// the last byte of the buffer was read
		*i = length - 1;
	}

	else {
		*i = (size_t) (carriage - buffer);

		if (!http_multipart_data (multipart, multipart->callbacks->on_part_data, buffer + *mark, *i - *mark)) {
			step = HTTP_MULTIPART_STEP_STOP;
		}

		else {
			*mark = *i;
			multipart->state = HTTP_MULTIPART_STATE_DATA_ALMOST_BOUNDARY;
			multipart->lookbehind[0] = HTTP_MULTIPART_CR;
		}
	}

	return step;

}

static HttpMultipartStep http_multipart_byte (
	CerverHttpMultipart *multipart, const char *buffer, const size_t length, size_t *i, size_t *mark
) {

	const char byte = buffer[*i];
	const bool last = (*i == (length - 1));

	HttpMultipartStep step = HTTP_MULTIPART_STEP_NEXT;

	switch (multipart->state) {
		case HTTP_MULTIPART_STATE_START:
			multipart->index = 0;
			multipart->state = HTTP_MULTIPART_STATE_START_BOUNDARY;
			step = http_multipart_start_boundary (multipart, byte);
			break;

		case HTTP_MULTIPART_STATE_START_BOUNDARY: step = http_multipart_start_boundary (multipart, byte); break;

		case HTTP_MULTIPART_STATE_FIELD_START:
			*mark = *i;
			multipart->state = HTTP_MULTIPART_STATE_FIELD;
			step = http_multipart_field (multipart, buffer, *i, *mark, last);
			break;

		case HTTP_MULTIPART_STATE_FIELD: step = http_multipart_field (multipart, buffer, *i, *mark, last); break;

		case HTTP_MULTIPART_STATE_HEADERS_ALMOST_DONE:
			step = http_multipart_line_feed (multipart, byte, HTTP_MULTIPART_STATE_DATA_START);
			break;

		case HTTP_MULTIPART_STATE_VALUE_START:
			if (byte == HTTP_MULTIPART_SPACE) break;
			*mark = *i;
			multipart->state = HTTP_MULTIPART_STATE_VALUE;
			step = http_multipart_value (multipart, buffer, *i, *mark, last);
			break;

		case HTTP_MULTIPART_STATE_VALUE: step = http_multipart_value (multipart, buffer, *i, *mark, last); break;

		case HTTP_MULTIPART_STATE_VALUE_ALMOST_DONE:
			step = http_multipart_line_feed (multipart, byte, HTTP_MULTIPART_STATE_FIELD_START);
			break;

		case HTTP_MULTIPART_STATE_DATA_START:
			if (!http_multipart_event (multipart, multipart->callbacks->on_headers_complete)) {
				step = HTTP_MULTIPART_STEP_STOP;
				break;
			}

			*mark = *i;
			multipart->state = HTTP_MULTIPART_STATE_DATA;
			step = http_multipart_data_piece (multipart, buffer, length, i, mark);
			break;

		case HTTP_MULTIPART_STATE_DATA: step = http_multipart_data_piece (multipart, buffer, length, i, mark); break;

		case HTTP_MULTIPART_STATE_DATA_ALMOST_BOUNDARY: step = http_multipart_data_almost_boundary (multipart, byte, mark, *i); break;
		case HTTP_MULTIPART_STATE_DATA_BOUNDARY: step = http_multipart_data_boundary (multipart, byte, mark, *i); break;
		case HTTP_MULTIPART_STATE_DATA_ALMOST_END: step = http_multipart_data_almost_end (multipart, byte); break;
		case HTTP_MULTIPART_STATE_FINAL_HYPHEN: step = http_multipart_final_hyphen (multipart, byte); break;
		case HTTP_MULTIPART_STATE_DATA_END: step = http_multipart_data_end (multipart, byte); break;

		// the bytes after the end of the body are not read
		case HTTP_MULTIPART_STATE_END: break;

		default: step = HTTP_MULTIPART_STEP_STOP; break;
	}

	return step;

}

size_t cerver_http_multipart_execute (
	CerverHttpMultipart *multipart, const char *buffer, const size_t length
) {

	size_t i = 0;
	size_t mark = 0;

	while (i < length) {
		const HttpMultipartStep step = http_multipart_byte (multipart, buffer, length, &i, &mark);
		if (step == HTTP_MULTIPART_STEP_STOP) return i;

		if (step == HTTP_MULTIPART_STEP_NEXT) i += 1;
	}

	return length;

}

#pragma endregion
