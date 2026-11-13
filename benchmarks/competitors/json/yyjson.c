#include <stddef.h>
#include <stdlib.h>

#include <yyjson.h>

#include "backend.h"

const char *json_backend_name = { "yyjson" };

void *json_backend_parse (const char *text, const size_t size) {

	return yyjson_read (text, size, 0);

}

size_t json_backend_serialize (void *document, char **text) {

	size_t size = 0;
	*text = yyjson_write ((const yyjson_doc *) document, 0, &size);

	return *text ? size : 0;

}

void json_backend_text_free (char *text) {

	free (text);

}

void json_backend_free (void *document) {

	yyjson_doc_free ((yyjson_doc *) document);

}
