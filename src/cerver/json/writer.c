#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"
#include "cerver/json/writer.h"

static const char JSON_WRITER_OBJECT_START = '{';
static const char JSON_WRITER_OBJECT_END = '}';
static const char JSON_WRITER_ARRAY_START = '[';
static const char JSON_WRITER_ARRAY_END = ']';
static const char JSON_WRITER_QUOTE = '"';
static const char JSON_WRITER_COMMA = ',';
static const char JSON_WRITER_COLON = ':';
static const char JSON_WRITER_SPACE = ' ';
static const char JSON_WRITER_LINE = '\n';

static const char JSON_WRITER_NULL[] = "null";
static const char JSON_WRITER_TRUE[] = "true";
static const char JSON_WRITER_FALSE[] = "false";

// the first buffer that a writer takes from its arena, and how much it grows each time
static const size_t JSON_WRITER_ARENA_INITIAL = 1 << 8;
static const size_t JSON_WRITER_ARENA_GROWTH = 2;

// the spaces of an indentation are written from here, some at a time
static const char JSON_WRITER_SPACES[] = "                                                                ";

// the escapes of a string
static const u8 JSON_WRITER_ASCII_END = 0x80;
static const u8 JSON_WRITER_SLASH = '/';
static const u8 JSON_WRITER_BACKSLASH = '\\';
static const char JSON_WRITER_HEXADECIMAL[] = "0123456789ABCDEF";
static const char JSON_WRITER_UNIT_START[] = "\\u";

// \uXXXX
#define JSON_WRITER_UNIT_SIZE				6
#define JSON_WRITER_UNIT_DIGITS				4
#define JSON_WRITER_DIGIT_BITS				4
#define JSON_WRITER_DIGIT_MASK				0xF

// the bits of the code point that the first byte of a character of two, three and four bytes
// has, and the ones of each byte that continues it
static const u32 JSON_WRITER_TWO_MASK = 0x1F;
static const u32 JSON_WRITER_THREE_MASK = 0x0F;
static const u32 JSON_WRITER_FOUR_MASK = 0x07;
static const u32 JSON_WRITER_CONTINUATION_MASK = 0x3F;
#define JSON_WRITER_CONTINUATION_BITS		6

static const size_t JSON_WRITER_TWO_BYTES = 2;
static const size_t JSON_WRITER_THREE_BYTES = 3;

// a code point over the ones of a unit is written as a pair of them
static const u32 JSON_WRITER_PAIR_BASE = 0x10000;
static const u32 JSON_WRITER_PAIR_HIGH = 0xD800;
static const u32 JSON_WRITER_PAIR_LOW = 0xDC00;
static const u32 JSON_WRITER_PAIR_MASK = 0x3FF;
#define JSON_WRITER_PAIR_BITS				10

#pragma region output

static inline void json_writer_fail (CerverJsonWriter *writer, const CerverJsonErrorCode error) {

	if (writer->error == CERVER_JSON_ERROR_NONE) writer->error = error;

}

// makes room in the arena for more bytes and the NUL after them
static bool json_writer_grow (CerverJsonWriter *writer, const size_t size) {

	bool retval = false;

	size_t capacity = writer->capacity ? writer->capacity : JSON_WRITER_ARENA_INITIAL;
	while (capacity <= (writer->length + size)) capacity *= JSON_WRITER_ARENA_GROWTH;

	char *buffer = (char *) arena_alloc (writer->arena, capacity);
	if (buffer) {
		if (writer->length) (void) memcpy (buffer, writer->buffer, writer->length);

		writer->buffer = buffer;
		writer->capacity = capacity;

		retval = true;
	}

	else {
		json_writer_fail (writer, CERVER_JSON_ERROR_MEMORY);
	}

	return retval;

}

// adds bytes to the text when they do not fit as they are: an arena
// grows, and in a buffer of the caller only the ones that fit go, but all of them count
static void json_writer_put_checked (CerverJsonWriter *writer, const char *bytes, const size_t size) {

	if (writer->error != CERVER_JSON_ERROR_NONE) return;

	if (writer->arena) {
		if (((writer->length + size) >= writer->capacity) && !json_writer_grow (writer, size)) return;

		(void) memcpy (writer->buffer + writer->length, bytes, size);
		writer->buffer[writer->length + size] = '\0';
	}

	else if (writer->capacity) {
		// the last byte of the buffer is for the NUL
		const size_t usable = writer->capacity - 1;
		if (writer->length < usable) {
			const size_t fits = ((usable - writer->length) < size) ? (usable - writer->length) : size;

			(void) memcpy (writer->buffer + writer->length, bytes, fits);
			writer->buffer[writer->length + fits] = '\0';
		}
	}

	writer->length += size;

}

// adds bytes to the text. Almost every call has room for them and for the NUL after
// them in a writer that has not failed: that path is in its caller, without a call
static inline void json_writer_put (CerverJsonWriter *writer, const char *bytes, const size_t size) {

	if ((writer->error == CERVER_JSON_ERROR_NONE) && ((writer->length + size) < writer->capacity)) {
		(void) memcpy (writer->buffer + writer->length, bytes, size);

		writer->length += size;
		writer->buffer[writer->length] = '\0';
	}

	else {
		json_writer_put_checked (writer, bytes, size);
	}

}

static inline void json_writer_put_character (CerverJsonWriter *writer, const char character) {

	json_writer_put (writer, &character, 1);

}

// a new line and the spaces of the level
static void json_writer_put_line (CerverJsonWriter *writer, const size_t depth) {

	size_t spaces = depth * writer->indent;

	json_writer_put_character (writer, JSON_WRITER_LINE);

	while (spaces) {
		const size_t size = (spaces < (sizeof (JSON_WRITER_SPACES) - 1)) ? spaces : (sizeof (JSON_WRITER_SPACES) - 1);

		json_writer_put (writer, JSON_WRITER_SPACES, size);
		spaces -= size;
	}

}

#pragma endregion

#pragma region state

static inline bool json_writer_in_object (const CerverJsonWriter *writer) {

	const size_t level = writer->depth - 1;

	return writer->depth
		&& ((writer->objects[level / CERVER_JSON_WRITER_WORD_BITS] >> (level % CERVER_JSON_WRITER_WORD_BITS)) & 1);

}

// what goes between two values of an array or two members of an object
static void json_writer_separate (CerverJsonWriter *writer) {

	if (!writer->empty) {
		json_writer_put_character (writer, JSON_WRITER_COMMA);

		if (!writer->indent && !(writer->flags & CERVER_JSON_WRITE_COMPACT)) {
			json_writer_put_character (writer, JSON_WRITER_SPACE);
		}
	}

	if (writer->indent) json_writer_put_line (writer, writer->depth);

}

// a value is next: at the top only one, in an object after its key, in an array after the others
static bool json_writer_value_start (CerverJsonWriter *writer) {

	if (writer->error != CERVER_JSON_ERROR_NONE) return false;

	if (!writer->depth) {
		if (!writer->empty) json_writer_fail (writer, CERVER_JSON_ERROR_STATE);
	}

	else if (json_writer_in_object (writer)) {
		if (!writer->keyed) json_writer_fail (writer, CERVER_JSON_ERROR_STATE);
	}

	else {
		json_writer_separate (writer);
	}

	writer->empty = false;
	writer->keyed = false;

	return writer->error == CERVER_JSON_ERROR_NONE;

}

static void json_writer_open (CerverJsonWriter *writer, const bool object) {

	if (!json_writer_value_start (writer)) return;

	if (writer->depth >= CERVER_JSON_WRITER_DEPTH) {
		json_writer_fail (writer, CERVER_JSON_ERROR_DEPTH);
		return;
	}

	const size_t word = writer->depth / CERVER_JSON_WRITER_WORD_BITS;
	const u64 bit = UINT64_C (1) << (writer->depth % CERVER_JSON_WRITER_WORD_BITS);

	if (object) writer->objects[word] |= bit;
	else writer->objects[word] &= ~bit;

	writer->depth += 1;
	writer->empty = true;

	json_writer_put_character (writer, object ? JSON_WRITER_OBJECT_START : JSON_WRITER_ARRAY_START);

}

static void json_writer_close (CerverJsonWriter *writer, const bool object) {

	if (writer->error != CERVER_JSON_ERROR_NONE) return;

	if (!writer->depth || (json_writer_in_object (writer) != object) || writer->keyed) {
		json_writer_fail (writer, CERVER_JSON_ERROR_STATE);
		return;
	}

	writer->depth -= 1;

	if (writer->indent && !writer->empty) json_writer_put_line (writer, writer->depth);

	// what was closed is a value of what has it
	writer->empty = false;

	json_writer_put_character (writer, object ? JSON_WRITER_OBJECT_END : JSON_WRITER_ARRAY_END);

}

#pragma endregion

#pragma region strings

// \uXXXX
static void json_writer_put_unit (CerverJsonWriter *writer, const u32 unit) {

	char escape[JSON_WRITER_UNIT_SIZE];

	(void) memcpy (escape, JSON_WRITER_UNIT_START, sizeof (JSON_WRITER_UNIT_START) - 1);

	for (size_t i = 0; i < JSON_WRITER_UNIT_DIGITS; i++) {
		const unsigned int shift = (unsigned int) ((JSON_WRITER_UNIT_DIGITS - 1 - i) * JSON_WRITER_DIGIT_BITS);

		escape[(sizeof (JSON_WRITER_UNIT_START) - 1) + i] = JSON_WRITER_HEXADECIMAL[(unit >> shift) & JSON_WRITER_DIGIT_MASK];
	}

	json_writer_put (writer, escape, sizeof (escape));

}

// the escape of one letter of a character, 0 when it has none
static char json_writer_escape_letter (const CerverJsonWriter *writer, const u8 byte) {

	switch (byte) {
		case '"': return '"';
		case '\\': return '\\';
		case '\b': return 'b';
		case '\f': return 'f';
		case '\n': return 'n';
		case '\r': return 'r';
		case '\t': return 't';
		case '/': return (writer->flags & CERVER_JSON_WRITE_ESCAPE_SLASH) ? '/' : 0;
		default: return 0;
	}

}

// a character of ASCII that can not be written as it is
static void json_writer_put_escape (CerverJsonWriter *writer, const u8 byte) {

	const char letter = json_writer_escape_letter (writer, byte);

	if (letter) {
		const char escape[] = { (char) JSON_WRITER_BACKSLASH, letter };
		json_writer_put (writer, escape, sizeof (escape));
	}

	else {
		json_writer_put_unit (writer, byte);
	}

}

// the code point of a character of UTF-8 that was already checked
static u32 json_writer_code_point (const u8 *bytes, const size_t size) {

	u32 code = 0;

	if (size == JSON_WRITER_TWO_BYTES) code = bytes[0] & JSON_WRITER_TWO_MASK;
	else if (size == JSON_WRITER_THREE_BYTES) code = bytes[0] & JSON_WRITER_THREE_MASK;
	else code = bytes[0] & JSON_WRITER_FOUR_MASK;

	for (size_t i = 1; i < size; i++) {
		code = (code << JSON_WRITER_CONTINUATION_BITS) | (bytes[i] & JSON_WRITER_CONTINUATION_MASK);
	}

	return code;

}

// a character that is not ASCII, as one unit or as a pair of them
static void json_writer_put_code_point (CerverJsonWriter *writer, const u32 code) {

	if (code < JSON_WRITER_PAIR_BASE) {
		json_writer_put_unit (writer, code);
	}

	else {
		const u32 offset = code - JSON_WRITER_PAIR_BASE;

		json_writer_put_unit (writer, JSON_WRITER_PAIR_HIGH + (offset >> JSON_WRITER_PAIR_BITS));
		json_writer_put_unit (writer, JSON_WRITER_PAIR_LOW + (offset & JSON_WRITER_PAIR_MASK));
	}

}

// how many bytes at the start of some bytes are written as they are: the plain
// part of a string for a reader, that with the flag of the slash also ends at one
static inline size_t json_writer_plain (const CerverJsonWriter *writer, const u8 *bytes, const size_t length) {

	const size_t plain = json_scan_plain (bytes, length);

	const u8 *slash = ((writer->flags & CERVER_JSON_WRITE_ESCAPE_SLASH) && plain) ?
		(const u8 *) memchr (bytes, JSON_WRITER_SLASH, plain) : NULL;

	return slash ? (size_t) (slash - bytes) : plain;

}

// a character that is not ASCII: checked, and written as it is or as its escape; its bytes, 0 when it is not valid
static size_t json_writer_put_character_wide (CerverJsonWriter *writer, const u8 *bytes, const size_t available) {

	const size_t size = json_text_character (bytes, available);

	if (!size) {
		json_writer_fail (writer, CERVER_JSON_ERROR_ENCODING);
	}

	else if (writer->flags & CERVER_JSON_WRITE_ENSURE_ASCII) {
		json_writer_put_code_point (writer, json_writer_code_point (bytes, size));
	}

	else {
		json_writer_put (writer, (const char *) bytes, size);
	}

	return size;

}

// a string with its quotes and its escapes
static void json_writer_put_string (CerverJsonWriter *writer, const char *string, const size_t length) {

	const u8 *bytes = (const u8 *) string;

	size_t position = 0;

	json_writer_put_character (writer, JSON_WRITER_QUOTE);

	while ((position < length) && (writer->error == CERVER_JSON_ERROR_NONE)) {
		// the bytes that are written as they are, together
		const size_t plain = position + json_writer_plain (writer, bytes + position, length - position);

		if (plain > position) {
			json_writer_put (writer, string + position, plain - position);
			position = plain;
		}

		if (position < length) {
			if (bytes[position] < JSON_WRITER_ASCII_END) {
				json_writer_put_escape (writer, bytes[position]);
				position += 1;
			}

			else {
				position += json_writer_put_character_wide (writer, bytes + position, length - position);
			}
		}
	}

	json_writer_put_character (writer, JSON_WRITER_QUOTE);

}

#pragma endregion

#pragma region public

static void json_writer_init (CerverJsonWriter *writer, const unsigned int flags) {

	(void) memset (writer, 0, sizeof (CerverJsonWriter));

	writer->flags = flags;
	writer->error = CERVER_JSON_ERROR_NONE;
	writer->empty = true;

}

void cerver_json_writer_init (
	CerverJsonWriter *writer,
	char *buffer, const size_t capacity,
	const unsigned int flags
) {

	if (writer) {
		json_writer_init (writer, flags);

		writer->buffer = buffer;
		writer->capacity = buffer ? capacity : 0;

		if (writer->capacity) writer->buffer[0] = '\0';
	}

}

void cerver_json_writer_init_arena (
	CerverJsonWriter *writer,
	Arena *arena,
	const unsigned int flags
) {

	if (writer) {
		json_writer_init (writer, flags);

		writer->arena = arena;

		if (!arena) writer->error = CERVER_JSON_ERROR_ARGUMENTS;
	}

}

void cerver_json_writer_set_indent (CerverJsonWriter *writer, const unsigned int spaces) {

	if (writer) writer->indent = spaces;

}

void cerver_json_writer_object_begin (CerverJsonWriter *writer) {

	if (writer) json_writer_open (writer, true);

}

void cerver_json_writer_object_end (CerverJsonWriter *writer) {

	if (writer) json_writer_close (writer, true);

}

void cerver_json_writer_array_begin (CerverJsonWriter *writer) {

	if (writer) json_writer_open (writer, false);

}

void cerver_json_writer_array_end (CerverJsonWriter *writer) {

	if (writer) json_writer_close (writer, false);

}

void cerver_json_writer_key_bytes (CerverJsonWriter *writer, const char *key, const size_t length) {

	if (!writer || (writer->error != CERVER_JSON_ERROR_NONE)) return;

	if (!key) {
		json_writer_fail (writer, CERVER_JSON_ERROR_ARGUMENTS);
		return;
	}

	if (!json_writer_in_object (writer) || writer->keyed) {
		json_writer_fail (writer, CERVER_JSON_ERROR_STATE);
		return;
	}

	json_writer_separate (writer);

	json_writer_put_string (writer, key, length);

	json_writer_put_character (writer, JSON_WRITER_COLON);
	if (!(writer->flags & CERVER_JSON_WRITE_COMPACT)) json_writer_put_character (writer, JSON_WRITER_SPACE);

	writer->empty = false;
	writer->keyed = true;

}

void cerver_json_writer_key (CerverJsonWriter *writer, const char *key) {

	cerver_json_writer_key_bytes (writer, key, key ? strlen (key) : 0);

}

void cerver_json_writer_string (CerverJsonWriter *writer, const char *string, const size_t length) {

	if (!writer) return;

	if (!string) {
		json_writer_fail (writer, CERVER_JSON_ERROR_ARGUMENTS);
		return;
	}

	if (json_writer_value_start (writer)) json_writer_put_string (writer, string, length);

}

void cerver_json_writer_null (CerverJsonWriter *writer) {

	if (writer && json_writer_value_start (writer)) {
		json_writer_put (writer, JSON_WRITER_NULL, sizeof (JSON_WRITER_NULL) - 1);
	}

}

void cerver_json_writer_boolean (CerverJsonWriter *writer, const bool value) {

	if (writer && json_writer_value_start (writer)) {
		if (value) json_writer_put (writer, JSON_WRITER_TRUE, sizeof (JSON_WRITER_TRUE) - 1);
		else json_writer_put (writer, JSON_WRITER_FALSE, sizeof (JSON_WRITER_FALSE) - 1);
	}

}

void cerver_json_writer_integer (CerverJsonWriter *writer, const i64 value) {

	char text[JSON_NUMBER_INTEGER_SIZE];

	if (writer && json_writer_value_start (writer)) {
		const size_t size = json_number_write_integer (text, value);

		json_writer_put (writer, text, size);
	}

}

void cerver_json_writer_unsigned (CerverJsonWriter *writer, const u64 value) {

	char text[JSON_NUMBER_INTEGER_SIZE];

	if (writer && json_writer_value_start (writer)) {
		const size_t size = json_number_write_unsigned (text, value);

		json_writer_put (writer, text, size);
	}

}

void cerver_json_writer_real (CerverJsonWriter *writer, const double value) {

	char text[JSON_NUMBER_REAL_SIZE];

	if (!writer) return;

	const size_t size = json_number_write_real (text, value);
	if (!size) {
		json_writer_fail (writer, CERVER_JSON_ERROR_RANGE);
		return;
	}

	if (json_writer_value_start (writer)) json_writer_put (writer, text, size);

}

void cerver_json_writer_raw (CerverJsonWriter *writer, const char *text, const size_t length) {

	if (!writer) return;

	if (!text || !length) {
		json_writer_fail (writer, CERVER_JSON_ERROR_ARGUMENTS);
		return;
	}

	if (json_writer_value_start (writer)) json_writer_put (writer, text, length);

}

#pragma endregion

#pragma region values

static void json_writer_value (CerverJsonWriter *writer, const CerverJsonValue *value);

static void json_writer_array (CerverJsonWriter *writer, const CerverJsonValue *array) {

	const size_t count = json_value_length (array);

	json_writer_open (writer, false);

	const CerverJsonValue *element = array + 1;
	for (size_t i = 0; (i < count) && (writer->error == CERVER_JSON_ERROR_NONE); i++) {
		json_writer_value (writer, element);

		element = json_value_next (element);
	}

	json_writer_close (writer, false);

}

static void json_writer_object (CerverJsonWriter *writer, const CerverJsonValue *object) {

	const size_t count = json_value_length (object);

	json_writer_open (writer, true);

	const CerverJsonValue *key = object + 1;
	for (size_t i = 0; (i < count) && (writer->error == CERVER_JSON_ERROR_NONE); i++) {
		cerver_json_writer_key_bytes (writer, key->content.string, json_value_length (key));

		json_writer_value (writer, key + 1);

		key = json_value_next (key + 1);
	}

	json_writer_close (writer, true);

}

// a value with everything inside: it calls itself for each level, and the levels of the writer end it
static void json_writer_value (CerverJsonWriter *writer, const CerverJsonValue *value) {

	switch (json_value_type (value)) {
		case CERVER_JSON_TYPE_NULL: cerver_json_writer_null (writer); break;

		case CERVER_JSON_TYPE_BOOLEAN: cerver_json_writer_boolean (writer, value->content.natural != 0); break;

		case CERVER_JSON_TYPE_INTEGER: cerver_json_writer_integer (writer, value->content.integer); break;

		case CERVER_JSON_TYPE_UNSIGNED: cerver_json_writer_unsigned (writer, value->content.natural); break;

		case CERVER_JSON_TYPE_REAL: cerver_json_writer_real (writer, value->content.real); break;

		case CERVER_JSON_TYPE_STRING:
			cerver_json_writer_string (writer, value->content.string, json_value_length (value));
			break;

		case CERVER_JSON_TYPE_ARRAY: json_writer_array (writer, value); break;

		case CERVER_JSON_TYPE_OBJECT: json_writer_object (writer, value); break;

		default: json_writer_fail (writer, CERVER_JSON_ERROR_ARGUMENTS); break;
	}

}

void cerver_json_writer_value (CerverJsonWriter *writer, const CerverJsonValue *value) {

	if (!writer) return;

	if (value) json_writer_value (writer, value);
	else json_writer_fail (writer, CERVER_JSON_ERROR_ARGUMENTS);

}

CerverJsonErrorCode cerver_json_writer_end (CerverJsonWriter *writer) {

	if (!writer) return CERVER_JSON_ERROR_ARGUMENTS;

	if (writer->error == CERVER_JSON_ERROR_NONE) {
		if (writer->depth || writer->empty || writer->keyed) writer->error = CERVER_JSON_ERROR_STATE;
		else if (!writer->arena && (writer->length >= writer->capacity)) writer->error = CERVER_JSON_ERROR_TRUNCATED;
	}

	return writer->error;

}

const char *cerver_json_writer_text (const CerverJsonWriter *writer) {

	return writer ? writer->buffer : NULL;

}

size_t cerver_json_writer_length (const CerverJsonWriter *writer) {

	return writer ? writer->length : 0;

}

size_t cerver_json_write (
	const CerverJsonValue *value,
	char *buffer, const size_t capacity,
	const unsigned int flags
) {

	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, capacity, flags);
	cerver_json_writer_value (&writer, value);

	const CerverJsonErrorCode error = cerver_json_writer_end (&writer);

	return ((error == CERVER_JSON_ERROR_NONE) || (error == CERVER_JSON_ERROR_TRUNCATED)) ? writer.length : 0;

}

#pragma endregion
