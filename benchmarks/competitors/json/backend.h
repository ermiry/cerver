#ifndef _CERVER_BENCHMARKS_COMPETITORS_JSON_BACKEND_H_
#define _CERVER_BENCHMARKS_COMPETITORS_JSON_BACKEND_H_

#include <stdbool.h>
#include <stddef.h>

// the bytes after the end of every text that a parse can read, in zero:
// the readers with SIMD read whole blocks
#define JSON_BACKEND_PADDING				(1 << 6)

#ifdef __cplusplus
extern "C" {
#endif

// the implementation, its version is in the header of the results
extern const char *json_backend_name;

// parses a whole document, its text ends with a NUL after size bytes,
// and it has JSON_BACKEND_PADDING bytes after its end
// returns the document, NULL when it is not valid; it is valid until the next parse
extern void *json_backend_parse (const char *text, const size_t size);

// writes the document as compact text, a buffer that the implementation can reuse
// returns the bytes of the text, 0 on error; the text is valid until the next serialize
extern size_t json_backend_serialize (void *document, char **text);

extern void json_backend_text_free (char *text);

extern void json_backend_free (void *document);

#ifdef __cplusplus
}
#endif

#endif
