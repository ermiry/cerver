#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include <cerver/collections/dlist.h>
#include <cerver/collections/pool.h>

#include "../test.h"

#define POOL_TEST_INITIAL_ELEMENTS			64
#define POOL_TEST_THREADS					8
#define POOL_TEST_ITERATIONS				100000

typedef struct PoolTestElement {

	unsigned int value;

} PoolTestElement;

static void *pool_test_element_new (void) {

	PoolTestElement *element = (PoolTestElement *) malloc (sizeof (PoolTestElement));
	if (element) element->value = 0;

	return element;

}

static void pool_test_element_delete (void *element_ptr) {

	if (element_ptr) free (element_ptr);

}

// walks the pool's list and checks that size, start and end are consistent
static void pool_test_check_list (const Pool *pool, const size_t expected_size) {

	const DoubleList *list = pool->dlist;

	test_check_unsigned_eq ((unsigned int) list->size, (unsigned int) expected_size, NULL);

	if (expected_size) {
		test_check_ptr (list->start);
		test_check_ptr (list->end);
		test_check_null_ptr (list->start->prev);
		test_check_null_ptr (list->end->next);
	}

	else {
		test_check_null_ptr (list->start);
		test_check_null_ptr (list->end);
	}

	size_t count = 0;
	const ListElement *last = NULL;
	for (const ListElement *element = list->start; element; element = element->next) {
		test_check_ptr (element->data);
		test_check (element->prev == last, NULL);

		last = element;
		count += 1;
	}

	test_check_unsigned_eq ((unsigned int) count, (unsigned int) expected_size, NULL);
	test_check (last == list->end, NULL);

}

static void test_pool_push_pop_single (void) {

	Pool *pool = pool_create (pool_test_element_delete);
	test_check_ptr (pool);

	test_check_int_eq (pool_init (pool, pool_test_element_new, POOL_TEST_INITIAL_ELEMENTS), 0, NULL);
	pool_test_check_list (pool, POOL_TEST_INITIAL_ELEMENTS);

	void *element = pool_pop (pool);
	test_check_ptr (element);
	pool_test_check_list (pool, POOL_TEST_INITIAL_ELEMENTS - 1);

	test_check_int_eq (pool_push (pool, element), 0, NULL);
	pool_test_check_list (pool, POOL_TEST_INITIAL_ELEMENTS);

	// an empty pool that can not produce returns NULL
	pool_reset (pool);
	pool_test_check_list (pool, 0);
	test_check_null_ptr (pool_pop (pool));

	// an empty pool that can produce creates a new element
	pool_set_create (pool, pool_test_element_new);
	pool_set_produce_if_empty (pool, true);
	element = pool_pop (pool);
	test_check_ptr (element);
	pool_test_element_delete (element);

	// NULL data is never pushed
	test_check_int_eq (pool_push (pool, NULL), 1, NULL);
	pool_test_check_list (pool, 0);

	pool_delete (pool);

}

// each thread pops an element and pushes it back,
// the pool must end with the same elements and a consistent list
static void *pool_test_borrow_thread (void *pool_ptr) {

	Pool *pool = (Pool *) pool_ptr;

	for (unsigned int i = 0; i < POOL_TEST_ITERATIONS; i++) {
		PoolTestElement *element = (PoolTestElement *) pool_pop (pool);
		if (element) {
			element->value += 1;
			(void) pool_push (pool, element);
		}
	}

	return NULL;

}

static void test_pool_push_pop_threads (void) {

	Pool *pool = pool_create (pool_test_element_delete);
	test_check_ptr (pool);

	// few elements for many threads, so the pool gets empty all the time
	test_check_int_eq (pool_init (pool, pool_test_element_new, POOL_TEST_THREADS / 2), 0, NULL);

	pthread_t threads[POOL_TEST_THREADS];
	for (unsigned int i = 0; i < POOL_TEST_THREADS; i++) {
		test_check_int_eq (pthread_create (&threads[i], NULL, pool_test_borrow_thread, pool), 0, NULL);
	}

	for (unsigned int i = 0; i < POOL_TEST_THREADS; i++) {
		test_check_int_eq (pthread_join (threads[i], NULL), 0, NULL);
	}

	pool_test_check_list (pool, POOL_TEST_THREADS / 2);

	pool_delete (pool);

}

// half of the threads produce new elements and the other half consume them
static void *pool_test_producer_thread (void *pool_ptr) {

	Pool *pool = (Pool *) pool_ptr;

	for (unsigned int i = 0; i < POOL_TEST_ITERATIONS; i++) {
		(void) pool_push (pool, pool_test_element_new ());
	}

	return NULL;

}

static void *pool_test_consumer_thread (void *pool_ptr) {

	Pool *pool = (Pool *) pool_ptr;

	size_t *consumed = (size_t *) malloc (sizeof (size_t));
	*consumed = 0;

	for (unsigned int i = 0; i < POOL_TEST_ITERATIONS; i++) {
		void *element = pool_pop (pool);
		if (element) {
			pool_test_element_delete (element);
			*consumed += 1;
		}
	}

	return consumed;

}

static void test_pool_producers_consumers_threads (void) {

	Pool *pool = pool_create (pool_test_element_delete);
	test_check_ptr (pool);

	pthread_t producers[POOL_TEST_THREADS / 2];
	pthread_t consumers[POOL_TEST_THREADS / 2];
	for (unsigned int i = 0; i < POOL_TEST_THREADS / 2; i++) {
		test_check_int_eq (pthread_create (&producers[i], NULL, pool_test_producer_thread, pool), 0, NULL);
		test_check_int_eq (pthread_create (&consumers[i], NULL, pool_test_consumer_thread, pool), 0, NULL);
	}

	size_t total_consumed = 0;
	for (unsigned int i = 0; i < POOL_TEST_THREADS / 2; i++) {
		void *consumed = NULL;
		test_check_int_eq (pthread_join (producers[i], NULL), 0, NULL);
		test_check_int_eq (pthread_join (consumers[i], &consumed), 0, NULL);

		total_consumed += *(size_t *) consumed;
		free (consumed);
	}

	const size_t total_produced = (size_t) (POOL_TEST_THREADS / 2) * POOL_TEST_ITERATIONS;
	pool_test_check_list (pool, total_produced - total_consumed);

	pool_delete (pool);

}

void collections_tests_pool (void) {

	(void) printf ("Testing COLLECTIONS pool...\n");

	test_pool_push_pop_single ();

	test_pool_push_pop_threads ();

	test_pool_producers_consumers_threads ();

	(void) printf ("Done!\n");

}
