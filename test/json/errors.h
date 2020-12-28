#ifndef _CERVER_TESTS_JSON_ERROR_H_
#define _CERVER_TESTS_JSON_ERROR_H_

#include "../test.h"

/* Assumes json_error_t error */
#define check_errors(code_, texts_, num_, source_, line_, column_, position_)            \
	do {                                                                                 \
		int i_, found_ = 0;                                                              \
		if (json_error_code(&error) != code_) {                                          \
			where;                                                                     \
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
			where;                                                                     \
			if (num_ == 1) {                                                             \
				fprintf(stderr, "text: \"%s\" != \"%s\"\n", error.text, texts_[0]);      \
			} else {                                                                     \
				fprintf(stderr, "text: \"%s\" does not match\n", error.text);            \
			}                                                                            \
			exit(1);                                                                     \
		}                                                                                \
		if (strcmp(error.source, source_) != 0) {                                        \
			where;                                                                     \
																						 \
			fprintf(stderr, "source: \"%s\" != \"%s\"\n", error.source, source_);        \
			exit(1);                                                                     \
		}                                                                                \
		if (error.line != line_) {                                                       \
			where;                                                                     \
			fprintf(stderr, "line: %d != %d\n", error.line, line_);                      \
			exit(1);                                                                     \
		}                                                                                \
		if (error.column != column_) {                                                   \
			where;                                                                     \
			fprintf(stderr, "column: %d != %d\n", error.column, column_);                \
			exit(1);                                                                     \
		}                                                                                \
		if (error.position != position_) {                                               \
			where;                                                                     \
			fprintf(stderr, "position: %d != %d\n", error.position, position_);          \
			exit(1);                                                                     \
		}                                                                                \
	} while (0)

/* Assumes json_error_t error */
#define check_error(code_, text_, source_, line_, column_, position_)                    \
	check_errors(code_, &text_, 1, source_, line_, column_, position_)

#endif