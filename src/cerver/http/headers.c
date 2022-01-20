#include <string.h>
#include <stdio.h>

#include <stdarg.h>

#include "cerver/http/headers.h"

const char *http_header_string (
	const http_header header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #string;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return http_header_string (HTTP_HEADER_UNDEFINED);

}

const char *http_header_description (
	const http_header header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #description;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return http_header_description (HTTP_HEADER_UNDEFINED);

}

const http_header http_header_type_by_string (
	const char *header_type_string
) {

	#define XX(num, name, string, description) if (!strcasecmp (#string, header_type_string)) return HTTP_HEADER_##name;
	HTTP_HEADERS_MAP(XX)
	#undef XX

	return HTTP_HEADER_UNDEFINED;

}

void http_header_set (
	HttpHeader *header, const char *format, ...
) {

	va_list args;
	va_start (args, format);

	header->len = vsnprintf (
		header->value, HTTP_HEADER_VALUE_SIZE,
		format, args
	);

	va_end (args);

}

void http_response_header_set_with_type (
	HttpHeader *header, const http_header type,
	const char *actual_header
) {

	header->len = snprintf (
		header->value, HTTP_HEADER_VALUE_SIZE,
		"%s: %s\r\n",
		http_header_string (type), actual_header
	);

}

void http_response_header_set_with_type_and_args (
	HttpHeader *header, const http_header type,
	const char *format, va_list args
) {

	char actual_header[HTTP_HEADER_ACTUAL_SIZE] = { 0 };
	(void) vsnprintf (
		actual_header, HTTP_HEADER_ACTUAL_SIZE,
		format, args
	);

	header->len = snprintf (
		header->value, HTTP_HEADER_VALUE_SIZE,
		"%s: %s\r\n",
		http_header_string (type), actual_header
	);

}

void http_header_print (const HttpHeader *header) {

	(void) printf ("(%d) - %s\n", header->len, header->value);

}
