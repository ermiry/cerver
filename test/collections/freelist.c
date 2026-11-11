#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include <cerver/collections/freelist.h>

#include "../test.h"

#define FREELIST_TEST_LIMIT					16
#define FREELIST_TEST_THREADS				8
#define FREELIST_TEST_ITERATIONS			100000
#define FREELIST_TEST_THREAD_ELEMENTS		4

typedef struct FreeListTestElement {

	unsigned int value;

	struct FreeListTestElement *next;

} FreeListTestElement;

static unsigned int freelist_test_destroyed = 0;

static void freelist_test_element_destroy (void *element_ptr) {

	freelist_test_destroyed += 1;

	free (element_ptr);

}

static FreeListTestElement *freelist_test_element_new (const unsigned int value) {

	FreeListTestElement *element = (FreeListTestElement *) malloc (sizeof (FreeListTestElement));
	test_check_ptr (element);

	element->value = value;
	element->next = NULL;

	return element;

}

static void test_freelist_empty (void) {

	FreeList list = FREELIST_INITIALIZER (FreeListTestElement, next, FREELIST_TEST_LIMIT);

	test_check_null_ptr (freelist_pop (&list));
	test_check_unsigned_eq (freelist_size (&list), 0, NULL);

	test_check_null_ptr (freelist_pop (NULL));
	test_check_unsigned_eq (freelist_push (NULL, NULL), 1, NULL);
	test_check_unsigned_eq (freelist_push (&list, NULL), 1, NULL);

	freelist_drain (&list, freelist_test_element_destroy);

}

// the last element that was pushed is the first one that is popped
static void test_freelist_order (void) {

	FreeList list = FREELIST_INITIALIZER (FreeListTestElement, next, FREELIST_TEST_LIMIT);

	FreeListTestElement *first = freelist_test_element_new (1);
	FreeListTestElement *second = freelist_test_element_new (2);

	test_check_unsigned_eq (freelist_push (&list, first), 0, NULL);
	test_check_unsigned_eq (freelist_push (&list, second), 0, NULL);
	test_check_unsigned_eq (freelist_size (&list), 2, NULL);

	FreeListTestElement *popped = (FreeListTestElement *) freelist_pop (&list);
	test_check_ptr_eq (popped, second);
	test_check_null_ptr (popped->next);

	popped = (FreeListTestElement *) freelist_pop (&list);
	test_check_ptr_eq (popped, first);
	test_check_null_ptr (popped->next);

	test_check_null_ptr (freelist_pop (&list));

	free (first);
	free (second);

}

// a full list does not take the element, the caller releases it
static void test_freelist_limit (void) {

	FreeList list = FREELIST_INITIALIZER (FreeListTestElement, next, FREELIST_TEST_LIMIT);

	for (unsigned int i = 0; i < FREELIST_TEST_LIMIT; i++) {
		test_check_unsigned_eq (freelist_push (&list, freelist_test_element_new (i)), 0, NULL);
	}

	FreeListTestElement *extra = freelist_test_element_new (FREELIST_TEST_LIMIT);
	test_check_unsigned_eq (freelist_push (&list, extra), 1, NULL);
	test_check_unsigned_eq (freelist_size (&list), FREELIST_TEST_LIMIT, NULL);
	free (extra);

	freelist_test_destroyed = 0;
	freelist_drain (&list, freelist_test_element_destroy);
	test_check_unsigned_eq (freelist_test_destroyed, FREELIST_TEST_LIMIT, NULL);
	test_check_unsigned_eq (freelist_size (&list), 0, NULL);
	test_check_null_ptr (freelist_pop (&list));

}

// a list without space never keeps an element
static void test_freelist_disabled (void) {

	FreeList list = FREELIST_INITIALIZER (FreeListTestElement, next, 0);

	FreeListTestElement *element = freelist_test_element_new (0);
	test_check_unsigned_eq (freelist_push (&list, element), 1, NULL);
	test_check_null_ptr (freelist_pop (&list));
	free (element);

}

static FreeList freelist_test_shared = FREELIST_INITIALIZER (
	FreeListTestElement, next, FREELIST_TEST_THREADS * FREELIST_TEST_THREAD_ELEMENTS
);

// every thread pops its elements and pushes them back,
// an element is never owned by two threads at the same time
static void *freelist_test_thread (void *args) {

	(void) args;

	FreeListTestElement *owned[FREELIST_TEST_THREAD_ELEMENTS] = { NULL };

	for (unsigned int i = 0; i < FREELIST_TEST_ITERATIONS; i++) {
		unsigned int n_owned = 0;
		for (unsigned int j = 0; j < FREELIST_TEST_THREAD_ELEMENTS; j++) {
			FreeListTestElement *element = (FreeListTestElement *) freelist_pop (&freelist_test_shared);
			if (element) {
				test_check_unsigned_eq (__atomic_add_fetch (&element->value, 1, __ATOMIC_ACQ_REL), 1, NULL);
				owned[n_owned++] = element;
			}
		}

		for (unsigned int j = 0; j < n_owned; j++) {
			test_check_unsigned_eq (__atomic_sub_fetch (&owned[j]->value, 1, __ATOMIC_ACQ_REL), 0, NULL);
			test_check_unsigned_eq (freelist_push (&freelist_test_shared, owned[j]), 0, NULL);
		}
	}

	return NULL;

}

static void test_freelist_threads (void) {

	const unsigned int n_elements = FREELIST_TEST_THREADS * FREELIST_TEST_THREAD_ELEMENTS;
	for (unsigned int i = 0; i < n_elements; i++) {
		test_check_unsigned_eq (freelist_push (&freelist_test_shared, freelist_test_element_new (0)), 0, NULL);
	}

	pthread_t threads[FREELIST_TEST_THREADS];
	for (unsigned int i = 0; i < FREELIST_TEST_THREADS; i++) {
		test_check_int_eq (pthread_create (&threads[i], NULL, freelist_test_thread, NULL), 0, NULL);
	}

	for (unsigned int i = 0; i < FREELIST_TEST_THREADS; i++) {
		(void) pthread_join (threads[i], NULL);
	}

	test_check_unsigned_eq (freelist_size (&freelist_test_shared), n_elements, NULL);

	freelist_test_destroyed = 0;
	freelist_drain (&freelist_test_shared, freelist_test_element_destroy);
	test_check_unsigned_eq (freelist_test_destroyed, n_elements, NULL);

}

void collections_tests_freelist (void) {

	(void) printf ("Testing FREELIST...\n");

	test_freelist_empty ();
	test_freelist_order ();
	test_freelist_limit ();
	test_freelist_disabled ();
	test_freelist_threads ();

	(void) printf ("Done!\n");

}
