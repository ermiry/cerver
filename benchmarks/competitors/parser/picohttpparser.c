#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <strings.h>
#include <sys/types.h>

#include "backend.h"
#include "picohttpparser.h"

const char *parser_backend_name = { "picohttpparser" };

// the most headers that a request has
#define PARSER_PICO_HEADERS					(1 << 6)

#define PARSER_PICO_DECIMAL					10

// the biggest chunked body of the cases: phr_decode_chunked () decodes in place, a copy of the body is decoded
#define PARSER_PICO_CHUNKED_SIZE			(1 << 16)

static const char *parser_pico_content_length = { "Content-Length" };
static const char *parser_pico_transfer_encoding = { "Transfer-Encoding" };

static char parser_pico_chunked[PARSER_PICO_CHUNKED_SIZE];

static bool parser_pico_named (const struct phr_header *header, const char *name) {

	const size_t length = strlen (name);

	return (header->name_len == length) && !strncasecmp (header->name, name, length);

}

// picohttpparser parses the head: the body of a request is the bytes of its Content-Length after it,
// or the chunks that phr_decode_chunked () reads; returns the bytes of the body, -1 when it is not valid
static ssize_t parser_pico_body (
	const struct phr_header *headers, const size_t count, const char *body, const size_t available
) {

	ssize_t length = 0;
	bool chunked = false;

	for (size_t i = 0; i < count; i++) {
		if (parser_pico_named (&headers[i], parser_pico_content_length)) {
			length = (ssize_t) strtoul (headers[i].value, NULL, PARSER_PICO_DECIMAL);
		}

		else if (parser_pico_named (&headers[i], parser_pico_transfer_encoding)) {
			chunked = true;
		}
	}

	if (chunked) {
		if (available > PARSER_PICO_CHUNKED_SIZE) return -1;
		(void) memcpy (parser_pico_chunked, body, available);

		struct phr_chunked_decoder decoder;
		(void) memset (&decoder, 0, sizeof (struct phr_chunked_decoder));
		decoder.consume_trailer = 1;

		size_t decoded = available;
		const ssize_t left = phr_decode_chunked (&decoder, parser_pico_chunked, &decoded);

		length = (left >= 0) ? (ssize_t) available - left : -1;
	}

	return length;

}

// the request at the start of a text, returns its bytes, 0 when it is not valid or it is not complete
static size_t parser_pico_message (const char *text, const size_t size, int *found) {

	const char *method = NULL;
	size_t method_length = 0;
	const char *path = NULL;
	size_t path_length = 0;
	int minor_version = 0;

	struct phr_header headers[PARSER_PICO_HEADERS];
	size_t count = PARSER_PICO_HEADERS;

	const int head = phr_parse_request (
		text, size, &method, &method_length, &path, &path_length,
		&minor_version, headers, &count, 0
	);

	size_t used = 0;
	if (head > 0) {
		const ssize_t body = parser_pico_body (headers, count, text + head, size - (size_t) head);
		if ((body >= 0) && (((size_t) head + (size_t) body) <= size)) used = (size_t) head + (size_t) body;
	}

	*found = (int) count;

	return used;

}

int parser_backend_parse (const char *request, const size_t size) {

	int headers = 0;

	return (parser_pico_message (request, size, &headers) == size) ? headers : -1;

}

int parser_backend_pipeline (const char *text, const size_t size) {

	int messages = 0;

	size_t offset = 0;
	while (offset < size) {
		int headers = 0;
		const size_t used = parser_pico_message (text + offset, size - offset, &headers);
		if (!used) return -1;

		offset += used;
		messages += 1;
	}

	return messages;

}
