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

static void http_headers_table_insert (const http_header type) {

	unsigned int slot = http_headers_hash (http_header_string (type)) & (HTTP_HEADERS_TABLE_SIZE - 1);
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

const http_header http_header_type_with_string (
	const char *header_type_string
) {

	http_header found = HTTP_HEADER_UNDEFINED;

	if (header_type_string) {
		(void) pthread_once (&http_headers_table_once, http_headers_table_init);

		unsigned int slot = http_headers_hash (header_type_string) & (HTTP_HEADERS_TABLE_SIZE - 1);
		while (http_headers_table[slot]) {
			const http_header candidate = (http_header) (http_headers_table[slot] - 1);
			if (!strcasecmp (http_header_string (candidate), header_type_string)) {
				found = candidate;
				break;
			}

			slot = (slot + 1) & (HTTP_HEADERS_TABLE_SIZE - 1);
		}
	}

	return found;

}

#pragma endregion
