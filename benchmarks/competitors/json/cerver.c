#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include "backend.h"

const char *json_backend_name = { "cerver" };

// the arena of the documents is reused, as the one of a request is in a
// server: its document lives until the next parse, and its value is kept with it
static Arena *json_cerver_arena = NULL;
static const CerverJsonValue *json_cerver_root = NULL;

// the buffer of the last serialize, that grows to the text that needs the most
static char *json_cerver_text = NULL;
static size_t json_cerver_capacity = 0;

void *json_backend_parse (const char *text, const size_t size) {

	if (!json_cerver_arena) json_cerver_arena = arena_create (0);

	arena_reset (json_cerver_arena);

	CerverJsonDocument *document = cerver_json_parse (
		json_cerver_arena, text, size, CERVER_JSON_FLAGS_NONE, NULL, NULL
	);

	json_cerver_root = cerver_json_document_root (document);

	return document;

}

size_t json_backend_serialize (void *document, char **text) {

	(void) document;

	size_t size = cerver_json_write (
		json_cerver_root, json_cerver_text, json_cerver_capacity, CERVER_JSON_WRITE_COMPACT
	);

	if (size >= json_cerver_capacity) {
		json_cerver_capacity = size + 1;
		json_cerver_text = (char *) realloc (json_cerver_text, json_cerver_capacity);

		size = cerver_json_write (json_cerver_root, json_cerver_text, json_cerver_capacity, CERVER_JSON_WRITE_COMPACT);
	}

	*text = json_cerver_text;

	return size;

}

bool json_backend_search (void *document, const char *key) {

	(void) document;

	return cerver_json_object_get (json_cerver_root, key) != NULL;

}

void json_backend_text_free (char *text) {

	(void) text;

}

void json_backend_free (void *document) {

	(void) document;

}
