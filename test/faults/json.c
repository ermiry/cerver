#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>
#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include "cerver/http/requests/internal.h"

#include <cerver/json/document.h>
#include <cerver/json/events.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include "cerver/json/internal.h"

#include "../test.h"
#include "faults.h"
#include "json.h"

// the allocations of the JSON of cerver that fail, with a fault injected in one of its calls:
// the reader answers that there is no memory, and what it took before is released with its arena

// chunks that are smaller than what the texts of the tests need, so they are allocated on their own
#define FAULTS_JSON_CHUNK_SIZE				(1 << 12)
#define FAULTS_JSON_SMALL_CHUNK_SIZE		(1 << 10)

// a text that does not fit in a chunk
#define FAULTS_JSON_TEXT_SIZE				(1 << 14)

// a text whose values fit in the rest of a chunk when it starts, and do not when they grow
#define FAULTS_JSON_GROWTH_ELEMENTS			300

// an object with more members than the ones that a reader has room for, in a block that does not fit in a chunk
#define FAULTS_JSON_MEMBERS					(1 << 12)
#define FAULTS_JSON_MEMBER_SIZE				16

// an object whose keys are sorted in a block that does not fit in a chunk, of a size that no other one has
#define FAULTS_JSON_SORTED_MEMBERS			3000

// an object whose values fit in the ones that its text starts with, and its directory and its table do not: keys
// of four digits, so each member is nine bytes
#define FAULTS_JSON_DIRECTORY_MEMBERS		200
#define FAULTS_JSON_DIRECTORY_FIRST			1000

// the bytes of a text for each value that it starts with, and how many times more the values are when they grow
#define FAULTS_JSON_VALUES_RATIO			4
#define FAULTS_JSON_VALUES_GROWTH			2

// the size of a value
#define FAULTS_JSON_VALUE_SIZE				16

// the header that an arena puts before an allocation that does not fit in a chunk
#define FAULTS_JSON_LARGE_HEADER			16

// the strings that a writer of an arena writes until its buffer does not fit in what its chunk has left
#define FAULTS_JSON_WRITER_STRINGS			(1 << 6)

static const CerverJsonEvents faults_json_events = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

// an array of ones: "[1,1,1]"
static char *faults_json_array (const size_t elements, size_t *length) {

	char *text = (char *) malloc ((elements * 2) + 2);
	test_check_ptr (text);

	size_t size = 0;
	text[size++] = '[';
	for (size_t i = 0; i < elements; i++) {
		if (i) text[size++] = ',';
		text[size++] = '1';
	}

	text[size++] = ']';
	text[size] = '\0';

	*length = size;

	return text;

}

// a string of a size, alone in its text
static char *faults_json_string (const size_t size) {

	char *text = (char *) malloc (size + 1);
	test_check_ptr (text);

	(void) memset (text, 'a', size);
	text[0] = '"';
	text[size - 1] = '"';
	text[size] = '\0';

	return text;

}

// a document without an arena creates its own one: without it there is no document
static void faults_json_own_arena (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	CerverJsonDocument *document = cerver_json_parse (NULL, "[1, 2]", 6, CERVER_JSON_FLAGS_NONE, NULL, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (document);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the arena was not created");

	// the same call with memory
	document = cerver_json_parse (NULL, "[1, 2]", 6, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	cerver_json_document_delete (document);

}

// the copy of a text that does not fit in a chunk of the arena of the caller
static void faults_json_text (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };

	Arena *arena = arena_create (FAULTS_JSON_CHUNK_SIZE);
	test_check_ptr (arena);

	char *text = faults_json_string (FAULTS_JSON_TEXT_SIZE);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	CerverJsonDocument *document = cerver_json_parse (
		arena, text, FAULTS_JSON_TEXT_SIZE, CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (document);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the text was not copied");

	// the arena of the caller is used again
	arena_reset (arena);

	document = cerver_json_parse (arena, text, FAULTS_JSON_TEXT_SIZE, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	test_check_unsigned_eq (
		cerver_json_string_length (cerver_json_document_root (document)), FAULTS_JSON_TEXT_SIZE - 2, NULL
	);

	free (text);
	arena_delete (arena);

}

// the values of a text that outgrow the ones that it started with, in the middle of the text
static void faults_json_growth (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	size_t length = 0;

	Arena *arena = arena_create (FAULTS_JSON_CHUNK_SIZE);
	test_check_ptr (arena);

	char *text = faults_json_array (FAULTS_JSON_GROWTH_ELEMENTS, &length);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	CerverJsonDocument *document = cerver_json_parse (arena, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (document);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the values did not grow");

	// the error is where the value that did not fit starts
	test_check_true ((error.offset > 0) && (error.offset < length));

	arena_reset (arena);

	document = cerver_json_parse (arena, text, length, CERVER_JSON_FLAGS_NONE, NULL, &error);
	test_check_ptr (document);
	test_check_unsigned_eq (
		cerver_json_array_size (cerver_json_document_root (document)), FAULTS_JSON_GROWTH_ELEMENTS, NULL
	);

	free (text);
	arena_delete (arena);

}

// an object with some members, "<first + i>":1
static char *faults_json_object (const unsigned int members, const unsigned int first, size_t *length) {

	char *text = (char *) malloc (((size_t) members * FAULTS_JSON_MEMBER_SIZE) + FAULTS_JSON_MEMBER_SIZE);
	test_check_ptr (text);

	size_t size = 0;
	text[size++] = '{';
	for (unsigned int i = 0; i < members; i++) {
		size += (size_t) snprintf (text + size, FAULTS_JSON_MEMBER_SIZE, "%s\"%u\":1", i ? "," : "", first + i);
	}

	text[size++] = '}';
	text[size] = '\0';

	*length = size;

	return text;

}

// parses an object with the allocation of a size failing: no document and no memory, and then the same text
// in the same arena with memory
static void faults_json_object_fails (
	const char *text, const size_t length, const unsigned int flags, const size_t size, const char *message
) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	Arena *arena = arena_create (FAULTS_JSON_CHUNK_SIZE);
	test_check_ptr (arena);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, size);
	CerverJsonDocument *document = cerver_json_parse (arena, text, length, flags, NULL, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (document);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, message);

	arena_reset (arena);

	document = cerver_json_parse (arena, text, length, flags, NULL, &error);
	test_check_ptr (document);
	test_check_true (cerver_json_is_object (cerver_json_document_root (document)));

	arena_delete (arena);

}

// the members of the objects that are open: a reader has room for some, and takes the rest from its arena
static void faults_json_members (void) {

	size_t length = 0;
	char *text = faults_json_object (FAULTS_JSON_MEMBERS, 0, &length);

	// the block of the members is the only allocation of its size: it does not fit in a chunk
	faults_json_object_fails (
		text, length, CERVER_JSON_FLAGS_NONE,
		FAULTS_JSON_LARGE_HEADER + (FAULTS_JSON_MEMBERS * sizeof (JsonMember)),
		"the members did not grow"
	);

	free (text);

}

// the directory and the table of an object that is closed, when the values have no room for them
static void faults_json_directory (void) {

	size_t length = 0;
	char *text = faults_json_object (FAULTS_JSON_DIRECTORY_MEMBERS, FAULTS_JSON_DIRECTORY_FIRST, &length);

	// the values grow once, when the object ends: twice the ones that the text started with
	faults_json_object_fails (
		text, length, CERVER_JSON_FLAGS_NONE,
		FAULTS_JSON_LARGE_HEADER
			+ ((length / FAULTS_JSON_VALUES_RATIO) * FAULTS_JSON_VALUES_GROWTH * FAULTS_JSON_VALUE_SIZE),
		"the values did not grow for the directory"
	);

	free (text);

}

// the keys of an object with many members whose table is not used are sorted to find the ones that are more
// than once
static void faults_json_duplicates (void) {

	size_t length = 0;
	char *text = faults_json_object (FAULTS_JSON_SORTED_MEMBERS, 0, &length);

	// with one slot for each search no table of so many keys is used
	cerver_json_object_table_probes_set (1);

	// the block of the keys is the only allocation of its size: a pointer for each member
	faults_json_object_fails (
		text, length, CERVER_JSON_PARSE_REJECT_DUPLICATES,
		FAULTS_JSON_LARGE_HEADER + (FAULTS_JSON_SORTED_MEMBERS * sizeof (const CerverJsonValue *)),
		"the keys were not sorted"
	);

	cerver_json_object_table_probes_set (0);

	free (text);

}

// the events: without an arena of their own, and without the copy of a text in the one of the caller
static void faults_json_events_read (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };

	unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	const bool own = cerver_json_read (NULL, "[1, 2]", 6, CERVER_JSON_FLAGS_NONE, NULL, &faults_json_events, NULL, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_false (own);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the arena was not created");

	Arena *arena = arena_create (FAULTS_JSON_CHUNK_SIZE);
	test_check_ptr (arena);

	char *text = faults_json_string (FAULTS_JSON_TEXT_SIZE);

	triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	const bool copied = cerver_json_read (
		arena, text, FAULTS_JSON_TEXT_SIZE, CERVER_JSON_FLAGS_NONE, NULL, &faults_json_events, NULL, &error
	);

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_false (copied);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the text was not copied");

	arena_reset (arena);

	test_check_true (cerver_json_read (
		arena, text, FAULTS_JSON_TEXT_SIZE, CERVER_JSON_FLAGS_NONE, NULL, &faults_json_events, NULL, &error
	));

	free (text);
	arena_delete (arena);

}

// a writer of an arena whose buffer can not grow keeps the error and what it had
static void faults_json_writer (void) {

	static const char string[] = "a string of some bytes";

	CerverJsonWriter writer;

	Arena *arena = arena_create (FAULTS_JSON_SMALL_CHUNK_SIZE);
	test_check_ptr (arena);

	cerver_json_writer_init_arena (&writer, arena, CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_array_begin (&writer);
	cerver_json_writer_string (&writer, string, strlen (string));

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	// the buffer grows until it needs another chunk of the arena
	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	for (unsigned int i = 0; i < FAULTS_JSON_WRITER_STRINGS; i++) {
		cerver_json_writer_string (&writer, string, strlen (string));
	}

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the buffer did not grow");

	const size_t length = cerver_json_writer_length (&writer);

	cerver_json_writer_array_end (&writer);

	test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (cerver_json_writer_length (&writer), length, NULL);

	// what it wrote before is still its text
	test_check_true (!strncmp (cerver_json_writer_text (&writer), "[\"a string of some bytes\", ", 27));

	arena_delete (arena);

}

// a request that can not create its arena answers that its body has no memory, every time
static void faults_json_request (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };

	HttpRequest *request = http_request_new ();
	test_check_ptr (request);

	String *body = str_new ("{\"name\": \"cerver\"}");
	request->body = body;

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	const CerverJsonValue *json = http_request_json (request, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (json);
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the arena was not created");

	// the body was read: the next call answers the same without another try
	error.code = CERVER_JSON_ERROR_NONE;
	test_check_null_ptr (http_request_json (request, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_MEMORY, NULL);
	test_check_null_ptr (request->json);

	request->body = NULL;
	str_delete (body);

	http_request_delete (request);

}

void faults_tests_json (void) {

	(void) printf ("Testing FAULTS json...\n");

	faults_json_own_arena ();
	faults_json_text ();
	faults_json_growth ();
	faults_json_members ();
	faults_json_directory ();
	faults_json_duplicates ();
	faults_json_events_read ();
	faults_json_writer ();
	faults_json_request ();

	(void) printf ("Done!\n");

}
