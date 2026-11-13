#include <stddef.h>

#include <cerver/http/parser/api.h>

#include "backend.h"
#include "callbacks.h"

const char *parser_backend_name = { "cerver" };

int parser_backend_parse (const char *request, const size_t size) {

	return parser_callbacks_parse (request, size);

}
