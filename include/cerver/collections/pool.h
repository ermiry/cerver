#ifndef _COLLECTIONS_POOL_H_
#define _COLLECTIONS_POOL_H_

#include <stdlib.h>
#include <stdbool.h>

#include "cerver/collections/dlist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pool {

	DoubleList *dlist;

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

// inserts the new data at the end of the pool
// returns 0 on success, 1 on error
extern int pool_push (Pool *pool, void *data);

// returns the data that is first in the pool
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