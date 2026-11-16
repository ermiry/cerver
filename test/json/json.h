#ifndef _CERVER_TESTS_JSON_H_
#define _CERVER_TESTS_JSON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>

#include "../test.h"

extern void json_tests_document (void);

extern void json_tests_values (void);

extern void json_tests_objects (void);

extern void json_tests_strings (void);

extern void json_tests_numbers (void);

extern void json_tests_pointer (void);

extern void json_tests_writer (void);

extern void json_tests_events (void);

// parses a text that is valid with the defaults, in its own arena
static inline CerverJsonDocument *json_test_parse (const char *text) {

	// a text that is read sets both fields of its error
	CerverJsonError error = { CERVER_JSON_ERROR_STOPPED, 1 };

	CerverJsonDocument *document = cerver_json_parse (
		NULL, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	if (!document) {
		(void) fprintf (
			stderr, "<%s> was not parsed: %s at %zu\n",
			text, cerver_json_error_to_string (error.code), error.offset
		);

		exit (1);
	}

	test_check_unsigned_eq (error.code, CERVER_JSON_ERROR_NONE, NULL);
	test_check_unsigned_eq (error.offset, 0, NULL);

	return document;

}

// checks that a text is rejected with an error at a position
static inline void json_test_rejects (
	const char *text, const size_t length, const unsigned int flags,
	const CerverJsonErrorCode code, const size_t offset
) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	CerverJsonDocument *document = cerver_json_parse (NULL, text, length, flags, NULL, &error);

	if (document || (error.code != code) || (error.offset != offset)) {
		(void) fprintf (
			stderr, "<%s> expected %s at %zu, got %s at %zu\n",
			text, cerver_json_error_to_string (code), offset,
			document ? "a document" : cerver_json_error_to_string (error.code), error.offset
		);

		exit (1);
	}

}

#endif
