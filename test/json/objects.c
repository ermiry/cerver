#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include "cerver/json/internal.h"

#include "../test.h"
#include "json.h"

// every size of an object up to this one: the ones around the blocks of a directory, the last
// one that is searched in its directory, the first ones with a table and a table that is bigger
#define OBJECTS_TEST_SIZES					((JSON_OBJECT_HASHED * 2) + 8)

// an object whose table has many slots
#define OBJECTS_TEST_MEMBERS				(1 << 11)

// an object that never fits in a table whose searches walk one slot
#define OBJECTS_TEST_CROWDED				(1 << 8)

#define OBJECTS_TEST_KEY_SIZE				32

// objects with one member one inside the other, in a text that is still a small one
#define OBJECTS_TEST_SMALL_LEVELS			48
#define OBJECTS_TEST_SMALL_SIZE				(1 << 8)

// the allocations of an arena that a document takes: itself, its text and its values
#define OBJECTS_TEST_ALLOCATIONS			3

#define OBJECTS_TEST_MEMBER_SIZE			64

// a key of a text and the value of its last member
typedef struct ObjectsTestMember {

	const char *key;
	size_t length;
	long value;

} ObjectsTestMember;

#pragma region texts

// writes an object with some members, "key-<i>": <i>; with a repeated key, the last
// members have the keys of the first ones again
static char *objects_test_text (const size_t count, const size_t repeated, size_t *length) {

	char *text = (char *) malloc ((count * OBJECTS_TEST_MEMBER_SIZE) + OBJECTS_TEST_MEMBER_SIZE);
	test_check_ptr (text);

	size_t written = 0;
	text[written++] = '{';

	for (size_t i = 0; i < count; i++) {
		const size_t key = (i >= (count - repeated)) ? (i - (count - repeated)) : i;

		written += (size_t) snprintf (
			text + written, OBJECTS_TEST_MEMBER_SIZE, "%s\"key-%zu\": %zu", i ? ", " : "", key, i
		);
	}

	text[written++] = '}';
	text[written] = '\0';

	*length = written;

	return text;

}

static CerverJsonDocument *objects_test_parse (const char *text, const size_t length, const unsigned int flags) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	CerverJsonDocument *document = cerver_json_parse (NULL, text, length, flags, NULL, &error);
	if (!document) {
		(void) fprintf (
			stderr, "An object was not parsed: %s at %zu\n", cerver_json_error_to_string (error.code), error.offset
		);

		exit (1);
	}

	return document;

}

// checks that a key of an object has a value, by its string and by its bytes
static void objects_test_has (const CerverJsonValue *object, const char *key, const long value) {

	const CerverJsonValue *found = cerver_json_object_get (object, key);
	const CerverJsonValue *sized = cerver_json_object_find (object, key, strlen (key));

	if (!found || (found != sized) || (cerver_json_integer_value (found) != value)) {
		(void) fprintf (stderr, "The key <%s> was expected to be %ld\n", key, value);

		exit (1);
	}

}

// checks that an object does not have a key, by its string and by its bytes
static void objects_test_lacks (const CerverJsonValue *object, const char *key) {

	if (cerver_json_object_get (object, key) || cerver_json_object_find (object, key, strlen (key))) {
		(void) fprintf (stderr, "The key <%s> was not expected\n", key);

		exit (1);
	}

}

// checks every key of an object written by objects_test_text (): the first ones have the value of their
// last member when they are repeated
static void objects_test_check (const CerverJsonValue *object, const size_t count, const size_t repeated) {

	char key[OBJECTS_TEST_KEY_SIZE] = { 0 };

	test_check_unsigned_eq (cerver_json_object_size (object), count, NULL);

	for (size_t i = 0; i < (count - repeated); i++) {
		(void) snprintf (key, sizeof (key), "key-%zu", i);

		objects_test_has (object, key, (long) ((i < repeated) ? (i + (count - repeated)) : i));
	}

	// the keys that it does not have: the next one, a shorter one and a longer one
	(void) snprintf (key, sizeof (key), "key-%zu", count);
	objects_test_lacks (object, key);

	(void) snprintf (key, sizeof (key), "key-%zu-", count ? (count - 1) : 0);
	objects_test_lacks (object, key);

	objects_test_lacks (object, "key-");
	objects_test_lacks (object, "key");
	objects_test_lacks (object, "ke");
	objects_test_lacks (object, "");

}

#pragma endregion

#pragma region directory

// an object of every size: the members of the last block of its directory and the ones of the first
static void test_objects_sizes (void) {

	for (size_t count = 0; count <= OBJECTS_TEST_SIZES; count++) {
		size_t length = 0;
		char *text = objects_test_text (count, 0, &length);

		CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

		const CerverJsonValue *root = cerver_json_document_root (document);
		objects_test_check (root, count, 0);

		// the directory and the table of an object are not values of the text
		test_check_unsigned_eq (cerver_json_document_values (document), 1 + (count * 2), NULL);

		// the walk does not need them
		size_t members = 0;
		CerverJsonObjectIterator iterator = cerver_json_object_iterator (root);
		const CerverJsonValue *value = NULL;
		while (cerver_json_object_iterator_next (&iterator, NULL, &value)) {
			test_check_long_int_eq (cerver_json_integer_value (value), (long) members, NULL);
			members += 1;
		}

		test_check_unsigned_eq (members, count, NULL);

		cerver_json_document_delete (document);

		free (text);
	}

}

// keys that are the same up to the bytes of their signature, and up to a whole word after them
static const ObjectsTestMember objects_test_signatures[] = {
	{ "", 0, 0 },
	{ "a", 1, 1 },
	{ "ab", 2, 2 },
	{ "abc", 3, 3 },
	{ "abcd", 4, 4 },
	{ "abcde", 5, 5 },
	{ "abcdf", 5, 6 },
	{ "abcdefgh", 8, 7 },
	{ "abcdefgi", 8, 8 },
	{ "abcdefghijkl", 12, 9 },
	{ "abcdefghijkm", 12, 10 },
	{ "abcdefghijklm", 13, 11 },
	{ "abcdefghijklmnopqrstuvwxyz0123456789", 36, 12 },
	{ "abcdefghijklmnopqrstuvwxy_0123456789", 36, 13 },
	{ "abcdefghijklmnopqrstuvwxyz012345678_", 36, 14 },
	{ "_bcd", 4, 15 },
	{ "abc_", 4, 16 }
};

static const char *objects_test_absent[] = {
	"b", "aa", "abd", "abcx", "abcdx", "abcdef", "abcdefg", "abcdefgj", "abcdefghi",
	"abcdefghijk", "abcdefghijkn", "abcdefghijklmn",
	"abcdefghijklmnopqrstuvwxyz012345678", "abcdefghijklmnopqrstuvwxyz01234567890",
	"abcdefghijklmnopqrstuvwxyz0123456780", "Abcd", "abcD"
};

static void test_objects_signatures (void) {

	static const char text[] = "{"
		"\"\": 0, \"a\": 1, \"ab\": 2, \"abc\": 3, \"abcd\": 4, \"abcde\": 5, \"abcdf\": 6, "
		"\"abcdefgh\": 7, \"abcdefgi\": 8, \"abcdefghijkl\": 9, \"abcdefghijkm\": 10, \"abcdefghijklm\": 11, "
		"\"abcdefghijklmnopqrstuvwxyz0123456789\": 12, \"abcdefghijklmnopqrstuvwxy_0123456789\": 13, "
		"\"abcdefghijklmnopqrstuvwxyz012345678_\": 14, \"_bcd\": 15, \"abc_\": 16"
	"}";

	const size_t count = sizeof (objects_test_signatures) / sizeof (objects_test_signatures[0]);

	CerverJsonDocument *document = objects_test_parse (text, strlen (text), CERVER_JSON_PARSE_REJECT_DUPLICATES);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), count, NULL);

	for (size_t i = 0; i < count; i++) {
		const ObjectsTestMember *member = &objects_test_signatures[i];

		objects_test_has (root, member->key, member->value);

		// by its bytes a key does not need a NUL after them: the next key can have them at its start
		const char *longer = ((i + 1) < count) ? objects_test_signatures[i + 1].key : member->key;
		if (!strncmp (member->key, longer, member->length)) {
			test_check_long_int_eq (
				cerver_json_integer_value (cerver_json_object_find (root, longer, member->length)), member->value, NULL
			);
		}
	}

	for (size_t i = 0; i < (sizeof (objects_test_absent) / sizeof (objects_test_absent[0])); i++) {
		objects_test_lacks (root, objects_test_absent[i]);
	}

	// only the bytes that are asked for
	test_check_null_ptr (cerver_json_object_find (root, "abcdefgh", 6));
	test_check_null_ptr (cerver_json_object_find (root, "abcdefgh", 7));
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_find (root, "abcdefgh", 5)), 5, NULL);

	cerver_json_document_delete (document);

}

// a NUL in the bytes of the signature of a key, after them and as a whole key
static void test_objects_nul (void) {

	static const char text[] = "{"
		"\"a\": 1, \"a\\u0000\": 2, \"a\\u0000b\": 3, \"a\\u0000\\u0000\\u0000\": 4, "
		"\"abcd\": 5, \"abcd\\u0000\": 6, \"abcd\\u0000e\": 7, \"abcde\": 8, "
		"\"\\u0000\": 9, \"\": 10, \"\\u0000\\u0000\\u0000\\u0000\": 11, \"\\u0000\\u0000\\u0000\\u0000\\u0000\": 12"
	"}";

	static const ObjectsTestMember members[] = {
		{ "a", 1, 1 }, { "a\0", 2, 2 }, { "a\0b", 3, 3 }, { "a\0\0\0", 4, 4 },
		{ "abcd", 4, 5 }, { "abcd\0", 5, 6 }, { "abcd\0e", 6, 7 }, { "abcde", 5, 8 },
		{ "\0", 1, 9 }, { "", 0, 10 }, { "\0\0\0\0", 4, 11 }, { "\0\0\0\0\0", 5, 12 }
	};

	CerverJsonDocument *document = objects_test_parse (text, strlen (text), CERVER_JSON_PARSE_REJECT_DUPLICATES);

	const CerverJsonValue *root = cerver_json_document_root (document);

	for (size_t i = 0; i < (sizeof (members) / sizeof (members[0])); i++) {
		const CerverJsonValue *found = cerver_json_object_find (root, members[i].key, members[i].length);

		test_check_ptr (found);
		test_check_long_int_eq (cerver_json_integer_value (found), members[i].value, NULL);
	}

	// a string ends at its first NUL: it is never a key with one
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "a")), 1, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "a\0b")), 1, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "abcd")), 5, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "abcd\0e")), 5, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "abcde")), 8, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "")), 10, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (root, "\0\0\0\0")), 10, NULL);

	test_check_null_ptr (cerver_json_object_find (root, "a\0\0", 3));
	test_check_null_ptr (cerver_json_object_find (root, "abcd\0\0", 6));
	test_check_null_ptr (cerver_json_object_find (root, "\0\0", 2));
	test_check_null_ptr (cerver_json_object_find (root, "\0\0\0", 3));

	cerver_json_document_delete (document);

	// without the key that a string can be, a string does not find the ones that start like it
	document = json_test_parse ("{\"a\\u0000\": 1, \"abcd\\u0000\": 2, \"\\u0000\": 3}");
	root = cerver_json_document_root (document);

	test_check_null_ptr (cerver_json_object_get (root, "a"));
	test_check_null_ptr (cerver_json_object_get (root, "abcd"));
	test_check_null_ptr (cerver_json_object_get (root, ""));

	cerver_json_document_delete (document);

}

// the directory of an object with a number of members that is not a whole block has zeros after
// their signatures: they are not the signature of a member for a key of NUL bytes
static void test_objects_padding (void) {

	static const char *texts[] = {
		"{\"a\": 1}", "{\"a\": 1, \"b\": 2}", "{\"a\": 1, \"b\": 2, \"c\": 3}",
		"{\"a\": 1, \"b\": 2, \"c\": 3, \"d\": 4, \"e\": 5}"
	};

	static const char zeros[OBJECTS_TEST_KEY_SIZE] = { 0 };

	for (size_t i = 0; i < (sizeof (texts) / sizeof (texts[0])); i++) {
		CerverJsonDocument *document = json_test_parse (texts[i]);

		const CerverJsonValue *root = cerver_json_document_root (document);

		for (size_t length = 0; length < OBJECTS_TEST_KEY_SIZE; length++) {
			test_check_null_ptr (cerver_json_object_find (root, zeros, length));
		}

		test_check_null_ptr (cerver_json_object_get (root, zeros));

		cerver_json_document_delete (document);
	}

}

// a small text never needs more values than the ones that it starts with, the ones of its directories
// included: an object with one member inside another is the text with the most of them for its bytes
static void test_objects_small (void) {

	char text[OBJECTS_TEST_SMALL_SIZE] = { 0 };

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	size_t length = 0;
	for (size_t i = 0; i < OBJECTS_TEST_SMALL_LEVELS; i++) {
		(void) memcpy (text + length, "{\"\":", 4);
		length += 4;
	}

	text[length++] = '1';

	for (size_t i = 0; i < OBJECTS_TEST_SMALL_LEVELS; i++) text[length++] = '}';

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	CerverJsonDocument *document = cerver_json_parse (arena, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);

	// the document, its text and its values
	test_check_unsigned_eq (arena->n_allocations, OBJECTS_TEST_ALLOCATIONS, NULL);
	test_check_unsigned_eq (cerver_json_document_values (document), 1 + (OBJECTS_TEST_SMALL_LEVELS * 2), NULL);

	const CerverJsonValue *value = cerver_json_document_root (document);
	for (size_t i = 0; i < OBJECTS_TEST_SMALL_LEVELS; i++) {
		test_check_unsigned_eq (cerver_json_object_size (value), 1, NULL);
		value = cerver_json_object_get (value, "");
	}

	test_check_long_int_eq (cerver_json_integer_value (value), 1, NULL);

	arena_delete (arena);

}

// the last member of a key is the one that counts: in the same block
// of the directory, in another one, and between keys with its signature
static void test_objects_last (void) {

	static const char text[] = "{"
		"\"key-a\": 1, \"key-b\": 2, \"key-a\": 3, \"x\": 4, \"key-b\": 5, \"y\": 6, \"z\": 7, \"key-c\": 8, "
		"\"x\": 9, \"key-a\": 10, \"key-d\": 11, \"x\": 12, \"x\": 13, \"w\": 14, \"key-e\": 15, \"key-c\": 16, \"y\": 17"
	"}";

	static const ObjectsTestMember members[] = {
		{ "key-a", 5, 10 }, { "key-b", 5, 5 }, { "key-c", 5, 16 }, { "key-d", 5, 11 }, { "key-e", 5, 15 },
		{ "w", 1, 14 }, { "x", 1, 13 }, { "y", 1, 17 }, { "z", 1, 7 }
	};

	CerverJsonDocument *document = json_test_parse (text);

	const CerverJsonValue *root = cerver_json_document_root (document);

	for (size_t i = 0; i < (sizeof (members) / sizeof (members[0])); i++) {
		objects_test_has (root, members[i].key, members[i].value);
	}

	objects_test_lacks (root, "key-f");
	objects_test_lacks (root, "v");

	cerver_json_document_delete (document);

	json_test_rejects (text, strlen (text), CERVER_JSON_PARSE_REJECT_DUPLICATES, CERVER_JSON_ERROR_DUPLICATE, strlen (text));

}

// the objects inside an object and after it are found over its directory
static void test_objects_nested (void) {

	CerverJsonDocument *document = json_test_parse (
		"[{\"a\": {\"b\": {\"c\": [1, {\"d\": 2, \"e\": {}}, 3], \"f\": 4}, \"g\": 5}, \"h\": 6}, {\"i\": 7}, 8]"
	);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_array_size (root), 3, NULL);
	test_check_unsigned_eq (cerver_json_document_values (document), 25, NULL);

	const CerverJsonValue *first = cerver_json_array_get (root, 0);
	const CerverJsonValue *a = cerver_json_object_get (first, "a");
	const CerverJsonValue *b = cerver_json_object_get (a, "b");
	const CerverJsonValue *c = cerver_json_object_get (b, "c");

	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (first, "h")), 6, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (a, "g")), 5, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (b, "f")), 4, NULL);

	test_check_unsigned_eq (cerver_json_array_size (c), 3, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (cerver_json_array_get (c, 1), "d")), 2, NULL);
	test_check_unsigned_eq (cerver_json_object_size (cerver_json_object_get (cerver_json_array_get (c, 1), "e")), 0, NULL);
	test_check_null_ptr (cerver_json_object_get (cerver_json_object_get (cerver_json_array_get (c, 1), "e"), "d"));
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_array_get (c, 2)), 3, NULL);

	// a key of an object inside is not one of the object
	test_check_null_ptr (cerver_json_object_get (first, "b"));
	test_check_null_ptr (cerver_json_object_get (a, "h"));

	test_check_long_int_eq (cerver_json_integer_value (cerver_json_object_get (cerver_json_array_get (root, 1), "i")), 7, NULL);
	test_check_long_int_eq (cerver_json_integer_value (cerver_json_array_get (root, 2)), 8, NULL);

	cerver_json_document_delete (document);

}

#pragma endregion

#pragma region table

// whether the searches of an object with many members use its table
static bool objects_test_table_used (const CerverJsonValue *object) {

	const size_t count = cerver_json_object_size (object);

	return json_object_table (object, count)[json_object_table_slots (count)] != 0;

}

// an object with a table of many slots, without and with keys more than once
static void test_objects_table (void) {

	size_t length = 0;
	char *text = objects_test_text (OBJECTS_TEST_MEMBERS, 0, &length);

	CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

	test_check_true (objects_test_table_used (cerver_json_document_root (document)));
	objects_test_check (cerver_json_document_root (document), OBJECTS_TEST_MEMBERS, 0);

	cerver_json_document_delete (document);

	free (text);

	// the first keys again at the end: their last members are the ones of the table
	text = objects_test_text (OBJECTS_TEST_MEMBERS, OBJECTS_TEST_SIZES, &length);

	document = objects_test_parse (text, length, CERVER_JSON_FLAGS_NONE);

	test_check_true (objects_test_table_used (cerver_json_document_root (document)));
	objects_test_check (cerver_json_document_root (document), OBJECTS_TEST_MEMBERS, OBJECTS_TEST_SIZES);

	cerver_json_document_delete (document);

	json_test_rejects (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES, CERVER_JSON_ERROR_DUPLICATE, length);

	free (text);

}

// the keys of an object with a table that are only different in the bytes of their signature: the ones that
// share the slots of a key are not that key
static void test_objects_table_heads (void) {

	char key[OBJECTS_TEST_KEY_SIZE] = { 0 };

	char *text = (char *) malloc ((OBJECTS_TEST_MEMBERS * OBJECTS_TEST_MEMBER_SIZE) + OBJECTS_TEST_MEMBER_SIZE);
	test_check_ptr (text);

	size_t length = 0;
	text[length++] = '{';
	for (size_t i = 0; i < OBJECTS_TEST_MEMBERS; i++) {
		length += (size_t) snprintf (text + length, OBJECTS_TEST_MEMBER_SIZE, "%s\"%04zu-key\": %zu", i ? ", " : "", i, i);
	}

	text[length++] = '}';
	text[length] = '\0';

	CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_true (objects_test_table_used (root));

	for (size_t i = 0; i < OBJECTS_TEST_MEMBERS; i++) {
		(void) snprintf (key, sizeof (key), "%04zu-key", i);

		objects_test_has (root, key, (long) i);
	}

	// with the bytes after the signature of every key, and a signature that none has
	for (size_t i = OBJECTS_TEST_MEMBERS; i < (OBJECTS_TEST_MEMBERS * 2); i++) {
		(void) snprintf (key, sizeof (key), "%04zu-key", i);

		objects_test_lacks (root, key);
	}

	cerver_json_document_delete (document);

	free (text);

}

// one key in every member of an object with a table
static void test_objects_table_same (void) {

	char *text = (char *) malloc (OBJECTS_TEST_MEMBERS * OBJECTS_TEST_MEMBER_SIZE);
	test_check_ptr (text);

	size_t length = 0;
	text[length++] = '{';
	for (size_t i = 0; i < OBJECTS_TEST_CROWDED; i++) {
		length += (size_t) snprintf (text + length, OBJECTS_TEST_MEMBER_SIZE, "%s\"same\": %zu", i ? ", " : "", i);
	}

	text[length++] = '}';
	text[length] = '\0';

	CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_FLAGS_NONE);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), OBJECTS_TEST_CROWDED, NULL);
	test_check_true (objects_test_table_used (root));

	objects_test_has (root, "same", OBJECTS_TEST_CROWDED - 1);
	objects_test_lacks (root, "sam");
	objects_test_lacks (root, "same-");

	cerver_json_document_delete (document);

	json_test_rejects (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES, CERVER_JSON_ERROR_DUPLICATE, length);

	free (text);

}

// a table that one of its keys does not fit in is not used: the object is searched in its directory,
// and its keys are sorted to know that none is more than once
static void test_objects_table_unused (void) {

	// with one slot for each search, two keys that share theirs are enough
	cerver_json_object_table_probes_set (1);

	size_t length = 0;
	char *text = objects_test_text (OBJECTS_TEST_CROWDED, 0, &length);

	CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

	test_check_false (objects_test_table_used (cerver_json_document_root (document)));
	objects_test_check (cerver_json_document_root (document), OBJECTS_TEST_CROWDED, 0);

	cerver_json_document_delete (document);

	free (text);

	text = objects_test_text (OBJECTS_TEST_CROWDED, 1, &length);

	document = objects_test_parse (text, length, CERVER_JSON_FLAGS_NONE);

	test_check_false (objects_test_table_used (cerver_json_document_root (document)));
	objects_test_check (cerver_json_document_root (document), OBJECTS_TEST_CROWDED, 1);

	cerver_json_document_delete (document);

	json_test_rejects (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES, CERVER_JSON_ERROR_DUPLICATE, length);

	// the tables of the documents that are read after it are used again
	cerver_json_object_table_probes_set (0);

	document = objects_test_parse (text, length, CERVER_JSON_FLAGS_NONE);

	test_check_true (objects_test_table_used (cerver_json_document_root (document)));
	objects_test_check (cerver_json_document_root (document), OBJECTS_TEST_CROWDED, 1);

	cerver_json_document_delete (document);

	free (text);

}

// writes an object without spaces, "<i>":<digit>: its values, its directory and its table
// are more than the values that its text starts with, so they move when it is closed
static size_t objects_test_compact (char *text, const size_t count) {

	size_t length = 0;

	text[length++] = '{';
	for (size_t i = 0; i < count; i++) {
		length += (size_t) snprintf (text + length, OBJECTS_TEST_MEMBER_SIZE, "%s\"%zu\":%zu", i ? "," : "", i, i % 10);
	}

	text[length++] = '}';
	text[length] = '\0';

	return length;

}

static void objects_test_compact_check (const CerverJsonValue *object, const size_t count) {

	char key[OBJECTS_TEST_KEY_SIZE] = { 0 };

	test_check_unsigned_eq (cerver_json_object_size (object), count, NULL);

	for (size_t i = 0; i < count; i++) {
		(void) snprintf (key, sizeof (key), "%zu", i);

		objects_test_has (object, key, (long) (i % 10));
	}

	(void) snprintf (key, sizeof (key), "%zu", count);
	objects_test_lacks (object, key);

}

// the values move to a bigger place when an object is closed, alone and inside another one that is open
static void test_objects_growth (void) {

	static const char first[] = "{\"first\":1,\"inner\":";
	static const char last[] = ",\"last\":2}";

	char *text = (char *) malloc ((OBJECTS_TEST_MEMBERS * OBJECTS_TEST_MEMBER_SIZE) + OBJECTS_TEST_MEMBER_SIZE);
	test_check_ptr (text);

	size_t length = objects_test_compact (text, OBJECTS_TEST_MEMBERS);

	CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

	test_check_unsigned_eq (cerver_json_document_values (document), 1 + (OBJECTS_TEST_MEMBERS * 2), NULL);
	objects_test_compact_check (cerver_json_document_root (document), OBJECTS_TEST_MEMBERS);

	cerver_json_document_delete (document);

	// in an arena of the caller
	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	document = cerver_json_parse (arena, text, length, CERVER_JSON_FLAGS_NONE, NULL, NULL);
	test_check_ptr (document);
	objects_test_compact_check (cerver_json_document_root (document), OBJECTS_TEST_MEMBERS);

	arena_delete (arena);

	// inside an object that is open, that moves with its members
	length = sizeof (first) - 1;
	(void) memcpy (text, first, length);
	length += objects_test_compact (text + length, OBJECTS_TEST_MEMBERS);
	(void) memcpy (text + length, last, sizeof (last));
	length += sizeof (last) - 1;

	document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), 3, NULL);

	objects_test_has (root, "first", 1);
	objects_test_has (root, "last", 2);
	objects_test_lacks (root, "0");
	objects_test_compact_check (cerver_json_object_get (root, "inner"), OBJECTS_TEST_MEMBERS);

	cerver_json_document_delete (document);

	free (text);

}

// objects with tables inside an object with one
static void test_objects_table_nested (void) {

	char key[OBJECTS_TEST_KEY_SIZE] = { 0 };

	size_t inner_length = 0;
	char *inner = objects_test_text (OBJECTS_TEST_SIZES, 0, &inner_length);

	char *text = (char *) malloc ((OBJECTS_TEST_SIZES * (inner_length + OBJECTS_TEST_MEMBER_SIZE)) + OBJECTS_TEST_MEMBER_SIZE);
	test_check_ptr (text);

	size_t length = 0;
	text[length++] = '{';
	for (size_t i = 0; i < OBJECTS_TEST_SIZES; i++) {
		length += (size_t) snprintf (text + length, OBJECTS_TEST_MEMBER_SIZE, "%s\"key-%zu\": ", i ? ", " : "", i);

		(void) memcpy (text + length, inner, inner_length);
		length += inner_length;
	}

	text[length++] = '}';
	text[length] = '\0';

	CerverJsonDocument *document = objects_test_parse (text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES);

	const CerverJsonValue *root = cerver_json_document_root (document);
	test_check_unsigned_eq (cerver_json_object_size (root), OBJECTS_TEST_SIZES, NULL);
	test_check_unsigned_eq (
		cerver_json_document_values (document), 1 + (OBJECTS_TEST_SIZES * (2 + (OBJECTS_TEST_SIZES * 2))), NULL
	);

	for (size_t i = 0; i < OBJECTS_TEST_SIZES; i++) {
		(void) snprintf (key, sizeof (key), "key-%zu", i);

		objects_test_check (cerver_json_object_get (root, key), OBJECTS_TEST_SIZES, 0);
	}

	objects_test_lacks (root, "key");

	cerver_json_document_delete (document);

	free (text);
	free (inner);

}

#pragma endregion

void json_tests_objects (void) {

	(void) printf ("\nTesting JSON objects...\n");

	test_run (test_objects_sizes);
	test_run (test_objects_signatures);
	test_run (test_objects_nul);
	test_run (test_objects_padding);
	test_run (test_objects_last);
	test_run (test_objects_nested);
	test_run (test_objects_small);

	test_run (test_objects_table);
	test_run (test_objects_table_heads);
	test_run (test_objects_table_same);
	test_run (test_objects_table_unused);
	test_run (test_objects_growth);
	test_run (test_objects_table_nested);

}
