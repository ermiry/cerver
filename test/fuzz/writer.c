#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

// the bytes of the input are the calls to a writer, in any order: a writer never writes out of its buffer,
// one that ends without an error wrote a text that is read back, the value that was read is written again
// with the same bytes, and a buffer of the caller and an arena get the same text.
// The first byte chooses the flags and the second one the indentation; each call is a byte and its arguments

#define WRITER_FUZZ_HEADER_SIZE				2

// the bits of the first byte that are the flags of the writer
#define WRITER_FUZZ_FLAGS_MASK				0x7

// the most spaces of a level
#define WRITER_FUZZ_INDENT_MASK				0x7

// the bytes of a string are the ones that follow, as many as the low bits of the next byte say
#define WRITER_FUZZ_STRING_MASK				0xF

// the calls of a writer
typedef enum WriterFuzzCall {

	WRITER_FUZZ_OBJECT_BEGIN,
	WRITER_FUZZ_OBJECT_END,
	WRITER_FUZZ_ARRAY_BEGIN,
	WRITER_FUZZ_ARRAY_END,
	WRITER_FUZZ_KEY,
	WRITER_FUZZ_STRING,
	WRITER_FUZZ_NULL,
	WRITER_FUZZ_TRUE,
	WRITER_FUZZ_FALSE,
	WRITER_FUZZ_INTEGER,
	WRITER_FUZZ_UNSIGNED,
	WRITER_FUZZ_REAL,
	WRITER_FUZZ_COUNT

} WriterFuzzCall;

// the input, as it is taken
typedef struct WriterFuzzInput {

	const uint8_t *data;
	size_t size;
	size_t position;

} WriterFuzzInput;

static Arena *writer_fuzz_arena = NULL;

// takes bytes of the input; the ones that it does not have are zeros
static void writer_fuzz_take (WriterFuzzInput *input, void *bytes, const size_t size) {

	const size_t left = input->size - input->position;
	const size_t taken = (left < size) ? left : size;

	(void) memset (bytes, 0, size);
	if (taken) (void) memcpy (bytes, input->data + input->position, taken);

	input->position += taken;

}

// the bytes of a string, inside the input
static const char *writer_fuzz_string (WriterFuzzInput *input, size_t *length) {

	uint8_t size = 0;
	writer_fuzz_take (input, &size, sizeof (size));

	const size_t left = input->size - input->position;
	*length = ((size_t) (size & WRITER_FUZZ_STRING_MASK) < left) ? (size_t) (size & WRITER_FUZZ_STRING_MASK) : left;

	const char *string = (const char *) (input->data + input->position);
	input->position += *length;

	return string;

}

static void writer_fuzz_call (CerverJsonWriter *writer, WriterFuzzInput *input, const WriterFuzzCall call) {

	size_t length = 0;
	const char *string = NULL;
	i64 integer = 0;
	u64 natural = 0;
	double real = 0;

	switch (call) {
		case WRITER_FUZZ_OBJECT_BEGIN: cerver_json_writer_object_begin (writer); break;
		case WRITER_FUZZ_OBJECT_END: cerver_json_writer_object_end (writer); break;
		case WRITER_FUZZ_ARRAY_BEGIN: cerver_json_writer_array_begin (writer); break;
		case WRITER_FUZZ_ARRAY_END: cerver_json_writer_array_end (writer); break;

		case WRITER_FUZZ_KEY:
			string = writer_fuzz_string (input, &length);
			cerver_json_writer_key_bytes (writer, string, length);
			break;

		case WRITER_FUZZ_STRING:
			string = writer_fuzz_string (input, &length);
			cerver_json_writer_string (writer, string, length);
			break;

		case WRITER_FUZZ_NULL: cerver_json_writer_null (writer); break;
		case WRITER_FUZZ_TRUE: cerver_json_writer_boolean (writer, true); break;
		case WRITER_FUZZ_FALSE: cerver_json_writer_boolean (writer, false); break;

		case WRITER_FUZZ_INTEGER:
			writer_fuzz_take (input, &integer, sizeof (integer));
			cerver_json_writer_integer (writer, integer);
			break;

		case WRITER_FUZZ_UNSIGNED:
			writer_fuzz_take (input, &natural, sizeof (natural));
			cerver_json_writer_unsigned (writer, natural);
			break;

		case WRITER_FUZZ_REAL:
			writer_fuzz_take (input, &real, sizeof (real));
			cerver_json_writer_real (writer, real);
			break;

		default: break;
	}

}

// makes the calls of the input to a writer and returns how it ended
static CerverJsonErrorCode writer_fuzz_run (CerverJsonWriter *writer, const uint8_t *data, const size_t size) {

	WriterFuzzInput input = { data, size, 0 };

	while (input.position < input.size) {
		uint8_t call = 0;
		writer_fuzz_take (&input, &call, sizeof (call));

		writer_fuzz_call (writer, &input, (WriterFuzzCall) (call % WRITER_FUZZ_COUNT));
	}

	return cerver_json_writer_end (writer);

}

// the text of a writer is read back, and its value is written again with the same bytes
static void writer_fuzz_read_back (const char *text, const size_t length, const unsigned int flags, const unsigned int indent) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	CerverJsonDocument *document = cerver_json_parse (NULL, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	if (!document) abort ();

	CerverJsonWriter again;
	cerver_json_writer_init_arena (&again, writer_fuzz_arena, flags);
	cerver_json_writer_set_indent (&again, indent);
	cerver_json_writer_value (&again, cerver_json_document_root (document));

	if (cerver_json_writer_end (&again) != CERVER_JSON_ERROR_NONE) abort ();
	if (cerver_json_writer_length (&again) != length) abort ();
	if (memcmp (cerver_json_writer_text (&again), text, length)) abort ();

	cerver_json_document_delete (document);

}

// the same calls into a buffer of the caller that is too small: what fits, its NUL and the whole length
static void writer_fuzz_truncated (
	const uint8_t *data, const size_t size, const unsigned int flags, const unsigned int indent,
	const char *text, const size_t length
) {

	const size_t capacity = length / 2;

	char *buffer = (char *) malloc (capacity + 1);
	if (!buffer) abort ();

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, buffer, capacity, flags);
	cerver_json_writer_set_indent (&writer, indent);

	if (writer_fuzz_run (&writer, data, size) != CERVER_JSON_ERROR_TRUNCATED) abort ();
	if (cerver_json_writer_length (&writer) != length) abort ();

	if (capacity) {
		if (memcmp (buffer, text, capacity - 1)) abort ();
		if (buffer[capacity - 1]) abort ();
	}

	free (buffer);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < WRITER_FUZZ_HEADER_SIZE) return 0;

	if (!writer_fuzz_arena) writer_fuzz_arena = arena_create (0);
	if (!writer_fuzz_arena) abort ();

	arena_reset (writer_fuzz_arena);

	const unsigned int flags = data[0] & WRITER_FUZZ_FLAGS_MASK;
	const unsigned int indent = data[1] & WRITER_FUZZ_INDENT_MASK;

	CerverJsonWriter writer;
	cerver_json_writer_init_arena (&writer, writer_fuzz_arena, flags);
	cerver_json_writer_set_indent (&writer, indent);

	const CerverJsonErrorCode error = writer_fuzz_run (
		&writer, data + WRITER_FUZZ_HEADER_SIZE, size - WRITER_FUZZ_HEADER_SIZE
	);

	if (error == CERVER_JSON_ERROR_NONE) {
		const char *text = cerver_json_writer_text (&writer);
		const size_t length = cerver_json_writer_length (&writer);

		if (strlen (text) != length) abort ();

		writer_fuzz_read_back (text, length, flags, indent);

		writer_fuzz_truncated (
			data + WRITER_FUZZ_HEADER_SIZE, size - WRITER_FUZZ_HEADER_SIZE, flags, indent, text, length
		);
	}

	return 0;

}
