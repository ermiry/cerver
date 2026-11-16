#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/json/document.h"
#include "cerver/json/internal.h"
#include "cerver/json/json.h"

static const u8 JSON_OBJECT_END = '}';
static const u8 JSON_ARRAY_END = ']';
static const u8 JSON_STRING_QUOTE = '"';
static const u8 JSON_SEPARATOR_VALUES = ',';
static const u8 JSON_SEPARATOR_KEY = ':';

static const char JSON_LITERAL_TRUE[] = "true";
static const char JSON_LITERAL_FALSE[] = "false";
static const char JSON_LITERAL_NULL[] = "null";

// one more element of an array or member of an object, in the tag of the value
static const u64 JSON_TAG_LENGTH_ONE = UINT64_C (1) << JSON_TAG_TYPE_BITS;

// the memory of the values of a text has its values and, after the members of each object, its directory
// and its table. A text that is valid and small has at most four of them for each five of its bytes
// ({"":{"":1}}: an object with one member is four bytes, and it is a value, a key and the two of its
// directory) and two more, and takes them at once; a bigger one starts with one for each
// JSON_BUILDER_RATIO bytes. A text that is not valid can have a value for each byte before its error
// is found ("[[[["), and with the directories and the tables no text has more than JSON_BUILDER_BOUND_RATIO
// for each byte
static const size_t JSON_BUILDER_SMALL_TEXT = 1 << 8;
static const size_t JSON_BUILDER_SMALL_NUMERATOR = 4;
static const size_t JSON_BUILDER_SMALL_DENOMINATOR = 5;
static const size_t JSON_BUILDER_SMALL_EXTRA = 2;
static const size_t JSON_BUILDER_RATIO = 4;
static const size_t JSON_BUILDER_BOUND_RATIO = 3;
static const size_t JSON_BUILDER_BOUND_EXTRA = 8;
static const size_t JSON_BUILDER_GROWTH = 2;

// the members of the objects that are open, before each directory goes to its place: a reader has this many in
// its own memory, and takes more from its arena for a text with more keys in the objects that it has open
#define JSON_BUILDER_MEMBERS				(1 << 6)

// the word after the last slot of the table of an object: the slots that its searches walk, 0 when it is not used
static size_t json_object_table_probes = JSON_OBJECT_TABLE_PROBES;

#pragma region builder

// what a reader does next
typedef enum JsonBuilderState {

	JSON_BUILDER_STATE_VALUE,
	JSON_BUILDER_STATE_AFTER,
	JSON_BUILDER_STATE_DONE,
	JSON_BUILDER_STATE_FAILED

} JsonBuilderState;

// the values of a text while it is read
typedef struct JsonBuilder {

	JsonCursor cursor;
	Arena *arena;

	// the memory of the values, the next one that is free, its end and the most that the text can need
	CerverJsonValue *values;
	CerverJsonValue *next;
	CerverJsonValue *end;
	size_t capacity_max;

	// the values of the text, without what the directories take, and their limit
	size_t count;
	size_t count_max;

	// the array or the object that is open, NULL at the top
	CerverJsonValue *container;
	size_t depth;
	size_t depth_max;

	// the members of the objects that are open, the ones of the innermost one at the end
	JsonMember *members;
	size_t members_count;
	size_t members_capacity;
	JsonMember members_own[JSON_BUILDER_MEMBERS];

	CerverJsonErrorCode error;

} JsonBuilder;

static JsonBuilderState json_builder_fail (JsonBuilder *builder, const CerverJsonErrorCode error) {

	builder->error = error;

	return JSON_BUILDER_STATE_FAILED;

}

// the most that the memory of the values of any text of a length can need
static size_t json_builder_bound (const size_t length) {

	return (length * JSON_BUILDER_BOUND_RATIO) + JSON_BUILDER_BOUND_EXTRA;

}

// what a text starts with: a valid one that is small never needs more
static size_t json_builder_capacity_initial (const size_t length) {

	return (length > JSON_BUILDER_SMALL_TEXT) ?
		(length / JSON_BUILDER_RATIO) :
		(((length * JSON_BUILDER_SMALL_NUMERATOR) / JSON_BUILDER_SMALL_DENOMINATOR) + JSON_BUILDER_SMALL_EXTRA);

}

// moves the values to a bigger block of the arena, with room for some more: what a value has
// of the others are distances, so they are the same in their new place
static bool json_builder_grow (JsonBuilder *builder, const size_t needed) {

	bool retval = false;

	const size_t used = (size_t) (builder->next - builder->values);
	const size_t capacity = (size_t) (builder->end - builder->values);

	// the bound of a text is never reached
	if ((used + needed) <= builder->capacity_max) {
		size_t bigger = capacity * JSON_BUILDER_GROWTH;
		if (bigger < (used + needed)) bigger = used + needed;
		if (bigger > builder->capacity_max) bigger = builder->capacity_max;

		CerverJsonValue *values = (CerverJsonValue *) arena_alloc (
			builder->arena, bigger * sizeof (CerverJsonValue)
		);

		if (values) {
			(void) memcpy (values, builder->values, used * sizeof (CerverJsonValue));

			if (builder->container) builder->container = values + (builder->container - builder->values);

			builder->values = values;
			builder->next = values + used;
			builder->end = values + bigger;

			retval = true;
		}
	}

	if (!retval) builder->error = CERVER_JSON_ERROR_MEMORY;

	return retval;

}

// room for some values more after the last one
static inline bool json_builder_reserve (JsonBuilder *builder, const size_t needed) {

	return (((size_t) (builder->end - builder->next)) >= needed) || json_builder_grow (builder, needed);

}

// the next value of the text, NULL with the error of the builder
static inline CerverJsonValue *json_builder_take (JsonBuilder *builder) {

	if (builder->count == builder->count_max) {
		builder->error = CERVER_JSON_ERROR_VALUES;
		return NULL;
	}

	if ((builder->next == builder->end) && !json_builder_grow (builder, 1)) return NULL;

	CerverJsonValue *value = builder->next;
	builder->next += 1;
	builder->count += 1;

	return value;

}

// the next value of the text as an element, when what is open is an array
static inline CerverJsonValue *json_builder_take_value (JsonBuilder *builder) {

	CerverJsonValue *value = json_builder_take (builder);

	if (value && builder->container && (json_value_type (builder->container) == CERVER_JSON_TYPE_ARRAY)) {
		builder->container->tag += JSON_TAG_LENGTH_ONE;
	}

	return value;

}

static bool json_builder_open (JsonBuilder *builder, const CerverJsonType type) {

	bool retval = false;

	if (builder->depth < builder->depth_max) {
		CerverJsonValue *value = json_builder_take_value (builder);
		if (value) {
			value->tag = (u64) type;

			// while it is open it keeps the way back to the one that has it,
			// and an object where its members start among the ones of the reader
			value->content.links.span = builder->container ? (u32) (value - builder->container) : 0;
			value->content.links.directory = (u32) builder->members_count;

			builder->container = value;
			builder->depth += 1;

			retval = true;
		}
	}

	else {
		builder->error = CERVER_JSON_ERROR_DEPTH;
	}

	return retval;

}

#pragma endregion

#pragma region members

// more room for the members of the objects that are open
static bool json_builder_members_grow (JsonBuilder *builder) {

	bool retval = false;

	const size_t capacity = builder->members_capacity * JSON_BUILDER_GROWTH;

	JsonMember *members = (JsonMember *) arena_alloc (builder->arena, capacity * sizeof (JsonMember));
	if (members) {
		(void) memcpy (members, builder->members, builder->members_count * sizeof (JsonMember));

		builder->members = members;
		builder->members_capacity = capacity;

		retval = true;
	}

	else {
		builder->error = CERVER_JSON_ERROR_MEMORY;
	}

	return retval;

}

// one more member of the object that is open, with its key
static bool json_builder_members_add (JsonBuilder *builder, const CerverJsonValue *key) {

	if ((builder->members_count == builder->members_capacity) && !json_builder_members_grow (builder)) return false;

	JsonMember *member = &builder->members[builder->members_count];
	member->offset = (u32) (key - builder->container);
	member->signature = json_member_signature (key->content.string, json_value_length (key));

	builder->members_count += 1;

	return true;

}

static inline bool json_builder_keys_equal (const CerverJsonValue *first, const CerverJsonValue *second) {

	return (first->tag == second->tag)
		&& !memcmp (first->content.string, second->content.string, json_value_length (first));

}

static inline bool json_builder_members_equal (
	const CerverJsonValue *object, const JsonMember *first, const JsonMember *second
) {

	return (first->signature == second->signature)
		&& json_builder_keys_equal (object + first->offset, object + second->offset);

}

// whether an object has a key more than once, each member against the ones after it
static bool json_builder_duplicates_linear (
	const CerverJsonValue *object, const JsonMember *members, const size_t count
) {

	for (size_t i = 0; i < count; i++) {
		for (size_t j = i + 1; j < count; j++) {
			if (json_builder_members_equal (object, &members[i], &members[j])) return true;
		}
	}

	return false;

}

// by their length and then by their bytes: two keys that are equal end one after the other
static int json_builder_keys_compare (const void *first, const void *second) {

	const CerverJsonValue *first_key = *((const CerverJsonValue * const *) first);
	const CerverJsonValue *second_key = *((const CerverJsonValue * const *) second);

	if (first_key->tag != second_key->tag) return (first_key->tag < second_key->tag) ? -1 : 1;

	return memcmp (first_key->content.string, second_key->content.string, json_value_length (first_key));

}

// whether an object with many members has a key more than once, with its keys sorted:
// for the object whose table is not used, that a comparison of every key with the others would stop
static bool json_builder_duplicates_sorted (
	JsonBuilder *builder, const CerverJsonValue *object, const JsonMember *members, const size_t count
) {

	bool retval = false;

	const CerverJsonValue **keys = (const CerverJsonValue **) arena_alloc (
		builder->arena, count * sizeof (const CerverJsonValue *)
	);

	if (keys) {
		for (size_t i = 0; i < count; i++) keys[i] = object + members[i].offset;

		qsort (keys, count, sizeof (const CerverJsonValue *), json_builder_keys_compare);

		for (size_t i = 1; !retval && (i < count); i++) {
			retval = json_builder_keys_equal (keys[i - 1], keys[i]);
		}
	}

	else {
		builder->error = CERVER_JSON_ERROR_MEMORY;
	}

	return retval;

}

// puts a member in the table of its object; a member with the same key that was there is replaced, so a
// search finds the last one. Returns false when the key needs more slots than a search walks
static bool json_builder_table_add (
	const CerverJsonValue *object, const JsonMember *members, u32 *table, const size_t slots,
	const size_t index, bool *duplicated
) {

	const CerverJsonValue *key = object + members[index].offset;

	size_t slot = (size_t) json_member_hash (key->content.string, json_value_length (key)) & (slots - 1);

	for (size_t probes = 0; probes < json_object_table_probes; probes++) {
		const u32 other = table[slot];

		if (other && json_builder_members_equal (object, &members[other - 1], &members[index])) *duplicated = true;

		if (!other || *duplicated) {
			table[slot] = (u32) (index + 1);
			return true;
		}

		slot = (slot + 1) & (slots - 1);
	}

	return false;

}

// the table of an object with many members, after its directory; whether it has a key more than
// once comes out of it, or of its sorted keys when the table is not used and it was asked for
static bool json_builder_table (
	JsonBuilder *builder, const CerverJsonValue *object, const JsonMember *members, const size_t count,
	bool *duplicated
) {

	const size_t slots = json_object_table_slots (count);

	u32 *table = (u32 *) builder->next;
	(void) memset (table, 0, json_object_table_values (slots) * sizeof (CerverJsonValue));

	bool used = true;
	bool repeated = false;
	for (size_t i = 0; used && (i < count); i++) {
		bool same = false;
		used = json_builder_table_add (object, members, table, slots, i, &same);
		if (same) repeated = true;
	}

	table[slots] = used ? (u32) json_object_table_probes : 0;

	builder->next += json_object_table_values (slots);

	if (!used && (builder->cursor.flags & CERVER_JSON_PARSE_REJECT_DUPLICATES)) {
		repeated = json_builder_duplicates_sorted (builder, object, members, count);
	}

	*duplicated = repeated;

	return builder->error == CERVER_JSON_ERROR_NONE;

}

// the directory of the object that is closed goes after its last member, and the table of a big one after it
static bool json_builder_directory (JsonBuilder *builder) {

	const size_t count = json_value_length (builder->container);
	const size_t base = builder->container->content.links.directory;

	const size_t directory_values = json_object_directory_values (count);
	const size_t table_values = (count > JSON_OBJECT_HASHED) ?
		json_object_table_values (json_object_table_slots (count)) : 0;

	if (!json_builder_reserve (builder, directory_values + table_values)) return false;

	// the values can be in a new place
	CerverJsonValue *object = builder->container;
	const JsonMember *members = builder->members + base;

	const size_t part_values = json_object_part_values (count);
	u32 *signatures = (u32 *) builder->next;
	u32 *offsets = (u32 *) (builder->next + part_values);

	// the last value of each part can have room for more members than the ones that are left
	(void) memset (builder->next + part_values - 1, 0, sizeof (CerverJsonValue));
	(void) memset (builder->next + directory_values - 1, 0, sizeof (CerverJsonValue));

	for (size_t i = 0; i < count; i++) {
		signatures[i] = members[i].signature;
		offsets[i] = members[i].offset;
	}

	object->content.links.directory = (u32) (builder->next - object);

	// the members stay where the reader has them until the next key of the text
	builder->next += directory_values;
	builder->members_count = base;

	bool duplicated = false;
	if (count > JSON_OBJECT_HASHED) {
		if (!json_builder_table (builder, object, members, count, &duplicated)) return false;
	}

	else if (builder->cursor.flags & CERVER_JSON_PARSE_REJECT_DUPLICATES) {
		duplicated = json_builder_duplicates_linear (object, members, count);
	}

	if (duplicated && (builder->cursor.flags & CERVER_JSON_PARSE_REJECT_DUPLICATES)) {
		builder->error = CERVER_JSON_ERROR_DUPLICATE;
		return false;
	}

	return true;

}

// the array or the object that is open ends: an object gets its directory, and both where they end
static bool json_builder_close (JsonBuilder *builder) {

	const u32 back = builder->container->content.links.span;

	if (json_value_type (builder->container) == CERVER_JSON_TYPE_OBJECT) {
		if (!json_value_length (builder->container)) builder->container->content.links.directory = 0;
		else if (!json_builder_directory (builder)) return false;
	}

	else {
		builder->container->content.links.directory = 0;
	}

	CerverJsonValue *container = builder->container;
	container->content.links.span = (u32) (builder->next - container);

	builder->container = back ? (container - back) : NULL;
	builder->depth -= 1;

	return true;

}

#pragma endregion

#pragma region values

static JsonBuilderState json_builder_string (JsonBuilder *builder) {

	const char *string = NULL;
	size_t length = 0;

	CerverJsonValue *value = json_builder_take_value (builder);
	if (!value) return JSON_BUILDER_STATE_FAILED;

	builder->cursor.position += 1;

	const CerverJsonErrorCode error = json_text_string (&builder->cursor, &string, &length);
	if (error != CERVER_JSON_ERROR_NONE) return json_builder_fail (builder, error);

	value->tag = ((u64) CERVER_JSON_TYPE_STRING) | (((u64) length) << JSON_TAG_TYPE_BITS);
	value->content.string = string;

	return JSON_BUILDER_STATE_AFTER;

}

static JsonBuilderState json_builder_number (JsonBuilder *builder) {

	JsonNumber number = { CERVER_JSON_TYPE_NONE, { 0 } };

	CerverJsonValue *value = json_builder_take_value (builder);
	if (!value) return JSON_BUILDER_STATE_FAILED;

	const CerverJsonErrorCode error = json_number_read (&builder->cursor, &number);
	if (error != CERVER_JSON_ERROR_NONE) {
		// a number that the text cut is not one that is written wrong
		const bool cut = (error == CERVER_JSON_ERROR_NUMBER) && (builder->cursor.position >= builder->cursor.end);

		return json_builder_fail (builder, cut ? CERVER_JSON_ERROR_END : error);
	}

	value->tag = (u64) number.type;

	// the three types of a number share the same eight bytes
	value->content.natural = number.value.natural;

	return JSON_BUILDER_STATE_AFTER;

}

// the text of a literal, without its NUL; false when the text has another thing
static bool json_builder_literal (JsonBuilder *builder, const char *literal, const size_t length) {

	bool retval = false;

	if (!memcmp (builder->cursor.position, literal, length)) {
		builder->cursor.position += length;
		retval = true;
	}

	else {
		builder->error = ((size_t) (builder->cursor.end - builder->cursor.position) < length) ?
			CERVER_JSON_ERROR_END : CERVER_JSON_ERROR_LITERAL;
	}

	return retval;

}

static JsonBuilderState json_builder_boolean (
	JsonBuilder *builder, const char *literal, const size_t length, const bool boolean
) {

	CerverJsonValue *value = json_builder_take_value (builder);
	if (!value || !json_builder_literal (builder, literal, length)) return JSON_BUILDER_STATE_FAILED;

	value->tag = (u64) CERVER_JSON_TYPE_BOOLEAN;
	value->content.natural = boolean ? 1 : 0;

	return JSON_BUILDER_STATE_AFTER;

}

static JsonBuilderState json_builder_null (JsonBuilder *builder) {

	CerverJsonValue *value = json_builder_take_value (builder);
	if (!value || !json_builder_literal (builder, JSON_LITERAL_NULL, sizeof (JSON_LITERAL_NULL) - 1)) {
		return JSON_BUILDER_STATE_FAILED;
	}

	value->tag = (u64) CERVER_JSON_TYPE_NULL;
	value->content.natural = 0;

	return JSON_BUILDER_STATE_AFTER;

}

// the key of a member and its colon: the value of the member is next
static JsonBuilderState json_builder_key (JsonBuilder *builder) {

	const char *string = NULL;
	size_t length = 0;

	if (*builder->cursor.position != JSON_STRING_QUOTE) {
		return json_builder_fail (builder, json_cursor_unexpected (&builder->cursor));
	}

	CerverJsonValue *key = json_builder_take (builder);
	if (!key) return JSON_BUILDER_STATE_FAILED;

	builder->cursor.position += 1;

	const CerverJsonErrorCode error = json_text_string (&builder->cursor, &string, &length);
	if (error != CERVER_JSON_ERROR_NONE) return json_builder_fail (builder, error);

	key->tag = ((u64) CERVER_JSON_TYPE_STRING) | (((u64) length) << JSON_TAG_TYPE_BITS);
	key->content.string = string;

	if (!json_builder_members_add (builder, key)) return JSON_BUILDER_STATE_FAILED;

	builder->container->tag += JSON_TAG_LENGTH_ONE;

	json_cursor_skip_whitespace (&builder->cursor);

	if (*builder->cursor.position != JSON_SEPARATOR_KEY) {
		return json_builder_fail (builder, json_cursor_unexpected (&builder->cursor));
	}

	builder->cursor.position += 1;

	return JSON_BUILDER_STATE_VALUE;

}

// the end of the array or of the object that is open: it is a value that ended
static JsonBuilderState json_builder_end (JsonBuilder *builder) {

	builder->cursor.position += 1;

	return json_builder_close (builder) ? JSON_BUILDER_STATE_AFTER : JSON_BUILDER_STATE_FAILED;

}

static JsonBuilderState json_builder_object (JsonBuilder *builder) {

	if (!json_builder_open (builder, CERVER_JSON_TYPE_OBJECT)) return JSON_BUILDER_STATE_FAILED;

	builder->cursor.position += 1;

	json_cursor_skip_whitespace (&builder->cursor);

	return (*builder->cursor.position == JSON_OBJECT_END) ?
		json_builder_end (builder) : json_builder_key (builder);

}

static JsonBuilderState json_builder_array (JsonBuilder *builder) {

	if (!json_builder_open (builder, CERVER_JSON_TYPE_ARRAY)) return JSON_BUILDER_STATE_FAILED;

	builder->cursor.position += 1;

	json_cursor_skip_whitespace (&builder->cursor);

	return (*builder->cursor.position == JSON_ARRAY_END) ?
		json_builder_end (builder) : JSON_BUILDER_STATE_VALUE;

}

// a value starts here
static JsonBuilderState json_builder_value (JsonBuilder *builder) {

	json_cursor_skip_whitespace (&builder->cursor);

	switch (*builder->cursor.position) {
		case '"': return json_builder_string (builder);

		case '{': return json_builder_object (builder);

		case '[': return json_builder_array (builder);

		case '-':
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			return json_builder_number (builder);

		case 't': return json_builder_boolean (builder, JSON_LITERAL_TRUE, sizeof (JSON_LITERAL_TRUE) - 1, true);

		case 'f': return json_builder_boolean (builder, JSON_LITERAL_FALSE, sizeof (JSON_LITERAL_FALSE) - 1, false);

		case 'n': return json_builder_null (builder);

		default: return json_builder_fail (builder, json_cursor_unexpected (&builder->cursor));
	}

}

// after a value of an array: another one or its end
static JsonBuilderState json_builder_after_element (JsonBuilder *builder) {

	const u8 byte = *builder->cursor.position;

	if (byte == JSON_SEPARATOR_VALUES) {
		builder->cursor.position += 1;
		return JSON_BUILDER_STATE_VALUE;
	}

	if (byte == JSON_ARRAY_END) return json_builder_end (builder);

	return json_builder_fail (builder, json_cursor_unexpected (&builder->cursor));

}

// after the value of a member of an object: another member or its end
static JsonBuilderState json_builder_after_member (JsonBuilder *builder) {

	const u8 byte = *builder->cursor.position;

	if (byte == JSON_SEPARATOR_VALUES) {
		builder->cursor.position += 1;

		json_cursor_skip_whitespace (&builder->cursor);

		return json_builder_key (builder);
	}

	if (byte == JSON_OBJECT_END) return json_builder_end (builder);

	return json_builder_fail (builder, json_cursor_unexpected (&builder->cursor));

}

// a value ended: what follows depends on what has it
static JsonBuilderState json_builder_after (JsonBuilder *builder) {

	if (!builder->container) return JSON_BUILDER_STATE_DONE;

	json_cursor_skip_whitespace (&builder->cursor);

	return (json_value_type (builder->container) == CERVER_JSON_TYPE_ARRAY) ?
		json_builder_after_element (builder) : json_builder_after_member (builder);

}

// reads the whole text: one value, and nothing but whitespace after it
static bool json_builder_read (JsonBuilder *builder) {

	JsonBuilderState state = JSON_BUILDER_STATE_VALUE;

	while ((state == JSON_BUILDER_STATE_VALUE) || (state == JSON_BUILDER_STATE_AFTER)) {
		state = (state == JSON_BUILDER_STATE_VALUE) ? json_builder_value (builder) : json_builder_after (builder);
	}

	if (state == JSON_BUILDER_STATE_DONE) {
		json_cursor_skip_whitespace (&builder->cursor);

		if (builder->cursor.position != builder->cursor.end) builder->error = CERVER_JSON_ERROR_TRAILING;
	}

	return builder->error == CERVER_JSON_ERROR_NONE;

}

#pragma endregion

#pragma region document

// what a call to parse has
typedef struct JsonParse {

	Arena *arena;

	// the text to copy, or the one to read in situ
	const char *text;
	char *in_situ;
	size_t length;

	unsigned int flags;
	CerverJsonLimits limits;

} JsonParse;

// the memory that a document of a text takes when it starts: one chunk of its own arena has all of it
static size_t json_document_memory (const size_t length, const size_t capacity) {

	return ARENA_ALIGNMENT + sizeof (CerverJsonDocument)
		+ ARENA_ALIGNMENT + length + CERVER_JSON_PADDING
		+ ARENA_ALIGNMENT + (capacity * sizeof (CerverJsonValue));

}

// the reader is big for the stack to be cleared whole: its members are written before they are read
static void json_builder_init (JsonBuilder *builder, const JsonParse *parse) {

	builder->cursor.start = NULL;
	builder->cursor.position = NULL;
	builder->cursor.end = NULL;
	builder->cursor.flags = parse->flags;

	builder->arena = NULL;

	builder->values = NULL;
	builder->next = NULL;
	builder->end = NULL;
	builder->capacity_max = json_builder_bound (parse->length);

	builder->count = 0;
	builder->count_max = parse->limits.values;

	builder->container = NULL;
	builder->depth = 0;
	builder->depth_max = parse->limits.depth;

	builder->members = builder->members_own;
	builder->members_count = 0;
	builder->members_capacity = JSON_BUILDER_MEMBERS;

	builder->error = CERVER_JSON_ERROR_NONE;

}

// takes the text and the first values from the arena
static bool json_builder_prepare (JsonBuilder *builder, const JsonParse *parse, const size_t capacity) {

	bool retval = false;

	const bool ready = parse->in_situ ? true : json_cursor_copy (
		&builder->cursor, builder->arena, parse->text, parse->length, parse->flags
	);

	if (ready) {
		if (parse->in_situ) json_cursor_in_situ (&builder->cursor, parse->in_situ, parse->length, parse->flags);

		builder->values = (CerverJsonValue *) arena_alloc (builder->arena, capacity * sizeof (CerverJsonValue));
		if (builder->values) {
			builder->next = builder->values;
			builder->end = builder->values + capacity;

			retval = true;
		}
	}

	if (!retval) builder->error = CERVER_JSON_ERROR_MEMORY;

	return retval;

}

static CerverJsonDocument *json_document_create (JsonBuilder *builder, const bool owns_arena) {

	CerverJsonDocument *document = (CerverJsonDocument *) arena_alloc (
		builder->arena, sizeof (CerverJsonDocument)
	);

	if (document) {
		document->arena = builder->arena;
		document->owns_arena = owns_arena;
		document->root = NULL;
		document->n_values = 0;
	}

	else {
		builder->error = CERVER_JSON_ERROR_MEMORY;
	}

	return document;

}

static CerverJsonDocument *json_document_parse (const JsonParse *parse, CerverJsonError *error) {

	CerverJsonDocument *document = NULL;

	JsonBuilder builder;
	json_builder_init (&builder, parse);

	const size_t capacity = json_builder_capacity_initial (parse->length);

	builder.arena = parse->arena ? parse->arena : arena_create (json_document_memory (parse->length, capacity));
	if (!builder.arena) {
		json_error_set (error, CERVER_JSON_ERROR_MEMORY, 0);
		return NULL;
	}

	document = json_document_create (&builder, !parse->arena);

	if (document && json_builder_prepare (&builder, parse, capacity) && json_builder_read (&builder)) {
		document->root = builder.values;
		document->n_values = builder.count;
	}

	// a text that was read has no position to report
	json_error_set (
		error, builder.error,
		((builder.error != CERVER_JSON_ERROR_NONE) && builder.cursor.start) ?
			(size_t) (builder.cursor.position - builder.cursor.start) : 0
	);

	if (builder.error != CERVER_JSON_ERROR_NONE) {
		if (!parse->arena) arena_delete (builder.arena);

		document = NULL;
	}

	return document;

}

// checks what a call has before anything is taken
static bool json_document_check (
	const void *text, const size_t length, const CerverJsonLimits *limits, CerverJsonError *error
) {

	CerverJsonErrorCode code = CERVER_JSON_ERROR_NONE;

	if (!text) code = CERVER_JSON_ERROR_ARGUMENTS;
	else if (length > limits->size) code = CERVER_JSON_ERROR_SIZE;

	if (code != CERVER_JSON_ERROR_NONE) json_error_set (error, code, 0);

	return code == CERVER_JSON_ERROR_NONE;

}

CerverJsonDocument *cerver_json_parse (
	Arena *arena,
	const char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	CerverJsonError *error
) {

	const JsonParse parse = { arena, text, NULL, length, flags, json_limits_resolve (limits) };

	return json_document_check (text, length, &parse.limits, error) ?
		json_document_parse (&parse, error) : NULL;

}

CerverJsonDocument *cerver_json_parse_in_situ (
	Arena *arena,
	char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	CerverJsonError *error
) {

	const JsonParse parse = { arena, NULL, text, length, flags, json_limits_resolve (limits) };

	return json_document_check (text, length, &parse.limits, error) ?
		json_document_parse (&parse, error) : NULL;

}

const CerverJsonValue *cerver_json_document_root (const CerverJsonDocument *document) {

	return document ? document->root : NULL;

}

size_t cerver_json_document_values (const CerverJsonDocument *document) {

	return document ? document->n_values : 0;

}

void cerver_json_document_delete (CerverJsonDocument *document) {

	// the document lives in its arena
	if (document && document->owns_arena) arena_delete (document->arena);

}

void cerver_json_object_table_probes_set (const size_t probes) {

	json_object_table_probes = probes ? probes : JSON_OBJECT_TABLE_PROBES;

}

#pragma endregion
