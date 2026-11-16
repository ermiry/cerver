#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/pointer.h>
#include <cerver/json/value.h>

#include "../test.h"
#include "json.h"

// a pointer and the integer that it names in the document of the RFC
typedef struct PointerTestInteger {

	const char *pointer;
	long value;

} PointerTestInteger;

// the document of the section 5 of RFC 6901
static const char pointer_test_document[] =
	"{"
	"\"foo\": [\"bar\", \"baz\"],"
	"\"\": 0,"
	"\"a/b\": 1,"
	"\"c%d\": 2,"
	"\"e^f\": 3,"
	"\"g|h\": 4,"
	"\"i\\\\j\": 5,"
	"\"k\\\"l\": 6,"
	"\" \": 7,"
	"\"m~n\": 8"
	"}";

static const PointerTestInteger pointer_test_integers[] = {
	{ "/", 0 },
	{ "/a~1b", 1 },
	{ "/c%d", 2 },
	{ "/e^f", 3 },
	{ "/g|h", 4 },
	{ "/i\\j", 5 },
	{ "/k\"l", 6 },
	{ "/ ", 7 },
	{ "/m~0n", 8 }
};

static const CerverJsonValue *pointer_test_get (const CerverJsonValue *value, const char *pointer) {

	return cerver_json_pointer_get (value, pointer, strlen (pointer));

}

// the examples of the RFC
static void test_pointer_examples (void) {

	CerverJsonDocument *document = json_test_parse (pointer_test_document);
	const CerverJsonValue *root = cerver_json_document_root (document);

	// the empty pointer is the whole document
	test_check_ptr_eq (pointer_test_get (root, ""), root);

	const CerverJsonValue *foo = pointer_test_get (root, "/foo");
	test_check_unsigned_eq (cerver_json_array_size (foo), 2, NULL);
	test_check_str_eq (cerver_json_string_value (pointer_test_get (root, "/foo/0")), "bar", NULL);
	test_check_str_eq (cerver_json_string_value (pointer_test_get (root, "/foo/1")), "baz", NULL);

	for (size_t i = 0; i < (sizeof (pointer_test_integers) / sizeof (pointer_test_integers[0])); i++) {
		const CerverJsonValue *value = pointer_test_get (root, pointer_test_integers[i].pointer);

		if (!value || (cerver_json_integer_value (value) != pointer_test_integers[i].value)) {
			(void) fprintf (stderr, "<%s> does not name %ld\n", pointer_test_integers[i].pointer, pointer_test_integers[i].value);
			exit (1);
		}
	}

	cerver_json_document_delete (document);

}

// what a pointer does not find, and the ones that are not pointers
static void test_pointer_missing (void) {

	CerverJsonDocument *document = json_test_parse (
		"{\"a\": {\"b\": [10, 20, {\"c\": 30}]}, \"1\": \"one\", \"~\": 1, \"a~b\": 2, \"01\": 3}"
	);

	const CerverJsonValue *root = cerver_json_document_root (document);

	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/a/b/0")) == 10);
	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/a/b/2/c")) == 30);
	test_check_str_eq (cerver_json_string_value (pointer_test_get (root, "/1")), "one", NULL);

	// in an object a token of digits is a key, with its zeros
	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/01")) == 3);

	// the escapes of a token
	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/~0")) == 1);
	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/a~0b")) == 2);

	// a key and a position that are not there
	test_check_null_ptr (pointer_test_get (root, "/b"));
	test_check_null_ptr (pointer_test_get (root, "/a/c"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/3"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/99999999999999999999999999"));

	// the element after the last one, and a position that is not written with its digits only
	test_check_null_ptr (pointer_test_get (root, "/a/b/-"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/01"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/00"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/+1"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/1e0"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/ 1"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/"));

	// a token into a value that has nothing inside
	test_check_null_ptr (pointer_test_get (root, "/1/0"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/0/c"));
	test_check_null_ptr (pointer_test_get (root, "/a/b/0/"));

	// a pointer starts with its separator, and a ~ has its digit
	test_check_null_ptr (pointer_test_get (root, "a"));
	test_check_null_ptr (pointer_test_get (root, "a/b"));
	test_check_null_ptr (pointer_test_get (root, "xa"));
	test_check_null_ptr (pointer_test_get (root, "/~"));
	test_check_null_ptr (pointer_test_get (root, "/~2"));
	test_check_null_ptr (pointer_test_get (root, "/a~"));
	test_check_null_ptr (pointer_test_get (root, "/a~b"));
	test_check_null_ptr (pointer_test_get (root, "/a/~x/b"));

	// the length is the one that is given
	test_check_ptr_eq (cerver_json_pointer_get (root, "/a/b/0", 2), pointer_test_get (root, "/a"));
	test_check_ptr_eq (cerver_json_pointer_get (root, "/a", 0), root);

	test_check_null_ptr (cerver_json_pointer_get (NULL, "", 0));
	test_check_null_ptr (cerver_json_pointer_get (NULL, "/a", 2));
	test_check_null_ptr (cerver_json_pointer_get (root, NULL, 0));

	cerver_json_document_delete (document);

}

// a pointer into a value that is not the root, and a key that is more than once
static void test_pointer_values (void) {

	CerverJsonDocument *document = json_test_parse ("{\"a\": [1, {\"b\": 2}], \"k\": 1, \"k\": {\"x\": 3}, \"m~n\": 4, \"m~n\": 5}");
	const CerverJsonValue *root = cerver_json_document_root (document);

	const CerverJsonValue *array = pointer_test_get (root, "/a");
	test_check_true (cerver_json_integer_value (pointer_test_get (array, "/1/b")) == 2);
	test_check_true (cerver_json_integer_value (pointer_test_get (pointer_test_get (array, "/1"), "/b")) == 2);

	// the last member of the key, with and without an escape in its token
	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/k/x")) == 3);
	test_check_true (cerver_json_integer_value (pointer_test_get (root, "/m~0n")) == 5);

	cerver_json_document_delete (document);

	// a value that is not an array nor an object only has itself
	document = json_test_parse ("7");
	root = cerver_json_document_root (document);

	test_check_ptr_eq (pointer_test_get (root, ""), root);
	test_check_null_ptr (pointer_test_get (root, "/"));
	test_check_null_ptr (pointer_test_get (root, "/0"));

	cerver_json_document_delete (document);

}

void json_tests_pointer (void) {

	(void) printf ("\nTesting JSON pointer...\n");

	test_run (test_pointer_examples);
	test_run (test_pointer_missing);
	test_run (test_pointer_values);

}
