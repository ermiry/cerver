#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cerver/http/headers/types.h"
#include "cerver/http/parser/body.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/requests/method.h"

#include "cerver/http/parser/internal.h"

#define HTTP_PARSER_STATUS_CONTINUE			100
#define HTTP_PARSER_STATUS_SWITCHING		101
#define HTTP_PARSER_STATUS_PROCESSING		102
#define HTTP_PARSER_STATUS_EARLY_HINTS		103
#define HTTP_PARSER_STATUS_NO_CONTENT		204
#define HTTP_PARSER_STATUS_NOT_MODIFIED		304

#define HTTP_PARSER_HEX_BITS				4
#define HTTP_PARSER_SIZE_BITS				64
#define HTTP_PARSER_HEX_LETTER				10

static const char http_parser_trailer_content_length[] = { "content-length" };
static const char http_parser_trailer_transfer_encoding[] = { "transfer-encoding" };
static const char http_parser_trailer_connection[] = { "connection" };
static const char http_parser_trailer_proxy_connection[] = { "proxy-connection" };

#define http_parser_literal_length(literal)		(sizeof (literal) - 1)

static inline bool http_parser_has (const CerverHttpParser *parser, const unsigned int flag) {

	return (parser->lenient & flag) != 0;

}

#pragma region complete

// the message is complete: a connection that is not kept only takes empty lines after it
static CerverHttpParserResult http_parser_message_complete (CerverHttpParser *parser) {

	parser->messages += 1;

	const bool closed = !parser->upgrade && !cerver_http_parser_keep_alive (parser)
		&& !http_parser_has (parser, HTTP_PARSER_LENIENT_KEEP_ALIVE);

	parser->state = closed ? HTTP_PARSER_STATE_CLOSED : HTTP_PARSER_STATE_COMPLETE;

	return CERVER_HTTP_PARSER_RESULT_COMPLETE;

}

static bool http_parser_response_without_body (const CerverHttpParser *parser) {

	const unsigned int status = parser->head.status;

	return (parser->type == CERVER_HTTP_PARSER_TYPE_RESPONSE)
		&& (
			(status == HTTP_PARSER_STATUS_CONTINUE) || (status == HTTP_PARSER_STATUS_PROCESSING)
			|| (status == HTTP_PARSER_STATUS_EARLY_HINTS) || (status == HTTP_PARSER_STATUS_NO_CONTENT)
			|| (status == HTTP_PARSER_STATUS_NOT_MODIFIED)
		);

}

// an upgrade ends with its head: a CONNECT, an upgrade without a body, or a 101
static bool http_parser_upgrade_ends (const CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;
	const bool has_body = (head->flags & CERVER_HTTP_HEAD_CHUNKED) || (head->content_length > 0);

	bool ends = false;

	if (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST) {
		ends = parser->upgrade
			&& ((head->method == REQUEST_METHOD_CONNECT) || parser->skip_body || !has_body);
	}

	else if (head->status == HTTP_PARSER_STATUS_SWITCHING) {
		ends = true;
	}

	return ends;

}

// how the body of a head is read (RFC 9112 section 6.3)
static CerverHttpParserResult http_parser_body_start (CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;

	if (http_parser_upgrade_ends (parser)) {
		parser->upgrade = true;
		return http_parser_message_complete (parser);
	}

	if (parser->skip_body || http_parser_response_without_body (parser)) return http_parser_message_complete (parser);

	if (head->flags & CERVER_HTTP_HEAD_CHUNKED) {
		parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_START;
	}

	else if ((head->flags & CERVER_HTTP_HEAD_TRANSFER_ENCODING) || cerver_http_parser_has_body (parser)) {
		if (head->flags & CERVER_HTTP_HEAD_TRANSFER_ENCODING) parser->state = HTTP_PARSER_STATE_EOF;
		else if (head->content_length) {
			parser->state = HTTP_PARSER_STATE_IDENTITY;
			parser->remaining = head->content_length;
		}

		else parser->state = HTTP_PARSER_STATE_EOF;
	}

	else {
		return http_parser_message_complete (parser);
	}

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

// a request without a body and that is not an upgrade, almost every GET, is complete with its head:
// http_parser_body_start () gives the same for it
static inline bool http_parser_request_bodiless (const CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;

	return (parser->state == HTTP_PARSER_STATE_HEAD) && (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST)
		&& !parser->upgrade && !head->content_length
		&& !(head->flags & (CERVER_HTTP_HEAD_CHUNKED | CERVER_HTTP_HEAD_TRANSFER_ENCODING));

}

#pragma endregion

#pragma region chunks

static unsigned int http_parser_hex_value (const unsigned char byte) {

	unsigned int value = 0;

	if ((byte >= '0') && (byte <= '9')) value = (unsigned int) (byte - '0');
	else value = (unsigned int) (http_parser_lower[byte] - 'a') + HTTP_PARSER_HEX_LETTER;

	return value;

}

// adds a digit to the size of the chunk, false when it does not fit in 64 bits
static bool http_parser_chunk_digit (CerverHttpParser *parser, const unsigned char byte) {

	const bool fits = (parser->remaining >> (HTTP_PARSER_SIZE_BITS - HTTP_PARSER_HEX_BITS)) == 0;

	if (fits) parser->remaining = (parser->remaining << HTTP_PARSER_HEX_BITS) | http_parser_hex_value (byte);

	return fits;

}

// the line of a chunk size ended: its data, or the trailers after the last one
static void http_parser_chunk_line_end (CerverHttpParser *parser) {

	if (parser->remaining) {
		parser->state = HTTP_PARSER_STATE_CHUNK_DATA;
	}

	else {
		parser->state = HTTP_PARSER_STATE_TRAILERS;
		parser->trailer = HTTP_PARSER_TRAILER_LINE;
	}

}

// a LF alone after a part of the line of a chunk size
static CerverHttpParserResult http_parser_chunk_line_feed (CerverHttpParser *parser) {

	if (!http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF)) {
		return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
	}

	http_parser_chunk_line_end (parser);

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

// the bytes of the extensions count as the ones of the headers
static CerverHttpParserResult http_parser_chunk_extension_byte (CerverHttpParser *parser) {

	const CerverHttpParserError error = http_parser_count_field (parser, 1);

	return error ? http_parser_fail (parser, error) : CERVER_HTTP_PARSER_RESULT_MORE;

}

// the end of a part of an extension: another one, the end of the line or a LF alone
static CerverHttpParserResult http_parser_chunk_extension_end (
	CerverHttpParser *parser, const unsigned char byte, bool *handled
) {

	*handled = true;

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	switch (byte) {
		case ';': parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSIONS; break;
		case '\r': parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_LF; break;
		case '\n': result = http_parser_chunk_line_feed (parser); break;
		default: *handled = false; break;
	}

	return result;

}

static CerverHttpParserResult http_parser_chunk_size_byte (
	CerverHttpParser *parser, const unsigned char byte
) {

	if (http_parser_is (byte, HTTP_PARSER_CLASS_HEX)) {
		if (parser->state == HTTP_PARSER_STATE_CHUNK_SIZE_SPACES) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);

		parser->state = HTTP_PARSER_STATE_CHUNK_SIZE;

		return http_parser_chunk_digit (parser, byte) ?
			CERVER_HTTP_PARSER_RESULT_MORE : http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);
	}

	if (parser->state == HTTP_PARSER_STATE_CHUNK_SIZE_START) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);

	if ((byte == ' ') || (byte == '\t')) {
		if (!http_parser_has (parser, HTTP_PARSER_LENIENT_SPACES_AFTER_CHUNK_SIZE)) {
			return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);
		}

		parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_SPACES;
		return CERVER_HTTP_PARSER_RESULT_MORE;
	}

	// a LF alone after the size waits for the LF of its CR like llhttp: a second LF is consumed with it,
	// and anything else needs the lenient flag of a CR without LF
	if (byte == '\n') {
		if (!http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

		parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_LF;
		return CERVER_HTTP_PARSER_RESULT_MORE;
	}

	bool handled = false;
	const CerverHttpParserResult result = http_parser_chunk_extension_end (parser, byte, &handled);

	return handled ? result : http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_SIZE);

}

// the name of an extension: a token, then its value, another one or the end of the line
static CerverHttpParserResult http_parser_chunk_extension_name (
	CerverHttpParser *parser, const unsigned char byte
) {

	if (http_parser_is (byte, HTTP_PARSER_CLASS_TOKEN)) return http_parser_chunk_extension_byte (parser);

	if (byte == '=') {
		parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSION_VALUE;
		return CERVER_HTTP_PARSER_RESULT_MORE;
	}

	bool handled = false;
	const CerverHttpParserResult result = http_parser_chunk_extension_end (parser, byte, &handled);

	return handled ? result : http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);

}

// the value of an extension: a token, or a quoted string from any of its bytes
static CerverHttpParserResult http_parser_chunk_extension_value (
	CerverHttpParser *parser, const unsigned char byte
) {

	if (byte == '"') {
		parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED;
		return http_parser_chunk_extension_byte (parser);
	}

	if (http_parser_is (byte, HTTP_PARSER_CLASS_TOKEN)) return http_parser_chunk_extension_byte (parser);

	bool handled = false;
	const CerverHttpParserResult result = http_parser_chunk_extension_end (parser, byte, &handled);

	return handled ? result : http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);

}

static CerverHttpParserResult http_parser_chunk_extension_quoted (
	CerverHttpParser *parser, const unsigned char byte
) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	switch (parser->state) {
		case HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED:
			if (byte == '"') parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED_END;
			else if (byte == '\\') parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSION_PAIR;
			else if (!http_parser_is (byte, HTTP_PARSER_CLASS_QUOTED)) result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);
			break;

		case HTTP_PARSER_STATE_CHUNK_EXTENSION_PAIR:
			if (http_parser_is (byte, HTTP_PARSER_CLASS_VALUE)) parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED;
			else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);
			break;

		default: {
			bool handled = false;
			result = http_parser_chunk_extension_end (parser, byte, &handled);
			if (!handled) result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);
		} break;
	}

	if ((result == CERVER_HTTP_PARSER_RESULT_MORE) && (parser->state != HTTP_PARSER_STATE_ERROR)) {
		result = http_parser_chunk_extension_byte (parser);
	}

	return result;

}

// after a ';': the name of an extension, that can not start with a space or a CR
static CerverHttpParserResult http_parser_chunk_extensions (
	CerverHttpParser *parser, const unsigned char byte
) {

	if ((byte == ' ') || (byte == '\r')) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_EXTENSION);

	parser->state = HTTP_PARSER_STATE_CHUNK_EXTENSION_NAME;

	return http_parser_chunk_extension_name (parser, byte);

}

// the LF after the CR of a chunk size; the lenient flag takes a CR alone, that does not consume the next byte
static CerverHttpParserResult http_parser_chunk_size_lf (
	CerverHttpParser *parser, const unsigned char byte, bool *consumed
) {

	*consumed = true;

	if (byte != '\n') {
		if (!http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
		*consumed = false;
	}

	http_parser_chunk_line_end (parser);

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

// the CRLF after the data of a chunk; a chunk right after it needs its lenient flag, that does not consume the byte
static CerverHttpParserResult http_parser_chunk_data_end (
	CerverHttpParser *parser, const unsigned char byte, bool *consumed
) {

	*consumed = true;

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if ((parser->state == HTTP_PARSER_STATE_CHUNK_DATA_CR) && (byte == '\r')) {
		parser->state = HTTP_PARSER_STATE_CHUNK_DATA_LF;
	}

	else if (byte == '\n') {
		if ((parser->state == HTTP_PARSER_STATE_CHUNK_DATA_CR) && !http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF)) {
			result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_DATA);
		}

		else parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_START;
	}

	else if (http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CRLF_AFTER_CHUNK)) {
		parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_START;
		*consumed = false;
	}

	else {
		result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CHUNK_DATA);
	}

	return result;

}

// the most digits of a chunk size that the fast path reads: they always fit in 64 bits
#define HTTP_PARSER_CHUNK_FAST_DIGITS		15

// the bytes of a CRLF
#define HTTP_PARSER_CRLF_SIZE				2

static inline bool http_parser_crlf_at (const char *buffer, const size_t length, const size_t i) {

	return ((i + 1) < length) && (buffer[i] == '\r') && (buffer[i + 1] == '\n');

}

// the CRLF after the data of a chunk and the line of the next size without extensions, read whole when they are in
// the buffer; anything else is read byte by byte, that gives the same errors (and the same result for these)
// returns the bytes that were read, 0 when the fast path does not apply
static size_t http_parser_chunk_fast (CerverHttpParser *parser, const char *buffer, const size_t length) {

	size_t i = 0;

	if (parser->state == HTTP_PARSER_STATE_CHUNK_DATA_CR) {
		if (!http_parser_crlf_at (buffer, length, i)) return 0;

		parser->state = HTTP_PARSER_STATE_CHUNK_SIZE_START;
		i = HTTP_PARSER_CRLF_SIZE;
	}

	if (parser->state != HTTP_PARSER_STATE_CHUNK_SIZE_START) return i;

	uint64_t size = 0;
	size_t digits = 0;
	while (
		((i + digits) < length) && (digits <= HTTP_PARSER_CHUNK_FAST_DIGITS)
		&& http_parser_is ((unsigned char) buffer[i + digits], HTTP_PARSER_CLASS_HEX)
	) {
		size = (size << HTTP_PARSER_HEX_BITS) | http_parser_hex_value ((unsigned char) buffer[i + digits]);
		digits += 1;
	}

	if (!digits || (digits > HTTP_PARSER_CHUNK_FAST_DIGITS) || !http_parser_crlf_at (buffer, length, i + digits)) return i;

	parser->remaining = size;
	http_parser_chunk_line_end (parser);

	return i + digits + HTTP_PARSER_CRLF_SIZE;

}

#pragma endregion

#pragma region trailers

// the name of a trailer is kept in lower case only while it can be a special one
static void http_parser_trailer_name_byte (CerverHttpParser *parser, const unsigned char byte) {

	if (parser->token_length < CERVER_HTTP_PARSER_TOKEN_SIZE) {
		parser->token[parser->token_length] = (char) http_parser_lower[byte];
	}

	parser->token_length += 1;

}

static bool http_parser_trailer_is (const CerverHttpParser *parser, const char *lower, const size_t length) {

	return (parser->token_length == length) && !memcmp (parser->token, lower, length);

}

// the special trailers: a Content-Length or a Transfer-Encoding in a chunked message is the smuggling of a length
// (llhttp rejects them unless their lenient flags), and a Connection still says whether the connection is kept
static CerverHttpParserResult http_parser_trailer_named (CerverHttpParser *parser) {

	HttpParserSpecial special = HTTP_PARSER_SPECIAL_NONE;

	if (http_parser_trailer_is (parser, http_parser_trailer_content_length, http_parser_literal_length (http_parser_trailer_content_length))) {
		special = HTTP_PARSER_SPECIAL_CONTENT_LENGTH;
	}

	else if (http_parser_trailer_is (parser, http_parser_trailer_transfer_encoding, http_parser_literal_length (http_parser_trailer_transfer_encoding))) {
		special = HTTP_PARSER_SPECIAL_TRANSFER_ENCODING;
	}

	else if (
		http_parser_trailer_is (parser, http_parser_trailer_connection, http_parser_literal_length (http_parser_trailer_connection))
		|| http_parser_trailer_is (parser, http_parser_trailer_proxy_connection, http_parser_literal_length (http_parser_trailer_proxy_connection))
	) {
		special = HTTP_PARSER_SPECIAL_CONNECTION;
	}

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if ((special == HTTP_PARSER_SPECIAL_CONTENT_LENGTH) && !http_parser_has (parser, HTTP_PARSER_LENIENT_CHUNKED_LENGTH)) {
		result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH);
	}

	else if (
		(special == HTTP_PARSER_SPECIAL_TRANSFER_ENCODING) && (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST)
		&& !http_parser_has (parser, HTTP_PARSER_LENIENT_TRANSFER_ENCODING)
	) {
		result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);
	}

	// the token keeps the value of a Connection
	parser->line = (special == HTTP_PARSER_SPECIAL_CONNECTION);
	parser->token_length = 0;

	return result;

}

// the token of a Connection, applied at its comma and at the end of its value
static void http_parser_trailer_token_end (CerverHttpParser *parser) {

	if (parser->line && (parser->token_length <= CERVER_HTTP_PARSER_TOKEN_SIZE)) {
		(void) http_parser_special_value (parser, HTTP_PARSER_SPECIAL_CONNECTION, parser->token, parser->token_length);
	}

	parser->token_length = 0;

}

static CerverHttpParserResult http_parser_trailer_value_byte (CerverHttpParser *parser, const unsigned char byte) {

	const bool valid = http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS) ?
		(byte != '\r') : http_parser_is (byte, HTTP_PARSER_CLASS_VALUE);

	if (!valid) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);

	if (parser->line) {
		if (byte == ',') http_parser_trailer_token_end (parser);
		else http_parser_trailer_name_byte (parser, byte);
	}

	const CerverHttpParserError error = http_parser_count_field (parser, 1);

	return error ? http_parser_fail (parser, error) : CERVER_HTTP_PARSER_RESULT_MORE;

}

// the end of the line of a field: CRLF, or a LF alone with its lenient flag
static CerverHttpParserResult http_parser_trailer_line_end (CerverHttpParser *parser, const unsigned char byte) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if (byte == '\r') parser->trailer = HTTP_PARSER_TRAILER_VALUE_LF;
	else if (http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF)) parser->trailer = HTTP_PARSER_TRAILER_FIELD_END;
	else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

	if (parser->trailer == HTTP_PARSER_TRAILER_FIELD_END) http_parser_trailer_token_end (parser);

	return result;

}

// a trailer counts as a header once it ended (a folded one counts once)
static CerverHttpParserResult http_parser_trailer_counted (CerverHttpParser *parser) {

	CerverHttpHead *head = &parser->head;

	head->fields_counted += 1;

	return (parser->limits.fields && (head->fields_counted > parser->limits.fields)) ?
		http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE) : CERVER_HTTP_PARSER_RESULT_MORE;

}

// the first byte of a line of the trailers: the empty line ends the message, a token starts a field
static CerverHttpParserResult http_parser_trailer_line (
	CerverHttpParser *parser, const unsigned char byte, bool *complete
) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if (byte == '\r') {
		parser->trailer = HTTP_PARSER_TRAILER_END_LF;
	}

	else if (byte == '\n') {
		if (http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF)) *complete = true;
		else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
	}

	else if (http_parser_is (byte, HTTP_PARSER_CLASS_TOKEN)) {
		parser->trailer = HTTP_PARSER_TRAILER_NAME;
		parser->token_length = 0;
		http_parser_trailer_name_byte (parser, byte);

		const CerverHttpParserError error = http_parser_count_field (parser, 1);
		if (error) result = http_parser_fail (parser, error);
	}

	else {
		result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	}

	return result;

}

static CerverHttpParserResult http_parser_trailer_name (CerverHttpParser *parser, const unsigned char byte) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if (http_parser_is (byte, HTTP_PARSER_CLASS_TOKEN) && (parser->trailer == HTTP_PARSER_TRAILER_NAME)) {
		http_parser_trailer_name_byte (parser, byte);

		const CerverHttpParserError error = http_parser_count_field (parser, 1);
		if (error) result = http_parser_fail (parser, error);
	}

	else if (byte == ':') {
		parser->trailer = HTTP_PARSER_TRAILER_SPACES;
		result = http_parser_trailer_named (parser);
	}

	// a space before the colon of a special field, only with the lenient flag of the headers
	else if (
		(byte == ' ') && http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)
		&& (
			http_parser_trailer_is (parser, http_parser_trailer_content_length, http_parser_literal_length (http_parser_trailer_content_length))
			|| http_parser_trailer_is (parser, http_parser_trailer_transfer_encoding, http_parser_literal_length (http_parser_trailer_transfer_encoding))
			|| http_parser_trailer_is (parser, http_parser_trailer_connection, http_parser_literal_length (http_parser_trailer_connection))
			|| http_parser_trailer_is (parser, http_parser_trailer_proxy_connection, http_parser_literal_length (http_parser_trailer_proxy_connection))
			|| (parser->trailer == HTTP_PARSER_TRAILER_NAME_SPACES)
		)
	) {
		parser->trailer = HTTP_PARSER_TRAILER_NAME_SPACES;
	}

	else {
		result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
	}

	return result;

}

static CerverHttpParserResult http_parser_trailer_byte (
	CerverHttpParser *parser, const unsigned char byte, bool *consumed, bool *complete
) {

	*consumed = true;

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	switch (parser->trailer) {
		case HTTP_PARSER_TRAILER_FIELD_END:
			// a space or a tab folds the field (obs-fold), only with the lenient flag of the headers
			if ((byte == ' ') || (byte == '\t')) {
				if (http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)) parser->trailer = HTTP_PARSER_TRAILER_VALUE;
				else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_FIELD_VALUE);
			}

			else {
				result = http_parser_trailer_counted (parser);
				if (result == CERVER_HTTP_PARSER_RESULT_MORE) {
					parser->trailer = HTTP_PARSER_TRAILER_LINE;
					parser->line = 0;
					*consumed = false;
				}
			}
			break;

		case HTTP_PARSER_TRAILER_LINE: result = http_parser_trailer_line (parser, byte, complete); break;

		case HTTP_PARSER_TRAILER_NAME:
		case HTTP_PARSER_TRAILER_NAME_SPACES:
			result = http_parser_trailer_name (parser, byte);
			break;

		case HTTP_PARSER_TRAILER_SPACES:
			if ((byte == ' ') || (byte == '\t')) break;
			parser->trailer = HTTP_PARSER_TRAILER_VALUE;
			result = ((byte == '\r') || (byte == '\n')) ?
				http_parser_trailer_line_end (parser, byte) : http_parser_trailer_value_byte (parser, byte);
			break;

		case HTTP_PARSER_TRAILER_VALUE:
			result = ((byte == '\r') || (byte == '\n')) ?
				http_parser_trailer_line_end (parser, byte) : http_parser_trailer_value_byte (parser, byte);
			break;

		case HTTP_PARSER_TRAILER_VALUE_LF:
			if (byte == '\n') {
				parser->trailer = HTTP_PARSER_TRAILER_FIELD_END;
				http_parser_trailer_token_end (parser);
			}

			else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
			break;

		case HTTP_PARSER_TRAILER_END_LF:
			if (byte != '\n') {
				if (http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR)) *consumed = false;
				else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
			}

			if (result == CERVER_HTTP_PARSER_RESULT_MORE) *complete = true;
			break;

		default: break;
	}

	return result;

}

#pragma endregion

#pragma region main

// one byte of the lines of a chunked body: the sizes, the extensions, the CRLF after the data and the trailers
static CerverHttpParserResult http_parser_chunked_byte (
	CerverHttpParser *parser, const unsigned char byte, bool *consumed, bool *complete
) {

	*consumed = true;

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	switch (parser->state) {
		case HTTP_PARSER_STATE_CHUNK_SIZE_START:
		case HTTP_PARSER_STATE_CHUNK_SIZE:
		case HTTP_PARSER_STATE_CHUNK_SIZE_SPACES:
			result = http_parser_chunk_size_byte (parser, byte);
			break;

		case HTTP_PARSER_STATE_CHUNK_EXTENSIONS: result = http_parser_chunk_extensions (parser, byte); break;
		case HTTP_PARSER_STATE_CHUNK_EXTENSION_NAME: result = http_parser_chunk_extension_name (parser, byte); break;
		case HTTP_PARSER_STATE_CHUNK_EXTENSION_VALUE: result = http_parser_chunk_extension_value (parser, byte); break;

		case HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED:
		case HTTP_PARSER_STATE_CHUNK_EXTENSION_PAIR:
		case HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED_END:
			result = http_parser_chunk_extension_quoted (parser, byte);
			break;

		case HTTP_PARSER_STATE_CHUNK_SIZE_LF: result = http_parser_chunk_size_lf (parser, byte, consumed); break;

		case HTTP_PARSER_STATE_CHUNK_DATA_CR:
		case HTTP_PARSER_STATE_CHUNK_DATA_LF:
			result = http_parser_chunk_data_end (parser, byte, consumed);
			break;

		case HTTP_PARSER_STATE_TRAILERS: result = http_parser_trailer_byte (parser, byte, consumed, complete); break;

		default: break;
	}

	return result;

}

// the data of a Content-Length, of a chunk or until the end of the connection
static CerverHttpParserResult http_parser_body_data (
	CerverHttpParser *parser,
	const char *buffer, const size_t length, size_t *consumed,
	const char **data, size_t *data_length
) {

	size_t taken = length;

	if (parser->state != HTTP_PARSER_STATE_EOF) {
		if (parser->remaining < taken) taken = (size_t) parser->remaining;
		parser->remaining -= taken;
	}

	*data = buffer;
	*data_length = taken;
	*consumed = taken;

	if ((parser->state == HTTP_PARSER_STATE_CHUNK_DATA) && !parser->remaining) {
		parser->state = HTTP_PARSER_STATE_CHUNK_DATA_CR;
	}

	return CERVER_HTTP_PARSER_RESULT_BODY;

}

static inline bool http_parser_state_is_data (const unsigned int state) {

	return (state == HTTP_PARSER_STATE_IDENTITY) || (state == HTTP_PARSER_STATE_EOF)
		|| (state == HTTP_PARSER_STATE_CHUNK_DATA);

}

static inline bool http_parser_state_is_chunked (const unsigned int state) {

	return (state >= HTTP_PARSER_STATE_CHUNK_SIZE_START) && (state <= HTTP_PARSER_STATE_TRAILERS)
		&& (state != HTTP_PARSER_STATE_CHUNK_DATA);

}

CerverHttpParserResult cerver_http_parser_body (
	CerverHttpParser *parser,
	const char *buffer, const size_t length, size_t *consumed,
	const char **data, size_t *data_length
) {

	*consumed = 0;
	*data = NULL;
	*data_length = 0;

	if (http_parser_request_bodiless (parser)) return http_parser_message_complete (parser);

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if (parser->state == HTTP_PARSER_STATE_HEAD) {
		result = http_parser_body_start (parser);
		if (result != CERVER_HTTP_PARSER_RESULT_MORE) return result;
	}

	// a Content-Length that was read whole
	if ((parser->state == HTTP_PARSER_STATE_IDENTITY) && !parser->remaining) return http_parser_message_complete (parser);

	size_t i = 0;
	while ((result == CERVER_HTTP_PARSER_RESULT_MORE) && (i < length)) {
		if (http_parser_state_is_data (parser->state)) {
			result = http_parser_body_data (parser, buffer + i, length - i, consumed, data, data_length);
			*consumed += i;
			return result;
		}

		if (!http_parser_state_is_chunked (parser->state)) break;

		const size_t fast = http_parser_chunk_fast (parser, buffer + i, length - i);
		if (fast) {
			i += fast;
			continue;
		}

		bool byte_consumed = true;
		bool complete = false;
		result = http_parser_chunked_byte (parser, (unsigned char) buffer[i], &byte_consumed, &complete);

		if (byte_consumed) i += 1;

		if ((result == CERVER_HTTP_PARSER_RESULT_MORE) && complete) result = http_parser_message_complete (parser);
	}

	*consumed = i;

	if (parser->state == HTTP_PARSER_STATE_ERROR) result = CERVER_HTTP_PARSER_RESULT_ERROR;

	return result;

}

CerverHttpParserResult cerver_http_parser_finish (CerverHttpParser *parser) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_ERROR;

	switch (parser->state) {
		case HTTP_PARSER_STATE_EOF: result = http_parser_message_complete (parser); break;

		case HTTP_PARSER_STATE_START:
		case HTTP_PARSER_STATE_COMPLETE:
		case HTTP_PARSER_STATE_CLOSED:
			result = ((parser->state == HTTP_PARSER_STATE_START) && parser->line) ?
				http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_INCOMPLETE) : CERVER_HTTP_PARSER_RESULT_MORE;
			break;

		case HTTP_PARSER_STATE_ERROR: break;

		default: result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_INCOMPLETE); break;
	}

	return result;

}

#pragma endregion
