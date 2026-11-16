/**
 * @file cerver/collections/freelist.h
 * @brief A free list: a bounded stack of released elements to be reused, that never allocates.
 */

#ifndef _COLLECTIONS_FREELIST_H_
#define _COLLECTIONS_FREELIST_H_

#include <stddef.h>

#include <pthread.h>

#include "cerver/config.h"

/**
 * @defgroup collections_freelist Free list
 * @ingroup collections
 * @brief A bounded stack of released elements of a type that has a member that points to the next element.
 * @{
 */

/** @brief The elements that a recycling free list keeps by default. */
#define FREELIST_DEFAULT_LIMIT				(1 << 10)

/**
 * @brief The limit of a free list that recycles: 0 in a build with CERVER_RECYCLE_DISABLED (the sanitizers), so a
 * use after free of a recycled element is reported instead of being hidden.
 *
 * @param limit the limit of the list when it recycles
 */
#ifdef CERVER_RECYCLE_DISABLED
	#define FREELIST_RECYCLE_LIMIT(limit)	0
#else
	#define FREELIST_RECYCLE_LIMIT(limit)	(limit)
#endif

/**
 * @brief The static initializer of a free list for the elements of a type.
 *
 * @param type the type of the elements
 * @param member the member of the type that points to the next element
 * @param limit the most elements that the list keeps
 */
#define FREELIST_INITIALIZER(type, member, limit)		\
	{ PTHREAD_MUTEX_INITIALIZER, NULL, 0, (limit), offsetof (type, member) }

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A bounded stack of released elements, linked through a member of each element.
 *
 * @threadsafety Safe to use from any thread, every method takes its mutex.
 */
typedef struct FreeList {

	/** @brief Protects the list. */
	pthread_mutex_t mutex;

	/** @brief The last element that was pushed, NULL if the list is empty. */
	void *head;

	/** @brief The elements inside the list. */
	unsigned int count;
	/** @brief The most elements that the list keeps. */
	unsigned int limit;

	/** @brief The offset of the pointer to the next element inside each element. */
	size_t next_offset;

} FreeList;

/**
 * @brief Takes the last element that was pushed.
 *
 * @param[in,out] list the list
 * @return the element, with its next pointer set to NULL; NULL for a NULL or an empty list
 */
CERVER_PUBLIC void *freelist_pop (FreeList *list);

/**
 * @brief Keeps a released element to be reused.
 *
 * @param[in,out] list the list
 * @param[in] element the element, its next member is overwritten
 * @retval 0 the element was kept
 * @retval 1 a NULL argument or the list is full: the caller must release the element
 */
CERVER_PUBLIC unsigned int freelist_push (FreeList *list, void *element);

/**
 * @brief Returns how many elements are inside the list.
 *
 * @param[in] list the list
 * @return the elements, 0 for a NULL list
 */
CERVER_PUBLIC unsigned int freelist_size (FreeList *list);

/**
 * @brief Empties the list and releases each of its elements.
 *
 * @param[in,out] list the list, it can be NULL
 * @param[in] destroy releases an element, it runs without the mutex; NULL only forgets the elements
 */
CERVER_PUBLIC void freelist_drain (
	FreeList *list, void (*destroy) (void *element)
);

#ifdef __cplusplus
}
#endif

/** @} */

#endif
