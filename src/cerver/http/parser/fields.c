#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cerver/http/headers/types.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/parser/parser.h"

#include "cerver/http/parser/internal.h"

#define HTTP_PARSER_DECIMAL_BASE			10u

uint8_t http_parser_fields_table[HTTP_PARSER_FIELDS_TABLE_SIZE];
uint8_t http_parser_fields_lengths[HTTP_HEADERS_SIZE];
char http_parser_fields_names[HTTP_HEADERS_SIZE][HTTP_PARSER_FIELD_NAME_SIZE];

static const char http_parser_proxy_connection[] = { "proxy-connection" };

static const char http_parser_close[] = { "close" };
static const char http_parser_keep_alive[] = { "keep-alive" };
static const char http_parser_upgrade[] = { "upgrade" };
static const char http_parser_chunked[] = { "chunked" };

static const char http_parser_list_separator = ',';

#define http_parser_literal_length(literal)		(sizeof (literal) - 1)

#pragma region types

void http_parser_fields_init (void) {

	for (unsigned int type = 0; type < HTTP_HEADER_UNDEFINED; type++) {
		const char *name = cerver_http_header_to_string ((CerverHttpHeader) type);
		const size_t length = strlen (name);

		http_parser_fields_lengths[type] = (uint8_t) length;
		for (size_t i = 0; (i < length) && (i < (HTTP_PARSER_FIELD_NAME_SIZE - 1)); i++) {
			http_parser_fields_names[type][i] = (char) http_parser_lower[(unsigned char) name[i]];
		}

		uint32_t slot = http_parser_fields_key (name, length);
		while (http_parser_fields_table[slot]) slot = (slot + 1) & HTTP_PARSER_FIELDS_TABLE_MASK;

		// 0 is an empty slot
		http_parser_fields_table[slot] = (uint8_t) (type + 1);
	}

}

CerverHttpHeader cerver_http_parser_field_type (const char *name, const size_t length) {

	http_parser_tables_init ();

	CerverHttpHeader found = HTTP_HEADER_UNDEFINED;

	// the bytes of a known name are a token
	bool token = (name != NULL);
	for (size_t i = 0; token && (i < length); i++) token = http_parser_is ((unsigned char) name[i], HTTP_PARSER_CLASS_TOKEN);

	if (token) found = http_parser_field_classify (name, length);

	return found;

}

HttpParserSpecial http_parser_special (
	const char *name, const size_t length, const CerverHttpHeader type
) {

	HttpParserSpecial special = HTTP_PARSER_SPECIAL_NONE;

	switch (type) {
		case HTTP_HEADER_CONNECTION: special = HTTP_PARSER_SPECIAL_CONNECTION; break;
		case HTTP_HEADER_CONTENT_LENGTH: special = HTTP_PARSER_SPECIAL_CONTENT_LENGTH; break;
		case HTTP_HEADER_TRANSFER_ENCODING: special = HTTP_PARSER_SPECIAL_TRANSFER_ENCODING; break;
		case HTTP_HEADER_UPGRADE: special = HTTP_PARSER_SPECIAL_UPGRADE; break;

		default:
			if (http_parser_name_equal (
				name, length, http_parser_proxy_connection, http_parser_literal_length (http_parser_proxy_connection)
			)) {
				special = HTTP_PARSER_SPECIAL_CONNECTION;
			}
			break;
	}

	return special;

}

#pragma endregion

#pragma region values

static inline bool http_parser_is_space (const char byte) {

	return (byte == ' ') || (byte == '\t');

}

// a folded value has its line breaks, that are spaces for its tokens
static inline bool http_parser_is_folded_space (const char byte) {

	return http_parser_is_space (byte) || (byte == '\r') || (byte == '\n');

}

// the bytes of an element of a list without the spaces around it
static void http_parser_list_trim (const char **element, size_t *length) {

	while (*length && http_parser_is_folded_space (**element)) {
		*element += 1;
		*length -= 1;
	}

	while (*length && http_parser_is_folded_space ((*element)[*length - 1])) *length -= 1;

}

static unsigned int http_parser_connection_flag (const char *element, size_t length) {

	unsigned int flag = 0;

	http_parser_list_trim (&element, &length);

	if (http_parser_name_equal (element, length, http_parser_close, http_parser_literal_length (http_parser_close))) {
		flag = CERVER_HTTP_HEAD_CONNECTION_CLOSE;
	}

	else if (http_parser_name_equal (element, length, http_parser_keep_alive, http_parser_literal_length (http_parser_keep_alive))) {
		flag = CERVER_HTTP_HEAD_CONNECTION_KEEP_ALIVE;
	}

	else if (http_parser_name_equal (element, length, http_parser_upgrade, http_parser_literal_length (http_parser_upgrade))) {
		flag = CERVER_HTTP_HEAD_CONNECTION_UPGRADE;
	}

	return flag;

}

// the tokens of a Connection (RFC 9110 section 7.6.1), case insensitive
static CerverHttpParserError http_parser_value_connection (
	CerverHttpParser *parser, const char *value, const size_t length
) {

	size_t start = 0;
	for (size_t i = 0; i <= length; i++) {
		if ((i == length) || (value[i] == http_parser_list_separator)) {
			parser->head.flags |= http_parser_connection_flag (value + start, i - start);
			start = i + 1;
		}
	}

	return CERVER_HTTP_PARSER_ERROR_NONE;

}

// the digits of a Content-Length followed only by spaces and tabs, without a sign (RFC 9110 section 8.6)
static CerverHttpParserError http_parser_value_content_length (
	CerverHttpParser *parser, const char *value, const size_t length
) {

	CerverHttpHead *head = &parser->head;

	if (head->flags & CERVER_HTTP_HEAD_CONTENT_LENGTH) return CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH;

	if (
		(head->flags & CERVER_HTTP_HEAD_TRANSFER_ENCODING)
		&& !(parser->lenient & HTTP_PARSER_LENIENT_CHUNKED_LENGTH)
	) {
		return CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH;
	}

	uint64_t content_length = 0;
	size_t i = 0;
	for (; (i < length) && (value[i] >= '0') && (value[i] <= '9'); i++) {
		const uint64_t digit = (uint64_t) (value[i] - '0');
		if (content_length > ((UINT64_MAX - digit) / HTTP_PARSER_DECIMAL_BASE)) return CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH;

		content_length = (content_length * HTTP_PARSER_DECIMAL_BASE) + digit;
	}

	// at least one digit, and only spaces after them
	CerverHttpParserError error = i ? CERVER_HTTP_PARSER_ERROR_NONE : CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH;
	for (; i < length; i++) {
		if (!http_parser_is_space (value[i])) error = CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH;
	}

	if (!error) {
		head->flags |= CERVER_HTTP_HEAD_CONTENT_LENGTH;
		head->content_length = content_length;
	}

	return error;

}

// an element is chunked when it is the word followed only by spaces
static bool http_parser_coding_is_chunked (const char *element, const size_t length) {

	const size_t chunked_length = http_parser_literal_length (http_parser_chunked);

	bool chunked = (length >= chunked_length)
		&& http_parser_name_equal (element, chunked_length, http_parser_chunked, chunked_length);

	for (size_t i = chunked_length; chunked && (i < length); i++) {
		chunked = (element[i] == ' ');
	}

	return chunked;

}

// a request can not have a coding after chunked (RFC 9112 section 6.1)
static bool http_parser_coding_after_chunked (const CerverHttpParser *parser) {

	return (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST)
		&& !(parser->lenient & HTTP_PARSER_LENIENT_TRANSFER_ENCODING);

}

// the codings of a Transfer-Encoding: chunked is only the last one
static CerverHttpParserError http_parser_value_transfer_encoding (
	CerverHttpParser *parser, const char *value, const size_t length
) {

	CerverHttpHead *head = &parser->head;

	if (
		(head->flags & CERVER_HTTP_HEAD_CONTENT_LENGTH)
		&& !(parser->lenient & HTTP_PARSER_LENIENT_CHUNKED_LENGTH)
	) {
		return CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING;
	}

	// another Transfer-Encoding after one that ended with chunked
	if ((head->flags & CERVER_HTTP_HEAD_CHUNKED) && http_parser_coding_after_chunked (parser)) {
		return CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING;
	}

	head->flags &= ~CERVER_HTTP_HEAD_CHUNKED;
	head->flags |= CERVER_HTTP_HEAD_TRANSFER_ENCODING;

	CerverHttpParserError error = CERVER_HTTP_PARSER_ERROR_NONE;

	size_t start = 0;
	for (size_t i = 0; !error && (i <= length); i++) {
		if ((i == length) || (value[i] == http_parser_list_separator)) {
			const char *element = value + start;
			size_t element_length = i - start;

			// the spaces and tabs after a comma are not part of the next coding
			while (element_length && http_parser_is_space (*element)) {
				element += 1;
				element_length -= 1;
			}

			const bool chunked = http_parser_coding_is_chunked (element, element_length);
			if (chunked && (i < length) && http_parser_coding_after_chunked (parser)) {
				error = CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING;
			}

			if (i == length) {
				if (chunked) head->flags |= CERVER_HTTP_HEAD_CHUNKED;
			}

			start = i + 1;
		}
	}

	return error;

}

CerverHttpParserError http_parser_special_value (
	CerverHttpParser *parser, const HttpParserSpecial special,
	const char *value, const size_t length
) {

	CerverHttpParserError error = CERVER_HTTP_PARSER_ERROR_NONE;

	switch (special) {
		case HTTP_PARSER_SPECIAL_CONNECTION:
			error = http_parser_value_connection (parser, value, length);
			break;

		case HTTP_PARSER_SPECIAL_CONTENT_LENGTH:
			error = http_parser_value_content_length (parser, value, length);
			break;

		case HTTP_PARSER_SPECIAL_TRANSFER_ENCODING:
			error = length ?
				http_parser_value_transfer_encoding (parser, value, length) :
				CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING;
			break;

		case HTTP_PARSER_SPECIAL_UPGRADE:
			if (length) parser->head.flags |= CERVER_HTTP_HEAD_UPGRADE;
			break;

		default: break;
	}

	return error;

}

#pragma endregion

#pragma region head

const char *cerver_http_head_at (
	const CerverHttpHead *head, const CerverHttpSpan span
) {

	return head->base + span.offset;

}

const CerverHttpField *cerver_http_head_field_with_type (
	const CerverHttpHead *head, const CerverHttpHeader type
) {

	const CerverHttpField *found = NULL;

	for (unsigned int i = 0; !found && (i < head->fields_count); i++) {
		if (head->fields[i].type == type) found = &head->fields[i];
	}

	return found;

}

const CerverHttpField *cerver_http_head_field_with_name (
	const CerverHttpHead *head, const char *name, const size_t length
) {

	const CerverHttpField *found = NULL;

	for (unsigned int i = 0; !found && name && (i < head->fields_count); i++) {
		const CerverHttpField *field = &head->fields[i];
		if (field->name.length != length) continue;

		bool equal = true;
		const char *field_name = head->base + field->name.offset;
		for (size_t j = 0; equal && (j < length); j++) {
			equal = (http_parser_lower[(unsigned char) field_name[j]] == http_parser_lower[(unsigned char) name[j]]);
		}

		if (equal) found = field;
	}

	return found;

}

#pragma endregion
