#include <stdbool.h>
#include <stddef.h>

#include <multipart_parser.h>

#include "backend.h"

// multipart-parser-c, the one that the parser of cerver replaced in the 3.0b-9
const char *multipart_backend_name = { "multipart-parser-c" };

// what the callbacks of a body saw
typedef struct MultipartLegacyState {

	int parts;
	size_t bytes;

} MultipartLegacyState;

static int multipart_legacy_on_data (multipart_parser *parser, const char *at, size_t length) {

	(void) at;

	((MultipartLegacyState *) multipart_parser_get_data (parser))->bytes += length;

	return 0;

}

static int multipart_legacy_on_begin (multipart_parser *parser) {

	((MultipartLegacyState *) multipart_parser_get_data (parser))->parts += 1;

	return 0;

}

static int multipart_legacy_on_event (multipart_parser *parser) {

	(void) parser;

	return 0;

}

static const multipart_parser_settings multipart_legacy_settings = {
	multipart_legacy_on_data, multipart_legacy_on_data, multipart_legacy_on_data,
	multipart_legacy_on_begin, multipart_legacy_on_event, multipart_legacy_on_event, multipart_legacy_on_event
};

// its parser is allocated for each body, like cerver did until the 3.0b-8
int multipart_backend_parse (const char *boundary, const char *body, const size_t size) {

	MultipartLegacyState state = { 0, 0 };

	multipart_parser *parser = multipart_parser_init (boundary, &multipart_legacy_settings);
	if (!parser) return -1;

	multipart_parser_set_data (parser, &state);

	const size_t read = multipart_parser_execute (parser, body, size);

	multipart_parser_free (parser);

	return (read == size) ? state.parts : -1;

}
