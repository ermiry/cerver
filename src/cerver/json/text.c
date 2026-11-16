#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"

static const u8 JSON_TEXT_QUOTE = '"';
static const u8 JSON_TEXT_BACKSLASH = '\\';
static const u8 JSON_TEXT_SPACE = ' ';

// the first bytes of the characters of UTF-8 by their length: 0xC0 and 0xC1 would start
// a character of two bytes that fits in one, and nothing over 0xF4 is below U+110000
static const u8 JSON_UTF8_ASCII_END = 0x80;
static const u8 JSON_UTF8_TWO_FIRST = 0xC2;
static const u8 JSON_UTF8_THREE_FIRST = 0xE0;
static const u8 JSON_UTF8_FOUR_FIRST = 0xF0;
static const u8 JSON_UTF8_FOUR_LAST = 0xF4;

// the first byte of the surrogates, U+D800 to U+DFFF, that are not characters
static const u8 JSON_UTF8_SURROGATES_FIRST = 0xED;

// the range of a byte that continues a character, and the ones of the second byte
// that keep the shortest form (after 0xE0 and 0xF0), leave the surrogates out
// (after 0xED) and stay below U+110000 (after 0xF4)
static const u8 JSON_UTF8_CONTINUATION_FIRST = 0x80;
static const u8 JSON_UTF8_CONTINUATION_LAST = 0xBF;
static const u8 JSON_UTF8_THREE_SECOND_FIRST = 0xA0;
static const u8 JSON_UTF8_SURROGATES_SECOND_LAST = 0x9F;
static const u8 JSON_UTF8_FOUR_SECOND_FIRST = 0x90;
static const u8 JSON_UTF8_FOUR_SECOND_LAST = 0x8F;

static const size_t JSON_UTF8_TWO_BYTES = 2;
static const size_t JSON_UTF8_THREE_BYTES = 3;
static const size_t JSON_UTF8_FOUR_BYTES = 4;

// the code points that need one, two and three bytes end here
static const u32 JSON_UTF8_ONE_LIMIT = 0x80;
static const u32 JSON_UTF8_TWO_LIMIT = 0x800;
static const u32 JSON_UTF8_THREE_LIMIT = 0x10000;

// the marks of the first byte of a character of two, three and four bytes
// and of a byte that continues it, with the bits of the code point that it carries
static const u32 JSON_UTF8_TWO_MARK = 0xC0;
static const u32 JSON_UTF8_THREE_MARK = 0xE0;
static const u32 JSON_UTF8_FOUR_MARK = 0xF0;
static const u32 JSON_UTF8_CONTINUATION_MARK = 0x80;
static const u32 JSON_UTF8_CONTINUATION_MASK = 0x3F;
static const unsigned int JSON_UTF8_CONTINUATION_BITS = 6;

// the backslash of an escape and its letter
static const size_t JSON_ESCAPE_PREFIX_SIZE = 2;

// an escape of four hexadecimal digits is a unit of UTF-16: the ones from
// 0xD800 to 0xDBFF start a pair that the ones from 0xDC00 to 0xDFFF end
static const size_t JSON_ESCAPE_UNIT_DIGITS = 4;
static const unsigned int JSON_ESCAPE_DIGIT_BITS = 4;
static const u32 JSON_ESCAPE_DIGIT_INVALID = 0xFF;
static const u32 JSON_ESCAPE_DECIMAL_COUNT = 10;
static const u32 JSON_ESCAPE_LETTER_VALUE = 10;
static const u32 JSON_ESCAPE_LETTER_COUNT = 6;
static const u32 JSON_SURROGATE_HIGH_FIRST = 0xD800;
static const u32 JSON_SURROGATE_LOW_FIRST = 0xDC00;
static const u32 JSON_SURROGATE_LAST = 0xDFFF;
static const u32 JSON_SURROGATE_MASK = 0x3FF;
static const unsigned int JSON_SURROGATE_BITS = 10;
static const u32 JSON_SURROGATE_BASE = 0x10000;

#pragma region characters

static inline bool json_text_continues (const u8 byte, const u8 first, const u8 last) {

	return (byte >= first) && (byte <= last);

}

static size_t json_text_character_two (const u8 *bytes, const size_t available) {

	return (
		(available >= JSON_UTF8_TWO_BYTES)
		&& json_text_continues (bytes[1], JSON_UTF8_CONTINUATION_FIRST, JSON_UTF8_CONTINUATION_LAST)
	) ? JSON_UTF8_TWO_BYTES : 0;

}

static size_t json_text_character_three (const u8 *bytes, const size_t available) {

	size_t retval = 0;

	if (available >= JSON_UTF8_THREE_BYTES) {
		u8 first = JSON_UTF8_CONTINUATION_FIRST;
		u8 last = JSON_UTF8_CONTINUATION_LAST;

		if (bytes[0] == JSON_UTF8_THREE_FIRST) first = JSON_UTF8_THREE_SECOND_FIRST;
		else if (bytes[0] == JSON_UTF8_SURROGATES_FIRST) last = JSON_UTF8_SURROGATES_SECOND_LAST;

		if (
			json_text_continues (bytes[1], first, last)
			&& json_text_continues (bytes[2], JSON_UTF8_CONTINUATION_FIRST, JSON_UTF8_CONTINUATION_LAST)
		) {
			retval = JSON_UTF8_THREE_BYTES;
		}
	}

	return retval;

}

static size_t json_text_character_four (const u8 *bytes, const size_t available) {

	size_t retval = 0;

	if (available >= JSON_UTF8_FOUR_BYTES) {
		u8 first = JSON_UTF8_CONTINUATION_FIRST;
		u8 last = JSON_UTF8_CONTINUATION_LAST;

		if (bytes[0] == JSON_UTF8_FOUR_FIRST) first = JSON_UTF8_FOUR_SECOND_FIRST;
		else if (bytes[0] == JSON_UTF8_FOUR_LAST) last = JSON_UTF8_FOUR_SECOND_LAST;

		if (
			json_text_continues (bytes[1], first, last)
			&& json_text_continues (bytes[2], JSON_UTF8_CONTINUATION_FIRST, JSON_UTF8_CONTINUATION_LAST)
			&& json_text_continues (bytes[3], JSON_UTF8_CONTINUATION_FIRST, JSON_UTF8_CONTINUATION_LAST)
		) {
			retval = JSON_UTF8_FOUR_BYTES;
		}
	}

	return retval;

}

size_t json_text_character (const u8 *bytes, const size_t available) {

	const u8 first = bytes[0];

	if (first < JSON_UTF8_TWO_FIRST) return 0;
	if (first < JSON_UTF8_THREE_FIRST) return json_text_character_two (bytes, available);
	if (first < JSON_UTF8_FOUR_FIRST) return json_text_character_three (bytes, available);
	if (first <= JSON_UTF8_FOUR_LAST) return json_text_character_four (bytes, available);

	return 0;

}

bool json_text_is_valid (const u8 *bytes, const size_t length) {

	bool retval = true;

	size_t position = 0;
	while (retval && (position < length)) {
		if (bytes[position] < JSON_UTF8_ASCII_END) {
			position += 1;
		}

		else {
			const size_t size = json_text_character (bytes + position, length - position);
			if (size) position += size;
			else retval = false;
		}
	}

	return retval;

}

// writes a code point as UTF-8 and returns its bytes
static size_t json_text_encode (u8 *output, const u32 code) {

	if (code < JSON_UTF8_ONE_LIMIT) {
		output[0] = (u8) code;
		return 1;
	}

	if (code < JSON_UTF8_TWO_LIMIT) {
		output[0] = (u8) (JSON_UTF8_TWO_MARK | (code >> JSON_UTF8_CONTINUATION_BITS));
		output[1] = (u8) (JSON_UTF8_CONTINUATION_MARK | (code & JSON_UTF8_CONTINUATION_MASK));
		return JSON_UTF8_TWO_BYTES;
	}

	if (code < JSON_UTF8_THREE_LIMIT) {
		output[0] = (u8) (JSON_UTF8_THREE_MARK | (code >> (JSON_UTF8_CONTINUATION_BITS * 2)));
		output[1] = (u8) (
			JSON_UTF8_CONTINUATION_MARK | ((code >> JSON_UTF8_CONTINUATION_BITS) & JSON_UTF8_CONTINUATION_MASK)
		);
		output[2] = (u8) (JSON_UTF8_CONTINUATION_MARK | (code & JSON_UTF8_CONTINUATION_MASK));
		return JSON_UTF8_THREE_BYTES;
	}

	output[0] = (u8) (JSON_UTF8_FOUR_MARK | (code >> (JSON_UTF8_CONTINUATION_BITS * 3)));
	output[1] = (u8) (
		JSON_UTF8_CONTINUATION_MARK | ((code >> (JSON_UTF8_CONTINUATION_BITS * 2)) & JSON_UTF8_CONTINUATION_MASK)
	);
	output[2] = (u8) (
		JSON_UTF8_CONTINUATION_MARK | ((code >> JSON_UTF8_CONTINUATION_BITS) & JSON_UTF8_CONTINUATION_MASK)
	);
	output[3] = (u8) (JSON_UTF8_CONTINUATION_MARK | (code & JSON_UTF8_CONTINUATION_MASK));
	return JSON_UTF8_FOUR_BYTES;

}

#pragma endregion

#pragma region escapes

// what a string is while it is read in situ: the bytes that were
// read and where the next ones go, never after the ones that are read
typedef struct JsonTextString {

	JsonCursor *cursor;
	u8 *read;
	u8 *write;

} JsonTextString;

static u32 json_text_escape_digit (const u8 byte) {

	const u32 decimal = (u32) byte - (u32) '0';
	if (decimal < JSON_ESCAPE_DECIMAL_COUNT) return decimal;

	const u32 lower = (u32) byte - (u32) 'a';
	if (lower < JSON_ESCAPE_LETTER_COUNT) return lower + JSON_ESCAPE_LETTER_VALUE;

	const u32 upper = (u32) byte - (u32) 'A';
	if (upper < JSON_ESCAPE_LETTER_COUNT) return upper + JSON_ESCAPE_LETTER_VALUE;

	return JSON_ESCAPE_DIGIT_INVALID;

}

// reads the four digits of a unit; a byte that is not a digit stops it,
// like the NUL at the end of the text, and leaves the read at that byte
static bool json_text_escape_unit (JsonTextString *string, u32 *unit) {

	u32 value = 0;
	for (size_t i = 0; i < JSON_ESCAPE_UNIT_DIGITS; i++) {
		const u32 digit = json_text_escape_digit (*string->read);
		if (digit == JSON_ESCAPE_DIGIT_INVALID) return false;

		value = (value << JSON_ESCAPE_DIGIT_BITS) | digit;
		string->read += 1;
	}

	*unit = value;

	return true;

}

// the unit that started a pair must be followed by the escape of the unit that ends it
static CerverJsonErrorCode json_text_escape_pair (JsonTextString *string, u32 *code) {

	u32 low = 0;

	if ((string->read[0] != JSON_TEXT_BACKSLASH) || (string->read[1] != 'u')) return CERVER_JSON_ERROR_SURROGATE;

	string->read += JSON_ESCAPE_PREFIX_SIZE;
	if (!json_text_escape_unit (string, &low)) return CERVER_JSON_ERROR_ESCAPE;

	if ((low < JSON_SURROGATE_LOW_FIRST) || (low > JSON_SURROGATE_LAST)) return CERVER_JSON_ERROR_SURROGATE;

	*code = JSON_SURROGATE_BASE
		+ (((*code & JSON_SURROGATE_MASK) << JSON_SURROGATE_BITS) | (low & JSON_SURROGATE_MASK));

	return CERVER_JSON_ERROR_NONE;

}

// reads an escape of a unit, after its u, and writes its character
static CerverJsonErrorCode json_text_escape_unicode (JsonTextString *string) {

	CerverJsonErrorCode error = CERVER_JSON_ERROR_NONE;

	u8 *start = string->read;

	u32 code = 0;
	if (!json_text_escape_unit (string, &code)) return CERVER_JSON_ERROR_ESCAPE;

	if ((code >= JSON_SURROGATE_HIGH_FIRST) && (code <= JSON_SURROGATE_LAST)) {
		if (code < JSON_SURROGATE_LOW_FIRST) error = json_text_escape_pair (string, &code);
		else error = CERVER_JSON_ERROR_SURROGATE;

		if (error == CERVER_JSON_ERROR_SURROGATE) string->read = start;
	}

	else if (!code && (string->cursor->flags & CERVER_JSON_PARSE_REJECT_NUL)) {
		string->read = start;
		error = CERVER_JSON_ERROR_NUL;
	}

	if (error == CERVER_JSON_ERROR_NONE) string->write += json_text_encode (string->write, code);

	return error;

}

// the character of an escape of one letter, 0 for a letter that is not one
static u8 json_text_escape_character (const u8 letter) {

	switch (letter) {
		case '"': return '"';
		case '\\': return '\\';
		case '/': return '/';
		case 'b': return '\b';
		case 'f': return '\f';
		case 'n': return '\n';
		case 'r': return '\r';
		case 't': return '\t';
		default: return 0;
	}

}

// reads an escape, at its backslash
static CerverJsonErrorCode json_text_escape (JsonTextString *string) {

	const u8 letter = string->read[1];

	if (letter == 'u') {
		string->read += JSON_ESCAPE_PREFIX_SIZE;
		return json_text_escape_unicode (string);
	}

	const u8 character = json_text_escape_character (letter);
	if (!character) {
		string->read += 1;
		return CERVER_JSON_ERROR_ESCAPE;
	}

	*string->write = character;
	string->write += 1;
	string->read += JSON_ESCAPE_PREFIX_SIZE;

	return CERVER_JSON_ERROR_NONE;

}

#pragma endregion

#pragma region strings

// moves the plain bytes that follow to where the string is written
static void json_text_string_plain (JsonTextString *string) {

	u8 *stop = json_scan_string (string->read);
	const size_t size = (size_t) (stop - string->read);

	if (size) {
		if (string->write != string->read) (void) memmove (string->write, string->read, size);

		string->write += size;
		string->read = stop;
	}

}

// a character that is not ASCII is checked and kept as it is
static CerverJsonErrorCode json_text_string_character (JsonTextString *string) {

	const size_t size = json_text_character (string->read, (size_t) (string->cursor->end - string->read));
	if (!size) return CERVER_JSON_ERROR_ENCODING;

	if (string->write != string->read) (void) memmove (string->write, string->read, size);

	string->write += size;
	string->read += size;

	return CERVER_JSON_ERROR_NONE;

}

// the byte where the plain part stopped: an escape, a character
// that is not ASCII, or a control character that a string can not have
static CerverJsonErrorCode json_text_string_stop (JsonTextString *string) {

	const u8 byte = *string->read;

	if (byte == JSON_TEXT_BACKSLASH) return json_text_escape (string);

	if (byte < JSON_TEXT_SPACE) return CERVER_JSON_ERROR_CONTROL;

	return json_text_string_character (string);

}

// a string with escapes or with characters that are not ASCII
static CerverJsonErrorCode json_text_string_whole (
	JsonCursor *cursor, u8 *start, u8 *stop, size_t *length
) {

	CerverJsonErrorCode error = CERVER_JSON_ERROR_NONE;

	JsonTextString string = { cursor, stop, stop };

	while ((error == CERVER_JSON_ERROR_NONE) && (*string.read != JSON_TEXT_QUOTE)) {
		error = json_text_string_stop (&string);
		if (error == CERVER_JSON_ERROR_NONE) json_text_string_plain (&string);
	}

	if (error == CERVER_JSON_ERROR_NONE) {
		*string.write = 0;
		*length = (size_t) (string.write - start);
		cursor->position = string.read + 1;
	}

	else {
		cursor->position = string.read;
		if (string.read >= cursor->end) error = CERVER_JSON_ERROR_END;
	}

	return error;

}

CerverJsonErrorCode json_text_string (
	JsonCursor *cursor, const char **string, size_t *length
) {

	u8 *start = cursor->position;
	u8 *stop = json_scan_string (start);

	*string = (const char *) start;

	// most strings are plain: the quote that ends them is the first stop
	if (*stop == JSON_TEXT_QUOTE) {
		*stop = 0;
		*length = (size_t) (stop - start);
		cursor->position = stop + 1;

		return CERVER_JSON_ERROR_NONE;
	}

	return json_text_string_whole (cursor, start, stop, length);

}

#pragma endregion
