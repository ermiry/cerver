#ifndef _CERVER_HTTP_HEADERS_FIELDS_H_
#define _CERVER_HTTP_HEADERS_FIELDS_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

// the fields that a list of headers keeps inside it, more of them are allocated
#define HTTP_HEADERS_FIELDS_SIZE		32
#define HTTP_HEADERS_FIELDS_MAX			256

// the bytes of the lines that a list of headers keeps inside it, more of them are allocated
#define HTTP_HEADERS_BUFFER_SIZE		512
#define HTTP_HEADERS_BUFFER_MAX			16384

#ifdef __cplusplus
extern "C" {
#endif

// a header of a list, its line "Name: value\r\n" is in the buffer of the list
struct _HttpHeaderField {

	// HTTP_HEADER_UNDEFINED when its name is not one of the known headers
	http_header type;

	unsigned int offset;

	unsigned short name_length;
	unsigned int value_length;

};

typedef struct _HttpHeaderField HttpHeaderField;

// a list of headers in the order they were added, a name can be repeated,
// its lines are kept one after the other, so they are sent as they are in HTTP/1.1
// and a protocol that encodes its headers gets each name and value
struct _HttpHeaders {

	unsigned int count;

	// the allocated fields, NULL while they fit inside the list
	HttpHeaderField *fields;
	unsigned int fields_size;

	size_t length;

	// the allocated lines, NULL while they fit inside the list
	char *buffer;
	size_t buffer_size;

	HttpHeaderField inside_fields[HTTP_HEADERS_FIELDS_SIZE];
	char inside_buffer[HTTP_HEADERS_BUFFER_SIZE];

};

typedef struct _HttpHeaders HttpHeaders;

// a name must be a token (RFC 9110) of up to HTTP_HEADER_NAME_MAX bytes
CERVER_PRIVATE bool http_header_name_is_valid (
	const char *name, const size_t name_length
);

// a value can not have a CR, a LF or a NUL, as they would end its line
CERVER_PRIVATE bool http_header_value_is_valid (
	const char *value, const size_t value_length
);

CERVER_PRIVATE void http_headers_init (HttpHeaders *headers);

// removes all the headers and releases the allocated memory
CERVER_PRIVATE void http_headers_reset (HttpHeaders *headers);

// adds a header at the end of the list, the ones with the same type are kept
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_add (
	HttpHeaders *headers, const http_header type,
	const char *value, const size_t value_length
);

// works like http_headers_add () with any name,
// the name of a known header is added with its type
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_add_with_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
);

// adds a header after removing the ones with the same type
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_set (
	HttpHeaders *headers, const http_header type,
	const char *value, const size_t value_length
);

// works like http_headers_set () with any name
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_headers_set_with_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
);

// returns the number of headers that were removed
CERVER_PRIVATE unsigned int http_headers_remove (
	HttpHeaders *headers, const http_header type
);

CERVER_PRIVATE unsigned int http_headers_remove_with_name (
	HttpHeaders *headers, const char *name, const size_t name_length
);

CERVER_PRIVATE unsigned int http_headers_count (
	const HttpHeaders *headers, const http_header type
);

CERVER_PRIVATE unsigned int http_headers_count_with_name (
	const HttpHeaders *headers, const char *name, const size_t name_length
);

// returns the header with the type in the position between the ones that have it
CERVER_PRIVATE const HttpHeaderField *http_headers_find (
	const HttpHeaders *headers, const http_header type, const unsigned int position
);

CERVER_PRIVATE const HttpHeaderField *http_headers_find_with_name (
	const HttpHeaders *headers,
	const char *name, const size_t name_length, const unsigned int position
);

// returns the header in the index of the list, NULL after the last one
CERVER_PRIVATE const HttpHeaderField *http_headers_get (
	const HttpHeaders *headers, const unsigned int index
);

// the name of a header of the list, it does not end with '\0'
CERVER_PRIVATE const char *http_headers_field_name (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *name_length
);

// the value of a header of the list, it does not end with '\0'
CERVER_PRIVATE const char *http_headers_field_value (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *value_length
);

// the line "Name: value\r\n" of a header of the list
CERVER_PRIVATE const char *http_headers_field_line (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *line_length
);

// the lines "Name: value\r\n" of all the headers, in the order they were added
CERVER_PRIVATE const char *http_headers_lines (
	const HttpHeaders *headers, size_t *length
);

#ifdef __cplusplus
}
#endif

#endif
