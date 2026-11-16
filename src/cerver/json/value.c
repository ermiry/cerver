#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"
#include "cerver/json/value.h"

#pragma region types

static inline bool json_value_is (const CerverJsonValue *value, const CerverJsonType type) {

	return value && (json_value_type (value) == type);

}

CerverJsonType cerver_json_type (const CerverJsonValue *value) {

	return value ? json_value_type (value) : CERVER_JSON_TYPE_NONE;

}

bool cerver_json_is_null (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_NULL);

}

bool cerver_json_is_boolean (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_BOOLEAN);

}

bool cerver_json_is_number (const CerverJsonValue *value) {

	return value
		&& (json_value_type (value) >= CERVER_JSON_TYPE_INTEGER)
		&& (json_value_type (value) <= CERVER_JSON_TYPE_REAL);

}

bool cerver_json_is_string (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_STRING);

}

bool cerver_json_is_array (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_ARRAY);

}

bool cerver_json_is_object (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_OBJECT);

}

#pragma endregion

#pragma region scalars

bool cerver_json_boolean_value (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_BOOLEAN) && value->content.natural;

}

i64 cerver_json_integer_value (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_INTEGER) ? value->content.integer : 0;

}

u64 cerver_json_unsigned_value (const CerverJsonValue *value) {

	if (json_value_is (value, CERVER_JSON_TYPE_UNSIGNED)) return value->content.natural;

	// an integer that is not negative has the same bits without its sign
	if (json_value_is (value, CERVER_JSON_TYPE_INTEGER) && (value->content.integer >= 0)) {
		return value->content.natural;
	}

	return 0;

}

double cerver_json_real_value (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_REAL) ? value->content.real : 0;

}

double cerver_json_number_value (const CerverJsonValue *value) {

	switch (cerver_json_type (value)) {
		case CERVER_JSON_TYPE_INTEGER: return (double) value->content.integer;
		case CERVER_JSON_TYPE_UNSIGNED: return (double) value->content.natural;
		case CERVER_JSON_TYPE_REAL: return value->content.real;
		default: return 0;
	}

}

const char *cerver_json_string_value (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_STRING) ? value->content.string : NULL;

}

size_t cerver_json_string_length (const CerverJsonValue *value) {

	return json_value_is (value, CERVER_JSON_TYPE_STRING) ? json_value_length (value) : 0;

}

#pragma endregion

#pragma region arrays

size_t cerver_json_array_size (const CerverJsonValue *array) {

	return json_value_is (array, CERVER_JSON_TYPE_ARRAY) ? json_value_length (array) : 0;

}

const CerverJsonValue *cerver_json_array_get (const CerverJsonValue *array, const size_t index) {

	const CerverJsonValue *element = NULL;

	if (index < cerver_json_array_size (array)) {
		element = array + 1;
		for (size_t i = 0; i < index; i++) element = json_value_next (element);
	}

	return element;

}

CerverJsonArrayIterator cerver_json_array_iterator (const CerverJsonValue *array) {

	CerverJsonArrayIterator iterator = { NULL, 0 };

	iterator.remaining = cerver_json_array_size (array);
	if (iterator.remaining) iterator.next = array + 1;

	return iterator;

}

const CerverJsonValue *cerver_json_array_iterator_next (CerverJsonArrayIterator *iterator) {

	const CerverJsonValue *element = NULL;

	if (iterator && iterator->remaining) {
		element = iterator->next;

		iterator->remaining -= 1;
		iterator->next = iterator->remaining ? json_value_next (element) : NULL;
	}

	return element;

}

#pragma endregion

#pragma region objects

size_t cerver_json_object_size (const CerverJsonValue *object) {

	return json_value_is (object, CERVER_JSON_TYPE_OBJECT) ? json_value_length (object) : 0;

}

// what a search looks for: the bytes of a key and their signature. A key that came as a string that ends with
// a NUL and that its signature does not have whole has no length: to measure it costs more than most searches
typedef struct JsonSearch {

	const char *key;
	size_t length;
	bool terminated;
	u32 signature;

} JsonSearch;

// the bits of the members of a whole block of signatures
#define JSON_BLOCK_ALL						((1U << JSON_MEMBER_BLOCK) - 1)

// the keys of an object are short: a call to compare them costs more than their bytes, and makes
// the search keep what it has across it, so they are compared here, whole words first
static inline bool json_key_equal (const char *first, const char *second, const size_t length) {

	size_t i = 0;

	for (; (length - i) >= sizeof (u64); i += sizeof (u64)) {
		u64 first_word = 0;
		u64 second_word = 0;
		(void) memcpy (&first_word, first + i, sizeof (u64));
		(void) memcpy (&second_word, second + i, sizeof (u64));

		if (first_word != second_word) return false;
	}

	for (; i < length; i++) {
		if (first[i] != second[i]) return false;
	}

	return true;

}

// whether the bytes of a key are a string that ends with a NUL, that is never read after its end
static inline bool json_key_equal_string (const char *key, const size_t length, const char *string) {

	for (size_t i = 0; i < length; i++) {
		// a NUL of the string is its end: it is shorter than the key, even when the key has a NUL there
		if ((key[i] != string[i]) || !string[i]) return false;
	}

	return !string[length];

}

// whether a member of an object whose signature is the one of a search has its key: the bytes
// of the signature are the same in both, so only their lengths and the bytes after them are left
static inline const CerverJsonValue *json_object_member_matches (
	const CerverJsonValue *object, const u32 offset, const JsonSearch *search
) {

	const CerverJsonValue *key = object + offset;
	const size_t length = json_value_length (key);

	const size_t known = (length < JSON_MEMBER_SIGNATURE_SIZE) ? length : JSON_MEMBER_SIGNATURE_SIZE;
	const char *rest = key->content.string + known;

	// a string without length has every byte of its signature and none of them is a NUL, so a key
	// with its signature has them too: both are read after them
	const bool equal = search->terminated ?
		json_key_equal_string (rest, length - known, search->key + known) :
		((length == search->length) && json_key_equal (rest, search->key + known, length - known));

	return equal ? (key + 1) : NULL;

}

// the bit of each member of a block of signatures that has the one of a search: they
// are compared at once, without a branch for each one
static inline unsigned int json_block_hits (const u32 *signatures, const u32 signature) {

	unsigned int hits = 0;

	for (size_t i = 0; i < JSON_MEMBER_BLOCK; i++) {
		hits |= ((unsigned int) (signatures[i] == signature)) << i;
	}

	return hits;

}

// the last member of a block that has its bit
static inline size_t json_block_last (const unsigned int hits) {

	return (sizeof (unsigned int) * JSON_BYTE_BITS) - 1 - (size_t) __builtin_clz (hits);

}

// the members of a block with the signature of a search, from the last one to the first one
static inline const CerverJsonValue *json_object_find_block (
	const CerverJsonValue *object, const u32 *offsets, unsigned int hits, const JsonSearch *search
) {

	while (hits) {
		const size_t member = json_block_last (hits);

		const CerverJsonValue *value = json_object_member_matches (object, offsets[member], search);
		if (value) return value;

		hits ^= 1U << member;
	}

	return NULL;

}

// from the last member to the first one: the first one that is found is the one that counts.
// It is the path of almost every search, and inside its caller the search stays in registers
CERVER_ATTRS ((always_inline)) static inline const CerverJsonValue *json_object_find_linear (
	const CerverJsonValue *object, const size_t count, const JsonSearch *search
) {

	const u32 *signatures = json_object_signatures (object);
	const u32 *offsets = json_object_offsets (object, count);

	const size_t blocks = json_object_part_values (count);

	// the last block can have fewer members: the zeros after them are not the signature of any
	unsigned int valid = (1U << (count - ((blocks - 1) * JSON_MEMBER_BLOCK))) - 1;

	for (size_t block = blocks; block > 0; block--) {
		const size_t first = (block - 1) * JSON_MEMBER_BLOCK;
		const unsigned int hits = json_block_hits (signatures + first, search->signature) & valid;

		const CerverJsonValue *value = hits ? json_object_find_block (object, offsets + first, hits, search) : NULL;
		if (value) return value;

		valid = JSON_BLOCK_ALL;
	}

	return NULL;

}

// in the table of an object with many members: each key is at most as many slots after the one of its hash
// as the word after the table says, and the last member with a key took the place of the ones before it
static const CerverJsonValue *json_object_find_hashed (
	const CerverJsonValue *object, const size_t count, const JsonSearch *search
) {

	const u32 *signatures = json_object_signatures (object);
	const u32 *offsets = json_object_offsets (object, count);
	const u32 *table = json_object_table (object, count);
	const size_t slots = json_object_table_slots (count);

	// a table that is not used: one of its keys needed more slots
	if (!table[slots]) return json_object_find_linear (object, count, search);

	// the hash of a key needs its length
	const size_t length = search->terminated ? strlen (search->key) : search->length;

	size_t slot = (size_t) json_member_hash (search->key, length) & (slots - 1);

	for (u32 probes = 0; (probes < table[slots]) && table[slot]; probes++) {
		const u32 member = table[slot] - 1;

		const CerverJsonValue *value = (signatures[member] == search->signature) ?
			json_object_member_matches (object, offsets[member], search) : NULL;

		if (value) return value;

		slot = (slot + 1) & (slots - 1);
	}

	return NULL;

}

// the value of the last member of an object with the key of a search, for the methods of
// this file: the public ones are called through the table of the library, these are not
static inline const CerverJsonValue *json_object_search (const CerverJsonValue *object, const JsonSearch *search) {

	const size_t count = json_value_length (object);

	if (!count) return NULL;

	return (count > JSON_OBJECT_HASHED) ?
		json_object_find_hashed (object, count, search) : json_object_find_linear (object, count, search);

}

static inline const CerverJsonValue *json_object_find (
	const CerverJsonValue *object, const char *key, const size_t length
) {

	const JsonSearch search = { key, length, false, json_member_signature (key, length) };

	return json_object_search (object, &search);

}

const CerverJsonValue *cerver_json_object_find (
	const CerverJsonValue *object, const char *key, const size_t length
) {

	return (key && json_value_is (object, CERVER_JSON_TYPE_OBJECT)) ? json_object_find (object, key, length) : NULL;

}

const CerverJsonValue *cerver_json_object_get (const CerverJsonValue *object, const char *key) {

	if (!key || !json_value_is (object, CERVER_JSON_TYPE_OBJECT)) return NULL;

	size_t taken = 0;
	const u32 signature = json_member_signature_string (key, &taken);

	// a key that its signature has whole is one with its length
	const JsonSearch search = { key, taken, taken == JSON_MEMBER_SIGNATURE_SIZE, signature };

	return json_object_search (object, &search);

}

CerverJsonObjectIterator cerver_json_object_iterator (const CerverJsonValue *object) {

	CerverJsonObjectIterator iterator = { NULL, 0 };

	iterator.remaining = cerver_json_object_size (object);
	if (iterator.remaining) iterator.next = object + 1;

	return iterator;

}

bool cerver_json_object_iterator_next (
	CerverJsonObjectIterator *iterator,
	const CerverJsonValue **key, const CerverJsonValue **value
) {

	bool retval = false;

	if (iterator && iterator->remaining) {
		const CerverJsonValue *member = iterator->next;

		if (key) *key = member;
		if (value) *value = member + 1;

		iterator->remaining -= 1;
		iterator->next = iterator->remaining ? json_value_next (member + 1) : NULL;

		retval = true;
	}

	return retval;

}

#pragma endregion

#pragma region equal

static bool json_value_equal (const CerverJsonValue *first, const CerverJsonValue *second);

static bool json_value_equal_strings (const CerverJsonValue *first, const CerverJsonValue *second) {

	// the tags are the same: the same length
	return !memcmp (first->content.string, second->content.string, json_value_length (first));

}

static bool json_value_equal_arrays (const CerverJsonValue *first, const CerverJsonValue *second) {

	bool retval = true;

	const size_t count = json_value_length (first);

	const CerverJsonValue *first_element = first + 1;
	const CerverJsonValue *second_element = second + 1;
	for (size_t i = 0; retval && (i < count); i++) {
		retval = json_value_equal (first_element, second_element);

		first_element = json_value_next (first_element);
		second_element = json_value_next (second_element);
	}

	return retval;

}

// whether the value of a member of an object is equal to the one that the other object has for its key
static bool json_value_member_inside (
	const CerverJsonValue *member, const CerverJsonValue *value, const CerverJsonValue *other
) {

	const CerverJsonValue *other_value = json_object_find (other, member->content.string, json_value_length (member));

	return other_value && json_value_equal (value, other_value);

}

// every key of an object has an equal value in the other one; for a
// key that is more than once, the value that counts in both is the last
static bool json_value_object_inside (const CerverJsonValue *object, const CerverJsonValue *other) {

	bool retval = true;

	const size_t count = json_value_length (object);

	const CerverJsonValue *member = object + 1;
	for (size_t i = 0; retval && (i < count); i++) {
		const CerverJsonValue *value = member + 1;

		// a member that is not the last one of its key does not count: the last one is compared in its turn
		if (json_object_find (object, member->content.string, json_value_length (member)) == value) {
			retval = json_value_member_inside (member, value, other);
		}

		member = json_value_next (value);
	}

	return retval;

}

static bool json_value_equal_objects (const CerverJsonValue *first, const CerverJsonValue *second) {

	return json_value_object_inside (first, second) && json_value_object_inside (second, first);

}

static bool json_value_equal (const CerverJsonValue *first, const CerverJsonValue *second) {

	// the type and the length
	if (first->tag != second->tag) return false;

	switch (json_value_type (first)) {
		case CERVER_JSON_TYPE_STRING: return json_value_equal_strings (first, second);

		case CERVER_JSON_TYPE_ARRAY: return json_value_equal_arrays (first, second);

		case CERVER_JSON_TYPE_OBJECT: return json_value_equal_objects (first, second);

		// two reals are compared by their value: a zero is equal to a negative zero
		case CERVER_JSON_TYPE_REAL: return !(first->content.real < second->content.real)
			&& !(first->content.real > second->content.real);

		default: return first->content.natural == second->content.natural;
	}

}

bool cerver_json_equal (const CerverJsonValue *first, const CerverJsonValue *second) {

	return first && second && json_value_equal (first, second);

}

#pragma endregion
