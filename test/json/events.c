#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/events.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include "../test.h"
#include "json.h"

#define EVENTS_TEST_BUFFER_SIZE				(1 << 10)

// the events of a text as they come, each one with a letter
#define EVENTS_TEST_LOG_SIZE				(1 << 10)

// a limit of nested arrays for the tests
#define EVENTS_TEST_DEPTH					8

// what the methods of a test keep
typedef struct EventsTestLog {

	char text[EVENTS_TEST_LOG_SIZE];
	size_t length;

	// the event that asks to stop, counted from 1; 0 for none
	size_t stop;
	size_t count;

} EventsTestLog;

#pragma region log

static bool events_test_log_add (EventsTestLog *log, const char *text, const size_t length) {

	test_check_true ((log->length + length + 2) < sizeof (log->text));

	if (log->length) log->text[log->length++] = ' ';

	(void) memcpy (log->text + log->length, text, length);
	log->length += length;
	log->text[log->length] = '\0';

	log->count += 1;

	return log->count != log->stop;

}

static bool events_test_log_null (void *data) {

	return events_test_log_add ((EventsTestLog *) data, "null", 4);

}

static bool events_test_log_boolean (void *data, const bool value) {

	return events_test_log_add ((EventsTestLog *) data, value ? "true" : "false", value ? 4 : 5);

}

static bool events_test_log_integer (void *data, const i64 value) {

	char text[EVENTS_TEST_BUFFER_SIZE];
	const int length = snprintf (text, sizeof (text), "i:%lld", (long long) value);

	return events_test_log_add ((EventsTestLog *) data, text, (size_t) length);

}

static bool events_test_log_unsigned (void *data, const u64 value) {

	char text[EVENTS_TEST_BUFFER_SIZE];
	const int length = snprintf (text, sizeof (text), "u:%llu", (unsigned long long) value);

	return events_test_log_add ((EventsTestLog *) data, text, (size_t) length);

}

static bool events_test_log_real (void *data, const double value) {

	char text[EVENTS_TEST_BUFFER_SIZE];
	const int length = snprintf (text, sizeof (text), "r:%g", value);

	return events_test_log_add ((EventsTestLog *) data, text, (size_t) length);

}

static bool events_test_log_string (void *data, const char *string, const size_t length) {

	char text[EVENTS_TEST_BUFFER_SIZE];
	const int size = snprintf (text, sizeof (text), "s:%zu:%s", length, string);

	// the string has its NUL after its bytes
	test_check_true (string[length] == '\0');

	return events_test_log_add ((EventsTestLog *) data, text, (size_t) size);

}

static bool events_test_log_key (void *data, const char *key, const size_t length) {

	char text[EVENTS_TEST_BUFFER_SIZE];
	const int size = snprintf (text, sizeof (text), "k:%zu:%s", length, key);

	test_check_true (key[length] == '\0');

	return events_test_log_add ((EventsTestLog *) data, text, (size_t) size);

}

static bool events_test_log_object_begin (void *data) {

	return events_test_log_add ((EventsTestLog *) data, "{", 1);

}

static bool events_test_log_object_end (void *data) {

	return events_test_log_add ((EventsTestLog *) data, "}", 1);

}

static bool events_test_log_array_begin (void *data) {

	return events_test_log_add ((EventsTestLog *) data, "[", 1);

}

static bool events_test_log_array_end (void *data) {

	return events_test_log_add ((EventsTestLog *) data, "]", 1);

}

static const CerverJsonEvents events_test_log = {
	events_test_log_null,
	events_test_log_boolean,
	events_test_log_integer,
	events_test_log_unsigned,
	events_test_log_real,
	events_test_log_string,
	events_test_log_key,
	events_test_log_object_begin,
	events_test_log_object_end,
	events_test_log_array_begin,
	events_test_log_array_end
};

#pragma endregion

#pragma region writer

// the methods that write each event again: the text of the events of a text
static bool events_test_write_null (void *data) {

	cerver_json_writer_null ((CerverJsonWriter *) data);
	return true;

}

static bool events_test_write_boolean (void *data, const bool value) {

	cerver_json_writer_boolean ((CerverJsonWriter *) data, value);
	return true;

}

static bool events_test_write_integer (void *data, const i64 value) {

	cerver_json_writer_integer ((CerverJsonWriter *) data, value);
	return true;

}

static bool events_test_write_unsigned (void *data, const u64 value) {

	cerver_json_writer_unsigned ((CerverJsonWriter *) data, value);
	return true;

}

static bool events_test_write_real (void *data, const double value) {

	cerver_json_writer_real ((CerverJsonWriter *) data, value);
	return true;

}

static bool events_test_write_string (void *data, const char *string, const size_t length) {

	cerver_json_writer_string ((CerverJsonWriter *) data, string, length);
	return true;

}

static bool events_test_write_key (void *data, const char *key, const size_t length) {

	cerver_json_writer_key_bytes ((CerverJsonWriter *) data, key, length);
	return true;

}

static bool events_test_write_object_begin (void *data) {

	cerver_json_writer_object_begin ((CerverJsonWriter *) data);
	return true;

}

static bool events_test_write_object_end (void *data) {

	cerver_json_writer_object_end ((CerverJsonWriter *) data);
	return true;

}

static bool events_test_write_array_begin (void *data) {

	cerver_json_writer_array_begin ((CerverJsonWriter *) data);
	return true;

}

static bool events_test_write_array_end (void *data) {

	cerver_json_writer_array_end ((CerverJsonWriter *) data);
	return true;

}

static const CerverJsonEvents events_test_write = {
	events_test_write_null,
	events_test_write_boolean,
	events_test_write_integer,
	events_test_write_unsigned,
	events_test_write_real,
	events_test_write_string,
	events_test_write_key,
	events_test_write_object_begin,
	events_test_write_object_end,
	events_test_write_array_begin,
	events_test_write_array_end
};

#pragma endregion

#pragma region events

// every event of a text, in its order
static void test_events_order (void) {

	static const char text[] =
		"{\"name\": \"cerver\", \"values\": [1, -2, 18446744073709551615, 1.5], \"nested\": {\"ok\": true,"
		" \"no\": false, \"none\": null, \"empty\": [], \"e\\u0041\": \"a\\nb\"}, \"\": {}}";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };
	EventsTestLog log = { { 0 }, 0, 0, 0 };

	test_check_true (cerver_json_read (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_NONE, NULL);

	test_check_str_eq (
		log.text,
		"{ k:4:name s:6:cerver k:6:values [ i:1 i:-2 u:18446744073709551615 r:1.5 ] k:6:nested {"
		" k:2:ok true k:2:no false k:4:none null k:5:empty [ ] k:2:eA s:3:a\nb } k:0: { } }",
		NULL
	);

}

// a text of one value that is not an array nor an object
static void test_events_scalars (void) {

	static const char *texts[] = { "null", " true ", "false", "7", "-7", "2.5", "\"text\"", "[]", "{}" };
	static const char *logs[] = { "null", "true", "false", "i:7", "i:-7", "r:2.5", "s:4:text", "[ ]", "{ }" };

	for (size_t i = 0; i < (sizeof (texts) / sizeof (texts[0])); i++) {
		EventsTestLog log = { { 0 }, 0, 0, 0 };

		test_check_true (cerver_json_read (
			NULL, texts[i], strlen (texts[i]), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, NULL
		));

		test_check_str_eq (log.text, logs[i], NULL);
	}

}

// a method that is NULL is skipped, and the text is still checked
static void test_events_skipped (void) {

	static const char text[] = "{\"a\": [1, 2.5, \"s\", true, null, {}], \"b\": 18446744073709551615}";

	const CerverJsonEvents none = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	CerverJsonEvents keys = none;
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	EventsTestLog log = { { 0 }, 0, 0, 0 };

	test_check_true (cerver_json_read (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &none, NULL, &error));

	test_check_false (cerver_json_read (NULL, "[1, 2", 5, CERVER_JSON_FLAGS_NONE, NULL, &none, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_END, NULL);
	test_check_unsigned_eq (error.offset, 5, NULL);

	// only the keys
	keys.on_key = events_test_log_key;
	test_check_true (cerver_json_read (NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &keys, &log, &error));
	test_check_str_eq (log.text, "k:1:a k:1:b", NULL);

}

// every event can stop the reader, where it is
static void test_events_stopped (void) {

	static const char text[] = "{\"a\": [1, 2.5, \"s\", true, null, {}], \"b\": 18446744073709551615}";

	// where the reader is at each event: at the bracket of a start, and after what it read of the others
	static const size_t offsets[] = { 0, 4, 6, 8, 13, 18, 24, 30, 32, 34, 35, 40, 62, 63 };

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	EventsTestLog all = { { 0 }, 0, 0, 0 };

	test_check_true (cerver_json_read (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &all, &error
	));

	test_check_unsigned_eq (all.count, sizeof (offsets) / sizeof (offsets[0]), NULL);

	for (size_t stop = 1; stop <= all.count; stop++) {
		EventsTestLog log = { { 0 }, 0, 0, 0 };
		log.stop = stop;

		test_check_false (cerver_json_read (
			NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error
		));

		test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_STOPPED, NULL);
		test_check_unsigned_eq (error.offset, offsets[stop - 1], NULL);

		// the events before it were given, and none after
		test_check_unsigned_eq (log.count, stop, NULL);
		test_check_true (!strncmp (log.text, all.text, log.length));
	}

}

#pragma endregion

#pragma region documents

static const char *events_test_texts[] = {
	// texts that are valid
	"null", "true", "false", "0", "-7", "18446744073709551615", "1.5", "\"text\"", "[]", "{}",
	"[1, 2, 3]", "{\"a\": 1}", "{\"a\": {\"b\": {\"c\": [1, [2, [3, {}]]]}}}",
	"[null, true, false, 0, -1, 1.5, \"s\", [], {}]",
	"{\"escaped\": \"a\\\"b\\\\c\\nd\\u0001\", \"unicode\": \"\xC3\xA9\xF0\x9F\x98\x80\", \"\": \"\"}",
	"{\"a\": 1, \"a\": 2, \"b\": [{\"a\": 1, \"a\": 2}]}", "  [ 1 , 2 ]  ", "[\"a\\u0000b\"]",
	// and texts that are not
	"", " ", "[", "[1", "[1,", "[1,]", "[,1]", "[1 2]", "[1}", "{", "{\"a\"", "{\"a\":", "{\"a\":1",
	"{\"a\":1,", "{\"a\":1,}", "{,}", "{1:2}", "{\"a\" 1}", "{\"a\":}", "{\"a\":1]", "]", "}", ":", ",",
	"nul", "nulx", "tru", "truex", "fals", "1 2", "{} {}", "[]]", "\"abc", "\"a\\x\"", "\"\\ud800\"",
	"\"\xFF\"", "\"a\nb\"", "01", "-", "1.", "1e", "1e+", "-a", "1.5.5", "18446744073709551616", "1e999",
	"[1, 18446744073709551616]", "{\"a\": 1e999}", "[[[[", "{\"a\": [1, {\"b\": tru}]}", "[1, 2] x"
};

// a reader by events takes the texts that a document takes, with the same values,
// and gives the same error at the same position for the ones that it does not
static void events_test_same (const char *text, const unsigned int flags, const CerverJsonLimits *limits) {

	char events_text[EVENTS_TEST_BUFFER_SIZE];
	char document_text[EVENTS_TEST_BUFFER_SIZE];

	CerverJsonError events_error = { CERVER_JSON_ERROR_NONE, 0 };
	CerverJsonError document_error = { CERVER_JSON_ERROR_NONE, 0 };

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, events_text, sizeof (events_text), CERVER_JSON_FLAGS_NONE);

	const bool read = cerver_json_read (
		NULL, text, strlen (text), flags, limits, &events_test_write, &writer, &events_error
	);

	CerverJsonDocument *document = cerver_json_parse (NULL, text, strlen (text), flags, limits, &document_error);

	if (
		(read != (document != NULL))
		|| (events_error.code != document_error.code) || (events_error.offset != document_error.offset)
	) {
		(void) fprintf (
			stderr, "<%s> by events: %s at %zu; as a document: %s at %zu\n",
			text, cerver_json_error_to_string (events_error.code), events_error.offset,
			cerver_json_error_to_string (document_error.code), document_error.offset
		);

		exit (1);
	}

	if (document) {
		test_check_unsigned_eq (cerver_json_writer_end (&writer), CERVER_JSON_ERROR_NONE, NULL);

		(void) cerver_json_write (
			cerver_json_document_root (document), document_text, sizeof (document_text), CERVER_JSON_FLAGS_NONE
		);

		test_check_str_eq (events_text, document_text, NULL);

		cerver_json_document_delete (document);
	}

}

static void test_events_documents (void) {

	const CerverJsonLimits depth = { 3, 0, 0 };
	const CerverJsonLimits values = { 0, 0, 4 };
	const CerverJsonLimits size = { 0, 8, 0 };

	for (size_t i = 0; i < (sizeof (events_test_texts) / sizeof (events_test_texts[0])); i++) {
		events_test_same (events_test_texts[i], CERVER_JSON_FLAGS_NONE, NULL);
		events_test_same (events_test_texts[i], CERVER_JSON_PARSE_REJECT_NUL, NULL);
		events_test_same (events_test_texts[i], CERVER_JSON_PARSE_BIG_AS_REAL, NULL);

		// with limits that some of the texts are over
		events_test_same (events_test_texts[i], CERVER_JSON_FLAGS_NONE, &depth);
		events_test_same (events_test_texts[i], CERVER_JSON_FLAGS_NONE, &values);
		events_test_same (events_test_texts[i], CERVER_JSON_FLAGS_NONE, &size);
	}

}

#pragma endregion

#pragma region calls

static void test_events_arguments (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 1 };
	EventsTestLog log = { { 0 }, 0, 0, 0 };
	char buffer[EVENTS_TEST_BUFFER_SIZE] = "[1]";

	test_check_false (cerver_json_read (NULL, NULL, 0, CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_ARGUMENTS, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);

	test_check_false (cerver_json_read (NULL, "[1]", 3, CERVER_JSON_FLAGS_NONE, NULL, NULL, &log, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_ARGUMENTS, NULL);

	test_check_false (cerver_json_read_in_situ (
		NULL, NULL, 0, CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_ARGUMENTS, NULL);

	test_check_false (cerver_json_read_in_situ (NULL, buffer, 3, CERVER_JSON_FLAGS_NONE, NULL, NULL, &log, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_ARGUMENTS, NULL);

	// the keys of an object are not kept, so they are not compared
	test_check_false (cerver_json_read (
		NULL, "{\"a\": 1}", 8, CERVER_JSON_PARSE_REJECT_DUPLICATES, NULL, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_UNSUPPORTED, NULL);
	test_check_unsigned_eq (log.count, 0, NULL);

	// the error and the data of a call are optional
	test_check_false (cerver_json_read (NULL, "[", 1, CERVER_JSON_FLAGS_NONE, NULL, &events_test_write, NULL, NULL));

}

// the limits of a text, as a document has them
static void test_events_limits (void) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	const CerverJsonLimits depth = { EVENTS_TEST_DEPTH, 0, 0 };
	const CerverJsonLimits values = { 0, 0, 3 };
	const CerverJsonLimits size = { 0, 4, 0 };

	char text[(EVENTS_TEST_DEPTH * 2) + 4];
	EventsTestLog log = { { 0 }, 0, 0, 0 };

	(void) memset (text, '[', EVENTS_TEST_DEPTH + 1);
	(void) memset (text + EVENTS_TEST_DEPTH + 1, ']', EVENTS_TEST_DEPTH + 1);
	text[(EVENTS_TEST_DEPTH + 1) * 2] = '\0';

	// the levels that fit, without the first bracket
	test_check_true (cerver_json_read (
		NULL, text + 1, EVENTS_TEST_DEPTH * 2, CERVER_JSON_FLAGS_NONE, &depth, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (log.count, EVENTS_TEST_DEPTH * 2, NULL);

	test_check_false (cerver_json_read (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, &depth, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_DEPTH, NULL);
	test_check_unsigned_eq (error.offset, EVENTS_TEST_DEPTH, NULL);

	test_check_true (cerver_json_read (NULL, "[1, 2]", 6, CERVER_JSON_FLAGS_NONE, &values, &events_test_write, NULL, &error));
	test_check_false (cerver_json_read (NULL, "[1, 2, 3]", 9, CERVER_JSON_FLAGS_NONE, &values, &events_test_write, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_VALUES, NULL);
	test_check_unsigned_eq (error.offset, 7, NULL);

	test_check_true (cerver_json_read (NULL, "[1]", 3, CERVER_JSON_FLAGS_NONE, &size, &events_test_write, NULL, &error));
	test_check_false (cerver_json_read (NULL, "[1,2]", 5, CERVER_JSON_FLAGS_NONE, &size, &events_test_write, NULL, &error));
	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_SIZE, NULL);

}

// in situ the strings are the bytes of the text; with an arena of the caller nothing else is allocated
static void test_events_memory (void) {

	static const char text[] = "{\"name\": \"cerver\", \"escaped\": \"a\\u0041b\"}";

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };
	EventsTestLog log = { { 0 }, 0, 0, 0 };

	char buffer[sizeof (text) + CERVER_JSON_PADDING];
	(void) memset (buffer, 'x', sizeof (buffer));
	(void) memcpy (buffer, text, strlen (text));

	test_check_true (cerver_json_read_in_situ (
		NULL, buffer, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error
	));

	test_check_str_eq (log.text, "{ k:4:name s:6:cerver k:7:escaped s:3:aAb }", NULL);

	// the text was changed: the string without its escape is there
	test_check_true (memcmp (buffer, text, strlen (text)) != 0);

	// the copy of the text and the levels of the reader
	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	(void) memset (&log, 0, sizeof (EventsTestLog));

	test_check_true (cerver_json_read (
		arena, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (arena->n_allocations, 2, NULL);
	test_check_str_eq (log.text, "{ k:4:name s:6:cerver k:7:escaped s:3:aAb }", NULL);

	// in situ only the levels
	arena_reset (arena);

	(void) memcpy (buffer, text, strlen (text));
	(void) memset (&log, 0, sizeof (EventsTestLog));

	test_check_true (cerver_json_read_in_situ (
		arena, buffer, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &events_test_log, &log, &error
	));

	test_check_unsigned_eq (arena->n_allocations, 1, NULL);

	arena_delete (arena);

}

#pragma endregion

void json_tests_events (void) {

	(void) printf ("\nTesting JSON events...\n");

	test_run (test_events_order);
	test_run (test_events_scalars);
	test_run (test_events_skipped);
	test_run (test_events_stopped);

	test_run (test_events_documents);

	test_run (test_events_arguments);
	test_run (test_events_limits);
	test_run (test_events_memory);

}
