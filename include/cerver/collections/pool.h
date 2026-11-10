#ifndef _COLLECTIONS_POOL_H_
#define _COLLECTIONS_POOL_H_

#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// the elements that a new pool has room for, it grows by doubling
#define POOL_DEFAULT_CAPACITY			16

// a pool without a limit keeps every element that is returned to it
#define POOL_NO_LIMIT					0

// a stack of reusable elements:
// the last element that was returned is the first one that is taken,
// so it is the one that is still in the cache,
// pushing and popping does not allocate, only growing beyond its capacity does
typedef struct Pool {

	void **elements;
	size_t count;
	size_t capacity;

	// the most elements that the pool keeps, POOL_NO_LIMIT to keep all of them
	size_t max_elements;

	pthread_mutex_t mutex;

	void (*destroy)(void *data);
	void *(*create)(void);
	bool produce;

} Pool;

// sets a destroy method to be used by the pool to correctly dispose data
extern void pool_set_destroy (Pool *pool, void (*destroy)(void *data));

// sets a create method to be used by the pool to correctly allocate new data
extern void pool_set_create (Pool *pool, void *(*create)(void));

// sets the pool's ability to produce a element when a pop request is done and the pool is empty
// the pool will use its create method to allocate a new element and fullfil the request
extern void pool_set_produce_if_empty (Pool *pool, bool produce);

// sets the most elements that the pool keeps (POOL_NO_LIMIT by default),
// an element that is pushed to a full pool is destroyed with the destroy method
extern void pool_set_max_elements (Pool *pool, size_t max_elements);

// returns how many elements are inside the pool
extern size_t pool_size (Pool *pool);

// creates a new pool
extern Pool *pool_create (void (*destroy)(void *data));

// uses the create method to populate the pool with n elements (NULL to use pool's set method)
// returns 0 on no error, 1 if at least one element failed to be inserted
extern int pool_init (
	Pool *pool,
	void *(*create)(void), unsigned int n_elements
);

// returns an element to the pool, it is the next one that pool_pop () takes
// an element that does not fit in a full pool is destroyed
// returns 0 on success, 1 on error
extern int pool_push (Pool *pool, void *data);

// returns the last element that was pushed to the pool
extern void *pool_pop (Pool *pool);

// only gets rid of the pool's elements, but the data is kept
// this is usefull if another structure points to the same data
extern void pool_clear (Pool *pool);

// destroys all of the pool's elements and their data but keeps the pool
extern void pool_reset (Pool *pool);

// deletes the pool and all of its members using the destroy method
extern void pool_delete (Pool *pool);

#ifdef __cplusplus
}
#endif

#endif
