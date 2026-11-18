#include <stdbool.h>
#include <string.h>

#include "cerver/files/files.h"

#include "cerver/http/requests/content.h"

const char *cerver_http_content_type_extension (
	const CerverHttpContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, extension, description, mime) case HTTP_CONTENT_TYPE_##name: return #extension;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_http_content_type_extension (HTTP_CONTENT_TYPE_NONE);

}

const char *cerver_http_content_type_description (
	const CerverHttpContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, extension, description, mime) case HTTP_CONTENT_TYPE_##name: return #description;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_http_content_type_description (HTTP_CONTENT_TYPE_NONE);

}

const char *cerver_http_content_type_mime (
	const CerverHttpContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, extension, description, mime) case HTTP_CONTENT_TYPE_##name: return #mime;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_http_content_type_mime (HTTP_CONTENT_TYPE_NONE);

}

// a NULL mime is not found
CerverHttpContentType cerver_http_content_type_with_mime (
	const char *mime_string
) {

	if (mime_string) {
		#define XX(num, name, extension, description, mime) if (!strcmp (#mime, mime_string)) return HTTP_CONTENT_TYPE_##name;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return HTTP_CONTENT_TYPE_NONE;

}

// a NULL extension is not found
CerverHttpContentType cerver_http_content_type_with_extension (
	const char *extension_string
) {

	if (extension_string) {
		#define XX(num, name, extension, description, mime) if (!strcmp (#extension, extension_string)) return HTTP_CONTENT_TYPE_##name;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return HTTP_CONTENT_TYPE_NONE;

}

// a NULL extension is not found
const char *cerver_http_content_type_mime_with_extension (
	const char *extension_string
) {

	if (extension_string) {
		#define XX(num, name, extension, description, mime) if (!strcmp (#extension, extension_string)) return #mime;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return NULL;

}

// a NULL content type is not json
bool cerver_http_content_type_is_json (
	const char *description
) {

	return description && !strcasecmp ("application/json", description);

}

CerverHttpContentType cerver_http_content_type_from_filename (
	const char *filename
) {

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	return ext ? cerver_http_content_type_with_extension (ext) : HTTP_CONTENT_TYPE_NONE;

}
