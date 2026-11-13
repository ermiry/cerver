#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include <stdarg.h>
#include <ctype.h>
#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/http/headers.h"

#pragma region types

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

#pragma endregion

#pragma region header

void http_header_reset (HttpHeader *header) {

	(void) memset (header, 0, sizeof (HttpHeader));

}

void http_header_set (
	HttpHeader *header, const char *format, ...
) {

	va_list args;
	va_start (args, format);

	header->len = vsnprintf (
		header->value, HTTP_HEADER_VALUE_SIZE,
		format, args
	);

	va_end (args);

}

void http_header_print (const HttpHeader *header) {

	(void) printf ("(%d) - %s\n", header->len, header->value);

}

#pragma endregion

#pragma region buffer

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

#pragma endregion

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

	return http_header_type_by_string (name_string);

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

u8 http_headers_add_by_name (
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

u8 http_headers_set_by_name (
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

		retval = http_headers_add_by_name (headers, name, name_length, value, value_length);
	}

	return retval;

}

unsigned int http_headers_remove (
	HttpHeaders *headers, const http_header type
) {

	return (headers && http_headers_type_is_known (type)) ?
		http_headers_remove_matches (headers, type, NULL, 0) : 0;

}

unsigned int http_headers_remove_by_name (
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

unsigned int http_headers_count_by_name (
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

const HttpHeaderField *http_headers_find_by_name (
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
