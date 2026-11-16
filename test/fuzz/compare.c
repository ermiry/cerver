#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include "cerver/http/json/config.h"
#include "cerver/http/json/internal.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

// the JSON of cerver against the one that it replaces, the copy of Jansson: a text is taken by both with
// the same values, or rejected by both. The differences are the ones that were decided:
// - an integer over the biggest one with sign and up to the biggest one without it is a value for cerver
//   and an error for Jansson;
// - a key of an object with an escaped NUL is a key for cerver and an error for Jansson;
// - a NUL byte outside a string is an error for cerver, and Jansson skips the one that follows a number
//   or a literal ("[1\0, 2]" is "[1, 2]" for it);
// - an object with a key more than once has every member for cerver and only the last one for Jansson:
//   both answer the last one in a search;
// - the levels are compared below the limit of both, where neither of them counts differently

// the levels of the texts that are compared, below the limit of both readers
#define COMPARE_FUZZ_DEPTH					(1 << 10)

static bool compare_fuzz_values (const json_t *expected, const CerverJsonValue *value);

static bool compare_fuzz_arrays (const json_t *expected, const CerverJsonValue *value) {

	bool retval = json_array_size (expected) == cerver_json_array_size (value);

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (value);
	for (size_t i = 0; retval && (i < json_array_size (expected)); i++) {
		retval = compare_fuzz_values (json_array_get (expected, i), cerver_json_array_iterator_next (&iterator));
	}

	return retval;

}

// every key of each one is in the other, with the value of its last member
static bool compare_fuzz_objects (const json_t *expected, const CerverJsonValue *value) {

	bool retval = true;

	const char *key = NULL;
	json_t *member = NULL;

	// a key of Jansson never has a NUL inside
	json_object_foreach ((json_t *) expected, key, member) {
		if (!compare_fuzz_values (member, cerver_json_object_get (value, key))) retval = false;
	}

	const CerverJsonValue *value_key = NULL;
	CerverJsonObjectIterator iterator = cerver_json_object_iterator (value);
	while (retval && cerver_json_object_iterator_next (&iterator, &value_key, NULL)) {
		retval = (strlen (cerver_json_string_value (value_key)) == cerver_json_string_length (value_key))
			&& (json_object_get (expected, cerver_json_string_value (value_key)) != NULL);
	}

	return retval;

}

static bool compare_fuzz_strings (const json_t *expected, const CerverJsonValue *value) {

	return (json_string_length (expected) == cerver_json_string_length (value))
		&& !memcmp (json_string_value (expected), cerver_json_string_value (value), json_string_length (expected));

}

static bool compare_fuzz_reals (const json_t *expected, const CerverJsonValue *value) {

	const double expected_real = json_real_value (expected);
	const double real = cerver_json_real_value (value);

	// bit by bit: the same double, with the same sign of a zero
	return (cerver_json_type (value) == CERVER_JSON_TYPE_REAL) && !memcmp (&expected_real, &real, sizeof (double));

}

static bool compare_fuzz_values (const json_t *expected, const CerverJsonValue *value) {

	if (!value) return false;

	switch (json_typeof (expected)) {
		case JSON_OBJECT: return cerver_json_is_object (value) && compare_fuzz_objects (expected, value);

		case JSON_ARRAY: return cerver_json_is_array (value) && compare_fuzz_arrays (expected, value);

		case JSON_STRING: return cerver_json_is_string (value) && compare_fuzz_strings (expected, value);

		case JSON_INTEGER:
			return (cerver_json_type (value) == CERVER_JSON_TYPE_INTEGER)
				&& (json_integer_value (expected) == cerver_json_integer_value (value));

		case JSON_REAL: return compare_fuzz_reals (expected, value);

		case JSON_TRUE: return cerver_json_is_boolean (value) && cerver_json_boolean_value (value);

		case JSON_FALSE: return cerver_json_is_boolean (value) && !cerver_json_boolean_value (value);

		case JSON_NULL: return cerver_json_is_null (value);

		default: return false;
	}

}

// whether a value has, at any level, what only cerver takes: an integer that
// only fits without sign, or a key of an object with a NUL inside
static bool compare_fuzz_only_cerver (const CerverJsonValue *value) {

	bool retval = cerver_json_type (value) == CERVER_JSON_TYPE_UNSIGNED;

	CerverJsonArrayIterator elements = cerver_json_array_iterator (value);
	for (
		const CerverJsonValue *element = cerver_json_array_iterator_next (&elements);
		!retval && element;
		element = cerver_json_array_iterator_next (&elements)
	) {
		retval = compare_fuzz_only_cerver (element);
	}

	const CerverJsonValue *key = NULL;
	const CerverJsonValue *member = NULL;
	CerverJsonObjectIterator members = cerver_json_object_iterator (value);
	while (!retval && cerver_json_object_iterator_next (&members, &key, &member)) {
		retval = (strlen (cerver_json_string_value (key)) != cerver_json_string_length (key))
			|| compare_fuzz_only_cerver (member);
	}

	return retval;

}

int LLVMFuzzerInitialize (int *argc, char ***argv) {

	(void) argc;
	(void) argv;

	// Jansson limits the memory of a text by its bytes: the texts of the fuzzer are compared without it
	json_set_parser_memory_factor (0);

	return 0;

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	const CerverJsonLimits limits = { COMPARE_FUZZ_DEPTH, 0, 0 };
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	json_error_t expected_error;

	CerverJsonDocument *document = cerver_json_parse (
		NULL, (const char *) data, size, CERVER_JSON_FLAGS_NONE, &limits, &error
	);

	// over the levels that are compared nothing is: Jansson counts them in its own way
	if (!document && (error.code == CERVER_JSON_ERROR_DEPTH)) return 0;

	json_t *expected = json_loadb ((const char *) data, size, JSON_DECODE_ANY | JSON_ALLOW_NUL, &expected_error);

	if (document && expected) {
		if (!compare_fuzz_values (expected, cerver_json_document_root (document))) abort ();
	}

	// a text that only cerver takes has what Jansson does not
	else if (document && !compare_fuzz_only_cerver (cerver_json_document_root (document))) {
		abort ();
	}

	// Jansson loses a NUL byte that follows a number or a literal, and takes the text without it
	else if (expected && !memchr (data, '\0', size)) {
		abort ();
	}

	if (expected) json_decref (expected);

	cerver_json_document_delete (document);

	return 0;

}
