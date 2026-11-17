#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cerver/http/headers/types.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/requests/method.h"

#include "cerver/http/parser/internal.h"

// the longest method of the table (UNSUBSCRIBE), a line without a space after it is not a request
#define HTTP_PARSER_METHOD_LONGEST			11

// the bytes that a request line can have around its target before it is too long
#define HTTP_PARSER_LINE_SLACK				(1 << 8)

#define HTTP_PARSER_VERSION_MAJOR			1
#define HTTP_PARSER_VERSION_MINOR_MAX		1
#define HTTP_PARSER_VERSION_OLD_MAJOR		0
#define HTTP_PARSER_VERSION_OLD_MINOR		9
#define HTTP_PARSER_VERSION_TWO_MAJOR		2
#define HTTP_PARSER_VERSION_TWO_MINOR		0

#define HTTP_PARSER_STATUS_DIGITS			3
// the bytes of a version after "HTTP/": a digit, a dot and a digit
#define HTTP_PARSER_VERSION_DIGITS			3
#define HTTP_PARSER_DECIMAL_BASE			10u

static const char http_parser_protocol[] = { "HTTP/" };
static const char http_parser_method_get[] = { "GET" };
static const char http_parser_method_post[] = { "POST" };
static const char http_parser_authority_start[] = { "//" };

#define http_parser_literal_length(literal)		(sizeof (literal) - 1)

typedef struct HttpParserMethod {

	const char *name;
	size_t length;
	unsigned int value;

} HttpParserMethod;

static const HttpParserMethod http_parser_methods[] = {
	#define XX(num, name, string) { #string, sizeof (#string) - 1, num },
	REQUEST_METHOD_MAP(XX)
	#undef XX
	{ "PRI", sizeof ("PRI") - 1, CERVER_HTTP_PARSER_METHOD_PRI }
};

#define HTTP_PARSER_METHODS			(sizeof (http_parser_methods) / sizeof (http_parser_methods[0]))

// how a line ended
typedef enum HttpParserEnd {

	HTTP_PARSER_END_CRLF	= 0,
	HTTP_PARSER_END_LF		= 1,
	HTTP_PARSER_END_CR		= 2

} HttpParserEnd;

// a complete line of the head
typedef struct HttpParserLine {

	const char *buffer;
	// the bytes of the buffer
	size_t length;
	size_t start;
	// the bytes of the line end before its line break
	size_t end;
	// the first byte of the next line
	size_t next;
	HttpParserEnd ending;
	// the request line has a version, not the one of HTTP/0.9
	bool versioned;

} HttpParserLine;

static inline bool http_parser_has (const CerverHttpParser *parser, const unsigned int flag) {

	return (parser->lenient & flag) != 0;

}

static inline bool http_parser_is_space (const char byte) {

	return (byte == ' ') || (byte == '\t');

}

#pragma region lines

// a line of the head ends with CRLF; the lenient flags take a LF or a CR alone
// (llhttp needs both flags for a LF alone after the request line)
static bool http_parser_line_ending_valid (
	const CerverHttpParser *parser, const HttpParserEnd ending, const bool request_line
) {

	bool valid = true;

	switch (ending) {
		case HTTP_PARSER_END_LF:
			valid = http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF)
				&& (!request_line || http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR));
			break;

		case HTTP_PARSER_END_CR:
			valid = http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR);
			break;

		default: break;
	}

	return valid;

}

// finds the next complete line from the offset of the parser
// returns false when the line is not complete yet
static bool http_parser_line_next (
	const CerverHttpParser *parser, const char *buffer, const size_t length, HttpParserLine *line
) {

	const size_t start = parser->offset;
	const bool carriage = http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR);

	const size_t found = start + http_parser_scan_line (buffer + start, length - start, carriage);
	if (found >= length) return false;

	line->buffer = buffer;
	line->length = length;
	line->start = start;
	line->versioned = false;

	if (buffer[found] == '\n') {
		const bool crlf = (found > start) && (buffer[found - 1] == '\r');
		line->end = crlf ? (found - 1) : found;
		line->next = found + 1;
		line->ending = crlf ? HTTP_PARSER_END_CRLF : HTTP_PARSER_END_LF;

		// the request line that ends with a LF alone needs the byte after it (http_parser_head_line ())
		return crlf || (parser->state != HTTP_PARSER_STATE_START)
			|| (parser->type != CERVER_HTTP_PARSER_TYPE_REQUEST) || (line->next < length);
	}

	// a CR: the byte after it says whether it ends the line alone
	if ((found + 1) >= length) return false;

	const bool crlf = (buffer[found + 1] == '\n');
	line->end = found;
	line->next = crlf ? (found + 2) : (found + 1);
	line->ending = crlf ? HTTP_PARSER_END_CRLF : HTTP_PARSER_END_CR;

	return true;

}

#pragma endregion

#pragma region target

// an authority (RFC 3986 section 3.2): its userinfo, its host and its port,
// it ends with the path or the query of an absolute target
static bool http_parser_authority_valid (const char *target, const size_t length, size_t i) {

	bool valid = true;
	bool at = false;

	for (; valid && (i < length); i++) {
		const unsigned char byte = (unsigned char) target[i];
		if ((byte == '/') || (byte == '?')) break;

		if (byte == '@') {
			valid = !at;
			at = true;
		}

		else {
			valid = http_parser_is (byte, HTTP_PARSER_CLASS_AUTHORITY);
			at = false;
		}
	}

	return valid;

}

// an absolute target: its scheme, "://" and its authority
static bool http_parser_absolute_valid (const char *target, const size_t length) {

	size_t i = 0;
	while ((i < length) && http_parser_is ((unsigned char) target[i], HTTP_PARSER_CLASS_ALPHA)) i++;

	const size_t start_length = http_parser_literal_length (http_parser_authority_start);

	bool valid = (i < length) && (target[i] == ':')
		&& ((length - i - 1) >= start_length)
		&& !memcmp (target + i + 1, http_parser_authority_start, start_length);

	return valid && http_parser_authority_valid (target, length, i + 1 + start_length);

}

// the four forms of a target (RFC 9112 section 3.2): origin and asterisk start with '/' or '*',
// absolute with a scheme, and authority only for CONNECT; every byte is visible ASCII
static bool http_parser_target_valid (
	const char *target, const size_t length, const bool connect
) {

	// visible ASCII: no byte below '!', no DEL and no byte with its high bit
	bool valid = (length > 0) && (http_parser_scan_visible (target, length) == length);

	if (valid) {
		if (connect) valid = http_parser_authority_valid (target, length, 0);
		else if ((target[0] == '/') || (target[0] == '*')) valid = true;
		else if (http_parser_is ((unsigned char) target[0], HTTP_PARSER_CLASS_ALPHA)) valid = http_parser_absolute_valid (target, length);
		else valid = false;
	}

	return valid;

}

#pragma endregion

#pragma region start

static unsigned int http_parser_method_find (const char *name, const size_t length) {

	// the methods of almost every request first
	if ((length == http_parser_literal_length (http_parser_method_get)) && !memcmp (name, http_parser_method_get, length)) {
		return REQUEST_METHOD_GET;
	}

	if ((length == http_parser_literal_length (http_parser_method_post)) && !memcmp (name, http_parser_method_post, length)) {
		return REQUEST_METHOD_POST;
	}

	unsigned int found = REQUEST_METHOD_UNDEFINED;

	for (size_t i = 0; i < HTTP_PARSER_METHODS; i++) {
		const HttpParserMethod *method = &http_parser_methods[i];
		if ((method->length == length) && !memcmp (method->name, name, length)) {
			found = method->value;
			break;
		}
	}

	return found;

}

static inline bool http_parser_is_digit (const char byte) {

	return (byte >= '0') && (byte <= '9');

}

// HTTP/1.0 and HTTP/1.1 are served, 0.9 and 2.0 are valid versions that are not,
// and the lenient flag of the version takes any one
static CerverHttpParserError http_parser_version_check (
	const CerverHttpParser *parser, const unsigned int major, const unsigned int minor
) {

	CerverHttpParserError error = CERVER_HTTP_PARSER_ERROR_NONE;

	if (!http_parser_has (parser, HTTP_PARSER_LENIENT_VERSION)) {
		const bool served = (major == HTTP_PARSER_VERSION_MAJOR) && (minor <= HTTP_PARSER_VERSION_MINOR_MAX);

		const bool known = ((major == HTTP_PARSER_VERSION_OLD_MAJOR) && (minor == HTTP_PARSER_VERSION_OLD_MINOR))
			|| ((major == HTTP_PARSER_VERSION_TWO_MAJOR) && (minor == HTTP_PARSER_VERSION_TWO_MINOR));

		if (!served) error = known ? CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED : CERVER_HTTP_PARSER_ERROR_VERSION;
	}

	return error;

}

// "HTTP/" DIGIT "." DIGIT from p, the version is read in the head
static CerverHttpParserError http_parser_version_read (
	CerverHttpParser *parser, const char *buffer, size_t p, const size_t end, size_t *after
) {

	const size_t protocol_length = http_parser_literal_length (http_parser_protocol);
	if (((end - p) < protocol_length) || memcmp (buffer + p, http_parser_protocol, protocol_length)) {
		return CERVER_HTTP_PARSER_ERROR_PROTOCOL;
	}

	p += protocol_length;

	// one digit, a dot and one digit
	const size_t version_length = HTTP_PARSER_VERSION_DIGITS;
	if (
		((end - p) < version_length)
		|| !http_parser_is_digit (buffer[p]) || (buffer[p + 1] != '.') || !http_parser_is_digit (buffer[p + 2])
	) {
		return CERVER_HTTP_PARSER_ERROR_VERSION;
	}

	parser->head.major = (unsigned int) (buffer[p] - '0');
	parser->head.minor = (unsigned int) (buffer[p + 2] - '0');

	*after = p + version_length;

	return CERVER_HTTP_PARSER_ERROR_NONE;

}

static inline size_t http_parser_skip_spaces (const char *buffer, size_t p, const size_t end) {

	while ((p < end) && (buffer[p] == ' ')) p++;

	return p;

}

static CerverHttpParserError http_parser_target_check (
	const CerverHttpParser *parser, const char *buffer, const size_t start, const size_t length
) {

	const size_t limit = parser->limits.target_size;
	if (limit && (length > limit)) return CERVER_HTTP_PARSER_ERROR_TARGET_SIZE;

	const bool connect = (parser->head.method == REQUEST_METHOD_CONNECT);

	// the authority of a CONNECT can be empty before the end of its line (llhttp)
	if (connect && !length) return CERVER_HTTP_PARSER_ERROR_NONE;

	return http_parser_target_valid (buffer + start, length, connect) ?
		CERVER_HTTP_PARSER_ERROR_NONE : CERVER_HTTP_PARSER_ERROR_TARGET;

}

// a request line without a version is the one of HTTP/0.9
static CerverHttpParserError http_parser_request_old (
	CerverHttpParser *parser, const HttpParserLine *line
) {

	CerverHttpParserError error = CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED;

	// the preface of HTTP/2 is never a request
	if (parser->head.method == CERVER_HTTP_PARSER_METHOD_PRI) {
		error = CERVER_HTTP_PARSER_ERROR_METHOD;
	}

	else if (http_parser_has (parser, HTTP_PARSER_LENIENT_VERSION) && (line->ending != HTTP_PARSER_END_CR)) {
		parser->head.major = HTTP_PARSER_VERSION_OLD_MAJOR;
		parser->head.minor = HTTP_PARSER_VERSION_OLD_MINOR;
		error = CERVER_HTTP_PARSER_ERROR_NONE;
	}

	return error;

}

// method SP target SP HTTP-version (RFC 9112 section 3), with the spaces of llhttp around the target
static CerverHttpParserError http_parser_request_line (
	CerverHttpParser *parser, HttpParserLine *line
) {

	const char *buffer = line->buffer;
	const size_t end = line->end;
	CerverHttpHead *head = &parser->head;

	size_t p = line->start;
	while ((p < end) && (buffer[p] != ' ')) p++;

	head->method = http_parser_method_find (buffer + line->start, p - line->start);
	if ((p == end) || (head->method == REQUEST_METHOD_UNDEFINED)) return CERVER_HTTP_PARSER_ERROR_METHOD;

	p = http_parser_skip_spaces (buffer, p, end);

	const size_t target_start = p;
	while ((p < end) && (buffer[p] != ' ')) p++;

	head->target.offset = (uint32_t) target_start;
	head->target.length = (uint32_t) (p - target_start);

	CerverHttpParserError error = http_parser_target_check (parser, buffer, target_start, p - target_start);
	if (error) return error;

	if (p == end) return http_parser_request_old (parser, line);

	p = http_parser_skip_spaces (buffer, p, end);
	line->versioned = true;

	size_t after = 0;
	error = http_parser_version_read (parser, buffer, p, end, &after);
	if (!error && (after != end)) error = CERVER_HTTP_PARSER_ERROR_VERSION;
	if (!error) error = http_parser_version_check (parser, head->major, head->minor);

	// the preface of HTTP/2 is not a request of HTTP/1.1
	if (!error && (head->method == CERVER_HTTP_PARSER_METHOD_PRI)) error = CERVER_HTTP_PARSER_ERROR_METHOD;

	if (!error && !http_parser_line_ending_valid (parser, line->ending, false)) error = CERVER_HTTP_PARSER_ERROR_LINE;

	return error;

}

// HTTP-version SP status-code [ SP reason-phrase ] (RFC 9112 section 4)
static CerverHttpParserError http_parser_status_line (
	CerverHttpParser *parser, const HttpParserLine *line
) {

	const char *buffer = line->buffer;
	const size_t end = line->end;
	CerverHttpHead *head = &parser->head;

	size_t p = 0;
	CerverHttpParserError error = http_parser_version_read (parser, buffer, line->start, end, &p);
	if (!error) error = http_parser_version_check (parser, head->major, head->minor);
	if (error) return error;

	if ((p == end) || (buffer[p] != ' ')) return CERVER_HTTP_PARSER_ERROR_VERSION;
	p += 1;

	unsigned int status = 0;
	for (unsigned int digit = 0; digit < HTTP_PARSER_STATUS_DIGITS; digit++, p++) {
		if ((p == end) || !http_parser_is_digit (buffer[p])) return CERVER_HTTP_PARSER_ERROR_STATUS;
		status = (status * HTTP_PARSER_DECIMAL_BASE) + (unsigned int) (buffer[p] - '0');
	}

	head->status = status;

	// the reason is anything until the end of the line, a CR always ends it
	if (p < end) {
		if (buffer[p] != ' ') return CERVER_HTTP_PARSER_ERROR_STATUS;
		head->reason.offset = (uint32_t) (p + 1);
		head->reason.length = (uint32_t) (end - p - 1);

		if (memchr (buffer + p, '\r', end - p)) return CERVER_HTTP_PARSER_ERROR_LINE;
	}

	return http_parser_line_ending_valid (parser, line->ending, false) ?
		CERVER_HTTP_PARSER_ERROR_NONE : CERVER_HTTP_PARSER_ERROR_LINE;

}

#pragma endregion

#pragma region fields

// HTAB, SP, VCHAR and obs-text (RFC 9110 section 5.5)
static bool http_parser_value_strict (const char *value, const size_t length) {

	bool valid = true;

	size_t i = 0;
	while (valid && (i < length)) {
		i += http_parser_scan_value (value + i, length - i);
		if (i < length) {
			valid = (value[i] == '\t');
			i += 1;
		}
	}

	return valid;

}

// the bytes of a value: strict, or with the lenient flag of the headers anything but a CR, that always ends a line
static bool http_parser_value_valid (
	const CerverHttpParser *parser, const char *value, const size_t length
) {

	return http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS) ?
		(memchr (value, '\r', length) == NULL) : http_parser_value_strict (value, length);

}

// the spaces and the tabs after a value are not part of it (RFC 9110 section 5.5)
static size_t http_parser_value_trimmed (const char *value, size_t length) {

	while (length && http_parser_is_space (value[length - 1])) length -= 1;

	return length;

}

static bool http_parser_field_empty_cr (
	const CerverHttpParser *parser, HttpParserLine *line, const size_t p
);

// a line that starts with a space or a tab folds the value of the field before it (obs-fold),
// only with the lenient flag of the headers
static CerverHttpParserError http_parser_field_fold (
	CerverHttpParser *parser, HttpParserLine *line
) {

	CerverHttpHead *head = &parser->head;
	CerverHttpField *field = &head->fields[head->fields_count - 1];

	if (!http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)) return CERVER_HTTP_PARSER_ERROR_FIELD_VALUE;

	const char *buffer = line->buffer;
	size_t p = line->start;
	while ((p < line->end) && http_parser_is_space (buffer[p])) p++;

	// while the value is empty, a CR alone after the spaces is another line break (llhttp)
	if (!field->value.length && ((line->ending == HTTP_PARSER_END_CR) || http_parser_field_empty_cr (parser, line, p))) {
		return (p == line->end) ? http_parser_count_field (parser, line->end - line->start) : CERVER_HTTP_PARSER_ERROR_LINE;
	}

	if (line->ending == HTTP_PARSER_END_CR) return CERVER_HTTP_PARSER_ERROR_LINE;

	if (!http_parser_value_valid (parser, buffer + p, line->end - p)) return CERVER_HTTP_PARSER_ERROR_FIELD_VALUE;

	// a LF alone, like after a value: only after a byte that only the lenient flag takes
	if (
		!http_parser_line_ending_valid (parser, line->ending, false)
		&& ((p == line->end) || http_parser_value_strict (buffer + p, line->end - p))
	) {
		return CERVER_HTTP_PARSER_ERROR_LINE;
	}

	const size_t length = http_parser_value_trimmed (buffer + p, line->end - p);

	if (length) {
		if (!field->value.length) field->value.offset = (uint32_t) p;
		else field->flags |= CERVER_HTTP_FIELD_FOLDED;

		field->value.length = (uint32_t) (p + length - field->value.offset);
	}

	return http_parser_count_field (parser, line->end - p);

}

// a field after a space before its colon: only the special fields with the lenient flag of the headers
static bool http_parser_field_spaced (
	const CerverHttpParser *parser, const char *name, const size_t length
) {

	return http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)
		&& (http_parser_special (name, length, http_parser_field_classify (name, length)) != HTTP_PARSER_SPECIAL_NONE);

}

static size_t http_parser_field_name (
	const CerverHttpParser *parser, const char *buffer, size_t p, const size_t end, CerverHttpParserError *error
) {

	const size_t start = p;
	while ((p < end) && http_parser_is ((unsigned char) buffer[p], HTTP_PARSER_CLASS_TOKEN)) p++;

	const size_t length = p - start;

	if (length && (p < end) && (buffer[p] == ' ') && http_parser_field_spaced (parser, buffer + start, length)) {
		p = http_parser_skip_spaces (buffer, p, end);
	}

	if (!length || (p == end) || (buffer[p] != ':')) *error = CERVER_HTTP_PARSER_ERROR_FIELD_NAME;

	return length;

}

static CerverHttpParserResult http_parser_field_store (
	CerverHttpParser *parser, const CerverHttpField *field, const size_t bytes
) {

	CerverHttpHead *head = &parser->head;

	if (head->fields_count >= head->fields_capacity) return CERVER_HTTP_PARSER_RESULT_FIELDS;

	head->fields_counted += 1;
	if (parser->limits.fields && (head->fields_counted > parser->limits.fields)) {
		return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE);
	}

	const CerverHttpParserError error = http_parser_count_field (parser, bytes);
	if (error) return http_parser_fail (parser, error);

	head->fields[head->fields_count] = *field;
	head->fields_count += 1;

	if (http_parser_special_candidate (field)) parser->specials = true;

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

// an empty value followed by a CR alone ends its line there, with the lenient flag of the headers (llhttp)
static bool http_parser_field_empty_cr (
	const CerverHttpParser *parser, HttpParserLine *line, const size_t p
) {

	const bool empty_cr = http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)
		&& (p < line->end) && (line->buffer[p] == '\r');

	if (empty_cr) {
		line->next = p + 1;
		line->end = p;
	}

	return empty_cr;

}

// field-name ":" OWS field-value OWS (RFC 9112 section 5)
static CerverHttpParserResult http_parser_field_line (
	CerverHttpParser *parser, HttpParserLine *line
) {

	const char *buffer = line->buffer;
	size_t end = line->end;

	// a CR alone only ends an empty value, with the lenient flag of the headers (llhttp)
	const bool carriage = (line->ending == HTTP_PARSER_END_CR);
	if (carriage && !http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

	// a LF alone after a value with a byte that only the lenient flag of the headers takes is its end for llhttp
	const bool ending_valid = carriage || http_parser_line_ending_valid (parser, line->ending, false);
	if (!ending_valid && !http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

	size_t p = line->start;

	// a space before the first field is only skipped with the lenient flag of the headers
	if (http_parser_is_space (buffer[p])) {
		if (parser->head.fields_count) {
			const CerverHttpParserError fold = http_parser_field_fold (parser, line);
			return fold ? http_parser_fail (parser, fold) : CERVER_HTTP_PARSER_RESULT_MORE;
		}

		if ((buffer[p] != ' ') || !http_parser_has (parser, HTTP_PARSER_LENIENT_HEADERS)) {
			return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_FIELD_NAME);
		}

		p += 1;

		// the space and the end of the line: the empty line that ends the head
		if (p == end) {
			return ending_valid ?
				http_parser_head_complete (parser) : http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
		}
	}

	CerverHttpParserError error = CERVER_HTTP_PARSER_ERROR_NONE;

	CerverHttpField field = { { (uint32_t) p, 0 }, { 0, 0 }, HTTP_HEADER_UNDEFINED, 0 };
	field.name.length = (uint32_t) http_parser_field_name (parser, buffer, p, end, &error);
	if (error) return http_parser_fail (parser, error);

	// the colon, after the spaces that the lenient flag takes
	const char *colon = (const char *) memchr (buffer + p, ':', end - p);
	p = (size_t) (colon - buffer) + 1;
	while ((p < end) && http_parser_is_space (buffer[p])) p++;

	// an empty value that ends at a CR alone is a line of its own: its end is that CR, not the one of the bytes after it
	const bool empty_cr = http_parser_field_empty_cr (parser, line, p);
	if (empty_cr) end = line->end;
	else if (carriage && (p != end)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

	// a CR in a value is a line that did not end with its LF
	if (!http_parser_value_valid (parser, buffer + p, end - p)) {
		return http_parser_fail (
			parser, memchr (buffer + p, '\r', end - p) ? CERVER_HTTP_PARSER_ERROR_LINE : CERVER_HTTP_PARSER_ERROR_FIELD_VALUE
		);
	}

	if (!empty_cr && !ending_valid && ((p == end) || http_parser_value_strict (buffer + p, end - p))) {
		return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
	}

	field.value.offset = (uint32_t) p;
	field.value.length = (uint32_t) http_parser_value_trimmed (buffer + p, end - p);
	field.type = http_parser_field_classify (buffer + field.name.offset, field.name.length);

	return http_parser_field_store (parser, &field, field.name.length + (end - p));

}

#pragma endregion

#pragma region complete

// the special fields are applied in their order once the head is complete,
// so a folded value is read whole
static CerverHttpParserError http_parser_head_specials (CerverHttpParser *parser) {

	CerverHttpHead *head = &parser->head;
	CerverHttpParserError error = CERVER_HTTP_PARSER_ERROR_NONE;

	// a head without them, like the one of almost every GET, is not read again
	if (!parser->specials) return error;

	for (unsigned int i = 0; !error && (i < head->fields_count); i++) {
		const CerverHttpField *field = &head->fields[i];
		if (!http_parser_special_candidate (field)) continue;

		const char *name = head->base + field->name.offset;

		const HttpParserSpecial special = http_parser_special (name, field->name.length, field->type);
		if (special == HTTP_PARSER_SPECIAL_NONE) continue;

		// a folded Content-Length is another one for llhttp; a folded Transfer-Encoding is read whole,
		// and chunked followed by its line break is not chunked
		if ((field->flags & CERVER_HTTP_FIELD_FOLDED) && (special == HTTP_PARSER_SPECIAL_CONTENT_LENGTH)) {
			error = CERVER_HTTP_PARSER_ERROR_CONTENT_LENGTH;
		}

		else {
			error = http_parser_special_value (parser, special, head->base + field->value.offset, field->value.length);
		}
	}

	return error;

}

// a request with a Transfer-Encoding that does not end with chunked has no length (RFC 9112 section 6.3)
static bool http_parser_head_length_unknown (const CerverHttpParser *parser) {

	const unsigned int flags = parser->head.flags;

	return (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST)
		&& (flags & CERVER_HTTP_HEAD_TRANSFER_ENCODING) && !(flags & CERVER_HTTP_HEAD_CHUNKED)
		&& !http_parser_has (parser, HTTP_PARSER_LENIENT_CHUNKED_LENGTH | HTTP_PARSER_LENIENT_TRANSFER_ENCODING);

}

CerverHttpParserResult http_parser_head_complete (CerverHttpParser *parser) {

	CerverHttpHead *head = &parser->head;

	const CerverHttpParserError error = http_parser_head_specials (parser);
	if (error) return http_parser_fail (parser, error);

	const bool upgrade_requested = (head->flags & CERVER_HTTP_HEAD_UPGRADE)
		&& (head->flags & CERVER_HTTP_HEAD_CONNECTION_UPGRADE);

	if (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST) {
		parser->upgrade = upgrade_requested || (head->method == REQUEST_METHOD_CONNECT);
	}

	if (http_parser_head_length_unknown (parser)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_TRANSFER_ENCODING);

	parser->state = HTTP_PARSER_STATE_HEAD;

	return CERVER_HTTP_PARSER_RESULT_HEAD;

}

#pragma endregion

#pragma region partial

// a CR that is followed by another byte than a LF is never the end of a line in the strict mode:
// it is an error as soon as that byte arrives, also in a line that is not complete
static bool http_parser_partial_carriage (
	const CerverHttpParser *parser, const char *buffer, const size_t length
) {

	bool found = false;

	// the lenient flag of the headers takes a CR alone after an empty value, the complete line tells it
	if (!http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR | HTTP_PARSER_LENIENT_HEADERS)) {
		size_t p = parser->offset;
		const char *carriage = NULL;
		while (!found && (p < length) && ((carriage = (const char *) memchr (buffer + p, '\r', length - p)) != NULL)) {
			p = (size_t) (carriage - buffer) + 1;
			found = (p < length) && (buffer[p] != '\n');
		}
	}

	return found;

}

// a request line that is not complete yet can already be a method that does not exist or a target too long
static CerverHttpParserResult http_parser_partial_start (
	CerverHttpParser *parser, const char *buffer, const size_t length
) {

	if (http_parser_partial_carriage (parser, buffer, length)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

	const size_t start = parser->offset;
	const char *space = (const char *) memchr (buffer + start, ' ', length - start);

	if (!space) {
		return ((length - start) > HTTP_PARSER_METHOD_LONGEST) ?
			http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_METHOD) : CERVER_HTTP_PARSER_RESULT_MORE;
	}

	const size_t limit = parser->limits.target_size;
	if (limit) {
		size_t p = http_parser_skip_spaces (buffer, (size_t) (space - buffer), length);
		const size_t target_start = p;
		while ((p < length) && (buffer[p] != ' ')) p++;

		if (((p - target_start) > limit) || ((length - start) > (limit + HTTP_PARSER_LINE_SLACK))) {
			return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_TARGET_SIZE);
		}
	}

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

// the bytes of the name and of the value of a field that is not complete yet
static CerverHttpParserResult http_parser_partial_field (
	CerverHttpParser *parser, const char *buffer, const size_t length
) {

	if (http_parser_partial_carriage (parser, buffer, length)) return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);

	const size_t limit = parser->limits.fields_size;

	if (limit) {
		const size_t start = parser->offset;
		size_t bytes = length - start;

		const char *colon = (const char *) memchr (buffer + start, ':', length - start);
		if (colon) {
			size_t p = (size_t) (colon - buffer) + 1;
			while ((p < length) && http_parser_is_space (buffer[p])) p++;
			bytes -= p - (size_t) (colon - buffer);
		}

		if ((parser->head.fields_size + bytes) > limit) {
			return http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE);
		}
	}

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

#pragma endregion

#pragma region fast

// the result of the fast path when the line is not a common one: the line by line path reads it
#define HTTP_PARSER_FAST_FALLBACK		((CerverHttpParserResult) -1)

// the end of a request line of the fast path, compared whole: the version and its CRLF
static const char http_parser_fast_version_one[] = { "HTTP/1.1\r\n" };
static const char http_parser_fast_version_zero[] = { "HTTP/1.0\r\n" };

// the bytes after the target of the fast path: its SP and the version with its CRLF
#define HTTP_PARSER_FAST_VERSION_BYTES		(1 + http_parser_literal_length (http_parser_fast_version_one))

// the methods of almost every request with their SP, compared whole
static const char http_parser_fast_get[] = { "GET " };
static const char http_parser_fast_post[] = { "POST " };

// the method of the fast path: a method of the table that is not PRI, followed by one SP
static unsigned int http_parser_fast_method (const char *buffer, const size_t start, const size_t length, size_t *space) {

	const size_t available = length - start;

	if ((available >= http_parser_literal_length (http_parser_fast_get)) && !memcmp (
		buffer + start, http_parser_fast_get, http_parser_literal_length (http_parser_fast_get)
	)) {
		*space = start + http_parser_literal_length (http_parser_fast_get) - 1;
		return REQUEST_METHOD_GET;
	}

	if ((available >= http_parser_literal_length (http_parser_fast_post)) && !memcmp (
		buffer + start, http_parser_fast_post, http_parser_literal_length (http_parser_fast_post)
	)) {
		*space = start + http_parser_literal_length (http_parser_fast_post) - 1;
		return REQUEST_METHOD_POST;
	}

	const size_t end = (available > HTTP_PARSER_METHOD_LONGEST) ? (start + HTTP_PARSER_METHOD_LONGEST + 1) : length;

	size_t p = start;
	while ((p < end) && (buffer[p] != ' ')) p++;

	unsigned int method = REQUEST_METHOD_UNDEFINED;
	if ((p < end) && (p > start)) {
		method = http_parser_method_find (buffer + start, p - start);
		*space = p;
	}

	return method;

}

#define HTTP_PARSER_FAST_MINOR_NONE			(-1)
#define HTTP_PARSER_FAST_MINOR_ZERO			0

// "HTTP/1.1" or "HTTP/1.0" and CRLF from p, the minor version or HTTP_PARSER_FAST_MINOR_NONE
static int http_parser_fast_minor (const char *buffer, const size_t p) {

	const size_t bytes = http_parser_literal_length (http_parser_fast_version_one);

	int minor = HTTP_PARSER_FAST_MINOR_NONE;
	if (!memcmp (buffer + p, http_parser_fast_version_one, bytes)) minor = HTTP_PARSER_VERSION_MINOR_MAX;
	else if (!memcmp (buffer + p, http_parser_fast_version_zero, bytes)) minor = HTTP_PARSER_FAST_MINOR_ZERO;

	return minor;

}

// the request line of the strict mode in one pass: a method of the table, one SP, an origin or an asterisk target
// of visible ASCII, one SP and HTTP/1.0 or HTTP/1.1 with its CRLF;
// any other line (or one that is not complete) is read by the line by line path, that gives the same errors
// returns true when the line was read
static bool http_parser_fast_start (CerverHttpParser *parser, const char *buffer, const size_t length) {

	const size_t start = parser->offset;
	size_t space = 0;

	// CONNECT has an authority, PRI is the preface of HTTP/2
	const unsigned int method = http_parser_fast_method (buffer, start, length, &space);
	if (
		(method == REQUEST_METHOD_UNDEFINED) || (method == REQUEST_METHOD_CONNECT)
		|| (method == CERVER_HTTP_PARSER_METHOD_PRI)
	) {
		return false;
	}

	const size_t target = space + 1;
	if ((target >= length) || ((buffer[target] != '/') && (buffer[target] != '*'))) return false;

	const size_t target_end = target + http_parser_scan_visible (buffer + target, length - target);
	const size_t target_length = target_end - target;

	const size_t limit = parser->limits.target_size;
	if ((limit && (target_length > limit)) || ((length - target_end) < HTTP_PARSER_FAST_VERSION_BYTES)) return false;
	if (buffer[target_end] != ' ') return false;

	const int minor = http_parser_fast_minor (buffer, target_end + 1);
	if (minor == HTTP_PARSER_FAST_MINOR_NONE) return false;

	CerverHttpHead *head = &parser->head;
	head->method = method;
	head->target.offset = (uint32_t) target;
	head->target.length = (uint32_t) target_length;
	head->major = HTTP_PARSER_VERSION_MAJOR;
	head->minor = (unsigned int) minor;

	parser->state = HTTP_PARSER_STATE_FIELDS;
	parser->offset = target_end + HTTP_PARSER_FAST_VERSION_BYTES;

	return true;

}

// the value of a field in the strict mode, from its first byte to its CR: HTAB, SP, VCHAR and obs-text
// returns the offset of the byte that ended it, or length when the line is not complete
static inline size_t http_parser_fast_value (const char *buffer, size_t p, const size_t length) {

	for (;;) {
		p += http_parser_scan_value (buffer + p, length - p);
		if ((p >= length) || (buffer[p] != '\t')) break;
		p += 1;
	}

	return p;

}

// a field in one pass in the strict mode: its name as a token until its colon, its value until its CRLF;
// anything else is read by the line by line path, that gives the same errors
static CerverHttpParserResult http_parser_fast_field (
	CerverHttpParser *parser, const char *buffer, const size_t length, const size_t start
) {

	size_t p = start + http_parser_scan_token (buffer + start, length - start);

	if (p >= length) return CERVER_HTTP_PARSER_RESULT_MORE;
	if ((buffer[p] != ':') || (p == start)) return HTTP_PARSER_FAST_FALLBACK;

	const size_t name_length = p - start;

	p += 1;
	while ((p < length) && ((buffer[p] == ' ') || (buffer[p] == '\t'))) p++;

	const size_t value_start = p;
	p = http_parser_fast_value (buffer, p, length);

	if ((p + 1) >= length) return CERVER_HTTP_PARSER_RESULT_MORE;
	if ((buffer[p] != '\r') || (buffer[p + 1] != '\n')) return HTTP_PARSER_FAST_FALLBACK;

	CerverHttpField field = {
		{ (uint32_t) start, (uint32_t) name_length },
		{ (uint32_t) value_start, (uint32_t) http_parser_value_trimmed (buffer + value_start, p - value_start) },
		http_parser_field_classify (buffer + start, name_length), 0
	};

	const CerverHttpParserResult result = http_parser_field_store (parser, &field, name_length + (p - value_start));
	if (result == CERVER_HTTP_PARSER_RESULT_MORE) parser->offset = p + 2;

	return result;

}

// the fields of a head in the strict mode, until the empty line that ends it
static CerverHttpParserResult http_parser_fast_fields (
	CerverHttpParser *parser, const char *buffer, const size_t length
) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	for (;;) {
		const size_t start = parser->offset;
		if (start >= length) break;

		const char byte = buffer[start];

		// the empty line
		if (byte == '\r') {
			if ((start + 1) >= length) break;
			if (buffer[start + 1] != '\n') return HTTP_PARSER_FAST_FALLBACK;

			parser->offset = start + 2;
			return http_parser_head_complete (parser);
		}

		if ((byte == '\n') || (byte == ' ') || (byte == '\t')) return HTTP_PARSER_FAST_FALLBACK;

		result = http_parser_fast_field (parser, buffer, length, start);
		if (parser->offset == start) break;
	}

	return result;

}

#pragma endregion

#pragma region main

// the empty lines before a start line are skipped (RFC 9112 section 2.2)
static void http_parser_head_skip (CerverHttpParser *parser, const char *buffer, const size_t length) {

	while ((parser->offset < length) && ((buffer[parser->offset] == '\r') || (buffer[parser->offset] == '\n'))) {
		parser->offset += 1;
	}

}

static CerverHttpParserResult http_parser_head_start (
	CerverHttpParser *parser, HttpParserLine *line
) {

	const CerverHttpParserError error = (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST) ?
		http_parser_request_line (parser, line) : http_parser_status_line (parser, line);

	if (error) return http_parser_fail (parser, error);

	parser->state = HTTP_PARSER_STATE_FIELDS;

	return CERVER_HTTP_PARSER_RESULT_MORE;

}

static CerverHttpParserResult http_parser_head_line (
	CerverHttpParser *parser, HttpParserLine *line
) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if (parser->state == HTTP_PARSER_STATE_START) {
		result = http_parser_head_start (parser, line);

		// a LF alone after the version waits for the LF of its CR like llhttp: a second LF is consumed with it,
		// and anything else needs the lenient flag of a CR without LF
		if ((result == CERVER_HTTP_PARSER_RESULT_MORE) && line->versioned && (line->ending == HTTP_PARSER_END_LF)) {
			if ((line->next < line->length) && (line->buffer[line->next] == '\n')) line->next += 1;
			else if (!http_parser_has (parser, HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR)) result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
		}
	}

	// the empty line ends the head
	else if (line->start == line->end) {
		result = http_parser_line_ending_valid (parser, line->ending, false) ?
			http_parser_head_complete (parser) :
			http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_LINE);
	}

	else {
		result = http_parser_field_line (parser, line);
	}

	return result;

}

// the head of the next message, after the one that was complete
static CerverHttpParserResult http_parser_head_next (
	CerverHttpParser *parser, const char *buffer, const size_t length, size_t *consumed
) {

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;

	if (parser->state == HTTP_PARSER_STATE_CLOSED) {
		size_t i = 0;
		while ((i < length) && ((buffer[i] == '\r') || (buffer[i] == '\n'))) i++;

		if (i < length) {
			if (http_parser_has (parser, HTTP_PARSER_LENIENT_DATA_AFTER_CLOSE)) *consumed = length;
			else result = http_parser_fail (parser, CERVER_HTTP_PARSER_ERROR_CLOSED);
		}
	}

	else {
		cerver_http_parser_reset (parser);
	}

	return result;

}

CerverHttpParserResult cerver_http_parser_head (
	CerverHttpParser *parser,
	const char *buffer, const size_t length, size_t *consumed
) {

	*consumed = 0;

	if ((parser->state == HTTP_PARSER_STATE_COMPLETE) || (parser->state == HTTP_PARSER_STATE_CLOSED)) {
		const CerverHttpParserResult next = http_parser_head_next (parser, buffer, length, consumed);
		if (parser->state != HTTP_PARSER_STATE_START) return next;
	}

	if (parser->state == HTTP_PARSER_STATE_HEAD) {
		*consumed = parser->head.length;
		return CERVER_HTTP_PARSER_RESULT_HEAD;
	}

	if ((parser->state != HTTP_PARSER_STATE_START) && (parser->state != HTTP_PARSER_STATE_FIELDS)) {
		return CERVER_HTTP_PARSER_RESULT_ERROR;
	}

	parser->head.base = buffer;

	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_MORE;
	while (result == CERVER_HTTP_PARSER_RESULT_MORE) {
		if (parser->state == HTTP_PARSER_STATE_START) {
			http_parser_head_skip (parser, buffer, length);

			// the request line of the strict mode in one pass, then its fields
			if (!parser->lenient && (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST)) {
				if (http_parser_fast_start (parser, buffer, length)) continue;
			}
		}

		// the fields of the strict mode in one pass, a line that is not common is read line by line
		else if (!parser->lenient) {
			result = http_parser_fast_fields (parser, buffer, length);
			if (result == CERVER_HTTP_PARSER_RESULT_MORE) {
				result = http_parser_partial_field (parser, buffer, length);
				break;
			}

			// a line that is not common goes on to the line by line path, that gives its result
			if (result != HTTP_PARSER_FAST_FALLBACK) break;
		}

		HttpParserLine line;
		if (!http_parser_line_next (parser, buffer, length, &line)) {
			result = (parser->state == HTTP_PARSER_STATE_START) ?
				http_parser_partial_start (parser, buffer, length) :
				http_parser_partial_field (parser, buffer, length);
			break;
		}

		result = http_parser_head_line (parser, &line);

		// a line is only consumed when it was read, an array that is full reads it again
		if ((result == CERVER_HTTP_PARSER_RESULT_MORE) || (result == CERVER_HTTP_PARSER_RESULT_HEAD)) {
			parser->offset = line.next;
		}
	}

	if (result == CERVER_HTTP_PARSER_RESULT_HEAD) {
		parser->head.length = parser->offset;
		*consumed = parser->offset;
	}

	// the bytes of a line that is not complete, a message that started for cerver_http_parser_finish ()
	else if (result == CERVER_HTTP_PARSER_RESULT_MORE) {
		parser->line = length - parser->offset;
	}

	return result;

}

#pragma endregion
