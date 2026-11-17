#ifndef _CERVER_TEST_CONFORMANCE_OUTCOME_H_
#define _CERVER_TEST_CONFORMANCE_OUTCOME_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llhttp.h>

#include <cerver/http/parser/body.h>
#include <cerver/http/parser/head.h>
#include <cerver/http/parser/parser.h>
#include <cerver/http/requests/method.h>

// what a parser read from a text, written the same way for llhttp and for the parser of cerver:
// the head of each message (its start line and its fields), the bytes of its body (their count and their hash),
// whether its connection is kept and whether it is an upgrade, and the error that ended it
// the policy of cerver is applied to llhttp like its callbacks do: only HTTP/1.0 and HTTP/1.1 are served
// (the other versions are an error of the version), and only the protocol of HTTP

#define OUTCOME_SIZE				(1 << 17)
#define OUTCOME_PIECE_SIZE			(1 << 16)
// the fields of the longest head: llhttp has no limit, so the array of cerver takes every field that fits in the
// head, each one with its line of 3 bytes at least (a name of one byte, its colon and a LF)
#define OUTCOME_FIELD_SMALLEST		3
#define OUTCOME_FIELDS				(OUTCOME_SIZE / OUTCOME_FIELD_SMALLEST)

#define OUTCOME_HASH_SEED			1469598103934665603ull
#define OUTCOME_HASH_PRIME			1099511628211ull

// a 101 ends the messages of HTTP for llhttp even without its headers, cerver says that it is an upgrade
#define OUTCOME_STATUS_SWITCHING	101

#define OUTCOME_VERSION_MAJOR		1
#define OUTCOME_VERSION_MINOR_MAX	1

typedef struct Outcome {

	char text[OUTCOME_SIZE];
	size_t length;

	// the outcome did not fit, it is not compared
	bool overflow;

} Outcome;

static void outcome_init (Outcome *outcome) {

	outcome->text[0] = '\0';
	outcome->length = 0;
	outcome->overflow = false;

}

CERVER_PRINTF_FORMAT (2, 3) static void outcome_add (Outcome *outcome, const char *format, ...) {

	if (outcome->overflow) return;

	va_list arguments;
	va_start (arguments, format);
	const int written = vsnprintf (
		outcome->text + outcome->length, OUTCOME_SIZE - outcome->length, format, arguments
	);
	va_end (arguments);

	if ((written < 0) || ((size_t) written >= (OUTCOME_SIZE - outcome->length))) outcome->overflow = true;
	else outcome->length += (size_t) written;

}

// the bytes of a token, with the line breaks of a folded value left out and the bytes that are not visible escaped
static void outcome_add_bytes (Outcome *outcome, const char *bytes, const size_t length) {

	for (size_t i = 0; i < length; i++) {
		const unsigned char byte = (unsigned char) bytes[i];

		if ((byte == '\r') || (byte == '\n')) continue;

		if ((byte < ' ') || (byte > '~') || (byte == '\\')) outcome_add (outcome, "\\x%02x", byte);
		else outcome_add (outcome, "%c", byte);
	}

}

// the spaces and the tabs after a value are not part of it
static size_t outcome_trimmed (const char *value, size_t length) {

	while (length && ((value[length - 1] == ' ') || (value[length - 1] == '\t'))) length -= 1;

	return length;

}

static uint64_t outcome_hash (uint64_t hash, const char *bytes, const size_t length) {

	for (size_t i = 0; i < length; i++) {
		hash ^= (unsigned char) bytes[i];
		hash *= OUTCOME_HASH_PRIME;
	}

	return hash;

}

#pragma region llhttp

typedef struct OutcomeLlhttp {

	Outcome *outcome;
	unsigned int lenient;

	char protocol[OUTCOME_PIECE_SIZE];
	size_t protocol_length;
	char target[OUTCOME_PIECE_SIZE];
	size_t target_length;
	char reason[OUTCOME_PIECE_SIZE];
	size_t reason_length;

	char field[OUTCOME_PIECE_SIZE];
	size_t field_length;
	char value[OUTCOME_PIECE_SIZE];
	size_t value_length;

	// the fields of the head, written when it is complete
	Outcome fields;

	size_t body_length;
	uint64_t body_hash;

	// the version of the message is not one that cerver serves
	bool unsupported;

	// the fields after the head are trailers
	bool headers_done;

} OutcomeLlhttp;

static bool outcome_piece (char *piece, size_t *length, const char *at, const size_t size) {

	bool fits = ((*length + size) < OUTCOME_PIECE_SIZE);
	if (fits) {
		(void) memcpy (piece + *length, at, size);
		*length += size;
	}

	return fits;

}

static int outcome_llhttp_begin (llhttp_t *parser) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	state->protocol_length = 0;
	state->target_length = 0;
	state->reason_length = 0;
	state->field_length = 0;
	state->value_length = 0;
	state->body_length = 0;
	state->body_hash = OUTCOME_HASH_SEED;
	state->unsupported = false;
	state->headers_done = false;

	outcome_init (&state->fields);

	return 0;

}

static int outcome_llhttp_protocol (llhttp_t *parser, const char *at, size_t length) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	return outcome_piece (state->protocol, &state->protocol_length, at, length) ? 0 : -1;

}

static int outcome_llhttp_url (llhttp_t *parser, const char *at, size_t length) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	return outcome_piece (state->target, &state->target_length, at, length) ? 0 : -1;

}

static int outcome_llhttp_status (llhttp_t *parser, const char *at, size_t length) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	return outcome_piece (state->reason, &state->reason_length, at, length) ? 0 : -1;

}

static int outcome_llhttp_field (llhttp_t *parser, const char *at, size_t length) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	return outcome_piece (state->field, &state->field_length, at, length) ? 0 : -1;

}

static int outcome_llhttp_value (llhttp_t *parser, const char *at, size_t length) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	return outcome_piece (state->value, &state->value_length, at, length) ? 0 : -1;

}

// the trailers after a body are not compared, cerver does not keep them
static int outcome_llhttp_value_complete (llhttp_t *parser) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	// the parser of cerver leaves out of the name the spaces that a lenient flag takes before the colon
	if (!state->headers_done) {
		outcome_add (&state->fields, "field ");
		outcome_add_bytes (&state->fields, state->field, outcome_trimmed (state->field, state->field_length));
		outcome_add (&state->fields, ": ");
		outcome_add_bytes (&state->fields, state->value, outcome_trimmed (state->value, state->value_length));
		outcome_add (&state->fields, "\n");
	}

	state->field_length = 0;
	state->value_length = 0;

	return 0;

}

static const char outcome_http_protocol[] = { "HTTP" };

// the callbacks of cerver: another protocol and a version that it does not serve are errors
static int outcome_llhttp_headers (llhttp_t *parser) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;
	Outcome *outcome = state->outcome;

	// a request line without a version is the one of HTTP/0.9, a response always has its protocol
	const bool http = ((state->protocol_length == (sizeof (outcome_http_protocol) - 1))
		&& !memcmp (state->protocol, outcome_http_protocol, state->protocol_length))
		|| (!state->protocol_length && (parser->type == HTTP_REQUEST));

	if (!http) return -1;

	// a request of HTTP/0.9 takes any method of llhttp, cerver only the ones of its table
	if ((parser->type == HTTP_REQUEST) && !http_request_method_is_valid ((RequestMethod) parser->method)) return -1;

	const bool served = (parser->http_major == OUTCOME_VERSION_MAJOR) && (parser->http_minor <= OUTCOME_VERSION_MINOR_MAX);
	if (!served && !(state->lenient & HTTP_PARSER_LENIENT_VERSION)) {
		state->unsupported = true;
		return -1;
	}

	if (parser->type == HTTP_REQUEST) {
		outcome_add (outcome, "head method=%u target=", (unsigned int) parser->method);
		outcome_add_bytes (outcome, state->target, state->target_length);
	}

	else {
		outcome_add (outcome, "head status=%u reason=", (unsigned int) parser->status_code);
		outcome_add_bytes (outcome, state->reason, state->reason_length);
	}

	outcome_add (outcome, " version=%u.%u\n", (unsigned int) parser->http_major, (unsigned int) parser->http_minor);
	outcome_add (outcome, "%s", state->fields.text);

	// a body is counted from here, the trailers that follow it are not fields
	state->body_length = 0;
	state->headers_done = true;

	return 0;

}

static int outcome_llhttp_body (llhttp_t *parser, const char *at, size_t length) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	state->body_length += length;
	state->body_hash = outcome_hash (state->body_hash, at, length);

	return 0;

}

static int outcome_llhttp_complete (llhttp_t *parser) {

	OutcomeLlhttp *state = (OutcomeLlhttp *) parser->data;

	outcome_add (
		state->outcome, "body %zu %016llx\ncomplete keep=%d upgrade=%d\n",
		state->body_length, (unsigned long long) state->body_hash,
		llhttp_should_keep_alive (parser) ? 1 : 0,
		(parser->upgrade || ((parser->type == HTTP_RESPONSE) && (parser->status_code == OUTCOME_STATUS_SWITCHING))) ? 1 : 0
	);

	return 0;

}

static void outcome_llhttp_error (const OutcomeLlhttp *state, Outcome *outcome) {

	outcome_add (outcome, state->unsupported ? "error version\n" : "error\n");

}

static void outcome_llhttp (
	Outcome *outcome, const bool response, const unsigned int lenient,
	const char *text, const size_t length
) {

	static OutcomeLlhttp state;
	(void) memset (&state, 0, sizeof (OutcomeLlhttp));
	state.outcome = outcome;
	state.lenient = lenient;

	llhttp_settings_t settings;
	llhttp_settings_init (&settings);
	settings.on_message_begin = outcome_llhttp_begin;
	settings.on_protocol = outcome_llhttp_protocol;
	settings.on_url = outcome_llhttp_url;
	settings.on_status = outcome_llhttp_status;
	settings.on_header_field = outcome_llhttp_field;
	settings.on_header_value = outcome_llhttp_value;
	settings.on_header_value_complete = outcome_llhttp_value_complete;
	settings.on_headers_complete = outcome_llhttp_headers;
	settings.on_body = outcome_llhttp_body;
	settings.on_message_complete = outcome_llhttp_complete;

	llhttp_t parser;
	llhttp_init (&parser, response ? HTTP_RESPONSE : HTTP_REQUEST, &settings);
	parser.data = &state;
	// the flags of cerver have the values of the ones of llhttp, that only has a setter for each one
	parser.lenient_flags = (uint16_t) lenient;

	llhttp_errno_t result = llhttp_execute (&parser, text, length);
	if (result == HPE_OK) result = llhttp_finish (&parser);

	if ((result != HPE_OK) && (result != HPE_PAUSED_UPGRADE)) outcome_llhttp_error (&state, outcome);

}

#pragma endregion

#pragma region cerver

static void outcome_cerver_head (Outcome *outcome, const CerverHttpParser *parser) {

	const CerverHttpHead *head = &parser->head;

	if (parser->type == CERVER_HTTP_PARSER_TYPE_REQUEST) {
		outcome_add (outcome, "head method=%u target=", head->method);
		outcome_add_bytes (outcome, cerver_http_head_at (head, head->target), head->target.length);
	}

	else {
		outcome_add (outcome, "head status=%u reason=", head->status);
		outcome_add_bytes (outcome, cerver_http_head_at (head, head->reason), head->reason.length);
	}

	outcome_add (outcome, " version=%u.%u\n", head->major, head->minor);

	for (unsigned int i = 0; i < head->fields_count; i++) {
		const CerverHttpField *field = &head->fields[i];
		outcome_add (outcome, "field ");
		outcome_add_bytes (outcome, cerver_http_head_at (head, field->name), field->name.length);
		outcome_add (outcome, ": ");
		outcome_add_bytes (outcome, cerver_http_head_at (head, field->value), field->value.length);
		outcome_add (outcome, "\n");
	}

}

static void outcome_cerver_error (Outcome *outcome, const CerverHttpParser *parser) {

	if (getenv ("OUTCOME_DEBUG")) {
		(void) fprintf (stderr, "cerver error %s - state %u\n", cerver_http_parser_error_string (parser->error), parser->state);
	}

	outcome_add (
		outcome, (parser->error == CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED) ? "error version\n" : "error\n"
	);

}

// the bytes of a text in pieces of a size (0 is the whole text), the head gathered like cerver does
typedef struct OutcomeFeed {

	CerverHttpParser parser;
	CerverHttpField fields[OUTCOME_FIELDS];

	char head[OUTCOME_SIZE];
	size_t head_length;
	bool in_body;

	size_t body_length;
	uint64_t body_hash;

	bool done;

} OutcomeFeed;

static void outcome_feed_complete (Outcome *outcome, OutcomeFeed *feed) {

	CerverHttpParser *parser = &feed->parser;

	outcome_add (
		outcome, "body %zu %016llx\ncomplete keep=%d upgrade=%d\n",
		feed->body_length, (unsigned long long) feed->body_hash,
		cerver_http_parser_keep_alive (parser) ? 1 : 0, parser->upgrade ? 1 : 0
	);

	feed->in_body = false;
	feed->body_length = 0;
	feed->body_hash = OUTCOME_HASH_SEED;

	if (parser->upgrade) feed->done = true;

}

// the bytes of a piece after the head: the body, and the head of the next message after it
static void outcome_feed_body (Outcome *outcome, OutcomeFeed *feed, const char *bytes, size_t length);

static void outcome_feed_head (Outcome *outcome, OutcomeFeed *feed, const char *bytes, const size_t length) {

	CerverHttpParser *parser = &feed->parser;

	if ((feed->head_length + length) > OUTCOME_SIZE) {
		outcome->overflow = true;
		feed->done = true;
		return;
	}

	(void) memcpy (feed->head + feed->head_length, bytes, length);
	const size_t before = feed->head_length;
	feed->head_length += length;

	size_t consumed = 0;
	const CerverHttpParserResult result = cerver_http_parser_head (parser, feed->head, feed->head_length, &consumed);

	switch (result) {
		case CERVER_HTTP_PARSER_RESULT_HEAD: {
			outcome_cerver_head (outcome, parser);
			feed->in_body = true;

			// the bytes of the piece after the head
			const size_t used = consumed - before;
			feed->head_length = 0;
			outcome_feed_body (outcome, feed, bytes + used, length - used);
		} break;

		case CERVER_HTTP_PARSER_RESULT_MORE:
			// the bytes after a message with close that its lenient flag discards
			if (consumed) feed->head_length = 0;
			break;

		default:
			outcome_cerver_error (outcome, parser);
			feed->done = true;
			break;
	}

}

static void outcome_feed_body (Outcome *outcome, OutcomeFeed *feed, const char *bytes, size_t length) {

	CerverHttpParser *parser = &feed->parser;

	bool reading = true;
	while (reading && !feed->done) {
		size_t consumed = 0;
		const char *data = NULL;
		size_t data_length = 0;

		const CerverHttpParserResult result = cerver_http_parser_body (parser, bytes, length, &consumed, &data, &data_length);

		bytes += consumed;
		length -= consumed;

		switch (result) {
			case CERVER_HTTP_PARSER_RESULT_BODY:
				feed->body_length += data_length;
				feed->body_hash = outcome_hash (feed->body_hash, data, data_length);
				break;

			case CERVER_HTTP_PARSER_RESULT_COMPLETE:
				outcome_feed_complete (outcome, feed);
				reading = false;
				if (!feed->done && length) outcome_feed_head (outcome, feed, bytes, length);
				break;

			case CERVER_HTTP_PARSER_RESULT_MORE:
				reading = false;
				break;

			default:
				outcome_cerver_error (outcome, parser);
				feed->done = true;
				break;
		}
	}

}

static void outcome_feed_piece (Outcome *outcome, OutcomeFeed *feed, const char *bytes, const size_t length) {

	if (feed->in_body) outcome_feed_body (outcome, feed, bytes, length);
	else outcome_feed_head (outcome, feed, bytes, length);

}

static void outcome_cerver (
	Outcome *outcome, const bool response, const unsigned int lenient,
	const char *text, const size_t length, const size_t piece
) {

	static OutcomeFeed feed;
	(void) memset (&feed, 0, sizeof (OutcomeFeed));
	feed.body_hash = OUTCOME_HASH_SEED;

	cerver_http_parser_init (
		&feed.parser, response ? CERVER_HTTP_PARSER_TYPE_RESPONSE : CERVER_HTTP_PARSER_TYPE_REQUEST,
		lenient, NULL, feed.fields, OUTCOME_FIELDS
	);

	// without limits, like llhttp
	feed.parser.limits.target_size = 0;
	feed.parser.limits.fields_size = 0;
	feed.parser.limits.fields = 0;

	size_t offset = 0;
	while (!feed.done && (offset < length)) {
		const size_t size = (piece && ((length - offset) > piece)) ? piece : (length - offset);
		outcome_feed_piece (outcome, &feed, text + offset, size);
		offset += size;
	}

	if (!feed.done) {
		const CerverHttpParserResult result = cerver_http_parser_finish (&feed.parser);
		if (result == CERVER_HTTP_PARSER_RESULT_COMPLETE) outcome_feed_complete (outcome, &feed);
		else if (result == CERVER_HTTP_PARSER_RESULT_ERROR) outcome_cerver_error (outcome, &feed.parser);
	}

}

#pragma endregion

#endif
