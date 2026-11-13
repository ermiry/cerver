#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "picohttpparser.h"

#include "backend.h"

const char *parser_backend_name = { "picohttpparser" };

// the most headers that a request has
#define PARSER_PICO_HEADERS					(1 << 6)

#define PARSER_PICO_DECIMAL					10

static const char *parser_pico_content_length = { "Content-Length" };

// picohttpparser parses the head: the body of a request is the bytes of its Content-Length after it
static size_t parser_pico_body (const struct phr_header *headers, const size_t count) {

	size_t body = 0;

	const size_t name_length = strlen (parser_pico_content_length);
	for (size_t i = 0; i < count; i++) {
		if ((headers[i].name_len == name_length) && !strncasecmp (headers[i].name, parser_pico_content_length, name_length)) {
			body = (size_t) strtoul (headers[i].value, NULL, PARSER_PICO_DECIMAL);
		}
	}

	return body;

}

int parser_backend_parse (const char *request, const size_t size) {

	const char *method = NULL;
	size_t method_length = 0;
	const char *path = NULL;
	size_t path_length = 0;
	int minor_version = 0;

	struct phr_header headers[PARSER_PICO_HEADERS];
	size_t count = PARSER_PICO_HEADERS;

	const int head = phr_parse_request (
		request, size, &method, &method_length, &path, &path_length,
		&minor_version, headers, &count, 0
	);

	const bool complete = (head > 0) && (((size_t) head + parser_pico_body (headers, count)) == size);

	return complete ? (int) count : -1;

}
