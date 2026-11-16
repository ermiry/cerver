#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

// the numbers of the JSON of cerver against the C library:
// - a text that is a number is read as strtod () reads it, bit by bit, or as strtoull () and strtoll ()
//   for an integer, and a real that does not fit is the infinite of the library;
// - the first bytes of the input are a double, that is written with the least digits that read it back:
//   the library reads the same double from them and needs as many digits

#define NUMBER_FUZZ_TEXT_SIZE				(1 << 6)

// the most digits that a double needs to be read back
#define NUMBER_FUZZ_DIGITS_MAX				17

#define NUMBER_FUZZ_BASE					10

// the bits of the mantissa of a double
#define NUMBER_FUZZ_MANTISSA_MASK			((UINT64_C (1) << 52) - 1)

static Arena *number_fuzz_arena = NULL;

// counts the digits of the mantissa of a real that was written, without the zeros that only place its point
static size_t number_fuzz_digits (const char *text) {

	size_t digits = 0;
	bool significant = false;

	const char *end = strchr (text, 'e');
	if (!end) end = text + strlen (text);

	for (const char *character = text; character < end; character++) {
		if ((*character >= '1') && (*character <= '9')) significant = true;

		if (significant && (*character >= '0') && (*character <= '9')) digits += 1;
	}

	while ((end > text) && ((end[-1] == '0') || (end[-1] == '.'))) {
		if (end[-1] == '0') digits -= 1;
		end -= 1;
	}

	return digits;

}

// the least digits that the C library needs to read a double back
static size_t number_fuzz_shortest (const double value) {

	char text[NUMBER_FUZZ_TEXT_SIZE];

	for (int precision = 0; precision < NUMBER_FUZZ_DIGITS_MAX; precision++) {
		(void) snprintf (text, sizeof (text), "%.*e", precision, value);

		const double read = strtod (text, NULL);
		if (!memcmp (&read, &value, sizeof (double))) return (size_t) precision + 1;
	}

	return NUMBER_FUZZ_DIGITS_MAX;

}

// a double written by cerver: read back by the library and by cerver, with the least digits
static void number_fuzz_write (const double value) {

	char text[NUMBER_FUZZ_TEXT_SIZE];
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, text, sizeof (text), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_real (&writer, value);

	if (!isfinite (value)) {
		if (cerver_json_writer_end (&writer) != CERVER_JSON_ERROR_RANGE) abort ();
		return;
	}

	if (cerver_json_writer_end (&writer) != CERVER_JSON_ERROR_NONE) abort ();

	const double read = strtod (text, NULL);
	if (memcmp (&read, &value, sizeof (double))) abort ();

	u64 bits = 0;
	(void) memcpy (&bits, &value, sizeof (u64));

	const size_t digits = number_fuzz_digits (text);
	const size_t shortest = number_fuzz_shortest (value);

	// the double below a power of two is at half of the distance of the one over it: a text with one
	// digit less can read it back from the far side, where the closest text that the library writes is not
	const bool power = !(bits & NUMBER_FUZZ_MANTISSA_MASK);

	if ((bits << 1) && (digits != shortest) && !(power && ((digits + 1) == shortest))) abort ();

	CerverJsonDocument *document = cerver_json_parse (
		number_fuzz_arena, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	if (!document || (cerver_json_type (cerver_json_document_root (document)) != CERVER_JSON_TYPE_REAL)) abort ();

	const double parsed = cerver_json_real_value (cerver_json_document_root (document));
	if (memcmp (&parsed, &value, sizeof (double))) abort ();

}

// a number that cerver read, against the library with the same text
static void number_fuzz_compare (const CerverJsonValue *value, const char *text) {

	switch (cerver_json_type (value)) {
		case CERVER_JSON_TYPE_INTEGER:
			if (cerver_json_integer_value (value) != strtoll (text, NULL, NUMBER_FUZZ_BASE)) abort ();
			break;

		case CERVER_JSON_TYPE_UNSIGNED:
			if (cerver_json_unsigned_value (value) != strtoull (text, NULL, NUMBER_FUZZ_BASE)) abort ();
			break;

		case CERVER_JSON_TYPE_REAL: {
			const double expected = strtod (text, NULL);
			const double real = cerver_json_real_value (value);
			if (memcmp (&expected, &real, sizeof (double))) abort ();
		} break;

		default: break;
	}

}

// a text read by cerver and by the library
static void number_fuzz_read (const uint8_t *data, const size_t size) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	// the library needs the NUL that the text of the fuzzer does not have
	char *text = (char *) malloc (size + 1);
	if (!text) abort ();

	(void) memcpy (text, data, size);
	text[size] = '\0';

	CerverJsonDocument *document = cerver_json_parse (
		number_fuzz_arena, (const char *) data, size, CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	if (document) {
		number_fuzz_compare (cerver_json_document_root (document), text);
	}

	// a real that does not fit, alone in its text, is an infinite for the library; an integer that
	// does not fit is read as a real with its flag, the one that the library reads
	else if ((error.code == CERVER_JSON_ERROR_RANGE) && !error.offset) {
		arena_reset (number_fuzz_arena);

		document = cerver_json_parse (
			number_fuzz_arena, (const char *) data, size, CERVER_JSON_PARSE_BIG_AS_REAL, NULL, &error
		);

		if (document) number_fuzz_compare (cerver_json_document_root (document), text);
		else if ((error.code == CERVER_JSON_ERROR_RANGE) && !isinf (strtod (text, NULL))) abort ();
	}

	free (text);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	double value = 0;

	if (!number_fuzz_arena) number_fuzz_arena = arena_create (0);
	if (!number_fuzz_arena) abort ();

	arena_reset (number_fuzz_arena);

	if (size >= sizeof (double)) {
		(void) memcpy (&value, data, sizeof (double));
		number_fuzz_write (value);

		arena_reset (number_fuzz_arena);
	}

	number_fuzz_read (data, size);

	return 0;

}
