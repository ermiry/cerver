#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include "cerver/json/internal.h"

#include "../test.h"
#include "json.h"

// the longest string of the tests that move a byte through every position of a word and of a vector
#define STRINGS_TEST_LENGTH					(1 << 7)

#define STRINGS_TEST_TEXT_SIZE				((STRINGS_TEST_LENGTH * 2) + CERVER_JSON_PADDING)

// a text with a string and the bytes that the string must have
typedef struct StringsTestValue {

	const char *text;
	const char *bytes;
	size_t length;

} StringsTestValue;

// a text that is rejected, with its bytes (it can have any byte inside)
typedef struct StringsTestError {

	const char *text;
	size_t length;
	CerverJsonErrorCode code;
	size_t offset;

} StringsTestError;

#define STRINGS_TEST_VALUE(text, bytes)		{ text, bytes, sizeof (bytes) - 1 }

#define STRINGS_TEST_ERROR(text, code, offset)	{ text, sizeof (text) - 1, code, offset }

#pragma region values

static const StringsTestValue strings_test_values[] = {
	STRINGS_TEST_VALUE ("\"\"", ""),
	STRINGS_TEST_VALUE ("\"a\"", "a"),
	STRINGS_TEST_VALUE ("\"cerver\"", "cerver"),
	STRINGS_TEST_VALUE ("\" spaces  inside \"", " spaces  inside "),
	// the delete character and the slash are bytes like the others
	STRINGS_TEST_VALUE ("\"a\x7F/b\"", "a\x7F/b"),
	// every escape of one letter
	STRINGS_TEST_VALUE ("\"\\\"\"", "\""),
	STRINGS_TEST_VALUE ("\"\\\\\"", "\\"),
	STRINGS_TEST_VALUE ("\"\\/\"", "/"),
	STRINGS_TEST_VALUE ("\"\\b\\f\\n\\r\\t\"", "\b\f\n\r\t"),
	STRINGS_TEST_VALUE ("\"a\\nb\\tc\"", "a\nb\tc"),
	STRINGS_TEST_VALUE ("\"\\\\n\"", "\\n"),
	STRINGS_TEST_VALUE ("\"quote \\\" inside\"", "quote \" inside"),
	STRINGS_TEST_VALUE ("\"ends with a backslash \\\\\"", "ends with a backslash \\"),
	// the escapes of a unit, with digits in both cases
	STRINGS_TEST_VALUE ("\"\\u0041\"", "A"),
	STRINGS_TEST_VALUE ("\"\\u0061\\u0062\"", "ab"),
	STRINGS_TEST_VALUE ("\"\\u00e9\"", "\xC3\xA9"),
	STRINGS_TEST_VALUE ("\"\\u00E9\"", "\xC3\xA9"),
	STRINGS_TEST_VALUE ("\"\\u007f\"", "\x7F"),
	STRINGS_TEST_VALUE ("\"\\u0080\"", "\xC2\x80"),
	STRINGS_TEST_VALUE ("\"\\u07ff\"", "\xDF\xBF"),
	STRINGS_TEST_VALUE ("\"\\u0800\"", "\xE0\xA0\x80"),
	STRINGS_TEST_VALUE ("\"\\u20AC\"", "\xE2\x82\xAC"),
	STRINGS_TEST_VALUE ("\"\\ud7ff\"", "\xED\x9F\xBF"),
	STRINGS_TEST_VALUE ("\"\\ue000\"", "\xEE\x80\x80"),
	STRINGS_TEST_VALUE ("\"\\uffff\"", "\xEF\xBF\xBF"),
	STRINGS_TEST_VALUE ("\"\\uFFFE\"", "\xEF\xBF\xBE"),
	// a pair of units is one character
	STRINGS_TEST_VALUE ("\"\\ud800\\udc00\"", "\xF0\x90\x80\x80"),
	STRINGS_TEST_VALUE ("\"\\uD83D\\uDE00\"", "\xF0\x9F\x98\x80"),
	STRINGS_TEST_VALUE ("\"\\udbff\\udfff\"", "\xF4\x8F\xBF\xBF"),
	STRINGS_TEST_VALUE ("\"a\\uD83D\\uDE00b\"", "a\xF0\x9F\x98\x80" "b"),
	// an escaped NUL is part of the string
	STRINGS_TEST_VALUE ("\"\\u0000\"", "\0"),
	STRINGS_TEST_VALUE ("\"a\\u0000b\"", "a\0b"),
	// the characters that are not ASCII, as they are
	STRINGS_TEST_VALUE ("\"\xC3\xA9\"", "\xC3\xA9"),
	STRINGS_TEST_VALUE ("\"\xC2\x80\xDF\xBF\"", "\xC2\x80\xDF\xBF"),
	STRINGS_TEST_VALUE ("\"\xE0\xA0\x80\xEF\xBF\xBF\"", "\xE0\xA0\x80\xEF\xBF\xBF"),
	STRINGS_TEST_VALUE ("\"\xED\x9F\xBF\xEE\x80\x80\"", "\xED\x9F\xBF\xEE\x80\x80"),
	STRINGS_TEST_VALUE ("\"\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\"", "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"),
	STRINGS_TEST_VALUE ("\"espa\xC3\xB1ol \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80\"", "espa\xC3\xB1ol \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80"),
	// escapes and characters that are not ASCII together: the bytes after an escape move
	STRINGS_TEST_VALUE ("\"\\n\xC3\xA9\\u00e9\xC3\xA9\\t\"", "\n\xC3\xA9\xC3\xA9\xC3\xA9\t"),
	STRINGS_TEST_VALUE ("\"\\u0041 a long tail of plain bytes after the escape\"", "A a long tail of plain bytes after the escape"),
	STRINGS_TEST_VALUE ("\"a long head of plain bytes before the escape \\u0041\"", "a long head of plain bytes before the escape A")
};

static void test_strings_values (void) {

	const size_t count = sizeof (strings_test_values) / sizeof (strings_test_values[0]);

	for (size_t i = 0; i < count; i++) {
		const StringsTestValue *expected = &strings_test_values[i];

		CerverJsonDocument *document = json_test_parse (expected->text);
		const CerverJsonValue *root = cerver_json_document_root (document);

		if (
			!cerver_json_is_string (root)
			|| (cerver_json_string_length (root) != expected->length)
			|| memcmp (cerver_json_string_value (root), expected->bytes, expected->length)
			|| cerver_json_string_value (root)[expected->length]
		) {
			(void) fprintf (stderr, "<%s> does not have the expected %zu bytes\n", expected->text, expected->length);
			exit (1);
		}

		cerver_json_document_delete (document);
	}

}

// the keys of an object are strings like the others
static void test_strings_keys (void) {

	CerverJsonDocument *document = json_test_parse (
		"{\"\\u006b\\u0065y\": 1, \"\xC3\xA9\": 2, \"a\\\"b\": 3, \"\": 4}"
	);

	const CerverJsonValue *root = cerver_json_document_root (document);

	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "key")) == 1);
	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "\xC3\xA9")) == 2);
	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "a\"b")) == 3);
	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "")) == 4);

	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region errors

static const StringsTestError strings_test_errors[] = {
	// a string that the text cut
	STRINGS_TEST_ERROR ("\"", CERVER_JSON_ERROR_END, 1),
	STRINGS_TEST_ERROR ("\"abc", CERVER_JSON_ERROR_END, 4),
	STRINGS_TEST_ERROR ("\"abc\\", CERVER_JSON_ERROR_END, 5),
	STRINGS_TEST_ERROR ("\"abc\\\"", CERVER_JSON_ERROR_END, 6),
	STRINGS_TEST_ERROR ("\"\\u", CERVER_JSON_ERROR_END, 3),
	STRINGS_TEST_ERROR ("\"\\u00", CERVER_JSON_ERROR_END, 5),
	STRINGS_TEST_ERROR ("[\"a\", \"b", CERVER_JSON_ERROR_END, 8),
	STRINGS_TEST_ERROR ("{\"a", CERVER_JSON_ERROR_END, 3),
	// a control character that is not escaped
	STRINGS_TEST_ERROR ("\"a\nb\"", CERVER_JSON_ERROR_CONTROL, 2),
	STRINGS_TEST_ERROR ("\"a\tb\"", CERVER_JSON_ERROR_CONTROL, 2),
	STRINGS_TEST_ERROR ("\"\r\"", CERVER_JSON_ERROR_CONTROL, 1),
	STRINGS_TEST_ERROR ("\"\x01\"", CERVER_JSON_ERROR_CONTROL, 1),
	STRINGS_TEST_ERROR ("\"\x1F\"", CERVER_JSON_ERROR_CONTROL, 1),
	STRINGS_TEST_ERROR ("\"a\0b\"", CERVER_JSON_ERROR_CONTROL, 2),
	STRINGS_TEST_ERROR ("\"a long head of plain bytes\x08\"", CERVER_JSON_ERROR_CONTROL, 27),
	STRINGS_TEST_ERROR ("\"\\n\x0B\"", CERVER_JSON_ERROR_CONTROL, 3),
	// an escape that is not one
	STRINGS_TEST_ERROR ("\"\\a\"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"\\x41\"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"\\0\"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"\\'\"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"\\U0041\"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"\\ \"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"\\\xC3\xA9\"", CERVER_JSON_ERROR_ESCAPE, 2),
	STRINGS_TEST_ERROR ("\"ab\\N\"", CERVER_JSON_ERROR_ESCAPE, 4),
	// the digits of a unit
	STRINGS_TEST_ERROR ("\"\\u004\"", CERVER_JSON_ERROR_ESCAPE, 6),
	STRINGS_TEST_ERROR ("\"\\u00g1\"", CERVER_JSON_ERROR_ESCAPE, 5),
	STRINGS_TEST_ERROR ("\"\\uzzzz\"", CERVER_JSON_ERROR_ESCAPE, 3),
	STRINGS_TEST_ERROR ("\"\\u 041\"", CERVER_JSON_ERROR_ESCAPE, 3),
	STRINGS_TEST_ERROR ("\"\\u-041\"", CERVER_JSON_ERROR_ESCAPE, 3),
	STRINGS_TEST_ERROR ("\"\\u00:1\"", CERVER_JSON_ERROR_ESCAPE, 5),
	STRINGS_TEST_ERROR ("\"\\u00`1\"", CERVER_JSON_ERROR_ESCAPE, 5),
	STRINGS_TEST_ERROR ("\"\\u00@1\"", CERVER_JSON_ERROR_ESCAPE, 5),
	STRINGS_TEST_ERROR ("\"\\ud83d\\ude0\"", CERVER_JSON_ERROR_ESCAPE, 12),
	// a surrogate without its pair
	STRINGS_TEST_ERROR ("\"\\ud800\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\udbff\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\udc00\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\udfff\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\ud800a\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\ud800\\n\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\ud800\\u0041\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\ud800\\ud800\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\ud800\\ue000\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"\\udc00\\ud800\"", CERVER_JSON_ERROR_SURROGATE, 3),
	STRINGS_TEST_ERROR ("\"a\\ud83db\"", CERVER_JSON_ERROR_SURROGATE, 4),
	// bytes that are not UTF-8: a byte that continues nothing, a character that is cut
	STRINGS_TEST_ERROR ("\"\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xBF\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"a\xC3\"", CERVER_JSON_ERROR_ENCODING, 2),
	STRINGS_TEST_ERROR ("\"\xC3(\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xE2\x82\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xE2\x82(\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF0\x9F\x98\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF0\x9F\x98(\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xC3\xA9\x80\"", CERVER_JSON_ERROR_ENCODING, 3),
	// a character with more bytes than it needs
	STRINGS_TEST_ERROR ("\"\xC0\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xC1\xBF\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xE0\x80\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xE0\x9F\xBF\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF0\x80\x80\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF0\x8F\xBF\xBF\"", CERVER_JSON_ERROR_ENCODING, 1),
	// the surrogates and what is over the last character
	STRINGS_TEST_ERROR ("\"\xED\xA0\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xED\xBF\xBF\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF4\x90\x80\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF5\x80\x80\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xF8\x88\x80\x80\x80\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xFE\"", CERVER_JSON_ERROR_ENCODING, 1),
	STRINGS_TEST_ERROR ("\"\xFF\"", CERVER_JSON_ERROR_ENCODING, 1),
	// in a key and after an escape
	STRINGS_TEST_ERROR ("{\"\xFF\": 1}", CERVER_JSON_ERROR_ENCODING, 2),
	STRINGS_TEST_ERROR ("\"\\n\xFF\"", CERVER_JSON_ERROR_ENCODING, 3),
	// a string does not end with another quote
	STRINGS_TEST_ERROR ("'a'", CERVER_JSON_ERROR_CHARACTER, 0),
	STRINGS_TEST_ERROR ("\"a\"\"", CERVER_JSON_ERROR_TRAILING, 3)
};

static void test_strings_errors (void) {

	const size_t count = sizeof (strings_test_errors) / sizeof (strings_test_errors[0]);

	for (size_t i = 0; i < count; i++) {
		const StringsTestError *expected = &strings_test_errors[i];

		json_test_rejects (expected->text, expected->length, CERVER_JSON_FLAGS_NONE, expected->code, expected->offset);
	}

}

// an escaped NUL is an error only with its flag, in a string and in a key
static void test_strings_reject_nul (void) {

	static const char string[] = "[\"a\\u0000b\"]";
	static const char key[] = "{\"a\\u0000\": 1}";
	static const char other[] = "[\"a\\u0001b\", \"\\u0030\"]";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	json_test_rejects (string, strlen (string), CERVER_JSON_PARSE_REJECT_NUL, CERVER_JSON_ERROR_NUL, 5);
	json_test_rejects (key, strlen (key), CERVER_JSON_PARSE_REJECT_NUL, CERVER_JSON_ERROR_NUL, 5);

	CerverJsonDocument *document = cerver_json_parse (
		NULL, other, strlen (other), CERVER_JSON_PARSE_REJECT_NUL, NULL, &error
	);

	test_check_ptr (document);
	test_check_str_eq (cerver_json_string_value (cerver_json_array_get (cerver_json_document_root (document), 1)), "0", NULL);
	cerver_json_document_delete (document);

	document = json_test_parse (string);
	test_check_unsigned_eq (
		cerver_json_string_length (cerver_json_array_get (cerver_json_document_root (document), 0)), 3, NULL
	);

	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region lengths

// a plain string of each length, with the quote that ends it at every position of a word
static void test_strings_lengths (void) {

	char text[STRINGS_TEST_TEXT_SIZE];

	for (size_t length = 0; length <= STRINGS_TEST_LENGTH; length++) {
		text[0] = '"';
		for (size_t i = 0; i < length; i++) text[1 + i] = (char) ('a' + (i % 26));

		text[1 + length] = '"';
		text[2 + length] = '\0';

		CerverJsonDocument *document = json_test_parse (text);
		const CerverJsonValue *root = cerver_json_document_root (document);

		test_check_unsigned_eq (cerver_json_string_length (root), length, NULL);
		test_check_true (!memcmp (cerver_json_string_value (root), text + 1, length));
		test_check_true (cerver_json_string_value (root)[length] == '\0');

		cerver_json_document_delete (document);
	}

}

// a byte that stops the plain part at every position of a string, and the text after it kept
static void test_strings_stops (void) {

	static const char *stops[] = { "\\n", "\\u0041", "\xC3\xA9", "\xF0\x9F\x98\x80", "\\\"", "\\\\" };
	static const char *results[] = { "\n", "A", "\xC3\xA9", "\xF0\x9F\x98\x80", "\"", "\\" };

	char text[STRINGS_TEST_TEXT_SIZE];
	char expected[STRINGS_TEST_TEXT_SIZE];

	for (size_t stop = 0; stop < (sizeof (stops) / sizeof (stops[0])); stop++) {
		for (size_t position = 0; position < STRINGS_TEST_LENGTH; position++) {
			size_t length = 0;
			size_t expected_length = 0;

			text[length++] = '"';
			for (size_t i = 0; i < position; i++) {
				text[length++] = (char) ('a' + (i % 26));
				expected[expected_length++] = (char) ('a' + (i % 26));
			}

			(void) memcpy (text + length, stops[stop], strlen (stops[stop]));
			length += strlen (stops[stop]);
			(void) memcpy (expected + expected_length, results[stop], strlen (results[stop]));
			expected_length += strlen (results[stop]);

			// a tail after it, so the bytes that move are checked
			for (size_t i = 0; i < (STRINGS_TEST_LENGTH - position); i++) {
				text[length++] = (char) ('A' + (i % 26));
				expected[expected_length++] = (char) ('A' + (i % 26));
			}

			text[length++] = '"';
			text[length] = '\0';

			CerverJsonDocument *document = json_test_parse (text);
			const CerverJsonValue *root = cerver_json_document_root (document);

			test_check_unsigned_eq (cerver_json_string_length (root), expected_length, NULL);
			test_check_true (!memcmp (cerver_json_string_value (root), expected, expected_length));

			cerver_json_document_delete (document);
		}
	}

}

// the scan that the readers use stops where the one that goes byte by byte does, for every
// byte at every position
static void test_strings_scan (void) {

	char text[STRINGS_TEST_TEXT_SIZE];

	for (unsigned int byte = 0; byte < (1 << 8); byte++) {
		for (size_t position = 0; position < STRINGS_TEST_LENGTH; position++) {
			(void) memset (text, 'a', STRINGS_TEST_LENGTH);
			(void) memset (text + STRINGS_TEST_LENGTH, 0, sizeof (text) - STRINGS_TEST_LENGTH);

			text[position] = (char) byte;

			const size_t expected = cerver_json_scan_string_stop (text, true);
			test_check_unsigned_eq (cerver_json_scan_string_stop (text, false), expected, NULL);

			// the byte stops the string or the NUL at the end does
			const bool stops = (byte < ' ') || (byte == '"') || (byte == '\\') || (byte >= 0x80);
			test_check_unsigned_eq (expected, stops ? position : STRINGS_TEST_LENGTH, NULL);
		}
	}

}

#pragma endregion

void json_tests_strings (void) {

	(void) printf ("\nTesting JSON strings...\n");

	test_run (test_strings_values);
	test_run (test_strings_keys);

	test_run (test_strings_errors);
	test_run (test_strings_reject_nul);

	test_run (test_strings_lengths);
	test_run (test_strings_stops);
	test_run (test_strings_scan);

}
