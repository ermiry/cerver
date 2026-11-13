#ifndef _CERVER_BENCHMARKS_COMPETITORS_PARSER_CALLBACKS_H_
#define _CERVER_BENCHMARKS_COMPETITORS_PARSER_CALLBACKS_H_

// the parse with llhttp and with the parser of cerver, that has its API:
// the file that includes it includes before the header of its parser

#include <stdbool.h>
#include <stddef.h>

// what the callbacks of one request saw
typedef struct ParserCallbacksState {

	int headers;
	bool complete;

} ParserCallbacksState;

static int parser_callbacks_on_data (llhttp_t *parser, const char *at, size_t length) {

	(void) parser;
	(void) at;
	(void) length;

	return 0;

}

static int parser_callbacks_on_header (llhttp_t *parser) {

	((ParserCallbacksState *) parser->data)->headers += 1;

	return 0;

}

static int parser_callbacks_on_complete (llhttp_t *parser) {

	((ParserCallbacksState *) parser->data)->complete = true;

	return 0;

}

// the spans of the URL, the headers and the body are taken, like a server takes them
static llhttp_settings_t parser_callbacks_settings (void) {

	llhttp_settings_t settings;
	llhttp_settings_init (&settings);

	settings.on_url = parser_callbacks_on_data;
	settings.on_header_field = parser_callbacks_on_data;
	settings.on_header_value = parser_callbacks_on_data;
	settings.on_header_value_complete = parser_callbacks_on_header;
	settings.on_body = parser_callbacks_on_data;
	settings.on_message_complete = parser_callbacks_on_complete;

	return settings;

}

static int parser_callbacks_parse (const char *request, const size_t size) {

	static bool initialized = false;
	static llhttp_settings_t settings;
	if (!initialized) {
		settings = parser_callbacks_settings ();
		initialized = true;
	}

	ParserCallbacksState state = { 0, false };

	llhttp_t parser;
	llhttp_init (&parser, HTTP_REQUEST, &settings);
	parser.data = &state;

	const llhttp_errno_t error = llhttp_execute (&parser, request, size);

	return ((error == HPE_OK) && state.complete) ? state.headers : -1;

}

#endif
