#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include "../test.h"
#include "json.h"

// two texts and whether their values are equal
typedef struct ValuesTestPair {

	const char *first;
	const char *second;
	bool equal;

} ValuesTestPair;

#pragma region getters

// every getter takes a NULL value and answers its empty result
static void test_values_null (void) {

	CerverJsonArrayIterator array_iterator = cerver_json_array_iterator (NULL);
	CerverJsonObjectIterator object_iterator = cerver_json_object_iterator (NULL);

	test_check_unsigned_eq (cerver_json_type (NULL), CERVER_JSON_TYPE_NONE, NULL);

	test_check_false (cerver_json_is_null (NULL));
	test_check_false (cerver_json_is_boolean (NULL));
	test_check_false (cerver_json_is_number (NULL));
	test_check_false (cerver_json_is_string (NULL));
	test_check_false (cerver_json_is_array (NULL));
	test_check_false (cerver_json_is_object (NULL));

	test_check_false (cerver_json_boolean_value (NULL));
	test_check_true (cerver_json_integer_value (NULL) == 0);
	test_check_true (cerver_json_unsigned_value (NULL) == 0);
	test_check_true (test_double_equal (cerver_json_real_value (NULL), 0.0));
	test_check_true (test_double_equal (cerver_json_number_value (NULL), 0.0));
	test_check_null_ptr (cerver_json_string_value (NULL));
	test_check_unsigned_eq (cerver_json_string_length (NULL), 0, NULL);

	test_check_unsigned_eq (cerver_json_array_size (NULL), 0, NULL);
	test_check_null_ptr (cerver_json_array_get (NULL, 0));
	test_check_null_ptr (cerver_json_array_iterator_next (&array_iterator));
	test_check_null_ptr (cerver_json_array_iterator_next (NULL));

	test_check_unsigned_eq (cerver_json_object_size (NULL), 0, NULL);
	test_check_null_ptr (cerver_json_object_get (NULL, "key"));
	test_check_null_ptr (cerver_json_object_find (NULL, "key", 3));
	test_check_false (cerver_json_object_iterator_next (&object_iterator, NULL, NULL));
	test_check_false (cerver_json_object_iterator_next (NULL, NULL, NULL));

	test_check_false (cerver_json_equal (NULL, NULL));

}

// a getter of another type answers its empty result, so a search can be given to the next one
static void test_values_types (void) {

	CerverJsonDocument *document = json_test_parse (
		"{\"null\": null, \"true\": true, \"false\": false, \"integer\": -7, \"unsigned\": 18446744073709551615,"
		" \"real\": 2.5, \"string\": \"text\", \"array\": [1, 2], \"object\": {\"a\": 1}}"
	);

	const CerverJsonValue *root = cerver_json_document_root (document);

	const CerverJsonValue *null_value = cerver_json_object_get (root, "null");
	const CerverJsonValue *true_value = cerver_json_object_get (root, "true");
	const CerverJsonValue *false_value = cerver_json_object_get (root, "false");
	const CerverJsonValue *integer = cerver_json_object_get (root, "integer");
	const CerverJsonValue *natural = cerver_json_object_get (root, "unsigned");
	const CerverJsonValue *real = cerver_json_object_get (root, "real");
	const CerverJsonValue *string = cerver_json_object_get (root, "string");
	const CerverJsonValue *array = cerver_json_object_get (root, "array");
	const CerverJsonValue *object = cerver_json_object_get (root, "object");

	test_check_unsigned_eq (cerver_json_type (null_value), CERVER_JSON_TYPE_NULL, NULL);
	test_check_unsigned_eq (cerver_json_type (true_value), CERVER_JSON_TYPE_BOOLEAN, NULL);
	test_check_unsigned_eq (cerver_json_type (false_value), CERVER_JSON_TYPE_BOOLEAN, NULL);
	test_check_unsigned_eq (cerver_json_type (integer), CERVER_JSON_TYPE_INTEGER, NULL);
	test_check_unsigned_eq (cerver_json_type (natural), CERVER_JSON_TYPE_UNSIGNED, NULL);
	test_check_unsigned_eq (cerver_json_type (real), CERVER_JSON_TYPE_REAL, NULL);
	test_check_unsigned_eq (cerver_json_type (string), CERVER_JSON_TYPE_STRING, NULL);
	test_check_unsigned_eq (cerver_json_type (array), CERVER_JSON_TYPE_ARRAY, NULL);
	test_check_unsigned_eq (cerver_json_type (object), CERVER_JSON_TYPE_OBJECT, NULL);

	test_check_true (cerver_json_is_null (null_value));
	test_check_false (cerver_json_is_null (false_value));
	test_check_true (cerver_json_is_boolean (true_value) && cerver_json_is_boolean (false_value));
	test_check_false (cerver_json_is_boolean (null_value));
	test_check_true (cerver_json_is_number (integer) && cerver_json_is_number (natural) && cerver_json_is_number (real));
	test_check_false (cerver_json_is_number (string) || cerver_json_is_number (true_value) || cerver_json_is_number (array));
	test_check_true (cerver_json_is_string (string));
	test_check_false (cerver_json_is_string (array));
	test_check_true (cerver_json_is_array (array));
	test_check_false (cerver_json_is_array (object));
	test_check_true (cerver_json_is_object (object));
	test_check_false (cerver_json_is_object (array));

	// the values of the other types
	test_check_false (cerver_json_boolean_value (integer));
	test_check_false (cerver_json_boolean_value (string));
	test_check_true (cerver_json_integer_value (true_value) == 0);
	test_check_true (cerver_json_integer_value (string) == 0);
	test_check_true (cerver_json_unsigned_value (integer) == 0);
	test_check_true (cerver_json_unsigned_value (true_value) == 0);
	test_check_true (test_double_equal (cerver_json_real_value (integer), 0.0));
	test_check_true (test_double_equal (cerver_json_number_value (string), 0.0));
	test_check_true (test_double_equal (cerver_json_number_value (true_value), 0.0));
	test_check_null_ptr (cerver_json_string_value (integer));
	test_check_unsigned_eq (cerver_json_string_length (array), 0, NULL);
	test_check_unsigned_eq (cerver_json_array_size (object), 0, NULL);
	test_check_unsigned_eq (cerver_json_array_size (string), 0, NULL);
	test_check_null_ptr (cerver_json_array_get (object, 0));
	test_check_unsigned_eq (cerver_json_object_size (array), 0, NULL);
	test_check_null_ptr (cerver_json_object_get (array, "a"));
	test_check_null_ptr (cerver_json_object_get (string, "a"));

	// a search that finds nothing goes through the next ones
	test_check_null_ptr (cerver_json_object_get (cerver_json_object_get (cerver_json_object_get (root, "none"), "a"), "b"));
	test_check_true (cerver_json_integer_value (cerver_json_array_get (cerver_json_object_get (root, "none"), 3)) == 0);

	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region containers

static void test_values_array (void) {

	CerverJsonDocument *document = json_test_parse ("[10, [20, 21, [22]], {\"a\": [30]}, \"40\", 50]");

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_array_size (root), 5, NULL);

	// an element after others that have values inside
	test_check_true (cerver_json_integer_value (cerver_json_array_get (root, 0)) == 10);
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_array_get (root, 1)), 3, NULL);
	test_check_true (cerver_json_is_object (cerver_json_array_get (root, 2)));
	test_check_str_eq (cerver_json_string_value (cerver_json_array_get (root, 3)), "40", NULL);
	test_check_true (cerver_json_integer_value (cerver_json_array_get (root, 4)) == 50);
	test_check_null_ptr (cerver_json_array_get (root, 5));
	test_check_null_ptr (cerver_json_array_get (root, (size_t) -1));

	test_check_true (
		cerver_json_integer_value (cerver_json_array_get (cerver_json_array_get (cerver_json_array_get (root, 1), 2), 0)) == 22
	);

	// the walk gives the elements in their order, and then nothing
	static const CerverJsonType types[] = {
		CERVER_JSON_TYPE_INTEGER, CERVER_JSON_TYPE_ARRAY, CERVER_JSON_TYPE_OBJECT,
		CERVER_JSON_TYPE_STRING, CERVER_JSON_TYPE_INTEGER
	};

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (root);
	for (size_t i = 0; i < (sizeof (types) / sizeof (types[0])); i++) {
		const CerverJsonValue *element = cerver_json_array_iterator_next (&iterator);
		test_check_ptr (element);
		test_check_ptr_eq (element, cerver_json_array_get (root, i));
		test_check_unsigned_eq (cerver_json_type (element), types[i], NULL);
	}

	test_check_null_ptr (cerver_json_array_iterator_next (&iterator));
	test_check_null_ptr (cerver_json_array_iterator_next (&iterator));

	// an array without elements, and a value that is not one
	iterator = cerver_json_array_iterator (cerver_json_array_get (root, 3));
	test_check_null_ptr (cerver_json_array_iterator_next (&iterator));

	cerver_json_document_delete (document);

	document = json_test_parse ("[]");
	iterator = cerver_json_array_iterator (cerver_json_document_root (document));
	test_check_null_ptr (cerver_json_array_iterator_next (&iterator));
	cerver_json_document_delete (document);

}

static void test_values_object (void) {

	static const char *keys[] = { "first", "second", "", "fourth", "first" };

	CerverJsonDocument *document = json_test_parse (
		"{\"first\": 1, \"second\": {\"first\": [2]}, \"\": 3, \"fourth\": [4, 5], \"first\": 6}"
	);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), 5, NULL);

	// the last member of a key is the one that a search finds
	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "first")) == 6);
	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "")) == 3);
	test_check_unsigned_eq (cerver_json_array_size (cerver_json_object_get (root, "fourth")), 2, NULL);

	// a key is compared whole
	test_check_null_ptr (cerver_json_object_get (root, "firs"));
	test_check_null_ptr (cerver_json_object_get (root, "first "));
	test_check_null_ptr (cerver_json_object_get (root, "FIRST"));
	test_check_null_ptr (cerver_json_object_get (root, NULL));
	test_check_null_ptr (cerver_json_object_find (root, NULL, 0));

	// by its length: only the bytes that are asked
	test_check_true (cerver_json_integer_value (cerver_json_object_find (root, "first and more", 5)) == 6);
	test_check_true (cerver_json_integer_value (cerver_json_object_find (root, "anything", 0)) == 3);
	test_check_null_ptr (cerver_json_object_find (root, "first", 4));

	// the walk gives every member in its order, the ones with the same key too
	CerverJsonObjectIterator iterator = cerver_json_object_iterator (root);
	for (size_t i = 0; i < (sizeof (keys) / sizeof (keys[0])); i++) {
		const CerverJsonValue *key = NULL;
		const CerverJsonValue *value = NULL;

		test_check_true (cerver_json_object_iterator_next (&iterator, &key, &value));
		test_check_true (cerver_json_is_string (key));
		test_check_str_eq (cerver_json_string_value (key), keys[i], NULL);
		test_check_ptr (value);
	}

	const CerverJsonValue *key = root;
	const CerverJsonValue *value = root;
	test_check_false (cerver_json_object_iterator_next (&iterator, &key, &value));
	test_check_ptr_eq (key, root);
	test_check_ptr_eq (value, root);

	// the key and the value of a member are optional
	iterator = cerver_json_object_iterator (root);
	test_check_true (cerver_json_object_iterator_next (&iterator, NULL, &value));
	test_check_true (cerver_json_integer_value (value) == 1);
	test_check_true (cerver_json_object_iterator_next (&iterator, &key, NULL));
	test_check_str_eq (cerver_json_string_value (key), "second", NULL);
	test_check_true (cerver_json_object_iterator_next (&iterator, NULL, NULL));

	cerver_json_document_delete (document);

}

// a key with a NUL inside is found by its length
static void test_values_object_nul (void) {

	static const char key[] = { 'a', '\0', 'b' };

	CerverJsonDocument *document = json_test_parse ("{\"a\": 1, \"a\\u0000b\": 2, \"a\\u0000\": 3}");

	const CerverJsonValue *root = cerver_json_document_root (document);

	test_check_true (cerver_json_integer_value (cerver_json_object_get (root, "a")) == 1);
	test_check_true (cerver_json_integer_value (cerver_json_object_find (root, key, sizeof (key))) == 2);
	test_check_true (cerver_json_integer_value (cerver_json_object_find (root, key, 2)) == 3);

	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region equal

static const ValuesTestPair values_test_pairs[] = {
	{ "null", "null", true },
	{ "true", "true", true },
	{ "true", "false", false },
	{ "null", "false", false },
	{ "0", "false", false },
	{ "0", "null", false },
	{ "1", "1", true },
	{ "1", "2", false },
	{ "1", "-1", false },
	// an integer is not a real
	{ "1", "1.0", false },
	{ "1.0", "1.0", true },
	{ "1.0", "1e0", true },
	{ "0.1", "0.10000000000000001", true },
	{ "0.0", "-0.0", true },
	{ "18446744073709551615", "18446744073709551615", true },
	{ "18446744073709551615", "-1", false },
	{ "\"a\"", "\"a\"", true },
	{ "\"a\"", "\"b\"", false },
	{ "\"a\"", "\"ab\"", false },
	{ "\"\"", "\"\"", true },
	{ "\"a\"", "\"\\u0061\"", true },
	{ "\"1\"", "1", false },
	{ "[]", "[]", true },
	{ "[]", "{}", false },
	{ "[]", "[[]]", false },
	{ "[1, 2, 3]", "[1,2,3]", true },
	{ "[1, 2, 3]", "[1, 3, 2]", false },
	{ "[1, 2]", "[1, 2, 3]", false },
	{ "[[1], [2, [3]]]", "[[1], [2, [3]]]", true },
	{ "[[1], [2, [3]]]", "[[1], [2, [4]]]", false },
	{ "{}", "{}", true },
	{ "{\"a\": 1}", "{\"a\": 1}", true },
	{ "{\"a\": 1}", "{\"a\": 2}", false },
	{ "{\"a\": 1}", "{\"b\": 1}", false },
	{ "{\"a\": 1}", "{\"a\": 1, \"b\": 2}", false },
	// the members of an object have no order
	{ "{\"a\": 1, \"b\": [2, {\"c\": 3}]}", "{\"b\": [2, {\"c\": 3}], \"a\": 1}", true },
	{ "{\"a\": 1, \"b\": [2, {\"c\": 3}]}", "{\"b\": [2, {\"c\": 4}], \"a\": 1}", false },
	// a key that is more than once counts with its last value, in both
	{ "{\"a\": 1, \"a\": 2}", "{\"a\": 3, \"a\": 2}", true },
	{ "{\"a\": 1, \"a\": 2}", "{\"a\": 2, \"a\": 1}", false },
	{ "{\"a\": 1, \"a\": 2}", "{\"a\": 2, \"b\": 3}", false },
	{ "{\"a\": 2, \"b\": 3}", "{\"a\": 1, \"a\": 2}", false },
	{ "{\"a\": 1, \"a\": 2}", "{\"a\": 2}", false }
};

static void test_values_equal (void) {

	const size_t count = sizeof (values_test_pairs) / sizeof (values_test_pairs[0]);

	for (size_t i = 0; i < count; i++) {
		const ValuesTestPair *pair = &values_test_pairs[i];

		CerverJsonDocument *first = json_test_parse (pair->first);
		CerverJsonDocument *second = json_test_parse (pair->second);

		const bool equal = cerver_json_equal (cerver_json_document_root (first), cerver_json_document_root (second));
		const bool reversed = cerver_json_equal (cerver_json_document_root (second), cerver_json_document_root (first));

		if ((equal != pair->equal) || (reversed != pair->equal)) {
			(void) fprintf (
				stderr, "<%s> and <%s> were expected to be %s\n",
				pair->first, pair->second, pair->equal ? "equal" : "different"
			);

			exit (1);
		}

		// a value is equal to itself
		test_check_true (cerver_json_equal (cerver_json_document_root (first), cerver_json_document_root (first)));

		cerver_json_document_delete (first);
		cerver_json_document_delete (second);
	}

	CerverJsonDocument *document = json_test_parse ("[1]");
	test_check_false (cerver_json_equal (cerver_json_document_root (document), NULL));
	test_check_false (cerver_json_equal (NULL, cerver_json_document_root (document)));
	cerver_json_document_delete (document);

}

#pragma endregion

void json_tests_values (void) {

	(void) printf ("\nTesting JSON values...\n");

	test_run (test_values_null);
	test_run (test_values_types);

	test_run (test_values_array);
	test_run (test_values_object);
	test_run (test_values_object_nul);

	test_run (test_values_equal);

}
