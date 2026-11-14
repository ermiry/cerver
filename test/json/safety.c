#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <cerver/http/json/config.h>
#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>

#include "../test.h"
#include "json.h"

// threads that create the first objects of the process at the same time
#define SAFETY_SEED_THREADS				8
#define SAFETY_SEED_OBJECTS				64

// [{}, {}, ...] takes more than JSON_PARSER_DEFAULT_MEMORY_FACTOR for each byte
#define SAFETY_EMPTY_OBJECTS			(1 << 14)
#define SAFETY_EMPTY_OBJECT				"{},"

// [{"value": 1}, ...] is a document with small objects that is accepted
#define SAFETY_SMALL_OBJECTS			(1 << 12)
#define SAFETY_SMALL_OBJECT				"{\"value\": 1},"

// deeper than what the parser accepts, built in code
#define SAFETY_DEEP_LEVELS				(JSON_DUMP_MAX_DEPTH * 2)

#define SAFETY_LONG_SOURCE_SIZE			(JSON_ERROR_SOURCE_LENGTH * 4)

// a locale with a decimal comma, to check that the numbers do not use it
#define SAFETY_COMMA_LOCALE				"de_DE.UTF-8"

static void *safety_seed_thread (void *data) {

	(void) data;

	for (int i = 0; i < SAFETY_SEED_OBJECTS; i++) {
		json_t *json = json_pack ("{s:i}", "value", i);
		test_check_ptr (json);
		test_check_int_eq (json_integer_value (json_object_get (json, "value")), i, NULL);
		json_decref (json);
	}

	return NULL;

}

// the first objects of the process set the seed of the hash at the same time,
// before it was written without synchronization (ThreadSanitizer sees the race)
static void safety_seed_race (void) {

	pthread_t threads[SAFETY_SEED_THREADS];
	for (int i = 0; i < SAFETY_SEED_THREADS; i++) {
		test_check_int_eq (pthread_create (&threads[i], NULL, safety_seed_thread, NULL), 0, NULL);
	}

	for (int i = 0; i < SAFETY_SEED_THREADS; i++) {
		test_check_int_eq (pthread_join (threads[i], NULL), 0, NULL);
	}

}

static char *safety_repeat (const char *piece, size_t count) {

	const size_t piece_length = strlen (piece);

	// '[' and ']' instead of the last ',', and the null
	char *document = (char *) malloc ((piece_length * count) + 2);
	test_check_ptr (document);

	document[0] = '[';
	for (size_t i = 0; i < count; i++) {
		(void) memcpy (document + 1 + (i * piece_length), piece, piece_length);
	}

	document[piece_length * count] = ']';
	document[(piece_length * count) + 1] = '\0';

	return document;

}

// a document can not take more memory than its size allows
static void safety_memory_budget (void) {

	json_error_t error;

	char *empty_objects = safety_repeat (SAFETY_EMPTY_OBJECT, SAFETY_EMPTY_OBJECTS);

	json_t *json = json_loads (empty_objects, 0, &error);
	test_check_null_ptr (json);
	test_check_int_eq (json_error_code (&error), json_error_out_of_memory, NULL);

	// without a limit the same document is parsed
	json_set_parser_memory_factor (0);
	json = json_loads (empty_objects, 0, &error);
	test_check_ptr (json);
	test_check_unsigned_eq (json_array_size (json), SAFETY_EMPTY_OBJECTS, NULL);
	json_decref (json);

	json_set_parser_memory_factor (JSON_PARSER_DEFAULT_MEMORY_FACTOR);

	free (empty_objects);

	// a document of small objects is accepted with the default factor
	char *small_objects = safety_repeat (SAFETY_SMALL_OBJECT, SAFETY_SMALL_OBJECTS);

	json = json_loads (small_objects, 0, &error);
	test_check_ptr (json);
	test_check_unsigned_eq (json_array_size (json), SAFETY_SMALL_OBJECTS, NULL);
	json_decref (json);

	free (small_objects);

}

// an underflow is zero or a subnormal value, only an overflow is an error
static void safety_numbers_range (void) {

	json_error_t error;

	json_t *json = json_loads ("[1e-400, 5e-324]", 0, &error);
	test_check_ptr (json);
	test_check (json_real_value (json_array_get (json, 0)) >= 0.0, NULL);
	test_check (json_real_value (json_array_get (json, 1)) > 0.0, NULL);
	json_decref (json);

	json = json_loads ("[1e400]", 0, &error);
	test_check_null_ptr (json);
	test_check_int_eq (json_error_code (&error), json_error_numeric_overflow, NULL);

	// NaN and the infinite values are not json
	json_t *real = json_real (1.5);
	test_check_ptr (real);
	test_check_int_ne (json_real_set (real, (double) NAN), 0);
	json_decref (real);

}

// the numbers have a dot whatever the locale of the thread is
static void safety_numbers_locale (void) {

	locale_t comma = newlocale (LC_NUMERIC_MASK, SAFETY_COMMA_LOCALE, (locale_t) 0);
	if (!comma) {
		(void) printf ("\tsafety_numbers_locale skipped: %s is not installed\n", SAFETY_COMMA_LOCALE);
		return;
	}

	const locale_t previous = uselocale (comma);

	json_error_t error;
	json_t *json = json_loads ("[1.5, 2.25]", 0, &error);
	test_check_ptr (json);
	test_check (fabs (json_real_value (json_array_get (json, 0)) - 1.5) < 1e-9, NULL);

	char *dumped = json_dumps (json, JSON_COMPACT);
	test_check_ptr (dumped);
	test_check_str_eq (dumped, "[1.5,2.25]", NULL);
	free (dumped);

	json_decref (json);

	(void) uselocale (previous);
	freelocale (comma);

}

// an integer that does not fit in an int is not cut
static void safety_unpack_int_range (void) {

	json_error_t error;
	json_t *json = json_loads ("{\"value\": 4294967297}", 0, &error);
	test_check_ptr (json);

	int value = 0;
	test_check_int_eq (json_unpack_ex (json, &error, 0, "{s:i}", "value", &value), -1, NULL);
	test_check_int_eq (json_error_code (&error), json_error_numeric_overflow, NULL);

	json_int_t large = 0;
	test_check_int_eq (json_unpack (json, "{s:I}", "value", &large), 0, NULL);
	test_check_int_eq (large, 4294967297LL, NULL);

	json_decref (json);

}

// a tree deeper than the parser accepts is not dumped, the stack is not exhausted
static void safety_dump_depth (void) {

	json_t *root = json_array ();
	test_check_ptr (root);

	json_t *current = root;
	for (int i = 0; i < SAFETY_DEEP_LEVELS; i++) {
		json_t *child = json_array ();
		test_check_ptr (child);
		test_check_int_eq (json_array_append_new (current, child), 0, NULL);
		current = child;
	}

	test_check_null_ptr (json_dumps (root, 0));

	json_decref (root);

}

// a long source of an error is cut and still ends with a null
static void safety_error_source (void) {

	char path[SAFETY_LONG_SOURCE_SIZE];
	(void) memset (path, 'a', sizeof (path) - 1);
	path[0] = '/';
	path[sizeof (path) - 1] = '\0';

	json_error_t error;
	(void) memset (&error, 'x', sizeof (error));

	test_check_null_ptr (json_load_file (path, 0, &error));
	test_check_unsigned_eq (strlen (error.source), JSON_ERROR_SOURCE_LENGTH - 1, NULL);

}

// an invalid UTF-8 byte has its own error code
static void safety_invalid_utf8 (void) {

	json_error_t error;
	test_check_null_ptr (json_loads ("[\"\xff\"]", 0, &error));
	test_check_int_eq (json_error_code (&error), json_error_invalid_utf8, NULL);

}

// the print of a NULL or of the values without a key does not crash
static void safety_print (void) {

	json_print (NULL);

	json_t *json = json_pack ("[s, i, b]", "value", 1, 1);
	test_check_ptr (json);
	json_print (json);
	json_decref (json);

}

// the first one runs before any other test of the binary creates an object
void json_tests_seed (void) {

	(void) printf ("Testing JSON seed...\n");

	safety_seed_race ();

	(void) printf ("Done!\n");

}

void json_tests_safety (void) {

	(void) printf ("Testing JSON safety...\n");

	safety_memory_budget ();

	safety_numbers_range ();

	safety_numbers_locale ();

	safety_unpack_int_range ();

	safety_dump_depth ();

	safety_error_source ();

	safety_invalid_utf8 ();

	safety_print ();

	(void) printf ("Done!\n");

}
