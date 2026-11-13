#include <cstddef>
#include <string>

#include "simdjson.h"

#include "backend.h"

const char *json_backend_name = { "simdjson" };

// the parser is reused, as simdjson recommends: its document lives until the next parse
static simdjson::dom::parser json_simdjson_parser;
static simdjson::dom::element json_simdjson_element;

// the text of the last serialize
static std::string json_simdjson_text;

void *json_backend_parse (const char *text, const size_t size) {

	void *document = nullptr;

	// the text has its padding, it is not copied
	if (!json_simdjson_parser.parse (text, size, false).get (json_simdjson_element)) {
		document = &json_simdjson_element;
	}

	return document;

}

size_t json_backend_serialize (void *document, char **text) {

	json_simdjson_text = simdjson::to_string (*static_cast <simdjson::dom::element *> (document));
	*text = json_simdjson_text.data ();

	return json_simdjson_text.size ();

}

void json_backend_text_free (char *text) {

	(void) text;

}

void json_backend_free (void *document) {

	(void) document;

}
