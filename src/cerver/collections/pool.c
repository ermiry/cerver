#include <stdlib.h>

#include "cerver/collections/pool.h"
#include "cerver/collections/dllist.h"

#pragma region internal

static Pool *pool_new (void) {

    Pool *pool = (Pool *) malloc (sizeof (Pool));
    if (pool) {
        pool->dlist = NULL;
        pool->destroy = NULL;
    }

    return pool;

}

#pragma endregion

Pool *pool_init (void (*destroy)(void *data)) {

    Pool *pool = pool_new ();
    if (pool) {
        pool->dlist = dlist_init (destroy, NULL);
        pool->destroy = destroy;
    }

    return pool;

}

void pool_delete (Pool *pool) {

    if (pool) {
        dlist_delete (pool->dlist);

        free (pool);
    }

}

int pool_push (Pool *pool, void *data) {

    int retval = 1;

    if (pool && data) {
        retval = dlist_insert_after (
            pool->dlist,
            dlist_end (pool->dlist),
            data
        );
    }

    return retval;

}

void *pool_pop (Pool *pool) {

    void *retval = NULL;

    if (pool) {
        retval = dlist_remove_element (
            pool->dlist,
            NULL
        );
    }

    return retval;

}