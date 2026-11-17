#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/http/parser/parser.h"

#include "cerver/http/parser/internal.h"

#define HTTP_PARSER_STATUS_INFORMATION		1
#define HTTP_PARSER_STATUS_HUNDREDS			100
#define HTTP_PARSER_STATUS_NO_CONTENT		204
#define HTTP_PARSER_STATUS_NOT_MODIFIED		304

static const char *http_parser_error_strings[] = {
	#define XX(num, name, string, reason) #string,
	CERVER_HTTP_PARSER_ERROR_MAP (XX)
	#undef XX
};

static const char *http_parser_error_reasons[] = {
	#define XX(num, name, string, reason) #reason,
	CERVER_HTTP_PARSER_ERROR_MAP (XX)
	#undef XX
};

#define HTTP_PARSER_ERRORS		(sizeof (http_parser_error_strings) / sizeof (http_parser_error_strings[0]))

static const char *http_parser_lenient_none = { "none" };

const char *http_parser_lenient_string (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #string;
		HTTP_PARSER_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *http_parser_lenient_description (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #description;
		HTTP_PARSER_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *cerver_http_parser_error_string (const CerverHttpParserError error) {

	return ((unsigned int) error < HTTP_PARSER_ERRORS) ?
		http_parser_error_strings[error] : http_parser_error_strings[CERVER_HTTP_PARSER_ERROR_NONE];

}

const char *cerver_http_parser_error_reason (const CerverHttpParserError error) {

	return ((unsigned int) error < HTTP_PARSER_ERRORS) ?
		http_parser_error_reasons[error] : http_parser_error_reasons[CERVER_HTTP_PARSER_ERROR_NONE];

}

void cerver_http_parser_limits_default (CerverHttpParserLimits *limits) {

	limits->target_size = CERVER_HTTP_PARSER_DEFAULT_TARGET_SIZE;
	limits->fields_size = CERVER_HTTP_PARSER_DEFAULT_FIELDS_SIZE;
	limits->fields = CERVER_HTTP_PARSER_DEFAULT_FIELDS;

}

static void http_parser_message_start (CerverHttpParser *parser) {

	CerverHttpField *fields = parser->head.fields;
	const unsigned int capacity = parser->head.fields_capacity;

	(void) memset (&parser->head, 0, sizeof (CerverHttpHead));
	parser->head.fields = fields;
	parser->head.fields_capacity = capacity;

	parser->state = HTTP_PARSER_STATE_START;
	parser->error = CERVER_HTTP_PARSER_ERROR_NONE;
	parser->offset = 0;
	parser->remaining = 0;
	parser->line = 0;
	parser->trailer = HTTP_PARSER_TRAILER_LINE;
	parser->token_length = 0;
	parser->upgrade = false;
	parser->specials = false;

}

void cerver_http_parser_init (
	CerverHttpParser *parser,
	const CerverHttpParserType type, const unsigned int lenient,
	const CerverHttpParserLimits *limits,
	CerverHttpField *fields, const unsigned int capacity
) {

	http_parser_tables_init ();

	(void) memset (parser, 0, sizeof (CerverHttpParser));

	parser->type = type;
	parser->lenient = lenient & HTTP_PARSER_LENIENT_ALL;

	if (limits) parser->limits = *limits;
	else cerver_http_parser_limits_default (&parser->limits);

	// the zeros are the start of a message (http_parser_message_start ())
	parser->head.fields = fields;
	parser->head.fields_capacity = fields ? capacity : 0;

}

void cerver_http_parser_reset (CerverHttpParser *parser) {

	parser->skip_body = false;

	http_parser_message_start (parser);

}

void cerver_http_parser_fields_set (
	CerverHttpParser *parser,
	CerverHttpField *fields, const unsigned int capacity
) {

	CerverHttpHead *head = &parser->head;

	if (fields && (capacity >= head->fields_count)) {
		if (head->fields_count && (fields != head->fields)) {
			(void) memmove (fields, head->fields, head->fields_count * sizeof (CerverHttpField));
		}

		head->fields = fields;
		head->fields_capacity = capacity;
	}

}

CerverHttpParserError cerver_http_parser_get_error (const CerverHttpParser *parser) {

	return parser->error;

}

bool cerver_http_parser_started (const CerverHttpParser *parser) {

	return (parser->state != HTTP_PARSER_STATE_START) && (parser->state != HTTP_PARSER_STATE_ERROR);

}

bool cerver_http_parser_head_read (const CerverHttpParser *parser) {

	return (parser->state >= HTTP_PARSER_STATE_HEAD) && (parser->state <= HTTP_PARSER_STATE_TRAILERS);

}

CerverHttpParserResult http_parser_fail (
	CerverHttpParser *parser, const CerverHttpParserError error
) {

	parser->error = error;
	parser->state = HTTP_PARSER_STATE_ERROR;

	return CERVER_HTTP_PARSER_RESULT_ERROR;

}

// the end of a response is the end of its connection (RFC 9112 section 6.3) when nothing else gives its length
static bool http_parser_needs_end (const CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;
	const unsigned int status = head->status;

	bool needs = (parser->type == CERVER_HTTP_PARSER_TYPE_RESPONSE)
		&& ((status / HTTP_PARSER_STATUS_HUNDREDS) != HTTP_PARSER_STATUS_INFORMATION)
		&& (status != HTTP_PARSER_STATUS_NO_CONTENT) && (status != HTTP_PARSER_STATUS_NOT_MODIFIED)
		&& !parser->skip_body;

	if (needs && !(head->flags & CERVER_HTTP_HEAD_TRANSFER_ENCODING)) {
		needs = !(head->flags & (CERVER_HTTP_HEAD_CHUNKED | CERVER_HTTP_HEAD_CONTENT_LENGTH));
	}

	else if (needs) {
		needs = !(head->flags & CERVER_HTTP_HEAD_CHUNKED);
	}

	return needs;

}

bool cerver_http_parser_keep_alive (const CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;

	// HTTP/1.1 keeps it unless it is closed, HTTP/1.0 only when it asks for it
	const bool kept = (head->major && head->minor) ?
		!(head->flags & CERVER_HTTP_HEAD_CONNECTION_CLOSE) :
		((head->flags & CERVER_HTTP_HEAD_CONNECTION_KEEP_ALIVE) != 0);

	return kept && !http_parser_needs_end (parser);

}

bool cerver_http_parser_has_body (const CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;

	return (head->flags & CERVER_HTTP_HEAD_CHUNKED) || (head->content_length > 0) || http_parser_needs_end (parser);

}

void cerver_http_parser_rebase (CerverHttpParser *parser, const char *base) {

	parser->head.base = base;

}
