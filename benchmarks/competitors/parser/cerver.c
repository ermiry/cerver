#include <stddef.h>

#include <cerver/http/parser/body.h>
#include <cerver/http/parser/head.h>
#include <cerver/http/parser/parser.h>

#include "backend.h"

const char *parser_backend_name = { "cerver" };

// the most headers that a request has, like the array of picohttpparser
#define PARSER_CERVER_HEADERS				(1 << 6)

static CerverHttpField parser_cerver_fields[PARSER_CERVER_HEADERS];
static CerverHttpParser parser_cerver;

static void parser_cerver_start (CerverHttpParser *parser) {

	cerver_http_parser_init (
		parser, CERVER_HTTP_PARSER_TYPE_REQUEST, HTTP_PARSER_LENIENT_NONE, NULL,
		parser_cerver_fields, PARSER_CERVER_HEADERS
	);

}

// the head and the body of the request at the start of a text, the spans of its head and the views of its body
// are taken; returns the bytes of the request, 0 when it is not valid or it is not complete
static size_t parser_cerver_message (CerverHttpParser *parser, const char *text, const size_t size, int *headers) {

	size_t consumed = 0;
	if (cerver_http_parser_head (parser, text, size, &consumed) != CERVER_HTTP_PARSER_RESULT_HEAD) return 0;

	*headers = (int) parser->head.fields_count;

	size_t offset = consumed;
	CerverHttpParserResult result = CERVER_HTTP_PARSER_RESULT_BODY;
	while (result == CERVER_HTTP_PARSER_RESULT_BODY) {
		const char *data = NULL;
		size_t data_length = 0;
		result = cerver_http_parser_body (parser, text + offset, size - offset, &consumed, &data, &data_length);
		offset += consumed;
	}

	return (result == CERVER_HTTP_PARSER_RESULT_COMPLETE) ? offset : 0;

}

int parser_backend_parse (const char *request, const size_t size) {

	CerverHttpParser *parser = &parser_cerver;
	parser_cerver_start (parser);

	int headers = 0;

	return (parser_cerver_message (parser, request, size, &headers) == size) ? headers : -1;

}

// one parser for the connection, that starts each request after the one that was complete
int parser_backend_pipeline (const char *text, const size_t size) {

	CerverHttpParser *parser = &parser_cerver;
	parser_cerver_start (parser);

	int messages = 0;

	size_t offset = 0;
	while (offset < size) {
		int headers = 0;
		const size_t used = parser_cerver_message (parser, text + offset, size - offset, &headers);
		if (!used) return -1;

		offset += used;
		messages += 1;
	}

	return messages;

}
