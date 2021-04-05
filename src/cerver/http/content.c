#include <string.h>
#include <stdbool.h>

#include "cerver/http/content.h"

const char *http_content_type_string (
	const ContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, string, description) case HTTP_CONTENT_TYPE_##name: return #string;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return NULL;

}

const char *http_content_type_description (
	const ContentType content_type
) {

	switch (content_type) {
		#define XX(num, name, string, description) case HTTP_CONTENT_TYPE_##name: return #description;
		HTTP_CONTENT_TYPE_MAP(XX)
		#undef XX
	}

	return NULL;

}

ContentType http_content_type_by_string (
	const char *content_type_string
) {

	#define XX(num, name, string, description) if (!strcmp (#description, content_type_string)) return HTTP_CONTENT_TYPE_##name;
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

	return HTTP_CONTENT_TYPE_NONE;

}

const char *http_content_type_by_extension (
	const char *extension
) {

	#define XX(num, name, string, description) if (!strcmp (#string, extension)) return #description;
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

	return NULL;

}

bool http_content_type_is_json (
	const char *description
) {

	return !strcasecmp ("application/json", description);

}
