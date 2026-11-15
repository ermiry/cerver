#ifndef _CERVER_HTTP_HEADERS_INTERNAL_H_
#define _CERVER_HTTP_HEADERS_INTERNAL_H_

#include <stddef.h>

// the end of the line of a header, shared by the list and the search in a compiled header
static const char http_headers_line_end[] = { "\r\n" };
static const size_t http_headers_line_end_length = 2;

#endif
