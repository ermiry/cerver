#ifndef _CERVER_TESTS_JSON_H_
#define _CERVER_TESTS_JSON_H_

#include <stdio.h>
#include <stdlib.h>

#if HAVE_LOCALE_H
#include <locale.h>
#endif

#define failhdr fprintf(stderr, "%s - %d: ", __FILE__, __LINE__)

#define fail(msg)                                                                        \
	do {                                                                                 \
		failhdr;                                                                         \
		fprintf(stderr, "%s\n", msg);                                                    \
		exit(1);                                                                         \
	} while (0)

/* Assumes json_error_t error */
#define check_errors(code_, texts_, num_, source_, line_, column_, position_)            \
	do {                                                                                 \
		int i_, found_ = 0;                                                              \
		if (json_error_code(&error) != code_) {                                          \
			failhdr;                                                                     \
			fprintf(stderr, "code: %d != %d\n", json_error_code(&error), code_);         \
			exit(1);                                                                     \
		}                                                                                \
		for (i_ = 0; i_ < num_; i_++) {                                                  \
			if (strcmp(error.text, texts_[i_]) == 0) {                                   \
				found_ = 1;                                                              \
				break;                                                                   \
			}                                                                            \
		}                                                                                \
		if (!found_) {                                                                   \
			failhdr;                                                                     \
			if (num_ == 1) {                                                             \
				fprintf(stderr, "text: \"%s\" != \"%s\"\n", error.text, texts_[0]);      \
			} else {                                                                     \
				fprintf(stderr, "text: \"%s\" does not match\n", error.text);            \
			}                                                                            \
			exit(1);                                                                     \
		}                                                                                \
		if (strcmp(error.source, source_) != 0) {                                        \
			failhdr;                                                                     \
																						 \
			fprintf(stderr, "source: \"%s\" != \"%s\"\n", error.source, source_);        \
			exit(1);                                                                     \
		}                                                                                \
		if (error.line != line_) {                                                       \
			failhdr;                                                                     \
			fprintf(stderr, "line: %d != %d\n", error.line, line_);                      \
			exit(1);                                                                     \
		}                                                                                \
		if (error.column != column_) {                                                   \
			failhdr;                                                                     \
			fprintf(stderr, "column: %d != %d\n", error.column, column_);                \
			exit(1);                                                                     \
		}                                                                                \
		if (error.position != position_) {                                               \
			failhdr;                                                                     \
			fprintf(stderr, "position: %d != %d\n", error.position, position_);          \
			exit(1);                                                                     \
		}                                                                                \
	} while (0)

/* Assumes json_error_t error */
#define check_error(code_, text_, source_, line_, column_, position_)                    \
	check_errors(code_, &text_, 1, source_, line_, column_, position_)

extern void json_tests_array (void);

extern void json_tests_chaos (void);

extern void json_tests_copy (void);

extern void json_tests_dump_cb (void);

extern void json_tests_dump (void);

extern void json_tests_equal (void);

extern void json_tests_load_cb (void);

extern void json_tests_load (void);

extern void json_tests_loadb (void);

extern void json_tests_memory (void);

extern void json_tests_numbers (void);

extern void json_tests_objects (void);

extern void json_tests_pack (void);

extern void json_tests_simple (void);

extern void json_tests_sprintf (void);

extern void json_tests_unpack (void);

#endif