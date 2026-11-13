#include <cstddef>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "backend.h"

const char *json_backend_name = { "rapidjson" };

// the buffer of the writer is reused between serializes
static rapidjson::StringBuffer json_rapidjson_buffer;

void *json_backend_parse (const char *text, const size_t size) {

	rapidjson::Document *document = new rapidjson::Document ();
	document->Parse (text, size);

	if (document->HasParseError ()) {
		delete document;
		document = nullptr;
	}

	return document;

}

size_t json_backend_serialize (void *document, char **text) {

	json_rapidjson_buffer.Clear ();
	rapidjson::Writer <rapidjson::StringBuffer> writer (json_rapidjson_buffer);

	const bool written = static_cast <rapidjson::Document *> (document)->Accept (writer);
	*text = const_cast <char *> (json_rapidjson_buffer.GetString ());

	return written ? json_rapidjson_buffer.GetSize () : 0;

}

void json_backend_text_free (char *text) {

	(void) text;

}

void json_backend_free (void *document) {

	delete static_cast <rapidjson::Document *> (document);

}
