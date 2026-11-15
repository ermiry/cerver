/**
 * @file http/headers/internal.h
 * @internal
 * @brief What the list of the headers and the search in a compiled header share.
 */

#ifndef _CERVER_HTTP_HEADERS_INTERNAL_H_
#define _CERVER_HTTP_HEADERS_INTERNAL_H_

#include <stddef.h>

/** @internal @brief The end of the line of a header. */
static const char http_headers_line_end[] = { "\r\n" };
/** @internal @brief The length of #http_headers_line_end. */
static const size_t http_headers_line_end_length = 2;

#endif
