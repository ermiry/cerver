#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <cerver/collections/avl.h>
#include <cerver/collections/dlist.h>
#include <cerver/collections/htab.h>
#include <cerver/collections/pool.h>

#include "../test.h"

// the methods that use the data of a collection while its mutex is held,
// and the methods of a collection that run with it

#define SAFE_TEST_ELEMENTS				8
#define SAFE_TEST_MISSING				(SAFE_TEST_ELEMENTS + 1)

static int *safe_test_value (const int value) {

	int *data = (int *) malloc (sizeof (int));
	test_check_ptr (data);
	*data = value;

	return data;

}

static int safe_test_compare (const void *one, const void *two) {

	const int first = *(const int *) one;
	const int second = *(const int *) two;

	return (first > second) - (first < second);

}

static void safe_test_copy (void *data, void *args) {

	*(int *) args = *(int *) data;

}

typedef struct SafeTestTraverse {

	const DoubleList *dlist;
	size_t sizes;

} SafeTestTraverse;

// a method of dlist_traverse () that reads the same list: it locked a mutex that the traverse held
static void safe_test_traverse_size (void *data, void *args) {

	(void) data;

	SafeTestTraverse *traverse = (SafeTestTraverse *) args;
	traverse->sizes += dlist_size (traverse->dlist);

}

static void test_safe_dlist (void) {

	DoubleList *dlist = dlist_init (free, safe_test_compare);
	test_check_ptr (dlist);

	for (int i = 0; i < SAFE_TEST_ELEMENTS; i++) {
		test_check_int_eq (dlist_insert_at_end (dlist, safe_test_value (i)), 0, NULL);
	}

	SafeTestTraverse traverse = { dlist, 0 };
	test_check_int_eq (dlist_traverse (dlist, safe_test_traverse_size, &traverse), 0, NULL);
	test_check_unsigned_eq ((unsigned int) traverse.sizes, SAFE_TEST_ELEMENTS * SAFE_TEST_ELEMENTS, NULL);

	const int query = SAFE_TEST_ELEMENTS / 2;
	int copy = -1;
	test_check_int_eq (dlist_get_apply (dlist, &query, NULL, safe_test_copy, &copy), 0, NULL);
	test_check_int_eq (copy, query, NULL);

	const int missing = SAFE_TEST_MISSING;
	test_check_int_eq (dlist_get_apply (dlist, &missing, NULL, safe_test_copy, &copy), 1, NULL);
	test_check_int_eq (dlist_get_apply (dlist, &query, NULL, NULL, &copy), 1, NULL);

	dlist_delete (dlist);

}

static bool safe_test_even (const void *one, const void *two) {

	(void) two;

	return !(*(const int *) one % 2);

}

// without a destroy method, the data that is removed with delete_data is released with free () (a leak before)
static void test_safe_dlist_remove_without_destroy (void) {

	DoubleList *dlist = dlist_init (NULL, NULL);
	test_check_ptr (dlist);

	for (int i = 0; i < SAFE_TEST_ELEMENTS; i++) {
		test_check_int_eq (dlist_insert_at_end (dlist, safe_test_value (i)), 0, NULL);
	}

	test_check_unsigned_eq (
		dlist_remove_with_condition (dlist, safe_test_even, NULL, true), SAFE_TEST_ELEMENTS / 2, NULL
	);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) free (le->data);
	dlist_delete (dlist);

}

static void test_safe_htab (void) {

	Htab *htab = htab_create (HTAB_DEFAULT_INIT_SIZE, NULL, free);
	test_check_ptr (htab);

	for (int i = 0; i < SAFE_TEST_ELEMENTS; i++) {
		test_check_int_eq (htab_insert (htab, &i, sizeof (int), safe_test_value (i), sizeof (int)), 0, NULL);
	}

	const int key = SAFE_TEST_ELEMENTS / 2;
	int copy = -1;
	test_check_unsigned_eq (htab_get_apply (htab, &key, sizeof (int), safe_test_copy, &copy), 0, NULL);
	test_check_int_eq (copy, key, NULL);

	const int missing = SAFE_TEST_MISSING;
	test_check_unsigned_eq (htab_get_apply (htab, &missing, sizeof (int), safe_test_copy, &copy), 1, NULL);
	test_check_unsigned_eq (htab_get_apply (htab, &key, sizeof (int), NULL, &copy), 1, NULL);

	htab_destroy (htab);

}

static int safe_test_avl_compare (const void *one, const void *two) {

	return safe_test_compare (one, two);

}

static void test_safe_avl (void) {

	AVLTree *tree = avl_init (safe_test_avl_compare, free);
	test_check_ptr (tree);

	for (int i = 0; i < SAFE_TEST_ELEMENTS; i++) {
		test_check_unsigned_eq (avl_insert_node (tree, safe_test_value (i)), 0, NULL);
	}

	int query = SAFE_TEST_ELEMENTS / 2;
	int copy = -1;
	test_check_unsigned_eq (avl_get_node_data_apply (tree, &query, NULL, safe_test_copy, &copy), 0, NULL);
	test_check_int_eq (copy, query, NULL);

	int missing = SAFE_TEST_MISSING;
	test_check_unsigned_eq (avl_get_node_data_apply (tree, &missing, NULL, safe_test_copy, &copy), 1, NULL);

	avl_delete (tree);

}

static Pool *safe_test_pool = NULL;
static size_t safe_test_pool_sizes = 0;

// a destroy method that uses its own pool: it locked a mutex that the reset held
static void safe_test_pool_destroy (void *data) {

	safe_test_pool_sizes += pool_size (safe_test_pool);

	free (data);

}

static void test_safe_pool_reset (void) {

	safe_test_pool = pool_create (safe_test_pool_destroy);
	test_check_ptr (safe_test_pool);

	for (int i = 0; i < SAFE_TEST_ELEMENTS; i++) {
		test_check_int_eq (pool_push (safe_test_pool, safe_test_value (i)), 0, NULL);
	}

	pool_reset (safe_test_pool);
	test_check_unsigned_eq ((unsigned int) pool_size (safe_test_pool), 0, NULL);
	test_check_unsigned_eq ((unsigned int) safe_test_pool_sizes, 0, "the destroy method ran before the pool was emptied");

	pool_delete (safe_test_pool);

}

void collections_tests_safe (void) {

	(void) printf ("Testing COLLECTIONS safe...\n");

	test_safe_dlist ();
	test_safe_dlist_remove_without_destroy ();
	test_safe_htab ();
	test_safe_avl ();
	test_safe_pool_reset ();

	(void) printf ("Done!\n");

}
