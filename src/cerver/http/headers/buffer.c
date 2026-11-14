#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <strings.h>

#include "cerver/types/types.h"

#include "cerver/http/headers/buffer.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/headers/internal.h"

#pragma region buffer

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

#pragma endregion
