#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include <cerver/collections/dlist.h>

#include "../test.h"

#include "check.h"
#include "data.h"

// bugs that this suite pinned:
// - insert_after in the middle did not set the next element prev link
// - insert_in_order only handled comparator results of -1, 0 and 1
// - insert_at with pos == size inserted at the start
// - get_element ignored its compare argument
// - sort did not update the end nor the prev links, required dlist->compare
//   and its recursive merge could overflow the stack with long lists
// - merge_two did not set the prev link of the joined element
// - split_half did not set the end of the new dlist
// - remove_at, get_at, search, get_element, to_array, copy and clone did not lock
// coverage holes:
// - dlist_internal_move_matches with concurrent writers in both dlists

#define INVARIANTS_SMALL_SIZE				16
#define INVARIANTS_RANDOM_SIZE				1024
#define INVARIANTS_RANDOM_RANGE				256
#define INVARIANTS_LONG_SIZE				65536
#define INVARIANTS_THREADS					8
#define INVARIANTS_THREAD_ITERATIONS		4096

// returns any magnitude, not only -1, 0 and 1, like memcmp () or strcmp ()
static int data_difference_comparator (const void *a, const void *b) {

	const Data *data_a = (const Data *) a;
	const Data *data_b = (const Data *) b;

	return (int) data_a->value - (int) data_b->value;

}

static bool data_value_is_even (const void *a, const void *b) {

	(void) b;

	return (((const Data *) a)->value % 2) == 0;

}

// creates a dlist with values 0 .. size - 1
static DoubleList *invariants_dlist_create (const unsigned int size) {

	DoubleList *dlist = dlist_init (data_delete, data_comparator);
	test_check_ptr (dlist);

	for (unsigned int i = 0; i < size; i++) {
		test_check_int_eq (dlist_insert_at_end (dlist, data_new (i, i)), 0, NULL);
	}

	collections_check_dlist (dlist, size);

	return dlist;

}

// checks that the values match the expected array forwards and backwards
static void invariants_check_values (
	const DoubleList *dlist,
	const unsigned int *expected, const size_t count
) {

	collections_check_dlist (dlist, count);

	size_t idx = 0;
	for (const ListElement *le = dlist->start; le; le = le->next) {
		test_check_unsigned_eq (((Data *) le->data)->value, expected[idx], NULL);
		idx++;
	}

	for (const ListElement *le = dlist->end; le; le = le->prev) {
		idx--;
		test_check_unsigned_eq (((Data *) le->data)->value, expected[idx], NULL);
	}

}

static void invariants_check_sorted (const DoubleList *dlist) {

	const ListElement *le = dlist->start;
	while (le && le->next) {
		const Data *current = (const Data *) le->data;
		const Data *next = (const Data *) le->next->data;

		test_check (current->value <= next->value, "the dlist is not sorted");

		// equal values keep their insertion order
		if (current->value == next->value) {
			test_check (current->idx < next->idx, "the sort is not stable");
		}

		le = le->next;
	}

}

static void test_invariants_insert_after_middle (void) {

	DoubleList *dlist = invariants_dlist_create (4);

	// 0 1 2 3 -> 0 1 100 2 3
	ListElement *second = dlist_get_element_at (dlist, 1);
	test_check_int_eq (dlist_insert_after (dlist, second, data_new (100, 100)), 0, NULL);

	const unsigned int expected[] = { 0, 1, 100, 2, 3 };
	invariants_check_values (dlist, expected, 5);

	// 0 1 100 2 3 -> 0 1 100 2 3 200 using the end as the element
	test_check_int_eq (dlist_insert_after (dlist, dlist->end, data_new (200, 200)), 0, NULL);

	const unsigned int expected_end[] = { 0, 1, 100, 2, 3, 200 };
	invariants_check_values (dlist, expected_end, 6);

	// the removal of the element after the inserted one must keep the links
	ListElement *after = dlist_get_element_at (dlist, 3);
	data_delete (dlist_remove_element (dlist, after));

	const unsigned int expected_remove[] = { 0, 1, 100, 3, 200 };
	invariants_check_values (dlist, expected_remove, 5);

	dlist_delete (dlist);

}

static void test_invariants_insert_before_middle (void) {

	DoubleList *dlist = invariants_dlist_create (4);

	ListElement *third = dlist_get_element_at (dlist, 2);
	test_check_int_eq (dlist_insert_before (dlist, third, data_new (100, 100)), 0, NULL);

	const unsigned int expected[] = { 0, 1, 100, 2, 3 };
	invariants_check_values (dlist, expected, 5);

	dlist_delete (dlist);

}

static void test_invariants_insert_at (void) {

	DoubleList *dlist = invariants_dlist_create (3);

	// at the start
	test_check_int_eq (dlist_insert_at (dlist, data_new (10, 10), 0), 0, NULL);

	// at the middle
	test_check_int_eq (dlist_insert_at (dlist, data_new (20, 20), 2), 0, NULL);

	// pos == size is the end
	test_check_int_eq (dlist_insert_at (dlist, data_new (30, 30), 5), 0, NULL);

	// pos > size is the end
	test_check_int_eq (dlist_insert_at (dlist, data_new (40, 40), 100), 0, NULL);

	const unsigned int expected[] = { 10, 0, 20, 1, 2, 30, 40 };
	invariants_check_values (dlist, expected, 7);

	dlist_delete (dlist);

	// an empty dlist
	DoubleList *empty = dlist_init (data_delete, data_comparator);
	test_check_int_eq (dlist_insert_at (empty, data_new (1, 1), 0), 0, NULL);
	test_check_int_eq (dlist_insert_at (empty, data_new (2, 2), 0), 0, NULL);

	const unsigned int expected_empty[] = { 2, 1 };
	invariants_check_values (empty, expected_empty, 2);

	dlist_delete (empty);

}

static void test_invariants_insert_in_order (void) {

	DoubleList *dlist = dlist_init (data_delete, data_difference_comparator);

	for (unsigned int i = 0; i < INVARIANTS_RANDOM_SIZE; i++) {
		unsigned int value = (unsigned int) rand () % INVARIANTS_RANDOM_RANGE;
		test_check_int_eq (dlist_insert_in_order (dlist, data_new (i, value)), 0, NULL);
	}

	collections_check_dlist (dlist, INVARIANTS_RANDOM_SIZE);
	invariants_check_sorted (dlist);

	dlist_delete (dlist);

}

static void test_invariants_remove_at (void) {

	DoubleList *dlist = invariants_dlist_create (5);

	Data *data = (Data *) dlist_remove_at (dlist, 0);
	test_check_unsigned_eq (data->value, 0, NULL);
	data_delete (data);

	data = (Data *) dlist_remove_at (dlist, 1);
	test_check_unsigned_eq (data->value, 2, NULL);
	data_delete (data);

	data = (Data *) dlist_remove_at (dlist, 2);
	test_check_unsigned_eq (data->value, 4, NULL);
	data_delete (data);

	test_check_null_ptr (dlist_remove_at (dlist, 2));

	const unsigned int expected[] = { 1, 3 };
	invariants_check_values (dlist, expected, 2);

	dlist_delete (dlist);

}

static void test_invariants_get_element_compare (void) {

	// the dlist has no compare, the argument must be used
	DoubleList *dlist = dlist_init (data_delete, NULL);
	for (unsigned int i = 0; i < INVARIANTS_SMALL_SIZE; i++) {
		(void) dlist_insert_at_end (dlist, data_new (i, i));
	}

	Data query = { 7, 7 };
	ListElement *le = dlist_get_element (dlist, &query, data_comparator);
	test_check_ptr (le);
	test_check_unsigned_eq (((Data *) le->data)->value, 7, NULL);

	Data *found = (Data *) dlist_search (dlist, &query, data_comparator);
	test_check_ptr (found);
	test_check_unsigned_eq (found->value, 7, NULL);

	// without any compare there is nothing to search
	test_check_null_ptr (dlist_get_element (dlist, &query, NULL));
	test_check_null_ptr (dlist_search (dlist, &query, NULL));

	Data missing = { 1000, 1000 };
	test_check_null_ptr (dlist_get_element (dlist, &missing, data_comparator));

	test_check_null_ptr (dlist_get_at (dlist, INVARIANTS_SMALL_SIZE));
	test_check_null_ptr (dlist_get_element_at (dlist, INVARIANTS_SMALL_SIZE));

	dlist_delete (dlist);

}

static void test_invariants_sort_random (void) {

	// the dlist has no compare, the argument must be used
	DoubleList *dlist = dlist_init (data_delete, NULL);
	for (unsigned int i = 0; i < INVARIANTS_RANDOM_SIZE; i++) {
		unsigned int value = (unsigned int) rand () % INVARIANTS_RANDOM_RANGE;
		(void) dlist_insert_at_end (dlist, data_new (i, value));
	}

	test_check_int_eq (dlist_sort (dlist, data_difference_comparator), 0, NULL);

	collections_check_dlist (dlist, INVARIANTS_RANDOM_SIZE);
	invariants_check_sorted (dlist);

	// without any compare the sort fails
	test_check_int_eq (dlist_sort (dlist, NULL), 1, NULL);

	dlist_delete (dlist);

}

static void test_invariants_sort_long (void) {

	// a reversed long dlist, the merge must not recurse per element
	DoubleList *dlist = dlist_init (data_delete, data_comparator);
	for (unsigned int i = 0; i < INVARIANTS_LONG_SIZE; i++) {
		(void) dlist_insert_at_end (dlist, data_new (i, INVARIANTS_LONG_SIZE - i));
	}

	test_check_int_eq (dlist_sort (dlist, data_difference_comparator), 0, NULL);

	collections_check_dlist (dlist, INVARIANTS_LONG_SIZE);
	invariants_check_sorted (dlist);

	test_check_unsigned_eq (((Data *) dlist->start->data)->value, 1, NULL);
	test_check_unsigned_eq (((Data *) dlist->end->data)->value, INVARIANTS_LONG_SIZE, NULL);

	dlist_delete (dlist);

}

static void test_invariants_sort_small (void) {

	DoubleList *empty = dlist_init (data_delete, data_comparator);
	test_check_int_eq (dlist_sort (empty, NULL), 0, NULL);
	collections_check_dlist (empty, 0);
	dlist_delete (empty);

	DoubleList *single = invariants_dlist_create (1);
	test_check_int_eq (dlist_sort (single, NULL), 0, NULL);
	collections_check_dlist (single, 1);
	dlist_delete (single);

}

static void test_invariants_merge_two (void) {

	DoubleList *one = invariants_dlist_create (3);
	DoubleList *two = dlist_init (data_delete, data_comparator);
	for (unsigned int i = 10; i < 13; i++) {
		(void) dlist_insert_at_end (two, data_new (i, i));
	}

	dlist_merge_two (one, two);

	const unsigned int expected[] = { 0, 1, 2, 10, 11, 12 };
	invariants_check_values (one, expected, 6);
	collections_check_dlist (two, 0);

	// merging into an empty dlist
	DoubleList *empty = dlist_init (data_delete, data_comparator);
	dlist_merge_two (empty, one);
	invariants_check_values (empty, expected, 6);
	collections_check_dlist (one, 0);

	// merging an empty dlist
	dlist_merge_two (empty, two);
	invariants_check_values (empty, expected, 6);

	// merging a dlist with itself does nothing
	dlist_merge_two (empty, empty);
	invariants_check_values (empty, expected, 6);

	dlist_delete (one);
	dlist_delete (two);
	dlist_delete (empty);

}

static void test_invariants_split_half (void) {

	DoubleList *dlist = invariants_dlist_create (7);

	DoubleList *half = dlist_split_half (dlist);
	test_check_ptr (half);

	const unsigned int first[] = { 0, 1, 2, 3 };
	const unsigned int second[] = { 4, 5, 6 };
	invariants_check_values (dlist, first, 4);
	invariants_check_values (half, second, 3);

	// both halves keep working after the split
	(void) dlist_insert_at_end (half, data_new (7, 7));
	const unsigned int second_after[] = { 4, 5, 6, 7 };
	invariants_check_values (half, second_after, 4);

	dlist_delete (dlist);
	dlist_delete (half);

	// a dlist with a single element can not be split
	DoubleList *single = invariants_dlist_create (1);
	test_check_null_ptr (dlist_split_half (single));
	dlist_delete (single);

}

static void test_invariants_conditions (void) {

	DoubleList *dlist = invariants_dlist_create (INVARIANTS_SMALL_SIZE);

	DoubleList *even = dlist_split_by_condition (dlist, data_value_is_even, NULL);
	test_check_ptr (even);
	collections_check_dlist (dlist, INVARIANTS_SMALL_SIZE / 2);
	collections_check_dlist (even, INVARIANTS_SMALL_SIZE / 2);

	DoubleList *merge = dlist_merge_two_by_condition (dlist, even, data_value_is_even, NULL);
	test_check_ptr (merge);
	collections_check_dlist (merge, INVARIANTS_SMALL_SIZE / 2);
	collections_check_dlist (even, 0);

	unsigned int removed = dlist_remove_by_condition (merge, data_value_is_even, NULL, true);
	test_check_unsigned_eq (removed, INVARIANTS_SMALL_SIZE / 2, NULL);
	collections_check_dlist (merge, 0);

	dlist_delete (dlist);
	dlist_delete (even);
	dlist_delete (merge);

}

static void test_invariants_merge_many (void) {

	DoubleList *many = dlist_init (dlist_delete, NULL);
	for (unsigned int i = 0; i < 4; i++) {
		(void) dlist_insert_at_end (many, invariants_dlist_create (i));
	}

	DoubleList *merge = dlist_merge_many (many);
	test_check_ptr (merge);

	// 0 + 1 + 2 + 3 elements
	collections_check_dlist (merge, 6);

	for (ListElement *le = many->start; le; le = le->next) {
		collections_check_dlist ((DoubleList *) le->data, 0);
	}

	dlist_delete (merge);
	dlist_delete (many);

}

static void test_invariants_copy_clone_array (void) {

	DoubleList *dlist = invariants_dlist_create (INVARIANTS_SMALL_SIZE);

	DoubleList *copy = dlist_copy (dlist);
	collections_check_dlist (copy, INVARIANTS_SMALL_SIZE);
	test_check_ptr_eq (copy->start->data, dlist->start->data);
	dlist_clear_and_delete (copy);

	size_t count = 0;
	void **array = dlist_to_array (dlist, &count);
	test_check_ptr (array);
	test_check (count == INVARIANTS_SMALL_SIZE, NULL);
	test_check_ptr_eq (array[count - 1], dlist->end->data);
	free (array);

	DoubleList *empty = dlist_init (data_delete, data_comparator);
	test_check_null_ptr (dlist_to_array (empty, &count));
	test_check (count == 0, NULL);
	dlist_delete (empty);

	dlist_delete (dlist);

}

typedef struct InvariantsThreadArgs {

	DoubleList *dlist;
	unsigned int thread;

} InvariantsThreadArgs;

// every thread inserts at both ends, in order and in the middle
// and removes the same number of elements it inserted
static void *invariants_thread_mixed (void *args_ptr) {

	InvariantsThreadArgs *args = (InvariantsThreadArgs *) args_ptr;
	DoubleList *dlist = args->dlist;

	for (unsigned int i = 0; i < INVARIANTS_THREAD_ITERATIONS; i++) {
		unsigned int value = (args->thread * INVARIANTS_THREAD_ITERATIONS) + i;

		switch (i % 4) {
			case 0: (void) dlist_insert_at_end (dlist, data_new (value, value)); break;
			case 1: (void) dlist_insert_at_start (dlist, data_new (value, value)); break;
			case 2: (void) dlist_insert_in_order (dlist, data_new (value, value)); break;
			default: (void) dlist_insert_at (dlist, data_new (value, value), i); break;
		}

		if (i % 2) {
			data_delete (dlist_remove_end (dlist));
			data_delete (dlist_remove_start (dlist));
		}

		(void) dlist_get_at (dlist, i);
	}

	return NULL;

}

static void test_invariants_threads (void) {

	DoubleList *dlist = dlist_init (data_delete, data_comparator);

	pthread_t threads[INVARIANTS_THREADS];
	InvariantsThreadArgs args[INVARIANTS_THREADS];
	for (unsigned int i = 0; i < INVARIANTS_THREADS; i++) {
		args[i].dlist = dlist;
		args[i].thread = i;
		test_check_int_eq (pthread_create (&threads[i], NULL, invariants_thread_mixed, &args[i]), 0, NULL);
	}

	for (unsigned int i = 0; i < INVARIANTS_THREADS; i++) {
		(void) pthread_join (threads[i], NULL);
	}

	// every iteration inserts one and every odd iteration removes two
	collections_check_dlist (dlist, 0);

	dlist_delete (dlist);

}

void collections_tests_dlist_invariants (void) {

	(void) printf ("Testing COLLECTIONS dlist invariants...\n");

	test_invariants_insert_after_middle ();
	test_invariants_insert_before_middle ();
	test_invariants_insert_at ();
	test_invariants_insert_in_order ();
	test_invariants_remove_at ();
	test_invariants_get_element_compare ();
	test_invariants_sort_random ();
	test_invariants_sort_long ();
	test_invariants_sort_small ();
	test_invariants_merge_two ();
	test_invariants_split_half ();
	test_invariants_conditions ();
	test_invariants_merge_many ();
	test_invariants_copy_clone_array ();
	test_invariants_threads ();

	(void) printf ("Done!\n");

}
