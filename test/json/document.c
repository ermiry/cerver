#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include "../test.h"
#include "json.h"

// a text that was rejected: its error and the position where it was found
typedef struct DocumentTestError {

	const char *text;
	CerverJsonErrorCode code;
	size_t offset;

} DocumentTestError;

// the allocations of an arena that a document takes: itself, its text and its values
#define DOCUMENT_TEST_ALLOCATIONS			3

// a depth that the default limit does not take, and what a parser that calls itself would not survive
#define DOCUMENT_TEST_DEEP					(1 << 20)

// a limit of nested arrays for the tests
#define DOCUMENT_TEST_DEPTH					8

// more elements than the values that a text starts with, so they are moved more than once
#define DOCUMENT_TEST_ELEMENTS				(1 << 12)

// more members than the ones that are compared one against the others
#define DOCUMENT_TEST_MEMBERS				(1 << 6)

#define DOCUMENT_TEST_KEY_SIZE				32

#define DOCUMENT_TEST_BUFFER_SIZE			(1 << 16)

#pragma region values

static void test_document_scalars (void) {

	CerverJsonDocument *document = json_test_parse ("null");
	test_check_unsigned_eq (cerver_json_type (cerver_json_document_root (document)), CERVER_JSON_TYPE_NULL, NULL);
	test_check_unsigned_eq (cerver_json_document_values (document), 1, NULL);
	cerver_json_document_delete (document);

	document = json_test_parse ("true");
	test_check_true (cerver_json_is_boolean (cerver_json_document_root (document)));
	test_check_true (cerver_json_boolean_value (cerver_json_document_root (document)));
	cerver_json_document_delete (document);

	document = json_test_parse ("false");
	test_check_true (cerver_json_is_boolean (cerver_json_document_root (document)));
	test_check_false (cerver_json_boolean_value (cerver_json_document_root (document)));
	cerver_json_document_delete (document);

	document = json_test_parse ("\"cerver\"");
	test_check_str_eq (cerver_json_string_value (cerver_json_document_root (document)), "cerver", NULL);
	cerver_json_document_delete (document);

	document = json_test_parse ("7");
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_document_root (document)), 7, NULL);
	cerver_json_document_delete (document);

}

// the four characters of whitespace, around a value and between the tokens
static void test_document_whitespace (void) {

	CerverJsonDocument *document = json_test_parse (" \t\r\n null \n\r\t ");
	test_check_true (cerver_json_is_null (cerver_json_document_root (document)));
	cerver_json_document_delete (document);

	document = json_test_parse (" { \"a\" \t:\n[ 1 ,\r2 ] , \"b\" : { } } ");

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), 2, NULL);
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_object_get (root, "a")), 2, NULL);
	test_check_true (cerver_json_is_object (cerver_json_object_get (root, "b")));
	test_check_unsigned_eq (cerver_json_document_values (document), 7, NULL);

	cerver_json_document_delete (document);

}

static void test_document_empty_containers (void) {

	CerverJsonDocument *document = json_test_parse ("[]");
	test_check_true (cerver_json_is_array (cerver_json_document_root (document)));
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_document_root (document)), 0, NULL);
	cerver_json_document_delete (document);

	document = json_test_parse ("{}");
	test_check_true (cerver_json_is_object (cerver_json_document_root (document)));
	test_check_unsigned_eq (cerver_json_object_size (cerver_json_document_root (document)), 0, NULL);
	cerver_json_document_delete (document);

	document = json_test_parse ("[[], {}, [[]], {\"a\": {}}]");

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_array_size (root), 4, NULL);
	test_check_true (cerver_json_is_array (cerver_json_array_get (root, 0)));
	test_check_true (cerver_json_is_object (cerver_json_array_get (root, 1)));
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_array_get (root, 2)), 1, NULL);
	test_check_unsigned_eq (cerver_json_object_size (cerver_json_array_get (root, 3)), 1, NULL);
	test_check_null_ptr (cerver_json_array_get (root, 4));

	cerver_json_document_delete (document);

}

// the body of a request to an API
static void test_document_nested (void) {

	CerverJsonDocument *document = json_test_parse (
		"{\"name\": \"cerver\", \"values\": [1, 2, 3], \"nested\": {\"ok\": true, \"none\": null}, \"last\": -1.5}"
	);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), 4, NULL);
	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (root, "name")), "cerver", NULL);

	const CerverJsonValue *values = cerver_json_object_get (root, "values");
	test_check_unsigned_eq (cerver_json_array_size (values), 3, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_array_get (values, 0)), 1, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_array_get (values, 1)), 2, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_array_get (values, 2)), 3, NULL);

	const CerverJsonValue *nested = cerver_json_object_get (root, "nested");
	test_check_true (cerver_json_boolean_value (cerver_json_object_get (nested, "ok")));
	test_check_true (cerver_json_is_null (cerver_json_object_get (nested, "none")));

	test_check_true (test_double_equal (cerver_json_real_value (cerver_json_object_get (root, "last")), -1.5));

	// the object, its four keys and their values, the three elements and the two members with their keys
	test_check_unsigned_eq (cerver_json_document_values (document), 16, NULL);

	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region errors

static const DocumentTestError document_test_errors[] = {
	// a text without a value
	{ "", CERVER_JSON_ERROR_END, 0 },
	{ " \n\t ", CERVER_JSON_ERROR_END, 4 },
	// a value that the text cut
	{ "[", CERVER_JSON_ERROR_END, 1 },
	{ "[1", CERVER_JSON_ERROR_END, 2 },
	{ "[1,", CERVER_JSON_ERROR_END, 3 },
	{ "{", CERVER_JSON_ERROR_END, 1 },
	{ "{\"a\"", CERVER_JSON_ERROR_END, 4 },
	{ "{\"a\":", CERVER_JSON_ERROR_END, 5 },
	{ "{\"a\":1", CERVER_JSON_ERROR_END, 6 },
	{ "{\"a\":1,", CERVER_JSON_ERROR_END, 7 },
	{ "[[[", CERVER_JSON_ERROR_END, 3 },
	{ "\"abc", CERVER_JSON_ERROR_END, 4 },
	{ "tru", CERVER_JSON_ERROR_END, 0 },
	{ "nul", CERVER_JSON_ERROR_END, 0 },
	{ "fals", CERVER_JSON_ERROR_END, 0 },
	{ "-", CERVER_JSON_ERROR_END, 1 },
	{ "1.", CERVER_JSON_ERROR_END, 2 },
	{ "1e", CERVER_JSON_ERROR_END, 2 },
	// a character that is not expected
	{ "[1,]", CERVER_JSON_ERROR_CHARACTER, 3 },
	{ "[,1]", CERVER_JSON_ERROR_CHARACTER, 1 },
	{ "[1 2]", CERVER_JSON_ERROR_CHARACTER, 3 },
	{ "[1}", CERVER_JSON_ERROR_CHARACTER, 2 },
	{ "{\"a\":1]", CERVER_JSON_ERROR_CHARACTER, 6 },
	{ "{\"a\":1,}", CERVER_JSON_ERROR_CHARACTER, 7 },
	{ "{,}", CERVER_JSON_ERROR_CHARACTER, 1 },
	{ "{1:2}", CERVER_JSON_ERROR_CHARACTER, 1 },
	{ "{\"a\" 1}", CERVER_JSON_ERROR_CHARACTER, 5 },
	{ "{\"a\":}", CERVER_JSON_ERROR_CHARACTER, 5 },
	{ "{\"a\"}", CERVER_JSON_ERROR_CHARACTER, 4 },
	{ "{\"a\":1 \"b\":2}", CERVER_JSON_ERROR_CHARACTER, 7 },
	{ "{null:1}", CERVER_JSON_ERROR_CHARACTER, 1 },
	{ "]", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "}", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ ":", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ ",", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "'a'", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "[1, // a comment\n2]", CERVER_JSON_ERROR_CHARACTER, 4 },
	{ "/* a comment */ 1", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "NaN", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "Infinity", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "+1", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ ".5", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "True", CERVER_JSON_ERROR_CHARACTER, 0 },
	{ "\xEF\xBB\xBF{}", CERVER_JSON_ERROR_CHARACTER, 0 },
	// a literal that is not one
	{ "nulx", CERVER_JSON_ERROR_LITERAL, 0 },
	{ "trux", CERVER_JSON_ERROR_LITERAL, 0 },
	{ "falsy", CERVER_JSON_ERROR_LITERAL, 0 },
	{ "[nil]", CERVER_JSON_ERROR_LITERAL, 1 },
	// content after the value
	{ "1 2", CERVER_JSON_ERROR_TRAILING, 2 },
	{ "{} {}", CERVER_JSON_ERROR_TRAILING, 3 },
	{ "[]]", CERVER_JSON_ERROR_TRAILING, 2 },
	{ "nullx", CERVER_JSON_ERROR_TRAILING, 4 },
	{ "truefalse", CERVER_JSON_ERROR_TRAILING, 4 },
	{ "\"a\" \"b\"", CERVER_JSON_ERROR_TRAILING, 4 },
	{ "1,", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "{}x", CERVER_JSON_ERROR_TRAILING, 2 }
};

static void test_document_errors (void) {

	const size_t count = sizeof (document_test_errors) / sizeof (document_test_errors[0]);

	for (size_t i = 0; i < count; i++) {
		const DocumentTestError *expected = &document_test_errors[i];

		json_test_rejects (
			expected->text, strlen (expected->text), CERVER_JSON_FLAGS_NONE, expected->code, expected->offset
		);
	}

}

// a NUL that is part of the text is a byte like the others: never its end
static void test_document_nul_bytes (void) {

	static const char only[] = { '\0' };
	static const char after[] = { '1', '\0', '2' };
	static const char inside[] = { '[', '1', ',', '\0', '2', ']' };
	static const char string[] = { '"', 'a', '\0', 'b', '"' };

	json_test_rejects (only, sizeof (only), CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_CHARACTER, 0);
	json_test_rejects (after, sizeof (after), CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_TRAILING, 1);
	json_test_rejects (inside, sizeof (inside), CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_CHARACTER, 3);
	json_test_rejects (string, sizeof (string), CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_CONTROL, 2);

}

// the text is read by its length: what follows it is not part of it
static void test_document_length (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	static const char text[] = "[1, 2]garbage";

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, strlen ("[1, 2]"), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_document_root (document)), 2, NULL);
	cerver_json_document_delete (document);

	document = cerver_json_parse (NULL, text, strlen ("[1, 2"), CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_null_ptr (document);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_END, NULL);

}

static void test_document_arguments (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };

	test_check_null_ptr (cerver_json_parse (NULL, NULL, 0, CERVER_JSON_FLAGS_NONE, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_ARGUMENTS, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);

	test_check_null_ptr (cerver_json_parse_in_situ (NULL, NULL, 0, CERVER_JSON_FLAGS_NONE, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_ARGUMENTS, NULL);

	// the error of a call is optional
	test_check_null_ptr (cerver_json_parse (NULL, "[", 1, CERVER_JSON_FLAGS_NONE, NULL, NULL));

	CerverJsonDocument *document = cerver_json_parse (NULL, "[]", 2, CERVER_JSON_FLAGS_NONE, NULL, NULL);
	test_check_ptr (document);
	cerver_json_document_delete (document);

	test_check_null_ptr (cerver_json_document_root (NULL));
	test_check_unsigned_eq (cerver_json_document_values (NULL), 0, NULL);
	cerver_json_document_delete (NULL);

}

static void test_document_error_position (void) {

	static const char text[] = "{\n\t\"a\": 1,\n\t\"b\": x\n}";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	size_t line = 0;
	size_t column = 0;

	test_check_null_ptr (cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_CHARACTER, NULL);

	cerver_json_error_position (text, strlen (text), error.offset, &line, &column);
	test_check_unsigned_eq (line, 3, NULL);
	test_check_unsigned_eq (column, 7, NULL);

	// the start of the text, and a position after its end
	cerver_json_error_position (text, strlen (text), 0, &line, &column);
	test_check_unsigned_eq (line, 1, NULL);
	test_check_unsigned_eq (column, 1, NULL);

	cerver_json_error_position (text, strlen (text), strlen (text) + 8, &line, &column);
	test_check_unsigned_eq (line, 4, NULL);
	test_check_unsigned_eq (column, 2, NULL);

	cerver_json_error_position (text, strlen (text), 2, NULL, NULL);

	cerver_json_error_position (NULL, 0, 4, &line, &column);
	test_check_unsigned_eq (line, 1, NULL);
	test_check_unsigned_eq (column, 1, NULL);

}

static void test_document_strings_of_enums (void) {

	test_check_str_eq (cerver_json_type_to_string (CERVER_JSON_TYPE_NULL), "Null", NULL);
	test_check_str_eq (cerver_json_type_to_string (CERVER_JSON_TYPE_OBJECT), "Object", NULL);
	test_check_str_eq (cerver_json_type_to_string ((CerverJsonType) 99), "None", NULL);

	test_check_str_eq (cerver_json_error_to_string (CERVER_JSON_ERROR_NONE), "No error", NULL);
	test_check_str_eq (
		cerver_json_error_to_string (CERVER_JSON_ERROR_TRAILING), "There is content after the value", NULL
	);
	test_check_str_eq (cerver_json_error_to_string ((CerverJsonErrorCode) 99), "No error", NULL);

}

#pragma endregion

#pragma region limits

// writes a text of nested arrays into a buffer of the test
static char *document_test_nested_arrays (const size_t depth) {

	char *text = (char *) malloc ((depth * 2) + 1);
	test_check_ptr (text);

	(void) memset (text, '[', depth);
	(void) memset (text + depth, ']', depth);
	text[depth * 2] = '\0';

	return text;

}

static void test_document_depth (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	const CerverJsonLimits limits = { DOCUMENT_TEST_DEPTH, 0, 0 };

	char *text = document_test_nested_arrays (DOCUMENT_TEST_DEPTH);

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_document_values (document), DOCUMENT_TEST_DEPTH, NULL);
	cerver_json_document_delete (document);
	free (text);

	// one more is over the limit, at the bracket that opens it
	text = document_test_nested_arrays (DOCUMENT_TEST_DEPTH + 1);
	test_check_null_ptr (cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_DEPTH, NULL);
	test_check_unsigned_eq (error.offset, DOCUMENT_TEST_DEPTH, NULL);
	free (text);

	// the default limit
	text = document_test_nested_arrays (CERVER_JSON_LIMIT_DEPTH);
	document = cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	cerver_json_document_delete (document);
	free (text);

	text = document_test_nested_arrays (CERVER_JSON_LIMIT_DEPTH + 1);
	test_check_null_ptr (cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_DEPTH, NULL);
	test_check_unsigned_eq (error.offset, CERVER_JSON_LIMIT_DEPTH, NULL);
	free (text);

}

// arrays and objects that are only opened: the reader does not use the stack for them
static void test_document_deep (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	const CerverJsonLimits limits = { DOCUMENT_TEST_DEEP, 0, 0 };

	char *text = (char *) malloc (DOCUMENT_TEST_DEEP + 1);
	test_check_ptr (text);

	(void) memset (text, '[', DOCUMENT_TEST_DEEP);
	text[DOCUMENT_TEST_DEEP] = '\0';

	test_check_null_ptr (cerver_json_parse (NULL, text, DOCUMENT_TEST_DEEP, CERVER_JSON_FLAGS_NONE, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_DEPTH, NULL);

	// with a limit that takes them, the text ends before its value does
	test_check_null_ptr (cerver_json_parse (NULL, text, DOCUMENT_TEST_DEEP, CERVER_JSON_FLAGS_NONE, &limits, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_END, NULL);
	test_check_unsigned_eq (error.offset, DOCUMENT_TEST_DEEP, NULL);

	free (text);

	// and a text that closes them is a document
	text = document_test_nested_arrays (DOCUMENT_TEST_DEEP);

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_document_values (document), DOCUMENT_TEST_DEEP, NULL);

	cerver_json_document_delete (document);
	free (text);

}

static void test_document_size (void) {

	static const char text[] = "[1, 2, 3]";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	CerverJsonLimits limits = { 0, sizeof (text) - 1, 0 };

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error
	);

	test_check_ptr (document);
	cerver_json_document_delete (document);

	limits.size = sizeof (text) - 2;
	test_check_null_ptr (cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_SIZE, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);

	// no limit takes a text over the biggest one: it is rejected by its length, before it is read
	error.code = CERVER_JSON_ERROR_NONE;
	limits.size = SIZE_MAX;
	test_check_null_ptr (
		cerver_json_parse (NULL, text, ((size_t) CERVER_JSON_SIZE_MAX) + 1, CERVER_JSON_FLAGS_NONE, &limits, &error)
	);

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_SIZE, NULL);

	error.code = CERVER_JSON_ERROR_NONE;
	test_check_null_ptr (
		cerver_json_parse_in_situ (
			NULL, (char *) (uintptr_t) text, ((size_t) CERVER_JSON_SIZE_MAX) + 1,
			CERVER_JSON_FLAGS_NONE, &limits, &error
		)
	);

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_SIZE, NULL);

}

static void test_document_values (void) {

	// the array, its three elements, the object, its key and its value
	static const char text[] = "[1, 2, {\"a\": 3}]";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	CerverJsonLimits limits = { 0, 0, 6 };

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_document_values (document), 6, NULL);
	cerver_json_document_delete (document);

	limits.values = 5;
	test_check_null_ptr (cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_VALUES, NULL);

	limits.values = 1;
	test_check_null_ptr (cerver_json_parse (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &limits, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_VALUES, NULL);

	document = cerver_json_parse (NULL, "7", 1, CERVER_JSON_FLAGS_NONE, &limits, &error);
	test_check_ptr (document);
	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region memory

// a document in an arena of the caller: three allocations, whatever the text has
static void test_document_arena (void) {

	static const char text[] = "{\"name\": \"cerver\", \"values\": [1, 2, 3], \"escaped\": \"a\\nb\"}";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	char copy[sizeof (text)];
	(void) memcpy (copy, text, sizeof (text));

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	CerverJsonDocument *document = cerver_json_parse (
		arena, copy, strlen (copy), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (arena->n_allocations, DOCUMENT_TEST_ALLOCATIONS, NULL);

	// the text of the caller is not changed, and it can go
	test_check_true (!memcmp (copy, text, sizeof (text)));
	(void) memset (copy, 'x', sizeof (copy));

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (root, "name")), "cerver", NULL);
	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (root, "escaped")), "a\nb", NULL);

	// the arena has the document: this does nothing
	cerver_json_document_delete (document);
	test_check_str_eq (cerver_json_string_value (cerver_json_object_get (root, "name")), "cerver", NULL);

	// the arena is used again for the next text
	arena_reset (arena);

	document = cerver_json_parse (arena, "[1, 2]", strlen ("[1, 2]"), CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	test_check_unsigned_eq (arena->n_allocations, DOCUMENT_TEST_ALLOCATIONS, NULL);

	arena_delete (arena);

}

// in situ the strings are the bytes of the text, changed where they have escapes
static void test_document_in_situ (void) {

	static const char text[] = "{\"name\": \"cerver\", \"escaped\": \"a\\u0041b\"}";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	char buffer[sizeof (text) + CERVER_JSON_PADDING];
	(void) memset (buffer, 'x', sizeof (buffer));
	(void) memcpy (buffer, text, strlen (text));

	CerverJsonDocument *document = cerver_json_parse_in_situ (
		NULL, buffer, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	test_check_ptr (document);

	const CerverJsonValue *root = cerver_json_document_root (document);
	const char *name = cerver_json_string_value (cerver_json_object_get (root, "name"));
	const char *escaped = cerver_json_string_value (cerver_json_object_get (root, "escaped"));

	test_check_str_eq (name, "cerver", NULL);
	test_check_str_eq (escaped, "aAb", NULL);
	test_check_unsigned_eq (cerver_json_string_length (cerver_json_object_get (root, "escaped")), 3, NULL);

	test_check_true ((name > buffer) && (name < (buffer + sizeof (text))));
	test_check_true ((escaped > buffer) && (escaped < (buffer + sizeof (text))));

	cerver_json_document_delete (document);

	// a text that is not valid is rejected in situ like the others
	(void) memset (buffer, 'x', sizeof (buffer));
	(void) memcpy (buffer, "[1, 2", strlen ("[1, 2"));

	test_check_null_ptr (cerver_json_parse_in_situ (
		NULL, buffer, strlen ("[1, 2"), CERVER_JSON_FLAGS_NONE, NULL, &error
	));

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_END, NULL);
	test_check_unsigned_eq (error.offset, 5, NULL);

}

// a text with more values than the ones that it starts with: they move and stay the same
static void test_document_growth (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	char *text = (char *) malloc (DOCUMENT_TEST_BUFFER_SIZE);
	test_check_ptr (text);

	// arrays inside arrays, so the one that is open moves too
	size_t length = 0;
	text[length++] = '[';
	for (size_t i = 0; i < DOCUMENT_TEST_ELEMENTS; i++) {
		if (i) text[length++] = ',';
		text[length++] = '[';
		text[length++] = (char) ('0' + (i % 10));
		text[length++] = ',';
		text[length++] = '[';
		text[length++] = ']';
		text[length++] = ']';
	}

	text[length++] = ']';

	CerverJsonDocument *document = cerver_json_parse (NULL, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_array_size (root), DOCUMENT_TEST_ELEMENTS, NULL);
	test_check_unsigned_eq (cerver_json_document_values (document), 1 + (DOCUMENT_TEST_ELEMENTS * 3), NULL);

	size_t count = 0;
	CerverJsonArrayIterator iterator = cerver_json_array_iterator (root);
	for (
		const CerverJsonValue *element = cerver_json_array_iterator_next (&iterator);
		element;
		element = cerver_json_array_iterator_next (&iterator)
	) {
		test_check_unsigned_eq (cerver_json_array_size (element), 2, NULL);
		test_check_long_int_eq (
			cerver_json_integer_value (cerver_json_array_get (element, 0)), (long) (count % 10), NULL
		);
		test_check_true (cerver_json_is_array (cerver_json_array_get (element, 1)));

		count += 1;
	}

	test_check_unsigned_eq (count, DOCUMENT_TEST_ELEMENTS, NULL);

	cerver_json_document_delete (document);

	// the same text in an arena of the caller
	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	document = cerver_json_parse (arena, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_document_values (document), 1 + (DOCUMENT_TEST_ELEMENTS * 3), NULL);

	arena_delete (arena);

	free (text);

}

#pragma endregion

#pragma region duplicates

// an object with a key more than once is a text of the grammar: the last one is the one that counts
static void test_document_duplicates_accepted (void) {

	CerverJsonDocument *document = json_test_parse ("{\"a\": 1, \"b\": 2, \"a\": 3, \"a\": {\"a\": 4, \"a\": 5}}");

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), 4, NULL);

	const CerverJsonValue *last = cerver_json_object_get (root, "a");
	test_check_true (cerver_json_is_object (last));
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (last, "a")), 5, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "b")), 2, NULL);

	cerver_json_document_delete (document);

}

static void test_document_duplicates_rejected (void) {

	static const char repeated[] = "{\"a\": 1, \"b\": 2, \"a\": 3}";
	static const char nested[] = "{\"a\": 1, \"b\": {\"c\": 1, \"c\": 2}}";
	static const char different[] = "{\"a\": 1, \"b\": {\"a\": 1, \"b\": 2}, \"c\": [{\"a\": 1}, {\"a\": 2}], \"\": 0}";
	static const char prefixes[] = "{\"a\": 1, \"ab\": 2, \"abc\": 3, \"A\": 4, \"a\\u0000\": 5}";
	static const char escaped[] = "{\"a\": 1, \"\\u0061\": 2}";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	// the object is checked when it ends
	json_test_rejects (
		repeated, strlen (repeated), CERVER_JSON_PARSE_REJECT_DUPLICATES,
		CERVER_JSON_ERROR_DUPLICATE, strlen (repeated)
	);

	json_test_rejects (
		nested, strlen (nested), CERVER_JSON_PARSE_REJECT_DUPLICATES,
		CERVER_JSON_ERROR_DUPLICATE, strlen (nested) - 1
	);

	// a key is compared without its escapes
	json_test_rejects (
		escaped, strlen (escaped), CERVER_JSON_PARSE_REJECT_DUPLICATES,
		CERVER_JSON_ERROR_DUPLICATE, strlen (escaped)
	);

	// the same key in different objects is not one more than once
	CerverJsonDocument *document = cerver_json_parse (
		NULL, different, strlen (different), CERVER_JSON_PARSE_REJECT_DUPLICATES, NULL, &error
	);

	test_check_ptr (document);
	cerver_json_document_delete (document);

	document = cerver_json_parse (
		NULL, prefixes, strlen (prefixes), CERVER_JSON_PARSE_REJECT_DUPLICATES, NULL, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_object_size (cerver_json_document_root (document)), 5, NULL);
	cerver_json_document_delete (document);

}

// writes an object with many members; the last key is the first one again when it is asked for
static size_t document_test_members (char *text, const bool repeated) {

	size_t length = 0;

	text[length++] = '{';
	for (size_t i = 0; i < DOCUMENT_TEST_MEMBERS; i++) {
		const size_t key = (repeated && ((i + 1) == DOCUMENT_TEST_MEMBERS)) ? 0 : i;

		length += (size_t) snprintf (
			text + length, DOCUMENT_TEST_KEY_SIZE, "%s\"key-%zu\": %zu", i ? ", " : "", key, i
		);
	}

	text[length++] = '}';
	text[length] = '\0';

	return length;

}

// an object with more members than the ones that are compared one against the others
static void test_document_duplicates_many (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	char *text = (char *) malloc (DOCUMENT_TEST_BUFFER_SIZE);
	test_check_ptr (text);

	size_t length = document_test_members (text, false);

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES, NULL, &error
	);

	test_check_ptr (document);
	test_check_unsigned_eq (cerver_json_object_size (cerver_json_document_root (document)), DOCUMENT_TEST_MEMBERS, NULL);
	cerver_json_document_delete (document);

	length = document_test_members (text, true);

	json_test_rejects (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES, CERVER_JSON_ERROR_DUPLICATE, length);

	// without the flag the last one is the one that a search finds
	document = cerver_json_parse (NULL, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	test_check_long_int_eq (
		cerver_json_integer_value (cerver_json_object_get (cerver_json_document_root (document), "key-0")),
		DOCUMENT_TEST_MEMBERS - 1, NULL
	);

	cerver_json_document_delete (document);

	free (text);

}

#pragma endregion

void json_tests_document (void) {

	(void) printf ("\nTesting JSON document...\n");

	test_run (test_document_scalars);
	test_run (test_document_whitespace);
	test_run (test_document_empty_containers);
	test_run (test_document_nested);

	test_run (test_document_errors);
	test_run (test_document_nul_bytes);
	test_run (test_document_length);
	test_run (test_document_arguments);
	test_run (test_document_error_position);
	test_run (test_document_strings_of_enums);

	test_run (test_document_depth);
	test_run (test_document_deep);
	test_run (test_document_size);
	test_run (test_document_values);

	test_run (test_document_arena);
	test_run (test_document_in_situ);
	test_run (test_document_growth);

	test_run (test_document_duplicates_accepted);
	test_run (test_document_duplicates_rejected);
	test_run (test_document_duplicates_many);

}
