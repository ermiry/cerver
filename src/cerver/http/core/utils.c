#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/http/core/internal.h"
#include "cerver/http/core/utils.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"
#include "cerver/json/writer.h"

#pragma region range

const char *http_range_type_string (
	const RangeType range_type
) {

	switch (range_type) {
		#define XX(num, name, string) case HTTP_RANGE_TYPE_##name: return #string;
		HTTP_RANGE_TYPE_MAP(XX)
		#undef XX
	}

	return http_range_type_string (HTTP_RANGE_TYPE_NONE);

}

void http_bytes_range_init (BytesRange *bytes_range) {

	bytes_range->file_size = 0;

	bytes_range->type = HTTP_RANGE_TYPE_NONE;
	bytes_range->start = 0;
	bytes_range->end = 0;
	bytes_range->chunk_size = 0;

}

void http_bytes_range_print (const BytesRange *bytes_range) {

	(void) printf ("Bytes Range: \n");
	(void) printf ("\tFile size: %ld", bytes_range->file_size);
	(void) printf ("\tType: %s", http_range_type_string (bytes_range->type));
	(void) printf ("\tStart: %ld", bytes_range->start);
	(void) printf ("\tEnd: %ld", bytes_range->end);
	(void) printf ("\tChunk size: %ld", bytes_range->chunk_size);

}

#pragma endregion

#pragma region json

// a writer fails its whole text with a string that is not UTF-8: the member is left out before it is written
void http_json_member_string (
	CerverJsonWriter *writer, const char *key, const char *value
) {

	const size_t length = value ? strlen (value) : 0;

	if (value && json_text_is_valid ((const u8 *) value, length)) {
		cerver_json_writer_key (writer, key);
		cerver_json_writer_string (writer, value, length);
	}

}

void http_json_member_integer (
	CerverJsonWriter *writer, const char *key, const i64 value
) {

	cerver_json_writer_key (writer, key);
	cerver_json_writer_integer (writer, value);

}

// a writer fails its whole text with a real that is not a number: the member is left out before it is written
void http_json_member_real (
	CerverJsonWriter *writer, const char *key, const double value
) {

	if (isfinite (value)) {
		cerver_json_writer_key (writer, key);
		cerver_json_writer_real (writer, value);
	}

}

void http_json_member_boolean (
	CerverJsonWriter *writer, const char *key, const bool value
) {

	cerver_json_writer_key (writer, key);
	cerver_json_writer_boolean (writer, value);

}

// the text of a writer that ended without an error, in memory of its own
static char *http_json_generate_copy (const CerverJsonWriter *writer) {

	const size_t length = cerver_json_writer_length (writer);

	char *text = (char *) malloc (length + 1);
	if (text) (void) memcpy (text, cerver_json_writer_text (writer), length + 1);

	return text;

}

// the values of a text can change while it is written (the stats of a cerver that is running),
// so it is written once into an arena instead of being counted first
char *http_json_generate (
	const HttpJsonWrite write, const void *data
) {

	char *text = NULL;

	Arena *arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
	if (arena) {
		CerverJsonWriter writer;
		cerver_json_writer_init_arena (&writer, arena, CERVER_JSON_FLAGS_NONE);

		write (&writer, data);

		if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
			text = http_json_generate_copy (&writer);
		}

		arena_delete (arena);
	}

	return text;

}

#pragma endregion
