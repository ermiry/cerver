#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <cerver/http/parser/multipart.h>

#include "backend.h"

const char *multipart_backend_name = { "cerver" };

// what the callbacks of a body saw
typedef struct MultipartCerverState {

	int parts;
	size_t bytes;

} MultipartCerverState;

static int multipart_cerver_on_data (CerverHttpMultipart *parser, const char *at, size_t length) {

	(void) at;

	((MultipartCerverState *) parser->data)->bytes += length;

	return 0;

}

static int multipart_cerver_on_begin (CerverHttpMultipart *parser) {

	((MultipartCerverState *) parser->data)->parts += 1;

	return 0;

}

static int multipart_cerver_on_event (CerverHttpMultipart *parser) {

	(void) parser;

	return 0;

}

static const CerverHttpMultipartCallbacks multipart_cerver_callbacks = {
	multipart_cerver_on_data, multipart_cerver_on_data, multipart_cerver_on_data,
	multipart_cerver_on_begin, multipart_cerver_on_event, multipart_cerver_on_event, multipart_cerver_on_event
};

// the parser lives in the receive of a request, without an allocation
static CerverHttpMultipart multipart_cerver;

int multipart_backend_parse (const char *boundary, const char *body, const size_t size) {

	MultipartCerverState state = { 0, 0 };

	if (!cerver_http_multipart_init (
		&multipart_cerver, boundary, strlen (boundary), &multipart_cerver_callbacks, &state
	)) {
		return -1;
	}

	const size_t read = cerver_http_multipart_execute (&multipart_cerver, body, size);

	return (read == size) ? state.parts : -1;

}
