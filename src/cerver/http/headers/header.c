#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <strings.h>

#include "cerver/types/types.h"

#include "cerver/http/headers/fields.h"
#include "cerver/http/headers/header.h"

#pragma region header

void http_header_reset (HttpHeader *header) {

	(void) memset (header, 0, sizeof (HttpHeader));

}

// a value that does not fit is truncated, its length is the one that was kept,
// and a format that fails leaves it empty
void http_header_set (
	HttpHeader *header, const char *format, ...
) {

	va_list args;
	va_start (args, format);

	if (vsnprintf (
		header->value, HTTP_HEADER_VALUE_SIZE,
		format, args
	) < 0) {
		header->value[0] = '\0';
	}

	header->len = (int) strlen (header->value);

	va_end (args);

}

void http_header_print (const HttpHeader *header) {

	(void) printf ("(%d) - %s\n", header->len, header->value);

}

#pragma endregion
