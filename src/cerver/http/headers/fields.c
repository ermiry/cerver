#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <strings.h>

#include "cerver/types/types.h"

#include "cerver/http/headers/fields.h"
#include "cerver/http/headers/internal.h"

#pragma region list

static const char http_headers_value_separator[] = { ": " };
static const size_t http_headers_value_separator_length = 2;

static const unsigned int http_headers_growth_factor = 2;

// the characters of a token that are not letters or digits (RFC 9110)
static const char http_header_name_symbols[] = { "!#$%&'*+-.^_`|~" };

static const char http_header_value_carriage_return = '\r';
static const char http_header_value_line_feed = '\n';
static const char http_header_value_null = '\0';

static bool http_header_name_character_is_valid (const char character) {

	return ((character >= 'a') && (character <= 'z'))
		|| ((character >= 'A') && (character <= 'Z'))
		|| ((character >= '0') && (character <= '9'))
		|| (character && strchr (http_header_name_symbols, character));

}

bool http_header_name_is_valid (
	const char *name, const size_t name_length
) {

	bool valid = name && name_length && (name_length <= HTTP_HEADER_NAME_MAX);

	for (size_t idx = 0; valid && (idx < name_length); idx++) {
		valid = http_header_name_character_is_valid (name[idx]);
	}

	return valid;

}

bool http_header_value_is_valid (
	const char *value, const size_t value_length
) {

	return value
		&& !memchr (value, http_header_value_carriage_return, value_length)
		&& !memchr (value, http_header_value_line_feed, value_length)
		&& !memchr (value, http_header_value_null, value_length);

}

static HttpHeaderField *http_headers_fields (HttpHeaders *headers) {

	return headers->fields ? headers->fields : headers->inside_fields;

}

static const HttpHeaderField *http_headers_fields_read (const HttpHeaders *headers) {

	return headers->fields ? headers->fields : headers->inside_fields;

}

static char *http_headers_buffer (HttpHeaders *headers) {

	return headers->buffer ? headers->buffer : headers->inside_buffer;

}

static const char *http_headers_buffer_read (const HttpHeaders *headers) {

	return headers->buffer ? headers->buffer : headers->inside_buffer;

}

static size_t http_headers_field_length (const HttpHeaderField *field) {

	return (size_t) field->name_length + http_headers_value_separator_length
		+ (size_t) field->value_length + http_headers_line_end_length;

}

void http_headers_init (HttpHeaders *headers) {

	headers->count = 0;

	headers->fields = NULL;
	headers->fields_size = HTTP_HEADERS_FIELDS_SIZE;

	headers->length = 0;

	headers->buffer = NULL;
	headers->buffer_size = HTTP_HEADERS_BUFFER_SIZE;

}

void http_headers_reset (HttpHeaders *headers) {

	if (headers) {
		if (headers->fields) free (headers->fields);
		if (headers->buffer) free (headers->buffer);

		http_headers_init (headers);
	}

}

// makes room for one more field, the allocated ones replace the ones inside the list
static bool http_headers_reserve_field (HttpHeaders *headers) {

	bool reserved = headers->count < headers->fields_size;

	if (!reserved && (headers->fields_size < HTTP_HEADERS_FIELDS_MAX)) {
		const unsigned int size = headers->fields_size * http_headers_growth_factor;

		HttpHeaderField *fields = (HttpHeaderField *) malloc (size * sizeof (HttpHeaderField));
		if (fields) {
			(void) memcpy (
				fields, http_headers_fields (headers),
				headers->count * sizeof (HttpHeaderField)
			);

			if (headers->fields) free (headers->fields);

			headers->fields = fields;
			headers->fields_size = size;

			reserved = true;
		}
	}

	return reserved;

}

// makes room for the line, the allocated buffer replaces the one inside the list
static bool http_headers_reserve_line (
	HttpHeaders *headers, const size_t line_length
) {

	const size_t needed = headers->length + line_length;

	bool reserved = needed <= headers->buffer_size;

	if (!reserved && (needed <= HTTP_HEADERS_BUFFER_MAX)) {
		size_t size = headers->buffer_size;
		while (size < needed) size *= http_headers_growth_factor;

		char *buffer = (char *) malloc (size);
		if (buffer) {
			(void) memcpy (buffer, http_headers_buffer (headers), headers->length);

			if (headers->buffer) free (headers->buffer);

			headers->buffer = buffer;
			headers->buffer_size = size;

			reserved = true;
		}
	}

	return reserved;

}

static void http_headers_write_line (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
) {

	char *end = http_headers_buffer (headers) + headers->length;

	(void) memcpy (end, name, name_length);
	end += name_length;

	(void) memcpy (end, http_headers_value_separator, http_headers_value_separator_length);
	end += http_headers_value_separator_length;

	(void) memcpy (end, value, value_length);
	end += value_length;

	(void) memcpy (end, http_headers_line_end, http_headers_line_end_length);

}

// the name and the value are already valid
static u8 http_headers_append (
	HttpHeaders *headers, const http_header type,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
) {

	u8 retval = 1;

	// a value that can not fit is rejected before its length is added
	if (value_length <= HTTP_HEADERS_BUFFER_MAX) {
		const size_t line_length = name_length + http_headers_value_separator_length
			+ value_length + http_headers_line_end_length;

		if (
			http_headers_reserve_field (headers)
			&& http_headers_reserve_line (headers, line_length)
		) {
			http_headers_write_line (headers, name, name_length, value, value_length);

			HttpHeaderField *field = http_headers_fields (headers) + headers->count;
			field->type = type;
			field->offset = (unsigned int) headers->length;
			field->name_length = (unsigned short) name_length;
			field->value_length = (unsigned int) value_length;

			headers->length += line_length;
			headers->count += 1;

			retval = 0;
		}
	}

	return retval;

}

static bool http_headers_type_is_known (const http_header type) {

	return (unsigned int) type < (unsigned int) HTTP_HEADER_UNDEFINED;

}

// the type of a name that does not end with '\0'
static http_header http_headers_name_type (
	const char *name, const size_t name_length
) {

	char name_string[HTTP_HEADER_NAME_MAX + 1];
	(void) memcpy (name_string, name, name_length);
	name_string[name_length] = '\0';

	return http_header_type_with_string (name_string);

}

u8 http_headers_add (
	HttpHeaders *headers, const http_header type,
	const char *value, const size_t value_length
) {

	u8 retval = 1;

	if (
		headers && http_headers_type_is_known (type)
		&& http_header_value_is_valid (value, value_length)
	) {
		const char *name = http_header_string (type);

		retval = http_headers_append (
			headers, type, name, strlen (name), value, value_length
		);
	}

	return retval;

}

u8 http_headers_add_with_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
) {

	u8 retval = 1;

	if (
		headers && http_header_name_is_valid (name, name_length)
		&& http_header_value_is_valid (value, value_length)
	) {
		const http_header type = http_headers_name_type (name, name_length);

		retval = http_headers_type_is_known (type) ?
			http_headers_add (headers, type, value, value_length) :
			http_headers_append (
				headers, HTTP_HEADER_UNDEFINED, name, name_length, value, value_length
			);
	}

	return retval;

}

// a known header matches by its type and any other one by its name without case
static bool http_headers_field_matches (
	const HttpHeaders *headers, const HttpHeaderField *field,
	const http_header type, const char *name, const size_t name_length
) {

	return http_headers_type_is_known (type) ? (field->type == type) : (
		(field->type == HTTP_HEADER_UNDEFINED)
		&& ((size_t) field->name_length == name_length)
		&& !strncasecmp (http_headers_buffer_read (headers) + field->offset, name, name_length)
	);

}

// the lines and the fields after the removed one take its place
static void http_headers_remove_field (
	HttpHeaders *headers, const unsigned int index
) {

	HttpHeaderField *fields = http_headers_fields (headers);
	char *buffer = http_headers_buffer (headers);

	const size_t line_length = http_headers_field_length (&fields[index]);
	const size_t line_end = (size_t) fields[index].offset + line_length;

	(void) memmove (
		buffer + fields[index].offset, buffer + line_end,
		headers->length - line_end
	);

	for (unsigned int idx = index + 1; idx < headers->count; idx++) {
		fields[idx].offset -= (unsigned int) line_length;
		fields[idx - 1] = fields[idx];
	}

	headers->length -= line_length;
	headers->count -= 1;

}

static unsigned int http_headers_remove_matches (
	HttpHeaders *headers,
	const http_header type, const char *name, const size_t name_length
) {

	unsigned int removed = 0;

	unsigned int index = 0;
	while (index < headers->count) {
		if (
			http_headers_field_matches (
				headers, http_headers_fields (headers) + index, type, name, name_length
			)
		) {
			http_headers_remove_field (headers, index);
			removed += 1;
		}

		else {
			index += 1;
		}
	}

	return removed;

}

u8 http_headers_set (
	HttpHeaders *headers, const http_header type,
	const char *value, const size_t value_length
) {

	u8 retval = 1;

	if (
		headers && http_headers_type_is_known (type)
		&& http_header_value_is_valid (value, value_length)
	) {
		(void) http_headers_remove_matches (headers, type, NULL, 0);

		retval = http_headers_add (headers, type, value, value_length);
	}

	return retval;

}

u8 http_headers_set_with_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
) {

	u8 retval = 1;

	if (
		headers && http_header_name_is_valid (name, name_length)
		&& http_header_value_is_valid (value, value_length)
	) {
		(void) http_headers_remove_matches (
			headers, http_headers_name_type (name, name_length), name, name_length
		);

		retval = http_headers_add_with_name (headers, name, name_length, value, value_length);
	}

	return retval;

}

unsigned int http_headers_remove (
	HttpHeaders *headers, const http_header type
) {

	return (headers && http_headers_type_is_known (type)) ?
		http_headers_remove_matches (headers, type, NULL, 0) : 0;

}

unsigned int http_headers_remove_with_name (
	HttpHeaders *headers, const char *name, const size_t name_length
) {

	return (headers && http_header_name_is_valid (name, name_length)) ?
		http_headers_remove_matches (
			headers, http_headers_name_type (name, name_length), name, name_length
		) : 0;

}

// the header in the position between the ones that match, and how many match
static const HttpHeaderField *http_headers_find_matches (
	const HttpHeaders *headers,
	const http_header type, const char *name, const size_t name_length,
	const unsigned int position, unsigned int *count
) {

	const HttpHeaderField *found = NULL;

	const HttpHeaderField *fields = http_headers_fields_read (headers);

	unsigned int matches = 0;
	for (unsigned int idx = 0; idx < headers->count; idx++) {
		if (http_headers_field_matches (headers, &fields[idx], type, name, name_length)) {
			if (!found && (matches == position)) found = &fields[idx];

			matches += 1;
		}
	}

	if (count) *count = matches;

	return found;

}

unsigned int http_headers_count (
	const HttpHeaders *headers, const http_header type
) {

	unsigned int count = 0;

	if (headers && http_headers_type_is_known (type)) {
		(void) http_headers_find_matches (headers, type, NULL, 0, 0, &count);
	}

	return count;

}

unsigned int http_headers_count_with_name (
	const HttpHeaders *headers, const char *name, const size_t name_length
) {

	unsigned int count = 0;

	if (headers && http_header_name_is_valid (name, name_length)) {
		(void) http_headers_find_matches (
			headers, http_headers_name_type (name, name_length),
			name, name_length, 0, &count
		);
	}

	return count;

}

const HttpHeaderField *http_headers_find (
	const HttpHeaders *headers, const http_header type, const unsigned int position
) {

	return (headers && http_headers_type_is_known (type)) ?
		http_headers_find_matches (headers, type, NULL, 0, position, NULL) : NULL;

}

const HttpHeaderField *http_headers_find_with_name (
	const HttpHeaders *headers,
	const char *name, const size_t name_length, const unsigned int position
) {

	return (headers && http_header_name_is_valid (name, name_length)) ?
		http_headers_find_matches (
			headers, http_headers_name_type (name, name_length),
			name, name_length, position, NULL
		) : NULL;

}

const HttpHeaderField *http_headers_get (
	const HttpHeaders *headers, const unsigned int index
) {

	return (headers && (index < headers->count)) ?
		http_headers_fields_read (headers) + index : NULL;

}

const char *http_headers_field_name (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *name_length
) {

	if (name_length) *name_length = (size_t) field->name_length;

	return http_headers_buffer_read (headers) + field->offset;

}

const char *http_headers_field_value (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *value_length
) {

	if (value_length) *value_length = (size_t) field->value_length;

	return http_headers_buffer_read (headers) + field->offset
		+ field->name_length + http_headers_value_separator_length;

}

const char *http_headers_field_line (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *line_length
) {

	if (line_length) *line_length = http_headers_field_length (field);

	return http_headers_buffer_read (headers) + field->offset;

}

const char *http_headers_lines (
	const HttpHeaders *headers, size_t *length
) {

	if (length) *length = headers->length;

	return http_headers_buffer_read (headers);

}

#pragma endregion
