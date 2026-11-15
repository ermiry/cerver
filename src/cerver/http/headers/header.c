#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>
#include <strings.h>

#include "cerver/types/types.h"

#include "cerver/http/headers/header.h"

// the line of a header in a response
#define HTTP_HEADER_LINE_FORMAT			"%s: %s\r\n"

void http_header_reset (HttpHeader *header) {

	(void) memset (header, 0, sizeof (HttpHeader));

}

void http_header_set (
	HttpHeader *header, const char *format, ...
) {

	va_list args;
	va_start (args, format);

	(void) vsnprintf (
		header->value, HTTP_HEADER_VALUE_SIZE,
		format, args
	);

	va_end (args);

	// what vsnprintf () returns is the length of the whole value, also when it was cut
	header->len = (int) strlen (header->value);

}

// the line is the header only when all of it was written, a header is never kept cut:
// the length of a header is the number of bytes that are sent from its value
// returns 0 on success, 1 when the line does not fit
static u8 http_response_header_take (
	HttpHeader *header, const char *line, const int length
) {

	u8 retval = 1;

	if ((length > 0) && (length < HTTP_HEADER_VALUE_SIZE)) {
		(void) memcpy (header->value, line, (size_t) length + 1);
		header->len = length;

		retval = 0;
	}

	return retval;

}

u8 http_response_header_set_with_type (
	HttpHeader *header, const http_header type,
	const char *actual_header
) {

	char line[HTTP_HEADER_VALUE_SIZE];
	const int length = snprintf (
		line, HTTP_HEADER_VALUE_SIZE,
		HTTP_HEADER_LINE_FORMAT,
		http_header_string (type), actual_header
	);

	return http_response_header_take (header, line, length);

}

u8 http_response_header_set_with_type_and_args (
	HttpHeader *header, const http_header type,
	const char *format, va_list args
) {

	u8 retval = 1;

	char actual_header[HTTP_HEADER_ACTUAL_SIZE];
	const int length = vsnprintf (
		actual_header, HTTP_HEADER_ACTUAL_SIZE,
		format, args
	);

	if ((length >= 0) && (length < HTTP_HEADER_ACTUAL_SIZE)) {
		retval = http_response_header_set_with_type (header, type, actual_header);
	}

	return retval;

}

void http_header_print (const HttpHeader *header) {

	(void) printf ("(%d) - %s\n", header->len, header->value);

}
