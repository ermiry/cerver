#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cerver/http/parser/internal.h"

// the bytes of each class (RFC 9110 and RFC 3986), the letters and the digits are added by range
static const char http_parser_token_symbols[] = { "!#$%&'*+-.^_`|~" };
static const char http_parser_authority_symbols[] = { "-_.!~*'()%;:&=+$,[]" };

#define HTTP_PARSER_BYTE_TAB			0x09
#define HTTP_PARSER_BYTE_SPACE			0x20
#define HTTP_PARSER_BYTE_FIRST_VISIBLE	0x21
#define HTTP_PARSER_BYTE_LAST_VISIBLE	0x7E
#define HTTP_PARSER_BYTE_FIRST_OBSOLETE	0x80
#define HTTP_PARSER_BYTE_QUOTE			0x22
#define HTTP_PARSER_BYTE_BACKSLASH		0x5C
#define HTTP_PARSER_BYTE_COMMA			0x2C

// the high nibbles of the bytes of a token, '!' to '~'
#define HTTP_PARSER_TOKEN_HIGH_FIRST	2u
#define HTTP_PARSER_TOKEN_HIGH_LAST		7u

uint8_t http_parser_classes[HTTP_PARSER_BYTES];
uint8_t http_parser_lower[HTTP_PARSER_BYTES];

uint8_t http_parser_token_low[HTTP_PARSER_NIBBLES];
uint8_t http_parser_token_high[HTTP_PARSER_NIBBLES];

static pthread_once_t http_parser_tables_once = PTHREAD_ONCE_INIT;
static bool http_parser_tables_filled = false;

static void http_parser_tables_add (const char *bytes, const unsigned int class_bit) {

	for (const char *byte = bytes; *byte; byte++) {
		http_parser_classes[(unsigned char) *byte] |= (uint8_t) class_bit;
	}

}

static void http_parser_tables_range (
	const unsigned int first, const unsigned int last, const unsigned int class_bit
) {

	for (unsigned int byte = first; byte <= last; byte++) {
		http_parser_classes[byte] |= (uint8_t) class_bit;
	}

}

static void http_parser_tables_letters (void) {

	const unsigned int letters = HTTP_PARSER_CLASS_TOKEN | HTTP_PARSER_CLASS_AUTHORITY
		| HTTP_PARSER_CLASS_ALPHA;

	http_parser_tables_range ('a', 'z', letters);
	http_parser_tables_range ('A', 'Z', letters);
	http_parser_tables_range ('0', '9', HTTP_PARSER_CLASS_TOKEN | HTTP_PARSER_CLASS_AUTHORITY | HTTP_PARSER_CLASS_HEX);
	http_parser_tables_range ('a', 'f', HTTP_PARSER_CLASS_HEX);
	http_parser_tables_range ('A', 'F', HTTP_PARSER_CLASS_HEX);

}

// the visible bytes, the tab, the space and obs-text
static void http_parser_tables_values (void) {

	const unsigned int visible = HTTP_PARSER_CLASS_VALUE | HTTP_PARSER_CLASS_LIST | HTTP_PARSER_CLASS_QUOTED;

	http_parser_tables_range (HTTP_PARSER_BYTE_FIRST_VISIBLE, HTTP_PARSER_BYTE_LAST_VISIBLE, visible | HTTP_PARSER_CLASS_TARGET);
	http_parser_tables_range (HTTP_PARSER_BYTE_FIRST_OBSOLETE, HTTP_PARSER_BYTES - 1, visible);

	http_parser_classes[HTTP_PARSER_BYTE_TAB] |= (uint8_t) visible;
	http_parser_classes[HTTP_PARSER_BYTE_SPACE] |= (uint8_t) visible;

	http_parser_classes[HTTP_PARSER_BYTE_QUOTE] &= (uint8_t) ~HTTP_PARSER_CLASS_QUOTED;
	http_parser_classes[HTTP_PARSER_BYTE_BACKSLASH] &= (uint8_t) ~HTTP_PARSER_CLASS_QUOTED;
	http_parser_classes[HTTP_PARSER_BYTE_COMMA] &= (uint8_t) ~HTTP_PARSER_CLASS_LIST;

}

// the tokens by nibbles, from their class: every byte of a token has a high nibble of 2 to 7,
// each one with a bit of its own, so a byte is a token exactly when the class of its low nibble has that bit
static void http_parser_tables_nibbles (void) {

	(void) memset (http_parser_token_low, 0, sizeof (http_parser_token_low));
	(void) memset (http_parser_token_high, 0, sizeof (http_parser_token_high));

	for (unsigned int high = HTTP_PARSER_TOKEN_HIGH_FIRST; high <= HTTP_PARSER_TOKEN_HIGH_LAST; high++) {
		http_parser_token_high[high] = (uint8_t) (1u << (high - HTTP_PARSER_TOKEN_HIGH_FIRST));
	}

	for (unsigned int byte = 0; byte < HTTP_PARSER_BYTES; byte++) {
		if (http_parser_classes[byte] & HTTP_PARSER_CLASS_TOKEN) {
			http_parser_token_low[byte & HTTP_PARSER_NIBBLE_MASK] |= http_parser_token_high[byte >> HTTP_PARSER_NIBBLE_BITS];
		}
	}

}

static void http_parser_tables_fill (void) {

	(void) memset (http_parser_classes, 0, sizeof (http_parser_classes));

	http_parser_tables_letters ();
	http_parser_tables_values ();

	http_parser_tables_add (http_parser_token_symbols, HTTP_PARSER_CLASS_TOKEN);
	http_parser_tables_add (http_parser_authority_symbols, HTTP_PARSER_CLASS_AUTHORITY);

	for (unsigned int byte = 0; byte < HTTP_PARSER_BYTES; byte++) {
		http_parser_lower[byte] = (uint8_t) (((byte >= 'A') && (byte <= 'Z')) ? (byte + ('a' - 'A')) : byte);
	}

	http_parser_tables_nibbles ();

	http_parser_fields_init ();

	__atomic_store_n (&http_parser_tables_filled, true, __ATOMIC_RELEASE);

}

// the tables are filled once: after that, a parser starts with one load
void http_parser_tables_init (void) {

	if (!__atomic_load_n (&http_parser_tables_filled, __ATOMIC_ACQUIRE)) {
		(void) pthread_once (&http_parser_tables_once, http_parser_tables_fill);
	}

}

bool http_parser_name_equal (
	const char *name, const size_t length, const char *lower, const size_t lower_length
) {

	bool equal = (length == lower_length);

	for (size_t i = 0; equal && (i < length); i++) {
		equal = (http_parser_lower[(unsigned char) name[i]] == (unsigned char) lower[i]);
	}

	return equal;

}
