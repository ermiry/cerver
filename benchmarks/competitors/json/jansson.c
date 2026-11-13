#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "backend.h"

const char *json_backend_name = { "jansson" };

void *json_backend_parse (const char *text, const size_t size) {

	json_error_t error;

	return json_loadb (text, size, 0, &error);

}

size_t json_backend_serialize (void *document, char **text) {

	*text = json_dumps ((const json_t *) document, JSON_COMPACT);

	return *text ? strlen (*text) : 0;

}

void json_backend_text_free (char *text) {

	free (text);

}

void json_backend_free (void *document) {

	json_decref ((json_t *) document);

}
