#include <string.h>
#include <strings.h>
#include <stdio.h>

#include <stdarg.h>
#include <ctype.h>
#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/http/headers.h"

// the line of a header in a response
#define HTTP_HEADER_LINE_FORMAT			"%s: %s\r\n"

const char *http_header_string (
	const http_header header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #string;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return http_header_string (HTTP_HEADER_UNDEFINED);

}

const char *http_header_description (
	const http_header header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #description;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return http_header_description (HTTP_HEADER_UNDEFINED);

}

// FNV-1a of the lower case name, so the lookup does not depend on case
static const unsigned int http_headers_hash_offset = 2166136261u;
static const unsigned int http_headers_hash_prime = 16777619u;

// a slot keeps the header type + 1, so 0 is an empty slot
static unsigned char http_headers_table[HTTP_HEADERS_TABLE_SIZE] = { 0 };
static pthread_once_t http_headers_table_once = PTHREAD_ONCE_INIT;

static unsigned int http_headers_hash (const char *name) {

	unsigned int hash = http_headers_hash_offset;
	for (const char *ch = name; *ch; ch++) {
		hash ^= (unsigned int) tolower ((unsigned char) *ch);
		hash *= http_headers_hash_prime;
	}

	return hash;

}

static void http_headers_table_insert (const http_header type) {

	unsigned int slot = http_headers_hash (http_header_string (type)) & (HTTP_HEADERS_TABLE_SIZE - 1);
	while (http_headers_table[slot]) {
		slot = (slot + 1) & (HTTP_HEADERS_TABLE_SIZE - 1);
	}

	http_headers_table[slot] = (unsigned char) (type + 1);

}

static void http_headers_table_init (void) {

	#define XX(num, name, string, description) if (HTTP_HEADER_##name != HTTP_HEADER_UNDEFINED) http_headers_table_insert (HTTP_HEADER_##name);
	HTTP_HEADERS_MAP(XX)
	#undef XX

}

const http_header http_header_type_by_string (
	const char *header_type_string
) {

	http_header found = HTTP_HEADER_UNDEFINED;

	if (header_type_string) {
		(void) pthread_once (&http_headers_table_once, http_headers_table_init);

		unsigned int slot = http_headers_hash (header_type_string) & (HTTP_HEADERS_TABLE_SIZE - 1);
		while (http_headers_table[slot]) {
			const http_header candidate = (http_header) (http_headers_table[slot] - 1);
			if (!strcasecmp (http_header_string (candidate), header_type_string)) {
				found = candidate;
				break;
			}

			slot = (slot + 1) & (HTTP_HEADERS_TABLE_SIZE - 1);
		}
	}

	return found;

}

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

static const char http_headers_line_end[] = { "\r\n" };
static const size_t http_headers_line_end_length = 2;
static const char http_headers_name_separator = ':';

// returns the line that starts at start, without its "\r\n"
static size_t http_headers_buffer_line (
	const char *start, const char *end
) {

	const char *line_end = start;
	while (
		((size_t) (end - line_end) >= http_headers_line_end_length)
		&& memcmp (line_end, http_headers_line_end, http_headers_line_end_length)
	) {
		line_end += 1;
	}

	return ((size_t) (end - line_end) >= http_headers_line_end_length) ?
		(size_t) (line_end - start) : (size_t) (end - start);

}

static void http_headers_buffer_value (
	const char *line, const size_t line_length, const size_t name_length,
	const char **value, size_t *value_length
) {

	const char *first = line + name_length + 1;
	const char *last = line + line_length;

	while ((first < last) && isspace ((unsigned char) *first)) first += 1;
	while ((last > first) && isspace ((unsigned char) *(last - 1))) last -= 1;

	if (value) *value = first;
	if (value_length) *value_length = (size_t) (last - first);

}

bool http_headers_buffer_find (
	const char *buffer, const size_t length,
	const char *name, const size_t name_length,
	const char **value, size_t *value_length
) {

	bool found = false;

	if (buffer && name) {
		const char *end = buffer + length;

		// the status line is not a header
		const char *line = buffer + http_headers_buffer_line (buffer, end) + http_headers_line_end_length;
		while (!found && (line < end)) {
			const size_t line_length = http_headers_buffer_line (line, end);
			if (!line_length) break;

			if (
				(line_length > name_length)
				&& (line[name_length] == http_headers_name_separator)
				&& !strncasecmp (line, name, name_length)
			) {
				http_headers_buffer_value (line, line_length, name_length, value, value_length);
				found = true;
			}

			line += line_length + http_headers_line_end_length;
		}
	}

	return found;

}
