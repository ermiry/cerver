#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/http/json/internal.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include "backend.h"

// the JSON that the one of cerver replaces: the copy of Jansson that cerver still has for its JWT
const char *json_backend_name = { "legacy" };

void *json_backend_parse (const char *text, const size_t size) {

	json_error_t error;

	return json_loadb (text, size, 0, &error);

}

size_t json_backend_serialize (void *document, char **text) {

	*text = json_dumps ((const json_t *) document, JSON_COMPACT);

	return *text ? strlen (*text) : 0;

}

bool json_backend_search (void *document, const char *key) {

	return json_object_get ((const json_t *) document, key) != NULL;

}

void json_backend_text_free (char *text) {

	free (text);

}

void json_backend_free (void *document) {

	json_decref ((json_t *) document);

}
