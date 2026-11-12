#ifndef _CERVER_HTTP_RECYCLE_H_
#define _CERVER_HTTP_RECYCLE_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/collections/freelist.h"

// the objects that the shared lists keep, 0 in the builds that do not recycle
#define HTTP_RECYCLE_LIMIT						FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

// the objects of each kind that a thread keeps without a lock
#define HTTP_RECYCLE_THREAD_CACHE_SIZE			16

// the buffers that are recycled go from 2^8 to 2^16 bytes in powers of 2,
// a bigger one is allocated and released every time
#define HTTP_RECYCLE_BUFFER_MIN_EXPONENT		8
#define HTTP_RECYCLE_BUFFER_MAX_EXPONENT		16
#define HTTP_RECYCLE_BUFFER_CLASSES				(HTTP_RECYCLE_BUFFER_MAX_EXPONENT - HTTP_RECYCLE_BUFFER_MIN_EXPONENT + 1)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpResponse;

// returns a released response to be initialized again,
// or NULL if there is none and a new one has to be allocated
CERVER_PRIVATE struct _HttpResponse *http_recycle_response_take (void);

// keeps a response that was reset to be used again,
// or releases it when the lists are full
CERVER_PRIVATE void http_recycle_response_give_back (
	struct _HttpResponse *response
);

// returns a buffer for at least size bytes, its capacity is the size of its class,
// a buffer bigger than every class has exactly size bytes
// returns NULL on error
CERVER_PRIVATE void *http_recycle_buffer_take (
	const size_t size, size_t *capacity
);

// keeps a buffer whose capacity is the size of a class, any other one is released,
// every buffer was allocated with malloc () or realloc ()
CERVER_PRIVATE void http_recycle_buffer_give_back (
	void *buffer, const size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
