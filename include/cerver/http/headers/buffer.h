/**
 * @file cerver/http/headers/buffer.h
 * @brief The search of a header in a compiled header.
 */

#ifndef _CERVER_HTTP_HEADERS_BUFFER_H_
#define _CERVER_HTTP_HEADERS_BUFFER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_headers_buffer Buffer
 * @ingroup http_headers
 * @brief The headers of a header that is already compiled.
 * @{
 */

/**
 * @brief Finds a header in a compiled header: the "Name: value\r\n" lines after its status line.
 *
 * The name is compared without case, and the search stops at the empty line that ends the header.
 *
 * @param[in] buffer the compiled header
 * @param[in] length its bytes
 * @param[in] name the name of the header
 * @param[in] name_length its length
 * @param[out] value the value, without the surrounding spaces; it points inside the buffer
 * @param[out] value_length the length of the value
 * @return true if it was found, false for a NULL buffer or name or a missing header
 */
CERVER_PUBLIC bool http_headers_buffer_find (
	const char *buffer, const size_t length,
	const char *name, const size_t name_length,
	const char **value, size_t *value_length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
