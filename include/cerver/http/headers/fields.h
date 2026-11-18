/**
 * @file cerver/http/headers/fields.h
 * @brief A list of headers: their lines in a buffer, in the order they were added.
 */

#ifndef _CERVER_HTTP_HEADERS_FIELDS_H_
#define _CERVER_HTTP_HEADERS_FIELDS_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

/**
 * @defgroup http_headers_fields Fields
 * @ingroup http_headers
 * @brief The list of the headers of a response or of a cerver.
 *
 * The functions of a list are for the internal methods of cerver: a response adds its headers with the functions of
 * the responses.
 * @{
 */

/** @brief The fields that a list of headers keeps inside it; more of them are allocated. */
#define HTTP_HEADERS_FIELDS_SIZE		32
/** @brief The most fields of a list of headers. */
#define HTTP_HEADERS_FIELDS_MAX			256

/** @brief The bytes of the lines that a list of headers keeps inside it; more of them are allocated. */
#define HTTP_HEADERS_BUFFER_SIZE		512
/** @brief The most bytes of the lines of a list of headers. */
#define HTTP_HEADERS_BUFFER_MAX			16384

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A header of a list; its line "Name: value\r\n" is in the buffer of the list.
 */
struct _HttpHeaderField {

	/** @brief The type of the header, HTTP_HEADER_UNDEFINED when its name is not one of the known headers. */
	CerverHttpHeader type;

	/** @brief Where its line starts in the buffer of the list. */
	unsigned int offset;

	/** @brief The length of its name. */
	unsigned short name_length;
	/** @brief The length of its value. */
	unsigned int value_length;

};

/** @brief A header of a list, see struct _HttpHeaderField. */
typedef struct _HttpHeaderField HttpHeaderField;

/**
 * @brief A list of headers in the order they were added; a name can be repeated.
 *
 * Its lines are kept one after the other, so they are sent as they are in HTTP/1.1, and a protocol that encodes its
 * headers gets each name and value.
 */
struct _HttpHeaders {

	/** @brief The headers of the list. */
	unsigned int count;

	/** @brief The allocated fields, NULL while they fit inside the list. */
	HttpHeaderField *fields;
	/** @brief The fields that fit in the current array. */
	unsigned int fields_size;

	/** @brief The bytes of all the lines. */
	size_t length;

	/** @brief The allocated lines, NULL while they fit inside the list. */
	char *buffer;
	/** @brief The bytes that fit in the current buffer. */
	size_t buffer_size;

	/** @brief The fields inside the list. */
	HttpHeaderField inside_fields[HTTP_HEADERS_FIELDS_SIZE];
	/** @brief The lines inside the list. */
	char inside_buffer[HTTP_HEADERS_BUFFER_SIZE];

};

/** @brief A list of headers, see struct _HttpHeaders. */
typedef struct _HttpHeaders HttpHeaders;

/**
 * @brief Returns whether a name is a token (RFC 9110) of up to #HTTP_HEADER_NAME_MAX bytes.
 *
 * @param[in] name the name
 * @param[in] name_length its length
 * @return true for a valid name, false for a NULL or an empty one
 */
CERVER_PRIVATE bool http_header_name_is_valid (
	const char *name, const size_t name_length
);

/**
 * @brief Returns whether a value has no CR, LF or NUL, which would end its line.
 *
 * @param[in] value the value
 * @param[in] value_length its length
 * @return true for a valid value, false for a NULL one
 */
CERVER_PRIVATE bool http_header_value_is_valid (
	const char *value, const size_t value_length
);

/**
 * @brief Initializes an empty list that uses its inside fields and buffer.
 *
 * @param[out] headers the list, it must not be NULL
 */
CERVER_PRIVATE void http_headers_init (HttpHeaders *headers);

/**
 * @brief Removes all the headers of a list and releases its allocated memory.
 *
 * @param[in,out] headers the list, it can be NULL
 */
CERVER_PRIVATE void http_headers_reset (HttpHeaders *headers);

/**
 * @brief Adds a known header at the end of a list; the ones with the same type are kept.
 *
 * @param[in,out] headers the list
 * @param[in] type a known header
 * @param[in] value the value, copied
 * @param[in] value_length its length
 * @retval 0 the header was added
 * @retval 1 a NULL list, an unknown type, an invalid value, or the list is full
 */
CERVER_PRIVATE u8 http_headers_add (
	HttpHeaders *headers, const CerverHttpHeader type,
	const char *value, const size_t value_length
);

/**
 * @brief Works like http_headers_add () with any name; the name of a known header is added with its type.
 *
 * @param[in,out] headers the list
 * @param[in] name the name, copied
 * @param[in] name_length its length
 * @param[in] value the value, copied
 * @param[in] value_length its length
 * @retval 0 the header was added
 * @retval 1 a NULL list, an invalid name or value, or the list is full
 */
CERVER_PRIVATE u8 http_headers_add_with_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
);

/**
 * @brief Adds a known header after removing the ones with the same type.
 *
 * @param[in,out] headers the list
 * @param[in] type a known header
 * @param[in] value the value, copied
 * @param[in] value_length its length
 * @retval 0 the header was set
 * @retval 1 a NULL list, an unknown type or an invalid value, that keep the list as it was; or the new header did
 * not fit, after the old ones were removed
 */
CERVER_PRIVATE u8 http_headers_set (
	HttpHeaders *headers, const CerverHttpHeader type,
	const char *value, const size_t value_length
);

/**
 * @brief Works like http_headers_set () with any name.
 *
 * @param[in,out] headers the list
 * @param[in] name the name, copied
 * @param[in] name_length its length
 * @param[in] value the value, copied
 * @param[in] value_length its length
 * @retval 0 the header was set
 * @retval 1 a NULL list or an invalid name or value, that keep the list as it was; or the new header did not fit,
 * after the old ones were removed
 */
CERVER_PRIVATE u8 http_headers_set_with_name (
	HttpHeaders *headers,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
);

/**
 * @brief Removes the headers of a type from a list.
 *
 * @param[in,out] headers the list
 * @param[in] type a known header
 * @return the headers that were removed, 0 for a NULL list or an unknown type
 */
CERVER_PRIVATE unsigned int http_headers_remove (
	HttpHeaders *headers, const CerverHttpHeader type
);

/**
 * @brief Removes the headers with a name from a list, compared without case.
 *
 * @param[in,out] headers the list
 * @param[in] name the name
 * @param[in] name_length its length
 * @return the headers that were removed, 0 for a NULL list or an invalid name
 */
CERVER_PRIVATE unsigned int http_headers_remove_with_name (
	HttpHeaders *headers, const char *name, const size_t name_length
);

/**
 * @brief Counts the headers of a type in a list.
 *
 * @param[in] headers the list
 * @param[in] type a known header
 * @return the headers, 0 for a NULL list or an unknown type
 */
CERVER_PRIVATE unsigned int http_headers_count (
	const HttpHeaders *headers, const CerverHttpHeader type
);

/**
 * @brief Counts the headers with a name in a list.
 *
 * @param[in] headers the list
 * @param[in] name the name
 * @param[in] name_length its length
 * @return the headers, 0 for a NULL list or an invalid name
 */
CERVER_PRIVATE unsigned int http_headers_count_with_name (
	const HttpHeaders *headers, const char *name, const size_t name_length
);

/**
 * @brief Returns a header of a type of a list, by its position between the ones that have the type.
 *
 * @param[in] headers the list
 * @param[in] type a known header
 * @param[in] position 0 for the first one
 * @return the header, that belongs to the list; NULL if there is none in the position
 */
CERVER_PRIVATE const HttpHeaderField *http_headers_find (
	const HttpHeaders *headers, const CerverHttpHeader type, const unsigned int position
);

/**
 * @brief Returns a header with a name of a list, by its position between the ones that have the name.
 *
 * @param[in] headers the list
 * @param[in] name the name
 * @param[in] name_length its length
 * @param[in] position 0 for the first one
 * @return the header, that belongs to the list; NULL if there is none in the position
 */
CERVER_PRIVATE const HttpHeaderField *http_headers_find_with_name (
	const HttpHeaders *headers,
	const char *name, const size_t name_length, const unsigned int position
);

/**
 * @brief Returns the header in an index of a list.
 *
 * @param[in] headers the list
 * @param[in] index the index
 * @return the header, that belongs to the list; NULL after the last one
 */
CERVER_PRIVATE const HttpHeaderField *http_headers_get (
	const HttpHeaders *headers, const unsigned int index
);

/**
 * @brief Returns the name of a header of a list; it does not end with '\0'.
 *
 * @param[in] headers the list
 * @param[in] field a header of the list
 * @param[out] name_length the length of the name, it can be NULL
 * @return the name, inside the buffer of the list
 */
CERVER_PRIVATE const char *http_headers_field_name (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *name_length
);

/**
 * @brief Returns the value of a header of a list; it does not end with '\0'.
 *
 * @param[in] headers the list
 * @param[in] field a header of the list
 * @param[out] value_length the length of the value, it can be NULL
 * @return the value, inside the buffer of the list
 */
CERVER_PRIVATE const char *http_headers_field_value (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *value_length
);

/**
 * @brief Returns the line "Name: value\r\n" of a header of a list.
 *
 * @param[in] headers the list
 * @param[in] field a header of the list
 * @param[out] line_length the length of the line, it can be NULL
 * @return the line, inside the buffer of the list
 */
CERVER_PRIVATE const char *http_headers_field_line (
	const HttpHeaders *headers, const HttpHeaderField *field, size_t *line_length
);

/**
 * @brief Returns the lines "Name: value\r\n" of all the headers of a list, in the order they were added.
 *
 * @param[in] headers the list
 * @param[out] length the bytes of the lines, it can be NULL
 * @return the lines, the buffer of the list
 */
CERVER_PRIVATE const char *http_headers_lines (
	const HttpHeaders *headers, size_t *length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
