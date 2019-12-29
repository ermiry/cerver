#ifndef _COLLECTIONS_POOL_H_
#define _COLLECTIONS_POOL_H_

#include <stdlib.h>

typedef struct PoolMember {

    void *data;
    struct PoolMember *next;

} PoolMember;

typedef struct Pool {

    size_t size;
    PoolMember *top;
    void (*destroy)(void *data);

} Pool;

#define POOL_SIZE(pool) ((pool)->size)

#define POOL_TOP(pool) ((pool)->top)

#define POOL_DATA(member) ((member)->data)

// Creates a new pool
extern Pool *pool_init (void (*destroy)(void *data));

// Inserts a the data as a new pool element at the top of the pool
extern void pool_push (Pool *pool, void *data);

// Returns the data of the pool element at the top of the pool
extern void *pool_pop (Pool *pool);

// Deletes the pool and all of its memebers using the destroy method
extern void pool_delete (Pool *pool);

#endif