#include <stdbool.h>
#include <stddef.h>

#include <http_parser.h>

#include "backend.h"

// the http_parser of Joyent, the parser of cerver until the 2.1b-7 (the one of Node.js before llhttp)
const char *parser_backend_name = { "http_parser" };

// what the callbacks of a text saw
typedef struct ParserJoyentState {

	int headers;
	int messages;

} ParserJoyentState;

static int parser_joyent_on_data (http_parser *parser, const char *at, size_t length) {

	(void) parser;
	(void) at;
	(void) length;

	return 0;

}

// a header of a buffer whole is one call of its name
static int parser_joyent_on_field (http_parser *parser, const char *at, size_t length) {

	(void) at;
	(void) length;

	((ParserJoyentState *) parser->data)->headers += 1;

	return 0;

}

static int parser_joyent_on_complete (http_parser *parser) {

	((ParserJoyentState *) parser->data)->messages += 1;

	return 0;

}

// the spans of the URL, the headers and the body are taken, like a server takes them
static http_parser_settings parser_joyent_settings (void) {

	http_parser_settings settings;
	http_parser_settings_init (&settings);

	settings.on_url = parser_joyent_on_data;
	settings.on_header_field = parser_joyent_on_field;
	settings.on_header_value = parser_joyent_on_data;
	settings.on_body = parser_joyent_on_data;
	settings.on_message_complete = parser_joyent_on_complete;

	return settings;

}

// the messages of a text, -1 when it is not valid
static int parser_joyent_execute (const char *text, const size_t size, ParserJoyentState *state) {

	static bool initialized = false;
	static http_parser_settings settings;
	if (!initialized) {
		settings = parser_joyent_settings ();
		initialized = true;
	}

	http_parser parser;
	http_parser_init (&parser, HTTP_REQUEST);
	parser.data = state;

	const size_t parsed = http_parser_execute (&parser, &settings, text, size);

	return ((parsed == size) && (HTTP_PARSER_ERRNO (&parser) == HPE_OK)) ? state->messages : -1;

}

int parser_backend_parse (const char *request, const size_t size) {

	ParserJoyentState state = { 0, 0 };

	return (parser_joyent_execute (request, size, &state) == 1) ? state.headers : -1;

}

int parser_backend_pipeline (const char *text, const size_t size) {

	ParserJoyentState state = { 0, 0 };

	return parser_joyent_execute (text, size, &state);

}
