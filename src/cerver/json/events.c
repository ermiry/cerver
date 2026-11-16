#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/json/events.h"
#include "cerver/json/internal.h"
#include "cerver/json/json.h"

static const u8 JSON_EVENTS_OBJECT_END = '}';
static const u8 JSON_EVENTS_ARRAY_END = ']';
static const u8 JSON_EVENTS_QUOTE = '"';
static const u8 JSON_EVENTS_SEPARATOR_VALUES = ',';
static const u8 JSON_EVENTS_SEPARATOR_KEY = ':';

static const char JSON_EVENTS_TRUE[] = "true";
static const char JSON_EVENTS_FALSE[] = "false";
static const char JSON_EVENTS_NULL[] = "null";

// a bit for each level that is open, in words of this many
#define JSON_EVENTS_WORD_BITS				64

#pragma region reader

// what a reader does next
typedef enum JsonEventsState {

	JSON_EVENTS_STATE_VALUE,
	JSON_EVENTS_STATE_AFTER,
	JSON_EVENTS_STATE_DONE,
	JSON_EVENTS_STATE_FAILED

} JsonEventsState;

// a text while its events are given
typedef struct JsonEventsReader {

	JsonCursor cursor;

	const CerverJsonEvents *events;
	void *data;

	// a bit for each level that is open: 1 for an object, 0 for an array
	u64 *objects;
	size_t depth;
	size_t depth_max;

	// the values that were read, the keys too, and their limit
	size_t values;
	size_t values_max;

	CerverJsonErrorCode error;

} JsonEventsReader;

static JsonEventsState json_events_fail (JsonEventsReader *reader, const CerverJsonErrorCode error) {

	reader->error = error;

	return JSON_EVENTS_STATE_FAILED;

}

// a method that asks to stop ends the reader
static JsonEventsState json_events_continue (JsonEventsReader *reader, const bool go_on) {

	return go_on ? JSON_EVENTS_STATE_AFTER : json_events_fail (reader, CERVER_JSON_ERROR_STOPPED);

}

// one more value of the text; false over its limit
static bool json_events_count (JsonEventsReader *reader) {

	bool retval = false;

	if (reader->values < reader->values_max) {
		reader->values += 1;
		retval = true;
	}

	else {
		reader->error = CERVER_JSON_ERROR_VALUES;
	}

	return retval;

}

static inline bool json_events_in_object (const JsonEventsReader *reader) {

	const size_t level = reader->depth - 1;

	return (reader->objects[level / JSON_EVENTS_WORD_BITS] >> (level % JSON_EVENTS_WORD_BITS)) & 1;

}

static bool json_events_open (JsonEventsReader *reader, const bool object) {

	bool retval = false;

	if (reader->depth < reader->depth_max) {
		if (json_events_count (reader)) {
			const size_t word = reader->depth / JSON_EVENTS_WORD_BITS;
			const u64 bit = UINT64_C (1) << (reader->depth % JSON_EVENTS_WORD_BITS);

			if (object) reader->objects[word] |= bit;
			else reader->objects[word] &= ~bit;

			reader->depth += 1;

			retval = true;
		}
	}

	else {
		reader->error = CERVER_JSON_ERROR_DEPTH;
	}

	return retval;

}

#pragma endregion

#pragma region values

static JsonEventsState json_events_string (JsonEventsReader *reader) {

	const char *string = NULL;
	size_t length = 0;

	if (!json_events_count (reader)) return JSON_EVENTS_STATE_FAILED;

	reader->cursor.position += 1;

	const CerverJsonErrorCode error = json_text_string (&reader->cursor, &string, &length);
	if (error != CERVER_JSON_ERROR_NONE) return json_events_fail (reader, error);

	return json_events_continue (
		reader, !reader->events->on_string || reader->events->on_string (reader->data, string, length)
	);

}

static bool json_events_number_emit (JsonEventsReader *reader, const JsonNumber *number) {

	const CerverJsonEvents *events = reader->events;

	switch (number->type) {
		case CERVER_JSON_TYPE_INTEGER:
			return !events->on_integer || events->on_integer (reader->data, number->value.integer);

		case CERVER_JSON_TYPE_UNSIGNED:
			return !events->on_unsigned || events->on_unsigned (reader->data, number->value.natural);

		default: return !events->on_real || events->on_real (reader->data, number->value.real);
	}

}

static JsonEventsState json_events_number (JsonEventsReader *reader) {

	JsonNumber number = { CERVER_JSON_TYPE_NONE, { 0 } };

	if (!json_events_count (reader)) return JSON_EVENTS_STATE_FAILED;

	const CerverJsonErrorCode error = json_number_read (&reader->cursor, &number);
	if (error != CERVER_JSON_ERROR_NONE) {
		// a number that the text cut is not one that is written wrong
		const bool cut = (error == CERVER_JSON_ERROR_NUMBER) && (reader->cursor.position >= reader->cursor.end);

		return json_events_fail (reader, cut ? CERVER_JSON_ERROR_END : error);
	}

	return json_events_continue (reader, json_events_number_emit (reader, &number));

}

// the text of a literal, without its NUL; false when the text has another thing
static bool json_events_literal (JsonEventsReader *reader, const char *literal, const size_t length) {

	bool retval = false;

	if (!json_events_count (reader)) return false;

	if (!memcmp (reader->cursor.position, literal, length)) {
		reader->cursor.position += length;
		retval = true;
	}

	else {
		reader->error = ((size_t) (reader->cursor.end - reader->cursor.position) < length) ?
			CERVER_JSON_ERROR_END : CERVER_JSON_ERROR_LITERAL;
	}

	return retval;

}

static JsonEventsState json_events_boolean (
	JsonEventsReader *reader, const char *literal, const size_t length, const bool boolean
) {

	if (!json_events_literal (reader, literal, length)) return JSON_EVENTS_STATE_FAILED;

	return json_events_continue (
		reader, !reader->events->on_boolean || reader->events->on_boolean (reader->data, boolean)
	);

}

static JsonEventsState json_events_null (JsonEventsReader *reader) {

	if (!json_events_literal (reader, JSON_EVENTS_NULL, sizeof (JSON_EVENTS_NULL) - 1)) {
		return JSON_EVENTS_STATE_FAILED;
	}

	return json_events_continue (reader, !reader->events->on_null || reader->events->on_null (reader->data));

}

// the key of a member and its colon: the value of the member is next
static JsonEventsState json_events_key (JsonEventsReader *reader) {

	const char *string = NULL;
	size_t length = 0;

	if (*reader->cursor.position != JSON_EVENTS_QUOTE) {
		return json_events_fail (reader, json_cursor_unexpected (&reader->cursor));
	}

	if (!json_events_count (reader)) return JSON_EVENTS_STATE_FAILED;

	reader->cursor.position += 1;

	const CerverJsonErrorCode error = json_text_string (&reader->cursor, &string, &length);
	if (error != CERVER_JSON_ERROR_NONE) return json_events_fail (reader, error);

	if (reader->events->on_key && !reader->events->on_key (reader->data, string, length)) {
		return json_events_fail (reader, CERVER_JSON_ERROR_STOPPED);
	}

	json_cursor_skip_whitespace (&reader->cursor);

	if (*reader->cursor.position != JSON_EVENTS_SEPARATOR_KEY) {
		return json_events_fail (reader, json_cursor_unexpected (&reader->cursor));
	}

	reader->cursor.position += 1;

	return JSON_EVENTS_STATE_VALUE;

}

// the end of the array or of the object that is open: it is a value that ended
static JsonEventsState json_events_end (JsonEventsReader *reader) {

	const CerverJsonEvents *events = reader->events;
	const bool object = json_events_in_object (reader);

	reader->cursor.position += 1;
	reader->depth -= 1;

	return json_events_continue (
		reader, object ?
			(!events->on_object_end || events->on_object_end (reader->data)) :
			(!events->on_array_end || events->on_array_end (reader->data))
	);

}

static JsonEventsState json_events_object (JsonEventsReader *reader) {

	if (!json_events_open (reader, true)) return JSON_EVENTS_STATE_FAILED;

	if (reader->events->on_object_begin && !reader->events->on_object_begin (reader->data)) {
		return json_events_fail (reader, CERVER_JSON_ERROR_STOPPED);
	}

	reader->cursor.position += 1;

	json_cursor_skip_whitespace (&reader->cursor);

	return (*reader->cursor.position == JSON_EVENTS_OBJECT_END) ?
		json_events_end (reader) : json_events_key (reader);

}

static JsonEventsState json_events_array (JsonEventsReader *reader) {

	if (!json_events_open (reader, false)) return JSON_EVENTS_STATE_FAILED;

	if (reader->events->on_array_begin && !reader->events->on_array_begin (reader->data)) {
		return json_events_fail (reader, CERVER_JSON_ERROR_STOPPED);
	}

	reader->cursor.position += 1;

	json_cursor_skip_whitespace (&reader->cursor);

	return (*reader->cursor.position == JSON_EVENTS_ARRAY_END) ?
		json_events_end (reader) : JSON_EVENTS_STATE_VALUE;

}

// a value starts here
static JsonEventsState json_events_value (JsonEventsReader *reader) {

	json_cursor_skip_whitespace (&reader->cursor);

	switch (*reader->cursor.position) {
		case '"': return json_events_string (reader);

		case '{': return json_events_object (reader);

		case '[': return json_events_array (reader);

		case '-':
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			return json_events_number (reader);

		case 't': return json_events_boolean (reader, JSON_EVENTS_TRUE, sizeof (JSON_EVENTS_TRUE) - 1, true);

		case 'f': return json_events_boolean (reader, JSON_EVENTS_FALSE, sizeof (JSON_EVENTS_FALSE) - 1, false);

		case 'n': return json_events_null (reader);

		default: return json_events_fail (reader, json_cursor_unexpected (&reader->cursor));
	}

}

// after a value of an array: another one or its end
static JsonEventsState json_events_after_element (JsonEventsReader *reader) {

	const u8 byte = *reader->cursor.position;

	if (byte == JSON_EVENTS_SEPARATOR_VALUES) {
		reader->cursor.position += 1;
		return JSON_EVENTS_STATE_VALUE;
	}

	if (byte == JSON_EVENTS_ARRAY_END) return json_events_end (reader);

	return json_events_fail (reader, json_cursor_unexpected (&reader->cursor));

}

// after the value of a member of an object: another member or its end
static JsonEventsState json_events_after_member (JsonEventsReader *reader) {

	const u8 byte = *reader->cursor.position;

	if (byte == JSON_EVENTS_SEPARATOR_VALUES) {
		reader->cursor.position += 1;

		json_cursor_skip_whitespace (&reader->cursor);

		return json_events_key (reader);
	}

	if (byte == JSON_EVENTS_OBJECT_END) return json_events_end (reader);

	return json_events_fail (reader, json_cursor_unexpected (&reader->cursor));

}

// a value ended: what follows depends on what has it
static JsonEventsState json_events_after (JsonEventsReader *reader) {

	if (!reader->depth) return JSON_EVENTS_STATE_DONE;

	json_cursor_skip_whitespace (&reader->cursor);

	return json_events_in_object (reader) ?
		json_events_after_member (reader) : json_events_after_element (reader);

}

// reads the whole text: one value, and nothing but whitespace after it
static bool json_events_run (JsonEventsReader *reader) {

	JsonEventsState state = JSON_EVENTS_STATE_VALUE;

	while ((state == JSON_EVENTS_STATE_VALUE) || (state == JSON_EVENTS_STATE_AFTER)) {
		state = (state == JSON_EVENTS_STATE_VALUE) ? json_events_value (reader) : json_events_after (reader);
	}

	if (state == JSON_EVENTS_STATE_DONE) {
		json_cursor_skip_whitespace (&reader->cursor);

		if (reader->cursor.position != reader->cursor.end) reader->error = CERVER_JSON_ERROR_TRAILING;
	}

	return reader->error == CERVER_JSON_ERROR_NONE;

}

#pragma endregion

#pragma region public

// what a call to read has
typedef struct JsonEventsRead {

	Arena *arena;

	// the text to copy, or the one to read in situ
	const char *text;
	char *in_situ;
	size_t length;

	unsigned int flags;
	const CerverJsonLimits *limits;

	const CerverJsonEvents *events;
	void *data;

} JsonEventsRead;

// the bytes of the bits of the levels that a text can have open
static size_t json_events_levels_size (const size_t depth) {

	return ((depth + JSON_EVENTS_WORD_BITS - 1) / JSON_EVENTS_WORD_BITS) * sizeof (u64);

}

// checks what a call has before anything is taken
static CerverJsonErrorCode json_events_check (const JsonEventsRead *read, const CerverJsonLimits *limits) {

	if ((!read->text && !read->in_situ) || !read->events) return CERVER_JSON_ERROR_ARGUMENTS;

	// the keys of an object are not kept: they can not be compared
	if (read->flags & CERVER_JSON_PARSE_REJECT_DUPLICATES) return CERVER_JSON_ERROR_UNSUPPORTED;

	if (read->length > limits->size) return CERVER_JSON_ERROR_SIZE;

	return CERVER_JSON_ERROR_NONE;

}

// takes the text and the levels from the arena
static bool json_events_prepare (JsonEventsReader *reader, Arena *arena, const JsonEventsRead *read) {

	bool retval = false;

	const bool ready = read->in_situ ? true : json_cursor_copy (
		&reader->cursor, arena, read->text, read->length, read->flags
	);

	if (ready) {
		if (read->in_situ) json_cursor_in_situ (&reader->cursor, read->in_situ, read->length, read->flags);

		reader->objects = (u64 *) arena_alloc (arena, json_events_levels_size (reader->depth_max));

		retval = reader->objects != NULL;
	}

	if (!retval) reader->error = CERVER_JSON_ERROR_MEMORY;

	return retval;

}

static bool json_events_read (const JsonEventsRead *read, CerverJsonError *error) {

	const CerverJsonLimits limits = json_limits_resolve (read->limits);

	JsonEventsReader reader;
	(void) memset (&reader, 0, sizeof (JsonEventsReader));

	reader.error = json_events_check (read, &limits);
	if (reader.error != CERVER_JSON_ERROR_NONE) {
		json_error_set (error, reader.error, 0);
		return false;
	}

	reader.events = read->events;
	reader.data = read->data;
	reader.depth_max = limits.depth;
	reader.values_max = limits.values;

	// its own arena has the copy of the text and the levels in one chunk
	Arena *arena = read->arena ? read->arena : arena_create (
		ARENA_ALIGNMENT + read->length + CERVER_JSON_PADDING
		+ ARENA_ALIGNMENT + json_events_levels_size (limits.depth)
	);

	if (!arena) {
		json_error_set (error, CERVER_JSON_ERROR_MEMORY, 0);
		return false;
	}

	if (json_events_prepare (&reader, arena, read)) (void) json_events_run (&reader);

	// a text that was read has no position to report
	json_error_set (
		error, reader.error,
		((reader.error != CERVER_JSON_ERROR_NONE) && reader.cursor.start) ?
			(size_t) (reader.cursor.position - reader.cursor.start) : 0
	);

	if (!read->arena) arena_delete (arena);

	return reader.error == CERVER_JSON_ERROR_NONE;

}

bool cerver_json_read (
	Arena *arena,
	const char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	const CerverJsonEvents *events, void *data,
	CerverJsonError *error
) {

	const JsonEventsRead read = { arena, text, NULL, length, flags, limits, events, data };

	return json_events_read (&read, error);

}

bool cerver_json_read_in_situ (
	Arena *arena,
	char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	const CerverJsonEvents *events, void *data,
	CerverJsonError *error
) {

	const JsonEventsRead read = { arena, NULL, text, length, flags, limits, events, data };

	return json_events_read (&read, error);

}

#pragma endregion
