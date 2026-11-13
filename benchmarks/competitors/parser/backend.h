#ifndef _CERVER_BENCHMARKS_COMPETITORS_PARSER_BACKEND_H_
#define _CERVER_BENCHMARKS_COMPETITORS_PARSER_BACKEND_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// the implementation, its version is in the header of the results
extern const char *parser_backend_name;

// parses a whole request: its line, its headers and its body
// returns the headers that it found, -1 when the request is not valid or it is not complete
extern int parser_backend_parse (const char *request, const size_t size);

#ifdef __cplusplus
}
#endif

#endif
