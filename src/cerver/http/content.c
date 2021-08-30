#include <string.h>
#include <stdbool.h>

#include "cerver/files.h"

#include "cerver/http/content.h"

const char *http_content_type_extension (
	const ContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, extension, description, mime) case HTTP_CONTENT_TYPE_##name: return #extension;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return http_content_type_extension (HTTP_CONTENT_TYPE_NONE);

}

const char *http_content_type_description (
	const ContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, extension, description, mime) case HTTP_CONTENT_TYPE_##name: return #description;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return http_content_type_description (HTTP_CONTENT_TYPE_NONE);

}

const char *http_content_type_mime (
	const ContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, extension, description, mime) case HTTP_CONTENT_TYPE_##name: return #mime;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return http_content_type_mime (HTTP_CONTENT_TYPE_NONE);

}

ContentType http_content_type_by_mime (
	const char *mime_string
) {

	#define XX(num, name, extension, description, mime) if (!strcmp (#mime, mime_string)) return HTTP_CONTENT_TYPE_##name;
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

	return HTTP_CONTENT_TYPE_NONE;

}

ContentType http_content_type_by_extension (
	const char *extension_string
) {

	#define XX(num, name, extension, description, mime) if (!strcmp (#extension, extension_string)) return HTTP_CONTENT_TYPE_##name;
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

	return HTTP_CONTENT_TYPE_NONE;

}

const char *http_content_type_mime_by_extension (
	const char *extension_string
) {

	#define XX(num, name, extension, description, mime) if (!strcmp (#extension, extension_string)) return #mime;
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

	return NULL;

}

bool http_content_type_is_json (
	const char *description
) {

	return !strcasecmp ("application/json", description);

}

ContentType http_content_type_from_filename (
	const char *filename
) {

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	return ext ? http_content_type_by_extension (ext) : HTTP_CONTENT_TYPE_NONE;

}
