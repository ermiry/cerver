#ifndef _CERVER_HTTP_HEADERS_BUFFER_H_
#define _CERVER_HTTP_HEADERS_BUFFER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// finds a header in a compiled header ("Name: value\r\n" lines after its status line),
// the name is compared without case and it stops at the empty line that ends the header
// returns true if it was found, and sets its value without the surrounding spaces
CERVER_PUBLIC bool http_headers_buffer_find (
	const char *buffer, const size_t length,
	const char *name, const size_t name_length,
	const char **value, size_t *value_length
);

#ifdef __cplusplus
}
#endif

#endif
