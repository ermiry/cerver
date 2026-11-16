#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"
#include "cerver/json/pointer.h"
#include "cerver/json/value.h"

static const char JSON_POINTER_SEPARATOR = '/';
static const char JSON_POINTER_ESCAPE = '~';
static const char JSON_POINTER_ESCAPED_ESCAPE = '0';
static const char JSON_POINTER_ESCAPED_SEPARATOR = '1';
static const char JSON_POINTER_ZERO = '0';

static const size_t JSON_POINTER_BASE = 10;

// the size of an escape, its ~ and its digit
static const size_t JSON_POINTER_ESCAPE_SIZE = 2;

// a token of a pointer, between two separators
typedef struct JsonPointerToken {

	const char *bytes;
	size_t length;

	// it has a ~, so its bytes are not the key
	bool escaped;

} JsonPointerToken;

#pragma region tokens

// takes the token that starts at a position, after its separator; false for a ~ without its digit
static bool json_pointer_token (
	const char *pointer, const size_t length, const size_t start, JsonPointerToken *token
) {

	size_t position = start;

	token->bytes = pointer + start;
	token->escaped = false;

	while ((position < length) && (pointer[position] != JSON_POINTER_SEPARATOR)) {
		if (pointer[position] == JSON_POINTER_ESCAPE) {
			const char next = ((position + 1) < length) ? pointer[position + 1] : 0;
			if ((next != JSON_POINTER_ESCAPED_ESCAPE) && (next != JSON_POINTER_ESCAPED_SEPARATOR)) return false;

			token->escaped = true;
			position += 1;
		}

		position += 1;
	}

	token->length = position - start;

	return true;

}

// compares a key with a token that has escapes, one byte of the key for each escape
static bool json_pointer_token_matches (const JsonPointerToken *token, const CerverJsonValue *key) {

	const char *bytes = key->content.string;
	const size_t length = json_value_length (key);

	size_t matched = 0;
	size_t position = 0;
	while (position < token->length) {
		char expected = token->bytes[position];
		size_t size = 1;

		if (expected == JSON_POINTER_ESCAPE) {
			expected = (token->bytes[position + 1] == JSON_POINTER_ESCAPED_SEPARATOR) ?
				JSON_POINTER_SEPARATOR : JSON_POINTER_ESCAPE;

			size = JSON_POINTER_ESCAPE_SIZE;
		}

		if ((matched >= length) || (bytes[matched] != expected)) return false;

		matched += 1;
		position += size;
	}

	return matched == length;

}

#pragma endregion

#pragma region values

// the member of an object that a token names, the last one with its key
static const CerverJsonValue *json_pointer_member (
	const CerverJsonValue *object, const JsonPointerToken *token
) {

	const CerverJsonValue *found = NULL;

	if (!token->escaped) return cerver_json_object_find (object, token->bytes, token->length);

	const size_t count = json_value_length (object);

	const CerverJsonValue *member = object + 1;
	for (size_t i = 0; i < count; i++) {
		if (json_pointer_token_matches (token, member)) found = member + 1;

		member = json_value_next (member + 1);
	}

	return found;

}

// the element of an array that a token names: digits, without a zero before them
static const CerverJsonValue *json_pointer_element (
	const CerverJsonValue *array, const JsonPointerToken *token
) {

	const size_t count = json_value_length (array);

	size_t index = 0;

	if (!token->length) return NULL;
	if ((token->bytes[0] == JSON_POINTER_ZERO) && (token->length > 1)) return NULL;

	for (size_t i = 0; i < token->length; i++) {
		const size_t digit = (size_t) (token->bytes[i] - JSON_POINTER_ZERO);
		if (digit >= JSON_POINTER_BASE) return NULL;

		// a position that the array does not have, before it can overflow
		index = (index * JSON_POINTER_BASE) + digit;
		if (index >= count) return NULL;
	}

	return cerver_json_array_get (array, index);

}

static const CerverJsonValue *json_pointer_step (
	const CerverJsonValue *value, const JsonPointerToken *token
) {

	switch (json_value_type (value)) {
		case CERVER_JSON_TYPE_OBJECT: return json_pointer_member (value, token);
		case CERVER_JSON_TYPE_ARRAY: return json_pointer_element (value, token);
		default: return NULL;
	}

}

const CerverJsonValue *cerver_json_pointer_get (
	const CerverJsonValue *value, const char *pointer, const size_t length
) {

	JsonPointerToken token = { NULL, 0, false };

	size_t position = 0;

	if (!pointer) return NULL;

	while (value && (position < length)) {
		if (pointer[position] != JSON_POINTER_SEPARATOR) return NULL;

		position += 1;

		if (!json_pointer_token (pointer, length, position, &token)) return NULL;

		value = json_pointer_step (value, &token);

		position += token.length;
	}

	return value;

}

#pragma endregion
