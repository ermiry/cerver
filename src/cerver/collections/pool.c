#include <stdlib.h>

#include "cerver/collections/pool.h"
#include "cerver/collections/dlist.h"

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

// returns how many elements are inside the pool
size_t pool_size (Pool *pool) {

	size_t retval = 0;

	if (pool) {
		retval = dlist_size (pool->dlist);
	}

	return retval;

}

Pool *pool_create (void (*destroy)(void *data)) {

	Pool *pool = pool_new ();
	if (pool) {
		pool->dlist = dlist_init (destroy, NULL);
		pool->destroy = destroy;
	}

	return pool;

}

// uses the create method to populate the pool with n elements
// returns 0 on no error, 1 if at least one element failed to be inserted
int pool_init (Pool *pool,
	void *(*create)(void), unsigned int n_elements) {

	int errors = 0;

	for (unsigned int i = 0; i < n_elements; i++) {
		errors |= dlist_insert_after (
			pool->dlist,
			dlist_end (pool->dlist),
			create ()
		);
	}

	return errors;

}

void pool_delete (Pool *pool) {

	if (pool) {
		dlist_delete (pool->dlist);

		free (pool);
	}

}

// destroys all of the dlist's elements and their data but keeps the dlist
void pool_reset (Pool *pool) {

	if (pool) {
		dlist_reset (pool->dlist);
	}

}

// only gets rid of the list elements, but the data is kept
// this is usefull if another dlist or structure points to the same data
void pool_clear (Pool *pool) {

	if (pool) {
		dlist_clear (pool->dlist);
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