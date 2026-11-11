#include <stdlib.h>

#include <pthread.h>

#include "cerver/collections/pool.h"

// the capacity is doubled each time the pool grows
#define POOL_GROWTH_FACTOR			2

#pragma region internal

static Pool *pool_new (void) {

	Pool *pool = (Pool *) malloc (sizeof (Pool));
	if (pool) {
		pool->elements = (void **) malloc (POOL_DEFAULT_CAPACITY * sizeof (void *));
		pool->count = 0;
		pool->capacity = pool->elements ? POOL_DEFAULT_CAPACITY : 0;

		pool->max_elements = POOL_NO_LIMIT;

		(void) pthread_mutex_init (&pool->mutex, NULL);

		pool->destroy = NULL;
		pool->create = NULL;

		pool->produce = false;
	}

	return pool;

}

// makes room for one more element, called with the pool's mutex
// returns 0 on success, 1 if the pool could not grow
static int pool_grow (Pool *pool) {

	int retval = 1;

	const size_t capacity = pool->capacity ?
		(pool->capacity * POOL_GROWTH_FACTOR) : POOL_DEFAULT_CAPACITY;

	void **elements = (void **) realloc (pool->elements, capacity * sizeof (void *));
	if (elements) {
		pool->elements = elements;
		pool->capacity = capacity;
		retval = 0;
	}

	return retval;

}

static bool pool_is_full (const Pool *pool) {

	return (pool->max_elements != POOL_NO_LIMIT) && (pool->count >= pool->max_elements);

}

static void pool_destroy_element (const Pool *pool, void *data) {

	if (pool->destroy) pool->destroy (data);

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

// sets the most elements that the pool keeps (POOL_NO_LIMIT by default),
// an element that is pushed to a full pool is destroyed with the destroy method
void pool_set_max_elements (Pool *pool, size_t max_elements) {

	if (pool) {
		(void) pthread_mutex_lock (&pool->mutex);

		pool->max_elements = max_elements;

		(void) pthread_mutex_unlock (&pool->mutex);
	}

}

// returns how many elements are inside the pool
size_t pool_size (Pool *pool) {

	size_t size = 0;

	if (pool) {
		(void) pthread_mutex_lock (&pool->mutex);

		size = pool->count;

		(void) pthread_mutex_unlock (&pool->mutex);
	}

	return size;

}

Pool *pool_create (void (*destroy)(void *data)) {

	Pool *pool = pool_new ();
	if (pool) {
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
				void *element = produce ();
				if (element) errors |= pool_push (pool, element);
				else errors = 1;
			}

			retval = errors;
		}
	}

	return retval;

}

// returns an element to the pool, it is the next one that pool_pop () takes
// an element that does not fit in a full pool is destroyed
// returns 0 on success, 1 on error
int pool_push (Pool *pool, void *data) {

	int retval = 1;

	if (pool && data) {
		bool discard = false;

		(void) pthread_mutex_lock (&pool->mutex);

		if (pool_is_full (pool)) {
			discard = true;
			retval = 0;
		}

		else if ((pool->count < pool->capacity) || !pool_grow (pool)) {
			pool->elements[pool->count] = data;
			pool->count += 1;
			retval = 0;
		}

		(void) pthread_mutex_unlock (&pool->mutex);

		// the destroy method runs without the pool's lock
		if (discard) pool_destroy_element (pool, data);
	}

	return retval;

}

// returns the last element that was pushed to the pool
void *pool_pop (Pool *pool) {

	void *retval = NULL;

	if (pool) {
		(void) pthread_mutex_lock (&pool->mutex);

		if (pool->count) {
			pool->count -= 1;
			retval = pool->elements[pool->count];
		}

		(void) pthread_mutex_unlock (&pool->mutex);

		if (!retval && pool->produce && pool->create) {
			retval = pool->create ();
		}
	}

	return retval;

}

// only gets rid of the pool's elements, but the data is kept
// this is usefull if another structure points to the same data
void pool_clear (Pool *pool) {

	if (pool) {
		(void) pthread_mutex_lock (&pool->mutex);

		pool->count = 0;

		(void) pthread_mutex_unlock (&pool->mutex);
	}

}

// destroys all of the pool's elements and their data but keeps the pool
void pool_reset (Pool *pool) {

	if (pool) {
		(void) pthread_mutex_lock (&pool->mutex);

		for (size_t index = 0; index < pool->count; index++) {
			pool_destroy_element (pool, pool->elements[index]);
		}

		pool->count = 0;

		(void) pthread_mutex_unlock (&pool->mutex);
	}

}

// deletes the pool and all of its members using the destroy method
void pool_delete (Pool *pool) {

	if (pool) {
		pool_reset (pool);

		free (pool->elements);

		(void) pthread_mutex_destroy (&pool->mutex);

		free (pool);
	}

}
