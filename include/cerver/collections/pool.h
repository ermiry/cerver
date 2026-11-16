/**
 * @file cerver/collections/pool.h
 * @brief A pool: a stack of reusable elements.
 */

#ifndef _COLLECTIONS_POOL_H_
#define _COLLECTIONS_POOL_H_

#include <stdbool.h>
#include <stdlib.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup collections_pool Pool
 * @ingroup collections
 * @brief A stack of reusable elements: the last element that was returned is the first one that is taken, so it
 * is the one that is still in the cache.
 * @{
 */

/** @brief The elements that a new pool has room for; it grows by doubling. */
#define POOL_DEFAULT_CAPACITY			16

/** @brief A pool without a limit keeps every element that is returned to it. */
#define POOL_NO_LIMIT					0

/**
 * @brief A stack of reusable elements; pushing and popping does not allocate, only growing beyond its capacity
 * does.
 *
 * @threadsafety The push, the pop, the size, the max elements, the clear and the reset take its mutex; the other
 * setters are meant to be called before the pool is shared.
 */
typedef struct Pool {

	/** @brief The elements, the last one is the next that is taken. */
	void **elements;
	/** @brief The elements inside the pool. */
	size_t count;
	/** @brief The room of the array of the elements. */
	size_t capacity;

	/** @brief The most elements that the pool keeps, #POOL_NO_LIMIT to keep all of them. */
	size_t max_elements;

	/** @brief Protects the elements. */
	pthread_mutex_t mutex;

	/** @brief Releases an element, NULL to keep it. */
	void (*destroy)(void *data);
	/** @brief Allocates a new element. */
	void *(*create)(void);
	/** @brief pool_pop () creates an element with the create method when the pool is empty. */
	bool produce;

} Pool;

/**
 * @brief Sets the method that releases an element.
 *
 * @param[in,out] pool the pool, NULL is ignored
 * @param[in] destroy the method, NULL to keep the elements
 */
extern void pool_set_destroy (Pool *pool, void (*destroy)(void *data));

/**
 * @brief Sets the method that allocates a new element, for pool_init () and pool_pop ().
 *
 * @param[in,out] pool the pool, NULL is ignored
 * @param[in] create the method
 */
extern void pool_set_create (Pool *pool, void *(*create)(void));

/**
 * @brief Sets whether pool_pop () creates a new element with the create method when the pool is empty.
 *
 * @param[in,out] pool the pool, NULL is ignored
 * @param[in] produce true to create them
 */
extern void pool_set_produce_if_empty (Pool *pool, bool produce);

/**
 * @brief Sets the most elements that the pool keeps; an element that is pushed to a full pool is destroyed with
 * the destroy method.
 *
 * @param[in,out] pool the pool, NULL is ignored
 * @param[in] max_elements the elements, #POOL_NO_LIMIT (the default) to keep all of them
 */
extern void pool_set_max_elements (Pool *pool, size_t max_elements);

/**
 * @brief Returns how many elements are inside the pool.
 *
 * @param[in] pool the pool
 * @return the elements, 0 for a NULL pool
 */
extern size_t pool_size (Pool *pool);

/**
 * @brief Creates an empty pool with room for #POOL_DEFAULT_CAPACITY elements.
 *
 * @param[in] destroy releases an element, NULL to keep it
 * @return the new pool, released with pool_delete (); NULL if it could not be allocated
 */
extern Pool *pool_create (void (*destroy)(void *data));

/**
 * @brief Pushes n elements made with a create method.
 *
 * @param[in,out] pool the pool
 * @param[in] create makes each element, NULL to use the create method of the pool
 * @param[in] n_elements the elements to make
 * @retval 0 every element was pushed
 * @retval 1 a NULL pool, no create method, or at least one element was not made or not pushed
 */
extern int pool_init (
	Pool *pool,
	void *(*create)(void), unsigned int n_elements
);

/**
 * @brief Returns an element to the pool, it is the next one that pool_pop () takes.
 *
 * @param[in,out] pool the pool
 * @param[in] data the element; in a full pool it is destroyed with the destroy method, without the mutex
 * @retval 0 the element was kept, or destroyed because the pool is full
 * @retval 1 a NULL argument or the pool could not grow; the element still belongs to the caller
 */
extern int pool_push (Pool *pool, void *data);

/**
 * @brief Takes the last element that was pushed.
 *
 * @param[in,out] pool the pool
 * @return the element; when the pool is empty, a new one from the create method if the pool produces them, or
 * NULL
 */
extern void *pool_pop (Pool *pool);

/**
 * @brief Forgets every element without releasing it, useful when another structure points to the same data.
 *
 * @param[in,out] pool the pool, it can be NULL
 */
extern void pool_clear (Pool *pool);

/**
 * @brief Releases every element with the destroy method, but keeps the pool.
 *
 * @param[in,out] pool the pool, it can be NULL
 * @note The elements are taken out with the mutex and the destroy method runs after it, so it can use the pool;
 * only without memory for that copy it runs with the mutex.
 */
extern void pool_reset (Pool *pool);

/**
 * @brief Releases every element with the destroy method, and the pool.
 *
 * @param[in] pool the pool, it can be NULL
 */
extern void pool_delete (Pool *pool);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
