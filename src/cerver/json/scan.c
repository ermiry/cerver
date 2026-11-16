#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/json/internal.h"

#pragma region characters

#define JSON_CHARACTERS_PLAIN		0
#define JSON_CHARACTERS_SPACE		JSON_CHARACTER_WHITESPACE
#define JSON_CHARACTERS_DIGIT		JSON_CHARACTER_DIGIT
#define JSON_CHARACTERS_STOP		JSON_CHARACTER_STOP
#define JSON_CHARACTERS_BREAK		(JSON_CHARACTER_STOP | JSON_CHARACTER_WHITESPACE)

// sixteen bytes that are part of a string as they are
#define JSON_CHARACTERS_ROW_PLAIN																	\
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,		\
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,		\
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,		\
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN

// sixteen bytes that the plain part of a string stops at
#define JSON_CHARACTERS_ROW_STOP																	\
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP,			\
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP,			\
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP,			\
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP

const u8 json_characters[1 << 8] = {
	// 0x00: the control characters, with the tab, the line feed and the carriage return
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP,
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP,
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_BREAK, JSON_CHARACTERS_BREAK, JSON_CHARACTERS_STOP,
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_BREAK, JSON_CHARACTERS_STOP, JSON_CHARACTERS_STOP,
	// 0x10
	JSON_CHARACTERS_ROW_STOP,
	// 0x20: the space and the quote
	JSON_CHARACTERS_SPACE, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_STOP, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	// 0x30: the digits
	JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT,
	JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT,
	JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_DIGIT, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	// 0x40
	JSON_CHARACTERS_ROW_PLAIN,
	// 0x50: the backslash
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	JSON_CHARACTERS_STOP, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN, JSON_CHARACTERS_PLAIN,
	// 0x60 and 0x70
	JSON_CHARACTERS_ROW_PLAIN,
	JSON_CHARACTERS_ROW_PLAIN,
	// 0x80 to 0xF0: the bytes that are not ASCII
	JSON_CHARACTERS_ROW_STOP, JSON_CHARACTERS_ROW_STOP, JSON_CHARACTERS_ROW_STOP, JSON_CHARACTERS_ROW_STOP,
	JSON_CHARACTERS_ROW_STOP, JSON_CHARACTERS_ROW_STOP, JSON_CHARACTERS_ROW_STOP, JSON_CHARACTERS_ROW_STOP
};

#pragma endregion

#pragma region strings

// a word of eight bytes with the same byte in each one
#define JSON_SCAN_WORD(byte)		(UINT64_C (0x0101010101010101) * (u64) (byte))

static const u64 JSON_SCAN_ONES = JSON_SCAN_WORD (0x01);
static const u64 JSON_SCAN_HIGHS = JSON_SCAN_WORD (0x80);
static const u64 JSON_SCAN_SPACES = JSON_SCAN_WORD (0x20);
static const u64 JSON_SCAN_QUOTES = JSON_SCAN_WORD ('"');
static const u64 JSON_SCAN_BACKSLASHES = JSON_SCAN_WORD ('\\');

static const unsigned int JSON_SCAN_BYTE_BITS = 8;

// the reference of every other scan: one byte at a time
static u8 *json_scan_string_bytes (u8 *position) {

	while (!(json_characters[*position] & JSON_CHARACTER_STOP)) position += 1;

	return position;

}

// one byte at a time, and never after the last one
static size_t json_scan_plain_bytes (const u8 *bytes, const size_t length) {

	size_t position = 0;
	while ((position < length) && !(json_characters[bytes[position]] & JSON_CHARACTER_STOP)) position += 1;

	return position;

}

#if defined (__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

// the high bit of each byte of a word that is zero is set; a byte
// after the first zero one can be set by its borrow, which never
// changes what the first byte that is set is
static inline u64 json_scan_zeros (const u64 word) {

	return (word - JSON_SCAN_ONES) & ~word & JSON_SCAN_HIGHS;

}

// eight bytes at once: the high bit of each byte that stops the string is set
// (below a space, a quote, a backslash or not ASCII), and the first one is the lowest
static inline u64 json_scan_stops (const u8 *position) {

	u64 word = 0;
	(void) memcpy (&word, position, sizeof (u64));

	return (word & JSON_SCAN_HIGHS)
		| ((word - JSON_SCAN_SPACES) & ~word & JSON_SCAN_HIGHS)
		| json_scan_zeros (word ^ JSON_SCAN_QUOTES)
		| json_scan_zeros (word ^ JSON_SCAN_BACKSLASHES);

}

// the position in a word of the first byte that stops the string
static inline size_t json_scan_first (const u64 stops) {

	return ((unsigned int) __builtin_ctzll (stops)) / JSON_SCAN_BYTE_BITS;

}

// from a byte of a string that does not stop it: whole words, the padding of the text has room for the last one.
// A scan with vectors of 16 bytes (NEON) was measured against this one and did not read any case faster
static u8 *json_scan_string_far (u8 *position) {

	for (;;) {
		const u64 stops = json_scan_stops (position);
		if (stops) return position + json_scan_first (stops);

		position += sizeof (u64);
	}

}

u8 *json_scan_string (u8 *position) {

	// most strings are short: the first bytes are cheaper one by one
	return (json_characters[*position] & JSON_CHARACTER_STOP) ? position : json_scan_string_far (position);

}

size_t json_scan_plain (const u8 *bytes, const size_t length) {

	size_t position = 0;

	// the whole words; the bytes that are left do not fill one, and are not read as one
	for (; (length - position) >= sizeof (u64); position += sizeof (u64)) {
		const u64 stops = json_scan_stops (bytes + position);
		if (stops) return position + json_scan_first (stops);
	}

	return position + json_scan_plain_bytes (bytes + position, length - position);

}

#else

u8 *json_scan_string (u8 *position) {

	return json_scan_string_bytes (position);

}

size_t json_scan_plain (const u8 *bytes, const size_t length) {

	return json_scan_plain_bytes (bytes, length);

}

#endif

size_t cerver_json_scan_string_stop (const char *text, const bool reference) {

	u8 *start = (u8 *) text;

	return (size_t) ((reference ? json_scan_string_bytes (start) : json_scan_string (start)) - start);

}

#pragma endregion
