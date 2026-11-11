#ifndef _COLLECTIONS_FREELIST_H_
#define _COLLECTIONS_FREELIST_H_

#include <stddef.h>

#include <pthread.h>

#include "cerver/config.h"

// the elements that a recycling free list keeps by default
#define FREELIST_DEFAULT_LIMIT				(1 << 10)

// a build with sanitizers does not recycle, so a use after free
// of a recycled element is reported instead of being hidden
#ifdef CERVER_RECYCLE_DISABLED
	#define FREELIST_RECYCLE_LIMIT(limit)	0
#else
	#define FREELIST_RECYCLE_LIMIT(limit)	(limit)
#endif

// a free list for the elements of a type that has a member
// that points to the next element: the list never allocates
#define FREELIST_INITIALIZER(type, member, limit)		\
	{ PTHREAD_MUTEX_INITIALIZER, NULL, 0, (limit), offsetof (type, member) }

#ifdef __cplusplus
extern "C" {
#endif

// a bounded stack of released elements, safe to use from any thread
typedef struct FreeList {

	pthread_mutex_t mutex;

	void *head;

	unsigned int count;
	unsigned int limit;

	// the offset of the pointer to the next element inside each element
	size_t next_offset;

} FreeList;

// returns a released element, or NULL if the list is empty
CERVER_PUBLIC void *freelist_pop (FreeList *list);

// keeps a released element to be reused
// returns 0 if the element was kept,
// 1 if the list is full and the element must be released by the caller
CERVER_PUBLIC unsigned int freelist_push (FreeList *list, void *element);

// returns how many elements are inside the list
CERVER_PUBLIC unsigned int freelist_size (FreeList *list);

// releases every element of the list with the destroy method
CERVER_PUBLIC void freelist_drain (
	FreeList *list, void (*destroy) (void *element)
);

#ifdef __cplusplus
}
#endif

#endif
