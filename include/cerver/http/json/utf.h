#ifndef _CERVER_HTTP_JSON_UTF_H_
#define _CERVER_HTTP_JSON_UTF_H_

#include <stddef.h>
#include <stdint.h>

extern int utf8_encode (
	int32_t codepoint, char *buffer, size_t *size
);

extern size_t utf8_check_first (char byte);

extern size_t utf8_check_full (
	const char *buffer, size_t size, int32_t *codepoint
);

extern const char *utf8_iterate (
	const char *buffer, size_t bufsize, int32_t *codepoint
);

extern int utf8_check_string (
	const char *string, size_t length
);

#endif