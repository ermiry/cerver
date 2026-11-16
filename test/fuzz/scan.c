#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/json/json.h>

#include "cerver/json/internal.h"

// the methods that walk the bytes of a string against the ones that go byte by byte:
// - the scan of the plain part of a string stops where the reference does, from every start of the input,
//   so each word and each vector of the scan gets the bytes in every position;
// - a buffer is valid UTF-8 for the reader when it is for a decoder written here from the definition

// the starts of the input that are scanned, the positions of a vector
#define SCAN_FUZZ_STARTS					(1 << 6)

// the first code point of each length of UTF-8, the surrogates and the last code point
#define SCAN_FUZZ_TWO_FIRST					0x80
#define SCAN_FUZZ_THREE_FIRST				0x800
#define SCAN_FUZZ_FOUR_FIRST				0x10000
#define SCAN_FUZZ_SURROGATES_FIRST			0xD800
#define SCAN_FUZZ_SURROGATES_LAST			0xDFFF
#define SCAN_FUZZ_LAST						0x10FFFF

// the bits of the first byte of a character that say its length, and the ones of a byte that continues it
#define SCAN_FUZZ_TWO_MARK					0xC0
#define SCAN_FUZZ_TWO_MASK					0xE0
#define SCAN_FUZZ_THREE_MARK				0xE0
#define SCAN_FUZZ_THREE_MASK				0xF0
#define SCAN_FUZZ_FOUR_MARK					0xF0
#define SCAN_FUZZ_FOUR_MASK					0xF8
#define SCAN_FUZZ_CONTINUATION_MARK			0x80
#define SCAN_FUZZ_CONTINUATION_MASK			0xC0
#define SCAN_FUZZ_CONTINUATION_BITS			6
#define SCAN_FUZZ_BYTE						0xFF

// the bytes of a character by its first one, 0 for a byte that does not start one
static size_t scan_fuzz_character_size (const uint8_t first) {

	if (first < SCAN_FUZZ_TWO_FIRST) return 1;
	if ((first & SCAN_FUZZ_TWO_MASK) == SCAN_FUZZ_TWO_MARK) return 2;
	if ((first & SCAN_FUZZ_THREE_MASK) == SCAN_FUZZ_THREE_MARK) return 3;
	if ((first & SCAN_FUZZ_FOUR_MASK) == SCAN_FUZZ_FOUR_MARK) return 4;

	return 0;

}

// the first code point that needs the bytes of a character: a lower one would have fewer
static uint32_t scan_fuzz_character_minimum (const size_t size) {

	switch (size) {
		case 2: return SCAN_FUZZ_TWO_FIRST;
		case 3: return SCAN_FUZZ_THREE_FIRST;
		case 4: return SCAN_FUZZ_FOUR_FIRST;
		default: return 0;
	}

}

// decodes each character: complete, with the bytes that it needs, not a surrogate and not over the last one
static bool scan_fuzz_is_valid (const uint8_t *bytes, const size_t length) {

	size_t position = 0;
	while (position < length) {
		const size_t size = scan_fuzz_character_size (bytes[position]);
		if (!size || ((position + size) > length)) return false;

		// the first byte of a character of some bytes has one bit more than them for its length
		uint32_t code = bytes[position];
		if (size > 1) code &= (uint32_t) (SCAN_FUZZ_BYTE >> (size + 1));

		for (size_t i = 1; i < size; i++) {
			if ((bytes[position + i] & SCAN_FUZZ_CONTINUATION_MASK) != SCAN_FUZZ_CONTINUATION_MARK) return false;

			code = (code << SCAN_FUZZ_CONTINUATION_BITS) | (bytes[position + i] & ~SCAN_FUZZ_CONTINUATION_MASK);
		}

		if (code < scan_fuzz_character_minimum (size)) return false;
		if ((code >= SCAN_FUZZ_SURROGATES_FIRST) && (code <= SCAN_FUZZ_SURROGATES_LAST)) return false;
		if (code > SCAN_FUZZ_LAST) return false;

		position += size;
	}

	return true;

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	// the text of a reader: its bytes, the NUL that ends it and the padding
	char *text = (char *) calloc (1, size + CERVER_JSON_PADDING);
	if (!text) abort ();

	if (size) (void) memcpy (text, data, size);

	const size_t starts = (size < SCAN_FUZZ_STARTS) ? size : SCAN_FUZZ_STARTS;
	for (size_t start = 0; start <= starts; start++) {
		const size_t expected = cerver_json_scan_string_stop (text + start, true);

		if (cerver_json_scan_string_stop (text + start, false) != expected) abort ();

		// never after the NUL at the end of the text
		if ((start + expected) > size) abort ();
	}

	if (json_text_is_valid (data, size) != scan_fuzz_is_valid (data, size)) abort ();

	free (text);

	return 0;

}
