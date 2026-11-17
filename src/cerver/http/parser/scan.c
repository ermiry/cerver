#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cerver/http/parser/internal.h"

size_t http_parser_scan_line (
	const char *start, const size_t length, const bool carriage
) {

	size_t found = length;

	if (!carriage) {
		const char *line_feed = (const char *) memchr (start, '\n', length);
		if (line_feed) found = (size_t) (line_feed - start);
	}

	else {
		size_t i = 0;
		for (; (i + HTTP_PARSER_WORD_SIZE) <= length; i += HTTP_PARSER_WORD_SIZE) {
			const uint64_t word = http_parser_word_load (start + i);
			const uint64_t mask = http_parser_word_equal (word, '\n') | http_parser_word_equal (word, '\r');
			if (mask) return i + http_parser_word_first (mask);
		}

		for (; i < length; i++) {
			if ((start[i] == '\n') || (start[i] == '\r')) return i;
		}
	}

	return found;

}
