/**
 * @file http/core/recycle.h
 * @internal
 * @brief The responses and the buffers that HTTP recycles, in a cache of each thread and in shared lists.
 */

#ifndef _CERVER_HTTP_RECYCLE_H_
#define _CERVER_HTTP_RECYCLE_H_

#include <stddef.h>

#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/http/responses/prepared.h"

/** @internal @brief The objects that the shared lists keep, 0 in the builds that do not recycle. */
#define HTTP_RECYCLE_LIMIT						FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

/** @internal @brief The objects of each kind that a thread keeps without a lock. */
#define HTTP_RECYCLE_THREAD_CACHE_SIZE			16

/** @internal @brief The smallest recycled buffer is of 2^8 bytes. */
#define HTTP_RECYCLE_BUFFER_MIN_EXPONENT		8
/** @internal @brief The biggest recycled buffer is of 2^16 bytes; a bigger one is allocated and released every time. */
#define HTTP_RECYCLE_BUFFER_MAX_EXPONENT		16
/** @internal @brief The classes of the recycled buffers, one for each power of 2. */
#define HTTP_RECYCLE_BUFFER_CLASSES				(HTTP_RECYCLE_BUFFER_MAX_EXPONENT - HTTP_RECYCLE_BUFFER_MIN_EXPONENT + 1)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpResponse;

/**
 * @internal
 * @brief Returns a released response to be initialized again.
 *
 * @return the response, from the cache of the thread or from the shared list; NULL if there is none and a new one
 * has to be allocated
 */
CERVER_PRIVATE struct _HttpResponse *http_recycle_response_take (void);

/**
 * @internal
 * @brief Keeps a response that was reset to be used again, or releases it when the cache and the list are full.
 *
 * @param[in] response the response
 */
CERVER_PRIVATE void http_recycle_response_give_back (
	struct _HttpResponse *response
);

/**
 * @internal
 * @brief Returns a buffer for at least size bytes.
 *
 * @param[in] size the bytes that are needed
 * @param[out] capacity the size of the class of the buffer; a buffer bigger than every class has exactly size
 * bytes; 0 on error
 * @return the buffer, NULL on error
 */
CERVER_PRIVATE void *http_recycle_buffer_take (
	const size_t size, size_t *capacity
);

/**
 * @internal
 * @brief Keeps a buffer whose capacity is the size of a class; any other one is released.
 *
 * @param[in] buffer the buffer, allocated with malloc () or realloc (); it can be NULL
 * @param[in] capacity its capacity
 */
CERVER_PRIVATE void http_recycle_buffer_give_back (
	void *buffer, const size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
