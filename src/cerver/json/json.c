#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/crypto/random.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"

static const char JSON_LINE_FEED = '\n';

static const size_t JSON_POSITION_FIRST = 1;

// the odd constant of the hash of the keys: the bits of the golden ratio
static const u64 JSON_HASH_MULTIPLIER = UINT64_C (0x9E3779B97F4A7C15);

#define JSON_HASH_WORD_BITS					64

#pragma region types

const char *cerver_json_type_to_string (const CerverJsonType type) {

	switch (type) {
		#define XX(num, name, string) case CERVER_JSON_TYPE_##name: return #string;
		CERVER_JSON_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_json_type_to_string (CERVER_JSON_TYPE_NONE);

}

#pragma endregion

#pragma region errors

const char *cerver_json_error_to_string (const CerverJsonErrorCode code) {

	switch (code) {
		#define XX(num, name, description) case CERVER_JSON_ERROR_##name: return #description;
		CERVER_JSON_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_json_error_to_string (CERVER_JSON_ERROR_NONE);

}

void cerver_json_error_position (
	const char *text, const size_t length, const size_t offset,
	size_t *line, size_t *column
) {

	size_t current_line = JSON_POSITION_FIRST;
	size_t current_column = JSON_POSITION_FIRST;

	if (text) {
		const size_t last = (offset < length) ? offset : length;
		for (size_t i = 0; i < last; i++) {
			if (text[i] == JSON_LINE_FEED) {
				current_line += 1;
				current_column = JSON_POSITION_FIRST;
			}

			else {
				current_column += 1;
			}
		}
	}

	if (line) *line = current_line;
	if (column) *column = current_column;

}

void json_error_set (
	CerverJsonError *error, const CerverJsonErrorCode code, const size_t offset
) {

	if (error) {
		error->code = code;
		error->offset = offset;
	}

}

#pragma endregion

#pragma region limits

CerverJsonLimits json_limits_resolve (const CerverJsonLimits *limits) {

	CerverJsonLimits resolved = { CERVER_JSON_LIMIT_DEPTH, CERVER_JSON_LIMIT_SIZE, CERVER_JSON_LIMIT_VALUES };

	if (limits) {
		if (limits->depth) resolved.depth = limits->depth;
		if (limits->size) resolved.size = limits->size;
		if (limits->values) resolved.values = limits->values;
	}

	// the values of a document know each other by distances of 32 bits
	if (resolved.size > CERVER_JSON_SIZE_MAX) resolved.size = CERVER_JSON_SIZE_MAX;

	return resolved;

}

#pragma endregion

#pragma region hash

static pthread_once_t json_hash_once = PTHREAD_ONCE_INIT;

static u64 json_hash_seed = 0;

// the seed of the process: without the random bytes of the kernel, what changes between two processes
static void json_hash_seed_init (void) {

	if (!cerver_crypto_random_bytes (&json_hash_seed, sizeof (json_hash_seed))) {
		json_hash_seed = (((u64) (uintptr_t) &json_hash_seed) * JSON_HASH_MULTIPLIER) ^ (u64) time (NULL);
	}

}

// the two halves of the product of a word and the constant, one over the other: every bit moves the others
static inline u64 json_hash_fold (const u64 value) {

	const JsonWide product = ((JsonWide) value) * JSON_HASH_MULTIPLIER;

	return ((u64) product) ^ ((u64) (product >> JSON_HASH_WORD_BITS));

}

u64 json_member_hash (const char *key, const size_t length) {

	(void) pthread_once (&json_hash_once, json_hash_seed_init);

	u64 hash = json_hash_fold (json_hash_seed ^ (u64) length);

	size_t position = 0;
	while ((length - position) >= sizeof (u64)) {
		u64 word = 0;
		(void) memcpy (&word, key + position, sizeof (u64));

		hash = json_hash_fold (hash ^ word);
		position += sizeof (u64);
	}

	if (position < length) {
		u64 word = 0;
		(void) memcpy (&word, key + position, length - position);

		hash = json_hash_fold (hash ^ word);
	}

	return hash;

}

#pragma endregion

#pragma region cursor

static void json_cursor_set (
	JsonCursor *cursor, u8 *text, const size_t length, const unsigned int flags
) {

	// the NUL at the end stops every scan, and the padding lets
	// a reader load whole words and vectors without leaving the text
	(void) memset (text + length, 0, CERVER_JSON_PADDING);

	cursor->start = text;
	cursor->position = text;
	cursor->end = text + length;
	cursor->flags = flags;

}

bool json_cursor_copy (
	JsonCursor *cursor, Arena *arena,
	const char *text, const size_t length, const unsigned int flags
) {

	bool retval = false;

	u8 *copy = (u8 *) arena_alloc (arena, length + CERVER_JSON_PADDING);
	if (copy) {
		if (length) (void) memcpy (copy, text, length);

		json_cursor_set (cursor, copy, length, flags);

		retval = true;
	}

	return retval;

}

void json_cursor_in_situ (
	JsonCursor *cursor,
	char *text, const size_t length, const unsigned int flags
) {

	json_cursor_set (cursor, (u8 *) text, length, flags);

}

#pragma endregion
