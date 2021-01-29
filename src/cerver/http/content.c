#include <string.h>

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

const char *http_content_type_by_extension (
	const char *extension
) {

	#define XX(num, name, string, description) if (!strcmp (#string, extension)) return #description;
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

	return NULL;

}