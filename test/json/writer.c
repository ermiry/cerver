#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include "../test.h"
#include "json.h"

#define WRITER_TEST_BUFFER_SIZE				(1 << 10)

// how many random doubles are written and read back
#define WRITER_TEST_RANDOM_COUNT			(1 << 16)

// the most digits that a double needs to be read back
#define WRITER_TEST_DIGITS_MAX				17

// more bytes than the first buffer that a writer takes from an arena, several times
#define WRITER_TEST_LONG_ELEMENTS			(1 << 12)

// the bits of the mantissa of a double
#define WRITER_TEST_MANTISSA_MASK			((UINT64_C (1) << 52) - 1)

// the powers of two that a double has, from the smallest subnormal to the biggest one
#define WRITER_TEST_POWER_FIRST				(-1074)
#define WRITER_TEST_POWER_LAST				1023

// the bytes that each level of the deepest text of a writer gets
#define WRITER_TEST_LEVEL_SIZE				8

#define WRITER_TEST_SEED					UINT64_C (0x2545F4914F6CDD1D)

// a real and the text that it is written with
typedef struct WriterTestReal {

	double value;
	const char *text;

} WriterTestReal;

// a string, the flags of its writer and the text that it is written with
typedef struct WriterTestString {

	const char *bytes;
	size_t length;
	unsigned int flags;
	const char *text;

} WriterTestString;

#define WRITER_TEST_STRING(bytes, flags, text)		{ bytes, sizeof (bytes) - 1, flags, text }

// the longest of the strings with some bytes in every position: more than four words of a scan
#define WRITER_TEST_STRING_LENGTH			40

// the letter of those strings
#define WRITER_TEST_LETTER					'a'

// the bit of a byte that is not ASCII
#define WRITER_TEST_NOT_ASCII				0x80

// some bytes of a string and what they are written as, without and with the flag of the slash
typedef struct WriterTestPlace {

	const char *bytes;
	size_t size;
	const char *text;
	const char *slashed;

} WriterTestPlace;

static u64 writer_test_state = WRITER_TEST_SEED;

static u64 writer_test_random (void) {

	writer_test_state ^= writer_test_state << 13;
	writer_test_state ^= writer_test_state >> 7;
	writer_test_state ^= writer_test_state << 17;

	return writer_test_state;

}

// ends a writer of a buffer and checks its text
static void writer_test_expect (CerverJsonWriter *writer, const char *expected) {

	const CerverJsonErrorCode error = cerver_json_writer_end (writer);

	if ((error != CERVER_JSON_ERROR_NONE) || strcmp (cerver_json_writer_text (writer), expected)) {
		(void) fprintf (
			stderr, "the writer has <%s> (%s), expected <%s>\n",
			cerver_json_writer_text (writer), cerver_json_error_to_string (error), expected
		);

		exit (1);
	}

	test_check_unsigned_eq (cerver_json_writer_length (writer), strlen (expected), NULL);

}

#pragma region formats

// the body of an API written value by value
static void writer_test_body (CerverJsonWriter *writer) {

	cerver_json_writer_object_begin (writer);
	cerver_json_writer_key (writer, "name");
	cerver_json_writer_string (writer, "cerver", strlen ("cerver"));
	cerver_json_writer_key (writer, "values");
	cerver_json_writer_array_begin (writer);
	cerver_json_writer_integer (writer, 1);
	cerver_json_writer_integer (writer, 2);
	cerver_json_writer_integer (writer, 3);
	cerver_json_writer_array_end (writer);
	cerver_json_writer_key (writer, "nested");
	cerver_json_writer_object_begin (writer);
	cerver_json_writer_key (writer, "ok");
	cerver_json_writer_boolean (writer, true);
	cerver_json_writer_key (writer, "none");
	cerver_json_writer_null (writer);
	cerver_json_writer_key (writer, "empty");
	cerver_json_writer_array_begin (writer);
	cerver_json_writer_array_end (writer);
	cerver_json_writer_object_end (writer);
	cerver_json_writer_key (writer, "last");
	cerver_json_writer_real (writer, -1.5);
	cerver_json_writer_object_end (writer);

}

// by default a space after each comma and each colon, as the responses of cerver have always been
static void test_writer_default (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	writer_test_body (&writer);

	writer_test_expect (
		&writer,
		"{\"name\": \"cerver\", \"values\": [1, 2, 3], \"nested\": {\"ok\": true, \"none\": null, \"empty\": []}, \"last\": -1.5}"
	);

	test_check_ptr_eq (cerver_json_writer_text (&writer), buffer);

}

static void test_writer_compact (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_WRITE_COMPACT);
	writer_test_body (&writer);

	writer_test_expect (
		&writer,
		"{\"name\":\"cerver\",\"values\":[1,2,3],\"nested\":{\"ok\":true,\"none\":null,\"empty\":[]},\"last\":-1.5}"
	);

}

static void test_writer_indent (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_set_indent (&writer, 2);
	writer_test_body (&writer);

	writer_test_expect (
		&writer,
		"{\n"
		"  \"name\": \"cerver\",\n"
		"  \"values\": [\n"
		"    1,\n"
		"    2,\n"
		"    3\n"
		"  ],\n"
		"  \"nested\": {\n"
		"    \"ok\": true,\n"
		"    \"none\": null,\n"
		"    \"empty\": []\n"
		"  },\n"
		"  \"last\": -1.5\n"
		"}"
	);

	// with the compact flag only the space after a colon goes
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_WRITE_COMPACT);
	cerver_json_writer_set_indent (&writer, 4);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, "a");
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_object_end (&writer);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_object_end (&writer);
	cerver_json_writer_array_end (&writer);

	writer_test_expect (&writer, "[\n    {\n        \"a\":1\n    },\n    {}\n]");

}

// a text of one value that is not an array nor an object
static void test_writer_scalars (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_null (&writer);
	writer_test_expect (&writer, "null");

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_boolean (&writer, false);
	writer_test_expect (&writer, "false");

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_string (&writer, "", 0);
	writer_test_expect (&writer, "\"\"");

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_array_end (&writer);
	writer_test_expect (&writer, "[]");

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_object_end (&writer);
	writer_test_expect (&writer, "{}");

	// a text that is already written goes as it is
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_raw (&writer, "{\"raw\":true}", strlen ("{\"raw\":true}"));
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_array_end (&writer);
	writer_test_expect (&writer, "[{\"raw\":true}, 1]");

}

#pragma endregion

#pragma region numbers

static void test_writer_integers (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_integer (&writer, 0);
	cerver_json_writer_integer (&writer, 7);
	cerver_json_writer_integer (&writer, -7);
	cerver_json_writer_integer (&writer, 10);
	cerver_json_writer_integer (&writer, 99);
	cerver_json_writer_integer (&writer, 100);
	cerver_json_writer_integer (&writer, 12345);
	cerver_json_writer_integer (&writer, INT64_C (4102444800));
	cerver_json_writer_integer (&writer, INT64_MAX);
	cerver_json_writer_integer (&writer, INT64_MIN);
	cerver_json_writer_unsigned (&writer, 0);
	cerver_json_writer_unsigned (&writer, UINT64_MAX);
	cerver_json_writer_unsigned (&writer, UINT64_C (10000000000000000000));
	cerver_json_writer_array_end (&writer);

	writer_test_expect (
		&writer,
		"[0, 7, -7, 10, 99, 100, 12345, 4102444800, 9223372036854775807, -9223372036854775808,"
		" 0, 18446744073709551615, 10000000000000000000]"
	);

}

static const WriterTestReal writer_test_reals[] = {
	{ 0.0, "0.0" },
	{ -0.0, "-0.0" },
	{ 1.0, "1.0" },
	{ -1.0, "-1.0" },
	{ 1.5, "1.5" },
	{ 0.1, "0.1" },
	{ 0.2, "0.2" },
	{ 0.3, "0.3" },
	{ 0.1 + 0.2, "0.30000000000000004" },
	{ 100.0, "100.0" },
	{ 123456789.125, "123456789.125" },
	{ 0.5, "0.5" },
	{ 0.25, "0.25" },
	{ 2.5e-5, "2.5e-5" },
	{ 0.0001, "0.0001" },
	{ 0.00012345, "0.00012345" },
	{ 0.00001, "1e-5" },
	{ 1.5e-7, "1.5e-7" },
	{ 1e15, "1000000000000000.0" },
	{ 1e16, "10000000000000000.0" },
	{ 1.2345e16, "12345000000000000.0" },
	{ 1e17, "1e17" },
	{ 1e21, "1e21" },
	{ 1e22, "1e22" },
	{ 1e23, "1e23" },
	{ 1.2345e25, "1.2345e25" },
	{ 1e100, "1e100" },
	{ 1e-100, "1e-100" },
	{ 9007199254740992.0, "9007199254740992.0" },
	{ 9007199254740994.0, "9007199254740994.0" },
	{ 2251799813685248.5, "2251799813685248.5" },
	// exactly between two texts of the same digits: the one whose last digit is even
	{ 1125899906842624.25, "1125899906842624.2" },
	{ 1125899906842624.75, "1125899906842624.8" },
	{ 3.141592653589793, "3.141592653589793" },
	{ 2.718281828459045, "2.718281828459045" },
	{ 123.456, "123.456" },
	{ 5e-324, "5e-324" },
	{ 2.2250738585072014e-308, "2.2250738585072014e-308" },
	{ 2.225073858507201e-308, "2.225073858507201e-308" },
	{ 1.7976931348623157e308, "1.7976931348623157e308" },
	{ 4294967296.0, "4294967296.0" },
	{ 0.1e-3, "0.0001" },
	{ 9.999999999999997e22, "9.999999999999997e22" },
	{ 1.0000000000000002, "1.0000000000000002" },
	{ 0.9999999999999999, "0.9999999999999999" },
	{ 5e-1, "0.5" },
	{ 1e-4, "0.0001" },
	{ 9.5e-5, "9.5e-5" },
	{ 99999999999999990.0, "99999999999999980.0" },
	{ 12345678901234567.0, "12345678901234568.0" }
};

static void test_writer_reals (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	for (size_t i = 0; i < (sizeof (writer_test_reals) / sizeof (writer_test_reals[0])); i++) {
		cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
		cerver_json_writer_real (&writer, writer_test_reals[i].value);
		writer_test_expect (&writer, writer_test_reals[i].text);
	}

	// JSON has no text for these
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_real (&writer, (double) NAN);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_RANGE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_real (&writer, (double) INFINITY);
	cerver_json_writer_array_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_RANGE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_real (&writer, (double) -INFINITY);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_RANGE, NULL);

}

// counts the digits of the mantissa of a real that was written, without the zeros that only place its point
static size_t writer_test_digits (const char *text) {

	size_t digits = 0;
	bool significant = false;

	for (const char *character = text; *character && (*character != 'e'); character++) {
		if ((*character >= '1') && (*character <= '9')) significant = true;

		if (significant && (*character >= '0') && (*character <= '9')) digits += 1;
	}

	// the zeros at the end are not digits of the number either
	const char *end = strchr (text, 'e');
	if (!end) end = text + strlen (text);

	while ((end > text) && ((end[-1] == '0') || (end[-1] == '.'))) {
		if (end[-1] == '0') digits -= 1;
		end -= 1;
	}

	return digits;

}

// the least digits that the C library needs to read a double back
static size_t writer_test_shortest (const double value) {

	char text[WRITER_TEST_BUFFER_SIZE];

	for (int precision = 0; precision < WRITER_TEST_DIGITS_MAX; precision++) {
		(void) snprintf (text, sizeof (text), "%.*e", precision, value);

		const double read = strtod (text, NULL);
		if (!memcmp (&read, &value, sizeof (double))) return (size_t) precision + 1;
	}

	return WRITER_TEST_DIGITS_MAX;

}

// the text of a real has the least digits: as many as the C library needs. The double below a power of two
// is at half of the distance of the one over it, so a text with one digit less can read it back from the
// far side, where the closest text, the one that the library writes, does not
static bool writer_test_is_shortest (const char *text, const double value) {

	u64 bits = 0;
	(void) memcpy (&bits, &value, sizeof (u64));

	const size_t digits = writer_test_digits (text);
	const size_t shortest = writer_test_shortest (value);

	return (digits == shortest) || (!(bits & WRITER_TEST_MANTISSA_MASK) && ((digits + 1) == shortest));

}

// every power of two that a double has, the ones whose text can be shorter than the closest one
static void test_writer_reals_powers (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	size_t shorter = 0;

	for (int exponent = WRITER_TEST_POWER_FIRST; exponent <= WRITER_TEST_POWER_LAST; exponent++) {
		const double value = ldexp (1.0, exponent);

		cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
		cerver_json_writer_real (&writer, value);
		test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

		const double read = strtod (buffer, NULL);
		if (memcmp (&read, &value, sizeof (double)) || !writer_test_is_shortest (buffer, value)) {
			(void) fprintf (stderr, "2^%d was written as <%s>, that is read as %.17e\n", exponent, buffer, read);
			exit (1);
		}

		if (writer_test_digits (buffer) < writer_test_shortest (value)) shorter += 1;
	}

	// some of them are written with one digit less than the library needs, like 2^305
	test_check_true (shorter > 0);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_real (&writer, ldexp (1.0, 305));
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);
	test_check_str_eq (buffer, "6.518515124270356e91", NULL);

}

// random doubles: each one is read back as it was, by the C library and by cerver, from the least digits
static void test_writer_reals_random (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (size_t i = 0; i < WRITER_TEST_RANDOM_COUNT; i++) {
		double value = 0;
		u64 bits = writer_test_random ();

		// some of them with few digits, the ones that people write
		if (!(i % 4)) {
			value = ((double) (bits % 100000)) / 1000.0;
			(void) memcpy (&bits, &value, sizeof (u64));
		}

		else {
			(void) memcpy (&value, &bits, sizeof (double));
		}

		if (isfinite (value)) {
			cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
			cerver_json_writer_real (&writer, value);
			test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

			const double read = strtod (buffer, NULL);
			if (memcmp (&read, &value, sizeof (double))) {
				(void) fprintf (stderr, "%.17e was written as <%s>, that is read as %.17e\n", value, buffer, read);
				exit (1);
			}

			// a zero has no digits to count
			if ((bits << 1) && !writer_test_is_shortest (buffer, value)) {
				(void) fprintf (
					stderr, "%.17e was written as <%s>, with %zu digits and %zu are enough\n",
					value, buffer, writer_test_digits (buffer), writer_test_shortest (value)
				);

				exit (1);
			}

			// and it is a real for the reader of cerver
			arena_reset (arena);

			CerverJsonDocument *document = cerver_json_parse (
				arena, buffer, strlen (buffer), CERVER_JSON_FLAGS_NONE, NULL, &error
			);

			test_check_ptr (document);
			test_check_unsigned_eq (cerver_json_type (cerver_json_document_root (document)), CERVER_JSON_TYPE_REAL, NULL);

			const double parsed = cerver_json_real_value (cerver_json_document_root (document));
			test_check_true (!memcmp (&parsed, &value, sizeof (double)));
		}
	}

	arena_delete (arena);

}

#pragma endregion

#pragma region strings

static const WriterTestString writer_test_strings[] = {
	WRITER_TEST_STRING ("", CERVER_JSON_FLAGS_NONE, "\"\""),
	WRITER_TEST_STRING ("cerver", CERVER_JSON_FLAGS_NONE, "\"cerver\""),
	WRITER_TEST_STRING ("a\"b", CERVER_JSON_FLAGS_NONE, "\"a\\\"b\""),
	WRITER_TEST_STRING ("a\\b", CERVER_JSON_FLAGS_NONE, "\"a\\\\b\""),
	WRITER_TEST_STRING ("\b\f\n\r\t", CERVER_JSON_FLAGS_NONE, "\"\\b\\f\\n\\r\\t\""),
	WRITER_TEST_STRING ("line\nline", CERVER_JSON_FLAGS_NONE, "\"line\\nline\""),
	// the other control characters, as units
	WRITER_TEST_STRING ("\x01\x1F", CERVER_JSON_FLAGS_NONE, "\"\\u0001\\u001F\""),
	WRITER_TEST_STRING ("a\0b", CERVER_JSON_FLAGS_NONE, "\"a\\u0000b\""),
	WRITER_TEST_STRING ("\x0B\x0E", CERVER_JSON_FLAGS_NONE, "\"\\u000B\\u000E\""),
	// the delete character is not one of them
	WRITER_TEST_STRING ("a\x7F", CERVER_JSON_FLAGS_NONE, "\"a\x7F\""),
	// a slash, only with its flag
	WRITER_TEST_STRING ("a/b", CERVER_JSON_FLAGS_NONE, "\"a/b\""),
	WRITER_TEST_STRING ("a/b", CERVER_JSON_WRITE_ESCAPE_SLASH, "\"a\\/b\""),
	WRITER_TEST_STRING ("</script>", CERVER_JSON_WRITE_ESCAPE_SLASH, "\"<\\/script>\""),
	// the characters that are not ASCII as they are, or as units with the flag
	WRITER_TEST_STRING ("\xC3\xA9", CERVER_JSON_FLAGS_NONE, "\"\xC3\xA9\""),
	WRITER_TEST_STRING ("\xC3\xA9", CERVER_JSON_WRITE_ENSURE_ASCII, "\"\\u00E9\""),
	WRITER_TEST_STRING ("\xC2\x80\xDF\xBF", CERVER_JSON_WRITE_ENSURE_ASCII, "\"\\u0080\\u07FF\""),
	WRITER_TEST_STRING ("\xE0\xA0\x80\xE2\x82\xAC\xEF\xBF\xBF", CERVER_JSON_WRITE_ENSURE_ASCII, "\"\\u0800\\u20AC\\uFFFF\""),
	WRITER_TEST_STRING ("\xF0\x90\x80\x80", CERVER_JSON_WRITE_ENSURE_ASCII, "\"\\uD800\\uDC00\""),
	WRITER_TEST_STRING ("\xF0\x9F\x98\x80", CERVER_JSON_WRITE_ENSURE_ASCII, "\"\\uD83D\\uDE00\""),
	WRITER_TEST_STRING ("\xF4\x8F\xBF\xBF", CERVER_JSON_WRITE_ENSURE_ASCII, "\"\\uDBFF\\uDFFF\""),
	WRITER_TEST_STRING ("a\xF0\x9F\x98\x80" "b\n", CERVER_JSON_WRITE_ENSURE_ASCII, "\"a\\uD83D\\uDE00b\\n\""),
	WRITER_TEST_STRING ("espa\xC3\xB1ol \xE6\x97\xA5", CERVER_JSON_FLAGS_NONE, "\"espa\xC3\xB1ol \xE6\x97\xA5\""),
	WRITER_TEST_STRING ("ascii only", CERVER_JSON_WRITE_ENSURE_ASCII | CERVER_JSON_WRITE_ESCAPE_SLASH, "\"ascii only\"")
};

static void test_writer_strings (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	for (size_t i = 0; i < (sizeof (writer_test_strings) / sizeof (writer_test_strings[0])); i++) {
		const WriterTestString *string = &writer_test_strings[i];

		cerver_json_writer_init (&writer, buffer, sizeof (buffer), string->flags);
		cerver_json_writer_string (&writer, string->bytes, string->length);
		writer_test_expect (&writer, string->text);

		// a key is written like a string
		cerver_json_writer_init (&writer, buffer, sizeof (buffer), string->flags | CERVER_JSON_WRITE_COMPACT);
		cerver_json_writer_object_begin (&writer);
		cerver_json_writer_key_bytes (&writer, string->bytes, string->length);
		cerver_json_writer_null (&writer);
		cerver_json_writer_object_end (&writer);

		test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);
		test_check_true (!strncmp (buffer + 1, string->text, strlen (string->text)));
	}

}

// writes a string of letters with some bytes in a position and checks what was written around them
static void writer_test_position (
	const WriterTestPlace *place, const size_t length, const size_t position, const unsigned int flags
) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	char expected[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	const char *escape = (flags & CERVER_JSON_WRITE_ESCAPE_SLASH) ? place->slashed : place->text;

	// with its exact size: a read after its last byte is seen by the tools that look at the memory
	char *string = (char *) malloc (length);
	test_check_ptr (string);

	(void) memset (string, WRITER_TEST_LETTER, length);
	(void) memcpy (string + position, place->bytes, place->size);

	size_t size = 0;
	expected[size++] = '"';
	(void) memset (expected + size, WRITER_TEST_LETTER, position);
	size += position;
	(void) memcpy (expected + size, escape, strlen (escape));
	size += strlen (escape);
	(void) memset (expected + size, WRITER_TEST_LETTER, length - position - place->size);
	size += length - position - place->size;
	expected[size++] = '"';
	expected[size] = '\0';

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), flags);
	cerver_json_writer_string (&writer, string, length);
	writer_test_expect (&writer, expected);

	// and with letters after its last byte: they are not part of the string
	char longer[WRITER_TEST_STRING_LENGTH + WRITER_TEST_STRING_LENGTH];
	(void) memset (longer, WRITER_TEST_LETTER, sizeof (longer));
	(void) memcpy (longer, string, length);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), flags);
	cerver_json_writer_string (&writer, longer, length);
	writer_test_expect (&writer, expected);

	free (string);

}

// the bytes that end the part of a string that is written as it is, in every position of strings of every
// length: the ones before them are looked at some at a time
static void test_writer_strings_positions (void) {

	static const WriterTestPlace places[] = {
		{ "\"", 1, "\\\"", "\\\"" },
		{ "\\", 1, "\\\\", "\\\\" },
		{ "\n", 1, "\\n", "\\n" },
		{ "\x01", 1, "\\u0001", "\\u0001" },
		{ "\x1F", 1, "\\u001F", "\\u001F" },
		{ "\0", 1, "\\u0000", "\\u0000" },
		{ "/", 1, "/", "\\/" },
		{ " ", 1, " ", " " },
		{ "\x7F", 1, "\x7F", "\x7F" },
		{ "\xC3\xA9", 2, "\xC3\xA9", "\xC3\xA9" }
	};

	for (size_t i = 0; i < (sizeof (places) / sizeof (places[0])); i++) {
		for (size_t length = places[i].size; length <= WRITER_TEST_STRING_LENGTH; length++) {
			for (size_t position = 0; (position + places[i].size) <= length; position++) {
				writer_test_position (&places[i], length, position, CERVER_JSON_FLAGS_NONE);
				writer_test_position (&places[i], length, position, CERVER_JSON_WRITE_ESCAPE_SLASH);
			}
		}
	}

}

// a string that is not UTF-8 is not written: the text would not be JSON
static void test_writer_strings_encoding (void) {

	static const char *invalid[] = {
		"\x80", "a\xC3", "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xFF", "\xE2\x82", "ok\xF0\x9F\x98"
	};

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	for (size_t i = 0; i < (sizeof (invalid) / sizeof (invalid[0])); i++) {
		// the bytes of ASCII before the ones that are not valid
		size_t valid = 0;
		while (invalid[i][valid] && !(invalid[i][valid] & WRITER_TEST_NOT_ASCII)) valid += 1;

		cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
		cerver_json_writer_string (&writer, invalid[i], strlen (invalid[i]));

		// the writer stops there: its quote and the bytes before, and nothing more whatever is written after
		test_check_unsigned_eq (cerver_json_writer_length (&writer), 1 + valid, NULL);
		test_check_true (!strncmp (buffer + 1, invalid[i], valid));
		test_check_unsigned_eq (strlen (buffer), 1 + valid, NULL);

		cerver_json_writer_string (&writer, "more", strlen ("more"));
		cerver_json_writer_null (&writer);
		cerver_json_writer_raw (&writer, "1", 1);
		cerver_json_writer_array_begin (&writer);
		cerver_json_writer_array_end (&writer);

		// what would be another error does not take the place of the first one
		cerver_json_writer_real (&writer, (double) NAN);
		cerver_json_writer_string (&writer, NULL, 0);
		cerver_json_writer_raw (&writer, NULL, 0);
		cerver_json_writer_value (&writer, NULL);

		test_check_unsigned_eq (cerver_json_writer_length (&writer), 1 + valid, NULL);
		test_check_unsigned_eq (strlen (buffer), 1 + valid, NULL);
		test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ENCODING, NULL);
		test_check_unsigned_eq (cerver_json_writer_length (&writer), 1 + valid, NULL);

		cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_WRITE_ENSURE_ASCII);
		cerver_json_writer_object_begin (&writer);
		cerver_json_writer_key (&writer, invalid[i]);
		cerver_json_writer_null (&writer);
		cerver_json_writer_object_end (&writer);
		test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ENCODING, NULL);
	}

}

#pragma endregion

#pragma region state

// what is written out of order leaves the writer with its error, whatever comes after
static void test_writer_state (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	// a text without a value
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// two values at the top
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_integer (&writer, 2);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_array_end (&writer);
	cerver_json_writer_object_begin (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// an array or an object that is still open
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_integer (&writer, 1);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// a key outside an object
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_key (&writer, "a");
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_key (&writer, "a");
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_array_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// a value of an object without its key, and a key after another
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_object_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, "a");
	cerver_json_writer_key (&writer, "b");
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_object_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// an object that ends after a key
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, "a");
	cerver_json_writer_object_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// an end that is not the one of what is open, and an end of nothing
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_object_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_array_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_object_end (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_STATE, NULL);

	// the first error stays, and nothing more is written
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_real (&writer, (double) NAN);

	const size_t length = cerver_json_writer_length (&writer);

	cerver_json_writer_key (&writer, "a");
	cerver_json_writer_integer (&writer, 1);
	cerver_json_writer_string (&writer, "\xFF", 1);
	cerver_json_writer_array_end (&writer);
	cerver_json_writer_array_end (&writer);

	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_RANGE, NULL);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_RANGE, NULL);
	test_check_unsigned_eq (cerver_json_writer_length (&writer), length, NULL);

}

static void test_writer_arguments (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	// every method takes a NULL writer
	cerver_json_writer_init (NULL, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_init_arena (NULL, NULL, CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_set_indent (NULL, 2);
	cerver_json_writer_object_begin (NULL);
	cerver_json_writer_object_end (NULL);
	cerver_json_writer_array_begin (NULL);
	cerver_json_writer_array_end (NULL);
	cerver_json_writer_key (NULL, "a");
	cerver_json_writer_key_bytes (NULL, "a", 1);
	cerver_json_writer_string (NULL, "a", 1);
	cerver_json_writer_null (NULL);
	cerver_json_writer_boolean (NULL, true);
	cerver_json_writer_integer (NULL, 1);
	cerver_json_writer_unsigned (NULL, 1);
	cerver_json_writer_real (NULL, 1.0);
	cerver_json_writer_raw (NULL, "1", 1);
	cerver_json_writer_value (NULL, NULL);
	test_check_unsigned_eq (cerver_json_writer_end (NULL), CERVER_JSON_ERROR_ARGUMENTS, NULL);
	test_check_null_ptr (cerver_json_writer_text (NULL));
	test_check_unsigned_eq (cerver_json_writer_length (NULL), 0, NULL);

	// what a method needs
	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_string (&writer, NULL, 0);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ARGUMENTS, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, NULL);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ARGUMENTS, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_raw (&writer, NULL, 1);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ARGUMENTS, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_raw (&writer, "1", 0);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ARGUMENTS, NULL);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_value (&writer, NULL);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ARGUMENTS, NULL);

	cerver_json_writer_init_arena (&writer, NULL, CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_integer (&writer, 1);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_ARGUMENTS, NULL);

}

// the levels that a writer can have open
static void test_writer_depth (void) {

	// an array is two bytes and an object with its key is six
	const size_t size = CERVER_JSON_WRITER_DEPTH * WRITER_TEST_LEVEL_SIZE;

	char *buffer = (char *) malloc (size);
	test_check_ptr (buffer);

	CerverJsonWriter writer;

	cerver_json_writer_init (&writer, buffer, size, CERVER_JSON_FLAGS_NONE);
	for (size_t i = 0; i < CERVER_JSON_WRITER_DEPTH; i++) {
		if (i % 2) {
			cerver_json_writer_object_begin (&writer);
			cerver_json_writer_key (&writer, "");
		}

		else {
			cerver_json_writer_array_begin (&writer);
		}
	}

	cerver_json_writer_null (&writer);

	for (size_t i = CERVER_JSON_WRITER_DEPTH; i > 0; i--) {
		if ((i - 1) % 2) cerver_json_writer_object_end (&writer);
		else cerver_json_writer_array_end (&writer);
	}

	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

	// one more
	cerver_json_writer_init (&writer, buffer, size, CERVER_JSON_FLAGS_NONE);
	for (size_t i = 0; i <= CERVER_JSON_WRITER_DEPTH; i++) cerver_json_writer_array_begin (&writer);

	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_DEPTH, NULL);

	free (buffer);

}

#pragma endregion

#pragma region buffers

// a buffer of the caller: the text is cut with its NUL and the writer keeps counting
static void test_writer_truncated (void) {

	static const char expected[] = "{\"name\": \"cerver\", \"values\": [1, 2, 3], \"nested\": {\"ok\": true, \"none\": null, \"empty\": []}, \"last\": -1.5}";

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	for (size_t capacity = 0; capacity <= (sizeof (expected) + 1); capacity++) {
		(void) memset (buffer, 'x', sizeof (buffer));

		cerver_json_writer_init (&writer, buffer, capacity, CERVER_JSON_FLAGS_NONE);
		writer_test_body (&writer);

		const CerverJsonErrorCode error = cerver_json_writer_end (&writer);

		test_check_unsigned_eq (cerver_json_writer_length (&writer), sizeof (expected) - 1, NULL);

		if (capacity >= sizeof (expected)) {
			test_check_unsigned_eq (error, CERVER_JSON_ERROR_NONE, NULL);
			test_check_str_eq (buffer, expected, NULL);
		}

		else {
			test_check_unsigned_eq (error, CERVER_JSON_ERROR_TRUNCATED, NULL);

			// what fits and its NUL, and nothing after the buffer
			if (capacity) {
				test_check_true (!memcmp (buffer, expected, capacity - 1));
				test_check_true (buffer[capacity - 1] == '\0');
			}
		}

		test_check_true (buffer[capacity] == 'x');
	}

	// without a buffer the writer only counts
	cerver_json_writer_init (&writer, NULL, 0, CERVER_JSON_FLAGS_NONE);
	writer_test_body (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_TRUNCATED, NULL);
	test_check_unsigned_eq (cerver_json_writer_length (&writer), sizeof (expected) - 1, NULL);
	test_check_null_ptr (cerver_json_writer_text (&writer));

	// a NULL buffer has no bytes, whatever its capacity says
	cerver_json_writer_init (&writer, NULL, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	writer_test_body (&writer);
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_TRUNCATED, NULL);

}

// in an arena the buffer grows with the text
static void test_writer_arena (void) {

	CerverJsonWriter writer;
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	cerver_json_writer_init_arena (&writer, arena, CERVER_JSON_FLAGS_NONE);
	test_check_null_ptr (cerver_json_writer_text (&writer));

	writer_test_body (&writer);

	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);
	test_check_str_eq (
		cerver_json_writer_text (&writer),
		"{\"name\": \"cerver\", \"values\": [1, 2, 3], \"nested\": {\"ok\": true, \"none\": null, \"empty\": []}, \"last\": -1.5}",
		NULL
	);

	// a text that outgrows its buffer many times, and a chunk of the arena
	arena_reset (arena);

	cerver_json_writer_init_arena (&writer, arena, CERVER_JSON_WRITE_COMPACT);
	cerver_json_writer_array_begin (&writer);
	for (size_t i = 0; i < WRITER_TEST_LONG_ELEMENTS; i++) {
		cerver_json_writer_object_begin (&writer);
		cerver_json_writer_key (&writer, "index");
		cerver_json_writer_unsigned (&writer, i);
		cerver_json_writer_key (&writer, "text");
		cerver_json_writer_string (&writer, "a string of some bytes", strlen ("a string of some bytes"));
		cerver_json_writer_object_end (&writer);
	}

	cerver_json_writer_array_end (&writer);

	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

	const char *text = cerver_json_writer_text (&writer);
	test_check_unsigned_eq (strlen (text), cerver_json_writer_length (&writer), NULL);

	// the text is read back with every element
	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, cerver_json_writer_length (&writer), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	test_check_ptr (document);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_array_size (root), WRITER_TEST_LONG_ELEMENTS, NULL);
	test_check_true (
		cerver_json_unsigned_value (
			cerver_json_object_get (cerver_json_array_get (root, WRITER_TEST_LONG_ELEMENTS - 1), "index")
		) == (WRITER_TEST_LONG_ELEMENTS - 1)
	);

	cerver_json_document_delete (document);

	arena_delete (arena);

}

#pragma endregion

#pragma region documents

static const char *writer_test_documents[] = {
	"null", "true", "false", "0", "-7", "18446744073709551615", "1.5", "\"text\"", "[]", "{}",
	"[1, 2, 3]", "{\"a\": 1}", "{\"a\": {\"b\": {\"c\": [1, [2, [3, {}]]]}}}",
	"[null, true, false, 0, -1, 1.5, \"s\", [], {}]",
	"{\"name\": \"cerver\", \"values\": [1, 2, 3], \"nested\": {\"ok\": true, \"none\": null}, \"last\": -1.5}",
	"{\"escaped\": \"a\\\"b\\\\c\\nd\\u0001\", \"unicode\": \"\xC3\xA9\xF0\x9F\x98\x80\", \"\": \"\"}",
	"{\"a\": 1, \"a\": 2, \"b\": [{\"a\": 1, \"a\": 2}]}",
	"[[[[[[[[[[]]]]]]]]]]", "[1e21, 1e-7, 0.1, 100.0, -0.0]"
};

// a value of a document is written as the text that it came from, when that text was the default one
static void test_writer_values (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];

	for (size_t i = 0; i < (sizeof (writer_test_documents) / sizeof (writer_test_documents[0])); i++) {
		const char *text = writer_test_documents[i];

		CerverJsonDocument *document = json_test_parse (text);
		const CerverJsonValue *root = cerver_json_document_root (document);

		const size_t length = cerver_json_write (root, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
		if ((length != strlen (text)) || strcmp (buffer, text)) {
			(void) fprintf (stderr, "<%s> was written as <%s>\n", text, buffer);
			exit (1);
		}

		// the compact and the indented ones are read back as the same value
		static const unsigned int indents[] = { 0, 3 };
		for (size_t indent = 0; indent < (sizeof (indents) / sizeof (indents[0])); indent++) {
			CerverJsonWriter writer;
			cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_WRITE_COMPACT);
			cerver_json_writer_set_indent (&writer, indents[indent]);
			cerver_json_writer_value (&writer, root);
			test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

			CerverJsonDocument *again = json_test_parse (buffer);
			test_check_true (cerver_json_equal (root, cerver_json_document_root (again)));
			cerver_json_document_delete (again);
		}

		// the bytes that the text needs, asked without a buffer
		test_check_unsigned_eq (cerver_json_write (root, NULL, 0, CERVER_JSON_FLAGS_NONE), strlen (text), NULL);

		// and a buffer where it does not fit
		if (strlen (text) > 1) {
			test_check_unsigned_eq (cerver_json_write (root, buffer, 2, CERVER_JSON_FLAGS_NONE), strlen (text), NULL);
			test_check_true ((buffer[0] == text[0]) && (buffer[1] == '\0'));
		}

		cerver_json_document_delete (document);
	}

	test_check_unsigned_eq (cerver_json_write (NULL, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE), 0, NULL);

}

// a value inside a text that is written by hand
static void test_writer_values_inside (void) {

	char buffer[WRITER_TEST_BUFFER_SIZE];
	CerverJsonWriter writer;

	CerverJsonDocument *document = json_test_parse ("{\"a\": [1, 2], \"b\": \"text\"}");
	const CerverJsonValue *root = cerver_json_document_root (document);

	cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, "document");
	cerver_json_writer_value (&writer, root);
	cerver_json_writer_key (&writer, "values");
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_value (&writer, cerver_json_object_get (root, "a"));
	cerver_json_writer_value (&writer, cerver_json_object_get (root, "b"));
	cerver_json_writer_array_end (&writer);
	cerver_json_writer_object_end (&writer);

	writer_test_expect (
		&writer, "{\"document\": {\"a\": [1, 2], \"b\": \"text\"}, \"values\": [[1, 2], \"text\"]}"
	);

	cerver_json_document_delete (document);

}

// a document with more levels than the writer has
static void test_writer_values_depth (void) {

	const CerverJsonLimits limits = { CERVER_JSON_WRITER_DEPTH + 1, 0, 0 };
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	const size_t size = ((CERVER_JSON_WRITER_DEPTH + 1) * 2) + 1;

	char *text = (char *) malloc (size);
	char *buffer = (char *) malloc (size);
	test_check_ptr (text);
	test_check_ptr (buffer);

	(void) memset (text, '[', CERVER_JSON_WRITER_DEPTH + 1);
	(void) memset (text + CERVER_JSON_WRITER_DEPTH + 1, ']', CERVER_JSON_WRITER_DEPTH + 1);
	text[size - 1] = '\0';

	CerverJsonDocument *document = cerver_json_parse (NULL, text, size - 1, CERVER_JSON_FLAGS_NONE, &limits, &error);
	test_check_ptr (document);

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, buffer, size, CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_value (&writer, cerver_json_document_root (document));
	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_DEPTH, NULL);

	test_check_unsigned_eq (
		cerver_json_write (cerver_json_document_root (document), buffer, size, CERVER_JSON_FLAGS_NONE), 0, NULL
	);

	// the array inside the first one fits
	test_check_unsigned_eq (
		cerver_json_write (
			cerver_json_array_get (cerver_json_document_root (document), 0), buffer, size, CERVER_JSON_FLAGS_NONE
		),
		size - 3, NULL
	);

	cerver_json_document_delete (document);

	free (buffer);
	free (text);

}

// the texts that the JSON of the responses of cerver writes today: the same bytes
static void test_writer_compatible (void) {

	static const char *texts[] = {
		"{\"value\": 7}", "{\"value\": 4102444800}", "{\"value\": 1.5}", "{\"value\": \"text\"}",
		"{\"value\": true}", "{\"oki\": \"doki\"}", "{\"error\": \"Bad request!\"}",
		"{\"name\": \"cerver\", \"values\": [1, 2, 3], \"nested\": {\"ok\": true, \"none\": null}}",
		"[]", "{}", "[1, \"two\", [3, {\"four\": 4}], {}, []]",
		"{\"escaped\": \"a\\\"b\\\\c\\n\\td\\u0001/\", \"unicode\": \"\xC3\xA9\xF0\x9F\x98\x80\"}",
		"{\"token\": \"eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.e30.c2lnbmF0dXJl\"}",
		"[0.5, 2.25, -100.125, 1e+100, 1024.0]"
	};

	static const size_t flags[][2] = {
		{ 0, CERVER_JSON_FLAGS_NONE },
		{ JSON_COMPACT, CERVER_JSON_WRITE_COMPACT },
		{ JSON_ESCAPE_SLASH, CERVER_JSON_WRITE_ESCAPE_SLASH },
		{ JSON_ENSURE_ASCII, CERVER_JSON_WRITE_ENSURE_ASCII }
	};

	char buffer[WRITER_TEST_BUFFER_SIZE];
	json_error_t error;

	for (size_t i = 0; i < (sizeof (texts) / sizeof (texts[0])); i++) {
		CerverJsonDocument *document = json_test_parse (texts[i]);

		json_t *json = json_loads (texts[i], JSON_DECODE_ANY, &error);
		test_check_ptr (json);

		for (size_t flag = 0; flag < (sizeof (flags) / sizeof (flags[0])); flag++) {
			char *expected = json_dumps (json, flags[flag][0] | JSON_ENCODE_ANY);
			test_check_ptr (expected);

			(void) cerver_json_write (
				cerver_json_document_root (document), buffer, sizeof (buffer), (unsigned int) flags[flag][1]
			);

			if (strcmp (buffer, expected)) {
				(void) fprintf (stderr, "<%s> is written as <%s> and it was <%s>\n", texts[i], buffer, expected);
				exit (1);
			}

			free (expected);
		}

		// with each value in its line
		char *expected = json_dumps (json, JSON_INDENT (4) | JSON_ENCODE_ANY);
		test_check_ptr (expected);

		CerverJsonWriter writer;
		cerver_json_writer_init (&writer, buffer, sizeof (buffer), CERVER_JSON_FLAGS_NONE);
		cerver_json_writer_set_indent (&writer, 4);
		cerver_json_writer_value (&writer, cerver_json_document_root (document));
		test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

		if (strcmp (buffer, expected)) {
			(void) fprintf (stderr, "<%s> is written as <%s> and it was <%s>\n", texts[i], buffer, expected);
			exit (1);
		}

		free (expected);

		json_decref (json);
		cerver_json_document_delete (document);
	}

}

#pragma endregion

void json_tests_writer (void) {

	(void) printf ("\nTesting JSON writer...\n");

	test_run (test_writer_default);
	test_run (test_writer_compact);
	test_run (test_writer_indent);
	test_run (test_writer_scalars);

	test_run (test_writer_integers);
	test_run (test_writer_reals);
	test_run (test_writer_reals_powers);
	test_run (test_writer_reals_random);

	test_run (test_writer_strings);
	test_run (test_writer_strings_positions);
	test_run (test_writer_strings_encoding);

	test_run (test_writer_state);
	test_run (test_writer_arguments);
	test_run (test_writer_depth);

	test_run (test_writer_truncated);
	test_run (test_writer_arena);

	test_run (test_writer_values);
	test_run (test_writer_values_inside);
	test_run (test_writer_values_depth);
	test_run (test_writer_compatible);

}
