#ifndef _CERVER_BENCHMARKS_COMPETITORS_MULTIPART_BACKEND_H_
#define _CERVER_BENCHMARKS_COMPETITORS_MULTIPART_BACKEND_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// the implementation, its version is in the header of the results
extern const char *multipart_backend_name;

// reads a whole multipart body with its boundary (with its leading "--"), the headers and the data of its parts
// are taken; returns the parts that it found, -1 when the body is not valid
extern int multipart_backend_parse (const char *boundary, const char *body, const size_t size);

#ifdef __cplusplus
}
#endif

#endif
