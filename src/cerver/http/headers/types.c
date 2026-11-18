#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <strings.h>

#include "cerver/types/types.h"

#include "cerver/http/headers/fields.h"

#pragma region types

const char *cerver_http_header_to_string (
	const CerverHttpHeader header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #string;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return cerver_http_header_to_string (HTTP_HEADER_UNDEFINED);

}

const char *cerver_http_header_description (
	const CerverHttpHeader header
) {

	switch (header) {
		#define XX(num, name, string, description) case HTTP_HEADER_##name: return #description;
		HTTP_HEADERS_MAP(XX)
		#undef XX
	}

	return cerver_http_header_description (HTTP_HEADER_UNDEFINED);

}

// FNV-1a of the lower case name, so the lookup does not depend on case
static const unsigned int http_headers_hash_offset = 2166136261u;
static const unsigned int http_headers_hash_prime = 16777619u;

// a slot keeps the header type + 1, so 0 is an empty slot
static unsigned char http_headers_table[HTTP_HEADERS_TABLE_SIZE] = { 0 };
static pthread_once_t http_headers_table_once = PTHREAD_ONCE_INIT;

static unsigned int http_headers_hash (const char *name) {

	unsigned int hash = http_headers_hash_offset;
	for (const char *ch = name; *ch; ch++) {
		hash ^= (unsigned int) tolower ((unsigned char) *ch);
		hash *= http_headers_hash_prime;
	}

	return hash;

}

static void http_headers_table_insert (const CerverHttpHeader type) {

	unsigned int slot = http_headers_hash (cerver_http_header_to_string (type)) & (HTTP_HEADERS_TABLE_SIZE - 1);
	while (http_headers_table[slot]) {
		slot = (slot + 1) & (HTTP_HEADERS_TABLE_SIZE - 1);
	}

	http_headers_table[slot] = (unsigned char) (type + 1);

}

static void http_headers_table_init (void) {

	#define XX(num, name, string, description) if (HTTP_HEADER_##name != HTTP_HEADER_UNDEFINED) http_headers_table_insert (HTTP_HEADER_##name);
	HTTP_HEADERS_MAP(XX)
	#undef XX

}

const CerverHttpHeader cerver_http_header_with_string (
	const char *header_type_string
) {

	CerverHttpHeader found = HTTP_HEADER_UNDEFINED;

	if (header_type_string) {
		(void) pthread_once (&http_headers_table_once, http_headers_table_init);

		unsigned int slot = http_headers_hash (header_type_string) & (HTTP_HEADERS_TABLE_SIZE - 1);
		while (http_headers_table[slot]) {
			const CerverHttpHeader candidate = (CerverHttpHeader) (http_headers_table[slot] - 1);
			if (!strcasecmp (cerver_http_header_to_string (candidate), header_type_string)) {
				found = candidate;
				break;
			}

			slot = (slot + 1) & (HTTP_HEADERS_TABLE_SIZE - 1);
		}
	}

	return found;

}

#pragma endregion
