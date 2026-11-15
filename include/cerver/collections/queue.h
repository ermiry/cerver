/**
 * @file cerver/collections/queue.h
 * @brief A queue: first in, first out, over a DoubleList.
 */

#ifndef _COLLECTIONS_QUEUE_H_
#define _COLLECTIONS_QUEUE_H_

#include <stdlib.h>

#include "cerver/collections/dlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup collections_queue Queue
 * @ingroup collections
 * @brief A first in, first out queue of pointers, over a DoubleList.
 * @{
 */

/**
 * @brief A queue of pointers.
 *
 * @threadsafety Each method takes the mutex of its DoubleList.
 */
typedef struct Queue {

	/** @brief The elements, the oldest one at the start. */
	DoubleList *dlist;
	/** @brief Releases the data of an element, NULL to keep it. */
	void (*destroy)(void *data);

} Queue;

/**
 * @brief Returns how many elements are inside the queue.
 *
 * @param[in] queue the queue
 * @return the elements, 0 for a NULL queue
 */
extern size_t queue_size (Queue *queue);

/**
 * @brief Creates an empty queue.
 *
 * @param[in] destroy releases the data of an element in queue_delete () and queue_reset (), NULL to keep it
 * @return the new queue, released with queue_delete (); NULL if it could not be allocated
 */
extern Queue *queue_create (void (*destroy)(void *data));

/**
 * @brief Pushes n elements made with a create method.
 *
 * @param[in,out] queue the queue
 * @param[in] create makes each element
 * @param[in] n_elements the elements to make
 * @retval 0 every element was pushed
 * @retval 1 a NULL argument, or at least one element was not made or not pushed
 */
extern int queue_init (
	Queue *queue,
	void *(*create)(void), unsigned int n_elements
);

/**
 * @brief Releases the queue, its elements and their data with the destroy method.
 *
 * @param[in] queue the queue, it can be NULL
 */
extern void queue_delete (Queue *queue);

/**
 * @brief Removes every element and releases their data with the destroy method, but keeps the queue.
 *
 * @param[in,out] queue the queue, it can be NULL
 */
extern void queue_reset (Queue *queue);

/**
 * @brief Removes every element but keeps their data, useful when another structure points to the same data.
 *
 * @param[in,out] queue the queue, it can be NULL
 */
extern void queue_clear (Queue *queue);

/**
 * @brief Pushes data at the end of the queue.
 *
 * @param[in,out] queue the queue
 * @param[in] data the data
 * @retval 0 the data was pushed
 * @retval 1 a NULL argument or it could not be pushed
 */
extern unsigned int queue_push (Queue *queue, void *data);

/**
 * @brief Takes the oldest data of the queue (the one at the start).
 *
 * @param[in,out] queue the queue
 * @return the data, NULL for a NULL or an empty queue
 */
extern void *queue_pop (Queue *queue);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
