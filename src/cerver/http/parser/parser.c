#include "cerver/http/parser/parser.h"

#include "cerver/http/parser/api.h"

static const char *http_parser_lenient_none = { "none" };

const char *http_parser_lenient_to_string (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #string;
		HTTP_PARSER_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *http_parser_lenient_description (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #description;
		HTTP_PARSER_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}
