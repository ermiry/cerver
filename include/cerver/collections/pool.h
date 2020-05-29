#ifndef _COLLECTIONS_POOL_H_
#define _COLLECTIONS_POOL_H_

#include <stdlib.h>

#include "cerver/collections/dllist.h"

typedef struct Pool {

    DoubleList *dlist;
    void (*destroy)(void *data);

} Pool;

// creates a new pool
extern Pool *pool_init (void (*destroy)(void *data));

// inserts the new data at the end of the pool
// returns 0 on success, 1 on error
extern int pool_push (Pool *pool, void *data);

// returns the data that is first in the pool
extern void *pool_pop (Pool *pool);

// deletes the pool and all of its members using the destroy method
extern void pool_delete (Pool *pool);

#endif