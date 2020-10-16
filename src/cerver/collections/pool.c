#include <stdlib.h>

#include "cerver/collections/pool.h"
#include "cerver/collections/dlist.h"

#pragma region internal

static Pool *pool_new (void) {

	Pool *pool = (Pool *) malloc (sizeof (Pool));
	if (pool) {
		pool->dlist = NULL;

		pool->destroy = NULL;
		pool->create = NULL;

		pool->produce = false;
	}

	return pool;

}

#pragma endregion

// sets a destroy method to be used by the pool to correctly dispose data
void pool_set_destroy (Pool *pool, void (*destroy)(void *data)) {

	if (pool) pool->destroy = destroy;

}

// sets a create method to be used by the pool to correctly allocate new data
void pool_set_create (Pool *pool, void *(*create)(void)) {

	if (pool) pool->create = create;

}

// sets the pool's ability to produce a element when a pop request is done and the pool is empty
// the pool will use its create method to allocate a new element and fullfil the request
void pool_set_produce_if_empty (Pool *pool, bool produce) {

	if (pool) pool->produce = produce;

}

// returns how many elements are inside the pool
size_t pool_size (Pool *pool) {

	return pool ? dlist_size (pool->dlist) : 0;

}

Pool *pool_create (void (*destroy)(void *data)) {

	Pool *pool = pool_new ();
	if (pool) {
		pool->dlist = dlist_init (destroy, NULL);
		pool->destroy = destroy;
	}

	return pool;

}

// uses the create method to populate the pool with n elements (NULL to use pool's set method)
// returns 0 on no error, 1 if at least one element failed to be inserted
int pool_init (
	Pool *pool,
	void *(*create)(void), unsigned int n_elements
) {

	int retval = 1;

	if (pool) {
		void *(*produce)(void) = create ? create : pool->create;
		if (produce) {
			int errors = 0;

			for (unsigned int i = 0; i < n_elements; i++) {
				errors |= dlist_insert_after (
					pool->dlist,
					dlist_end (pool->dlist),
					produce ()
				);
			}
		}
	}

	return retval;

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

		if (!retval && pool->produce) {
			retval = pool->create ();
		}
	}

	return retval;

}

// only gets rid of the list elements, but the data is kept
// this is usefull if another structure points to the same data
void pool_clear (Pool *pool) {

	if (pool) {
		dlist_clear (pool->dlist);
	}

}

// destroys all of the dlist's elements and their data but keeps the pool
void pool_reset (Pool *pool) {

	if (pool) {
		dlist_reset (pool->dlist);
	}

}

// deletes the pool and all of its members using the destroy method
void pool_delete (Pool *pool) {

	if (pool) {
		dlist_delete (pool->dlist);

		free (pool);
	}

}