#include "cerver/http/headers.h"

const char *http_header_string (
	const HttpHeader header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #string;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return http_header_string (HTTP_HEADER_INVALID);

}

const char *http_header_description (
	const HttpHeader header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #description;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return http_header_description (HTTP_HEADER_INVALID);

}