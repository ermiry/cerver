#include <stddef.h>

#include "backend.h"
#include "callbacks.h"
#include "llhttp.h"

const char *parser_backend_name = { "llhttp" };

int parser_backend_parse (const char *request, const size_t size) {

	return parser_callbacks_parse (request, size);

}
