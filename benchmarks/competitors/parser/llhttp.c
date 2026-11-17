#include <stdbool.h>
#include <stddef.h>

#include <llhttp.h>

#include "backend.h"

const char *parser_backend_name = { "llhttp" };

// what the callbacks of a text saw
typedef struct ParserLlhttpState {

	int headers;
	int messages;

} ParserLlhttpState;

static int parser_llhttp_on_data (llhttp_t *parser, const char *at, size_t length) {

	(void) parser;
	(void) at;
	(void) length;

	return 0;

}

static int parser_llhttp_on_header (llhttp_t *parser) {

	((ParserLlhttpState *) parser->data)->headers += 1;

	return 0;

}

static int parser_llhttp_on_complete (llhttp_t *parser) {

	((ParserLlhttpState *) parser->data)->messages += 1;

	return 0;

}

// the spans of the URL, the headers and the body are taken, like a server takes them
static llhttp_settings_t parser_llhttp_settings (void) {

	llhttp_settings_t settings;
	llhttp_settings_init (&settings);

	settings.on_url = parser_llhttp_on_data;
	settings.on_header_field = parser_llhttp_on_data;
	settings.on_header_value = parser_llhttp_on_data;
	settings.on_header_value_complete = parser_llhttp_on_header;
	settings.on_body = parser_llhttp_on_data;
	settings.on_message_complete = parser_llhttp_on_complete;

	return settings;

}

// the messages of a text, -1 when it is not valid
static int parser_llhttp_execute (const char *text, const size_t size, ParserLlhttpState *state) {

	static bool initialized = false;
	static llhttp_settings_t settings;
	if (!initialized) {
		settings = parser_llhttp_settings ();
		initialized = true;
	}

	llhttp_t parser;
	llhttp_init (&parser, HTTP_REQUEST, &settings);
	parser.data = state;

	const llhttp_errno_t error = llhttp_execute (&parser, text, size);

	return (error == HPE_OK) ? state->messages : -1;

}

int parser_backend_parse (const char *request, const size_t size) {

	ParserLlhttpState state = { 0, 0 };

	return (parser_llhttp_execute (request, size, &state) == 1) ? state.headers : -1;

}

int parser_backend_pipeline (const char *text, const size_t size) {

	ParserLlhttpState state = { 0, 0 };

	return parser_llhttp_execute (text, size, &state);

}
