#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../measure.h"

#include "backend.h"

static const char *json_module = { "json" };

// the documents of the corpus, generated with a fixed seed: every implementation reads the same bytes
#define JSON_SEED							0x9E3779B97F4A7C15ULL

#define JSON_RECORDS						(1 << 8)
#define JSON_NUMBERS						(1 << 16)
#define JSON_STRINGS						(1 << 13)

#define JSON_TEXT_INITIAL					(1 << 12)
#define JSON_FORMAT_SIZE					(1 << 10)

#define JSON_SCORE_SCALE					1000.0
#define JSON_ZIP_MODULE						100000
#define JSON_RANDOM_BITS					11
#define JSON_RANDOM_SCALE					((double) (1ULL << 53))

typedef struct JsonText {

	char *text;
	size_t size;
	size_t capacity;

} JsonText;

static uint64_t json_random_state = JSON_SEED;

// xorshift64
static uint64_t json_random (void) {

	json_random_state ^= json_random_state << 13;
	json_random_state ^= json_random_state >> 7;
	json_random_state ^= json_random_state << 17;

	return json_random_state;

}

static double json_random_double (void) {

	return (double) (json_random () >> JSON_RANDOM_BITS) / JSON_RANDOM_SCALE;

}

static void json_text_append (JsonText *text, const char *format, ...) {

	char piece[JSON_FORMAT_SIZE];

	va_list arguments;
	va_start (arguments, format);
	const int length = vsnprintf (piece, JSON_FORMAT_SIZE, format, arguments);
	va_end (arguments);

	while ((text->size + (size_t) length + 1) > text->capacity) {
		text->capacity = text->capacity ? (text->capacity << 1) : JSON_TEXT_INITIAL;
		text->text = (char *) realloc (text->text, text->capacity);
	}

	(void) memcpy (text->text + text->size, piece, (size_t) length + 1);
	text->size += (size_t) length;

}

// the text ends with JSON_BACKEND_PADDING bytes in zero after its NUL
static void json_text_pad (JsonText *text) {

	text->capacity = text->size + 1 + JSON_BACKEND_PADDING;
	text->text = (char *) realloc (text->text, text->capacity);
	(void) memset (text->text + text->size, 0, 1 + JSON_BACKEND_PADDING);

}

// a copy of size bytes with its NUL and its padding
static void json_text_copy (JsonText *copy, const char *source, const size_t size) {

	copy->size = size;
	copy->capacity = 0;
	copy->text = NULL;
	json_text_pad (copy);

	(void) memcpy (copy->text, source, size);

}

static void json_record (JsonText *text, const unsigned int id) {

	json_text_append (
		text,
		"{\"id\":%u,\"name\":\"user-%u\",\"email\":\"user%u@cerver.test\",\"active\":%s,\"score\":%.3f,"
		"\"roles\":[\"read\",\"write\"],"
		"\"address\":{\"street\":\"%u Main\",\"city\":\"city-%u\",\"zip\":\"%05u\"},"
		"\"tags\":[\"a%u\",\"b%u\",\"c%u\"]}",
		id, id, id, (json_random () & 1) ? "true" : "false", json_random_double () * JSON_SCORE_SCALE,
		id, id % JSON_RECORDS, (unsigned int) (json_random () % JSON_ZIP_MODULE),
		id, id + 1, id + 2
	);

}

// an API request body
static void json_small (JsonText *text) {

	json_record (text, 1);

}

static void json_records (JsonText *text) {

	json_text_append (text, "[");
	for (unsigned int i = 0; i < JSON_RECORDS; i++) {
		if (i) json_text_append (text, ",");
		json_record (text, i);
	}

	json_text_append (text, "]");

}

// integers of 64 bits and doubles with every digit
static void json_numbers (JsonText *text) {

	json_text_append (text, "[");
	for (unsigned int i = 0; i < JSON_NUMBERS; i++) {
		if (i) json_text_append (text, ",");
		if (i & 1) json_text_append (text, "%.17g", json_random_double () * (double) json_random ());
		else json_text_append (text, "%lld", (long long) (json_random () >> 1));
	}

	json_text_append (text, "]");

}

// strings with escapes and characters of UTF-8, raw and escaped
static void json_strings (JsonText *text) {

	json_text_append (text, "[");
	for (unsigned int i = 0; i < JSON_STRINGS; i++) {
		if (i) json_text_append (text, ",");
		json_text_append (
			text, "\"line %u \\\"quoted\\\" \\\\ tab\\t new\\n caf\\u00e9 ma\xc3\xb1" "ana %llu\"",
			i, (unsigned long long) json_random ()
		);
	}

	json_text_append (text, "]");

}

typedef struct JsonCase {

	const char *name;
	void (*generate) (JsonText *text);

} JsonCase;

static const JsonCase json_cases[] = {
	{ "small", json_small },
	{ "records", json_records },
	{ "numbers", json_numbers },
	{ "strings", json_strings }
};

typedef struct JsonWork {

	const JsonText *text;
	void *document;

} JsonWork;

static bool json_work_parse (void *data) {

	JsonWork *work = (JsonWork *) data;

	void *document = json_backend_parse (work->text->text, work->text->size);
	if (document) json_backend_free (document);

	return (document != NULL);

}

static bool json_work_serialize (void *data) {

	JsonWork *work = (JsonWork *) data;

	char *text = NULL;
	const size_t size = json_backend_serialize (work->document, &text);
	if (text) json_backend_text_free (text);

	return (size > 0);

}

// the document parses, and its text parses again
static bool json_check (const JsonText *text) {

	bool valid = false;

	void *document = json_backend_parse (text->text, text->size);
	if (document) {
		char *serialized = NULL;
		const size_t size = json_backend_serialize (document, &serialized);

		JsonText copy = { NULL, 0, 0 };
		if (size) json_text_copy (&copy, serialized, size);
		if (serialized) json_backend_text_free (serialized);
		json_backend_free (document);

		if (copy.text) {
			void *again = json_backend_parse (copy.text, copy.size);
			valid = (again != NULL);
			if (again) json_backend_free (again);
			free (copy.text);
		}
	}

	return valid;

}

static bool json_measure (const JsonCase *json_case) {

	JsonText text = { NULL, 0, 0 };
	json_case->generate (&text);
	json_text_pad (&text);

	bool succeeded = json_check (&text);
	if (!succeeded) (void) fprintf (stderr, "%s did not take the %s document\n", json_backend_name, json_case->name);

	JsonWork work = { &text, NULL };
	MeasureResult result;

	if (succeeded) {
		succeeded = measure_run (json_work_parse, &work, text.size, &result);
		if (succeeded) measure_print (json_module, json_case->name, json_backend_name, "parse", &result);
	}

	if (succeeded) {
		work.document = json_backend_parse (text.text, text.size);
		succeeded = measure_run (json_work_serialize, &work, text.size, &result);
		if (succeeded) measure_print (json_module, json_case->name, json_backend_name, "serialize", &result);
		json_backend_free (work.document);
	}

	free (text.text);

	return succeeded;

}

int main (void) {

	bool succeeded = true;

	const size_t count = sizeof (json_cases) / sizeof (JsonCase);
	for (size_t i = 0; succeeded && (i < count); i++) {
		succeeded = json_measure (&json_cases[i]);
	}

	return succeeded ? 0 : 1;

}
