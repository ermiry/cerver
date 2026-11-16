#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/events.h>
#include <cerver/json/json.h>
#include <cerver/json/pointer.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

// a text is read by every reader of the JSON of cerver and they must agree: a document from a copy,
// a document in situ and the events take the same texts and give the same error at the same position.
// A text that is taken is written, and what was written is read back as the same value and written
// again with the same bytes. The first byte chooses the flags, the second one the limit of the levels and
// the third one the limit of the values

// the bytes of the input that choose how the text is read
#define JSON_FUZZ_HEADER_SIZE				3

// the bits of the first byte: one for each flag of a reader
#define JSON_FUZZ_FLAGS_MASK				0x7

// the bits of the second and the third bytes that are a limit, 0 for its default
#define JSON_FUZZ_LIMIT_MASK				0xF

static Arena *json_fuzz_arena = NULL;

// the methods of the events write each one again
static bool json_fuzz_on_null (void *data) {

	cerver_json_writer_null ((CerverJsonWriter *) data);
	return true;

}

static bool json_fuzz_on_boolean (void *data, const bool value) {

	cerver_json_writer_boolean ((CerverJsonWriter *) data, value);
	return true;

}

static bool json_fuzz_on_integer (void *data, const i64 value) {

	cerver_json_writer_integer ((CerverJsonWriter *) data, value);
	return true;

}

static bool json_fuzz_on_unsigned (void *data, const u64 value) {

	cerver_json_writer_unsigned ((CerverJsonWriter *) data, value);
	return true;

}

static bool json_fuzz_on_real (void *data, const double value) {

	cerver_json_writer_real ((CerverJsonWriter *) data, value);
	return true;

}

static bool json_fuzz_on_string (void *data, const char *string, const size_t length) {

	if (string[length]) abort ();

	cerver_json_writer_string ((CerverJsonWriter *) data, string, length);
	return true;

}

static bool json_fuzz_on_key (void *data, const char *key, const size_t length) {

	if (key[length]) abort ();

	cerver_json_writer_key_bytes ((CerverJsonWriter *) data, key, length);
	return true;

}

static bool json_fuzz_on_object_begin (void *data) {

	cerver_json_writer_object_begin ((CerverJsonWriter *) data);
	return true;

}

static bool json_fuzz_on_object_end (void *data) {

	cerver_json_writer_object_end ((CerverJsonWriter *) data);
	return true;

}

static bool json_fuzz_on_array_begin (void *data) {

	cerver_json_writer_array_begin ((CerverJsonWriter *) data);
	return true;

}

static bool json_fuzz_on_array_end (void *data) {

	cerver_json_writer_array_end ((CerverJsonWriter *) data);
	return true;

}

static const CerverJsonEvents json_fuzz_events = {
	json_fuzz_on_null,
	json_fuzz_on_boolean,
	json_fuzz_on_integer,
	json_fuzz_on_unsigned,
	json_fuzz_on_real,
	json_fuzz_on_string,
	json_fuzz_on_key,
	json_fuzz_on_object_begin,
	json_fuzz_on_object_end,
	json_fuzz_on_array_begin,
	json_fuzz_on_array_end
};

// writes a value into the arena of the fuzzer, with its flags
static const char *json_fuzz_write (const CerverJsonValue *value, const unsigned int flags, size_t *length) {

	CerverJsonWriter writer;

	cerver_json_writer_init_arena (&writer, json_fuzz_arena, flags);
	cerver_json_writer_value (&writer, value);

	if (cerver_json_writer_end (&writer) != CERVER_JSON_ERROR_NONE) abort ();

	*length = cerver_json_writer_length (&writer);

	return cerver_json_writer_text (&writer);

}

// what was written is read back as the same value, and written again with the same bytes
static void json_fuzz_round_trip (const CerverJsonValue *root, const unsigned int flags) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	size_t length = 0;
	size_t again_length = 0;

	const char *text = json_fuzz_write (root, flags, &length);

	if (strlen (text) != length) abort ();

	// the size that a buffer of the caller needs is the one of the text
	if (cerver_json_write (root, NULL, 0, flags) != length) abort ();

	CerverJsonDocument *document = cerver_json_parse (NULL, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	if (!document) abort ();

	if (!cerver_json_equal (root, cerver_json_document_root (document))) abort ();
	if (!cerver_json_equal (cerver_json_document_root (document), root)) abort ();

	const char *again = json_fuzz_write (cerver_json_document_root (document), flags, &again_length);
	if ((again_length != length) || memcmp (again, text, length)) abort ();

	cerver_json_document_delete (document);

}

// the events of the text, written, are the text of its document
static void json_fuzz_events_text (
	const uint8_t *text, const size_t size, const unsigned int flags, const CerverJsonLimits *limits,
	const CerverJsonValue *root, const CerverJsonError *expected
) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	size_t length = 0;

	CerverJsonWriter writer;
	cerver_json_writer_init_arena (&writer, json_fuzz_arena, CERVER_JSON_FLAGS_NONE);

	const bool read = cerver_json_read (
		NULL, (const char *) text, size, flags, limits, &json_fuzz_events, &writer, &error
	);

	if (read != (root != NULL)) abort ();
	if ((error.code != expected->code) || (error.offset != expected->offset)) abort ();

	if (root) {
		if (cerver_json_writer_end (&writer) != CERVER_JSON_ERROR_NONE) abort ();

		const char *events_text = cerver_json_writer_text (&writer);
		const size_t events_length = cerver_json_writer_length (&writer);

		const char *document_text = json_fuzz_write (root, CERVER_JSON_FLAGS_NONE, &length);
		if ((events_length != length) || memcmp (events_text, document_text, length)) abort ();
	}

}

// the same text in situ: the same document or the same error
static void json_fuzz_in_situ (
	const uint8_t *text, const size_t size, const unsigned int flags, const CerverJsonLimits *limits,
	const CerverJsonValue *root, const CerverJsonError *expected
) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	char *copy = (char *) malloc (size + CERVER_JSON_PADDING);
	if (!copy) abort ();

	if (size) (void) memcpy (copy, text, size);

	CerverJsonDocument *document = cerver_json_parse_in_situ (json_fuzz_arena, copy, size, flags, limits, &error);

	if ((document != NULL) != (root != NULL)) abort ();
	if ((error.code != expected->code) || (error.offset != expected->offset)) abort ();

	if (document && !cerver_json_equal (root, cerver_json_document_root (document))) abort ();

	free (copy);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < JSON_FUZZ_HEADER_SIZE) return 0;

	if (!json_fuzz_arena) json_fuzz_arena = arena_create (0);
	if (!json_fuzz_arena) abort ();

	arena_reset (json_fuzz_arena);

	const unsigned int flags = data[0] & JSON_FUZZ_FLAGS_MASK;
	const CerverJsonLimits limits = {
		(size_t) (data[1] & JSON_FUZZ_LIMIT_MASK), 0, (size_t) (data[2] & JSON_FUZZ_LIMIT_MASK)
	};

	const uint8_t *text = data + JSON_FUZZ_HEADER_SIZE;
	const size_t length = size - JSON_FUZZ_HEADER_SIZE;

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	CerverJsonDocument *document = cerver_json_parse (NULL, (const char *) text, length, flags, &limits, &error);
	const CerverJsonValue *root = cerver_json_document_root (document);

	if ((document != NULL) != (error.code == CERVER_JSON_ERROR_NONE)) abort ();
	if (error.offset > length) abort ();

	json_fuzz_in_situ (text, length, flags, &limits, root, &error);

	// the events do not keep the keys of an object: they can not reject the ones that are more than once
	if (!(flags & CERVER_JSON_PARSE_REJECT_DUPLICATES)) {
		json_fuzz_events_text (text, length, flags, &limits, root, &error);
	}

	if (root) {
		json_fuzz_round_trip (root, CERVER_JSON_FLAGS_NONE);
		json_fuzz_round_trip (root, CERVER_JSON_WRITE_COMPACT | CERVER_JSON_WRITE_ESCAPE_SLASH);
		json_fuzz_round_trip (root, CERVER_JSON_WRITE_ENSURE_ASCII);

		// the text as a pointer into its own document: any bytes, never out of the document
		(void) cerver_json_pointer_get (root, (const char *) text, length);
	}

	cerver_json_document_delete (document);

	return 0;

}
