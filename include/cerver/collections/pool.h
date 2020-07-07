#ifndef _COLLECTIONS_POOL_H_
#define _COLLECTIONS_POOL_H_

#include <stdlib.h>

#include "cerver/collections/dlist.h"

typedef struct Pool {

    DoubleList *dlist;
    void (*destroy)(void *data);

} Pool;

// returns how many elements are inside the pool
extern size_t pool_size (Pool *pool);

// creates a new pool
extern Pool *pool_create (void (*destroy)(void *data));

// uses the create method to populate the pool with n elements
// returns 0 on no error, 1 if at least one element failed to be inserted
extern int pool_init (Pool *pool, 
    void *(*create)(void), unsigned int n_elements);

// deletes the pool and all of its members using the destroy method
extern void pool_delete (Pool *pool);

// destroys all of the pool's elements and their data but keeps the pool
extern void pool_reset (Pool *pool);

// only gets rid of the pool's elements, but the data is kept
// this is usefull if another structure points to the same data
extern void pool_clear (Pool *pool);

// inserts the new data at the end of the pool
// returns 0 on success, 1 on error
extern int pool_push (Pool *pool, void *data);

// returns the data that is first in the pool
extern void *pool_pop (Pool *pool);

#endif