#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <pthread.h>

#include <cerver/collections/dlist.h>

#pragma region integer

typedef struct { int value; } Integer;

static Integer *integer_new (int value) {

	Integer *integer = (Integer *) malloc (sizeof (Integer));
	if (integer) integer->value = value;
	return integer;

}

static void integer_delete (void *integer_ptr) {

	if (integer_ptr) free (integer_ptr);

}

static int integer_comparator (const void *one, const void *two) {

	Integer *int_a = (Integer *) one;
	Integer *int_b = (Integer *) two;

	if (int_a->value < int_b->value) return -1;
	else if (int_a->value == int_b->value) return 0;
	return 1;

}

static void *integer_clone (const void *original) {

	Integer *cloned_integer = NULL;

	if (original) {
		cloned_integer = integer_new (((Integer *) original)->value);
	}

	return cloned_integer;

}

#pragma endregion

#pragma region main

static int dlist_test_empty (void) {

	printf ("dlist_is_empty () & dlist_is_not_empty ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	if (dlist_is_empty (dlist)) printf ("BEFORE - List is empty\n");
	if (dlist_is_not_empty (dlist)) printf ("BEFORE - List is not empty\n");

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	if (dlist_is_empty (dlist)) printf ("AFTER - List is empty\n");
	if (dlist_is_not_empty (dlist)) printf ("AFTER - List is not empty\n");

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_delete_if_empty (void) {

	int errors = 0;

	printf ("dlist_delete_if_empty ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	if (!dlist_delete_if_empty (dlist)) {
		printf ("\n\nDeleted dlist BUT it is not empty!");
		dlist = NULL;

		errors = 1;
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return errors;

}

static int dlist_test_reset (void) {

	int errors = 0;

	printf ("dlist_reset ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	printf ("%4d", ((Integer *) le->data)->value);
	// }

	dlist_reset (dlist);

	printf ("\ndlist size after reset: %ld\n", dlist->size);
	if (dlist->size) errors = 1;

	if (!dlist->start && !dlist->end) {
		printf ("\ndlist has NULL start & NULL end!\n");
	}

	else {
		printf ("\ndlist start or end are not NULL!\n");
		errors = 1;
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return errors;

}

#pragma endregion

#pragma region insert

static int dlist_test_insert_before_at_start (void) {

	printf ("dlist_insert_before () at START\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_end (void) {

	printf ("dlist_insert_before () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before (dlist, dlist_end (dlist), integer);

		// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		// 	printf ("%4d", ((Integer *) le->data)->value);
		// }

		// printf ("\n");
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_middle (void) {

	printf ("dlist_insert_before () at MIDDLE\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 5; i++) {
		integer = integer_new (i);
		dlist_insert_before (dlist, dlist_end (dlist), integer);
	}

	for (unsigned int i = 5; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_start_unsafe (void) {

	printf ("dlist_insert_before () at START UNSAFE\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_end_unsafe (void) {

	printf ("dlist_insert_before () at END UNSAFE\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_at_start (void) {

	printf ("dlist_insert_after () at START\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_at_end (void) {

	printf ("dlist_insert_after () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_unsafe_at_start (void) {

	printf ("dlist_insert_after_unsafe () at START\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_unsafe_at_end (void) {

	printf ("dlist_insert_after_unsafe () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at (void) {

	printf ("dlist_insert_at ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (unsigned int i = 0; i < 5; i++) {
		integer = integer_new (i);
		dlist_insert_at (dlist, integer, 2);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_start (void) {

	printf ("dlist_insert_at_start ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_start (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_start_unsafe (void) {

	printf ("dlist_insert_at_start_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_start_unsafe (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_end (void) {

	printf ("dlist_insert_at_end ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_end_unsafe (void) {

	printf ("dlist_insert_at_end_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_in_order (void) {

	printf ("dlist_test_insert_in_order ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	// test insert at
	printf ("\nInsert 100 random numbers\n");
	Integer *integer = NULL;
	for (unsigned int i = 0; i < 100; i++) {
		integer = integer_new (i);
		integer->value = rand () % 999 + 1;
		// printf ("%d\n", integer->value);
		dlist_insert_in_order (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region remove

static int dlist_test_remove (void) {

	printf ("dlist_remove ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	printf ("%4d", ((Integer *) le->data)->value);
	// }

	Integer query = { 0 };
	for (unsigned int i = 0; i < 10; i++) {
		query.value = i;
		dlist_remove (dlist, &query, NULL) ;
	}

	printf ("dlist_remove () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_start (void) {

	printf ("dlist_remove_start ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	printf ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_start (dlist);
	}

	printf ("dlist_remove_start () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_start_unsafe (void) {

	printf ("dlist_remove_start_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	printf ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_start_unsafe (dlist);
	}

	printf ("dlist_remove_start_unsafe () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_end (void) {

	printf ("dlist_remove_end ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	printf ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_end (dlist);
	}

	printf ("dlist_remove_end () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_end_unsafe (void) {

	printf ("dlist_remove_end_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	printf ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_end_unsafe (dlist);
	}

	printf ("dlist_remove_end_unsafe () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static bool dlist_test_remove_by_condition_less_than_condition (
	const void *a, const void *b
) {

	Integer *integer_a = (Integer *) a;
	Integer *integer_b = (Integer *) b;

	if (integer_a->value < integer_b->value) return true;

	return false;

}

static bool dlist_test_remove_by_condition_greater_than_condition (
	const void *a, const void *b
) {

	Integer *integer_a = (Integer *) a;
	Integer *integer_b = (Integer *) b;

	if (integer_a->value > integer_b->value) return true;

	return false;

}

static int dlist_test_remove_by_condition (void) {

	printf ("dlist_remove_by_condition ()\n");

	DoubleList *list = dlist_init (integer_delete, integer_comparator);

	printf ("Insert 10 numbers:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	ListElement *le = NULL;
	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	Integer match = { 4 };
	unsigned int matches = dlist_remove_by_condition (
		list, dlist_test_remove_by_condition_less_than_condition, &match, true
	);

	printf ("\n\nRemoved %d elements smaller than 4:\n", matches);
	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	matches = dlist_remove_by_condition (
		list, dlist_test_remove_by_condition_greater_than_condition, &match, true
	);
	printf ("\n\nRemoved %d elements greater than 4:\n", matches);
	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (list);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int test_remove_at (void) {

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 10; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	// print list
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	// get 10 random values from the list
	// for (unsigned int i = 0; i < 5; i++) {
		// unsigned int idx = rand () % 9 + 1;
		unsigned idx = 7;

		// printf ("Removing element at idx: %d...\n", idx);
		// ListElement *le = dlist_get_element_at (list, idx);
		// if (le) {
		// 	Integer *integer = (Integer *) le->data;
		// 	printf ("%3d\n", integer->value);
		// }

		Integer *integer = (Integer *) dlist_remove_at (list, idx);
		// if (integer) printf ("%3d\n", integer->value);
	// }

	printf ("\n\n");

	// print list
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	dlist_delete (list);

	return 0;

}

#pragma endregion

#pragma region double

static int test_insert_end_remove_start (void) {

	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 101; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);

		if (i < 100) {
			Integer *query = (Integer *) malloc (sizeof (int));
			query->value = i;

			free (dlist_remove (list, query, NULL));
			// free (dlist_remove_element (list, NULL));
		}
	}

	printf ("\nRemaining list item: %d -- size: %ld\n", ((Integer *) list->start->data)->value, list->size);

	dlist_delete (list);

	return 0;

}

static int test_insert_end_remove_end (void) {

	DoubleList *list = dlist_init (free, integer_comparator);

	for (int i = 0; i < 100; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	for (int i = 0; i < 50; i++) {
		free (dlist_remove_element (list, list->end));
	}

	printf ("\nRemaining list item: %d -- size: %ld\n", ((Integer *) list->start->data)->value, list->size);

	dlist_delete (list);

	return 0;

}

static int test_insert_and_remove (void) {

	printf ("test_insert_and_remove ()\n");

	DoubleList *list = dlist_init (free, integer_comparator);

	printf ("Insert 10 numbers:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	ListElement *le = NULL;
	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nRemove 5 from the end:\n");
	for (int i = 0; i < 5; i++) {
		free (dlist_remove_element (list, list->end));
	}

	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\n");
	printf ("Remove 2 from the start:\n");
	for (int i = 0; i < 2; i++) {
		free (dlist_remove_start (list));
	}

	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}
	printf ("\n\n");

	printf ("Remove 2 from the start again:\n");
	for (int i = 0; i < 2; i++) {
		free (dlist_remove_element (list, NULL));
	}

	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}
	printf ("\n\n");

	dlist_delete (list);

	printf ("----------------------------------------\n");

	return 0;

}

static int test_insert_and_remove_unsafe (void) {

	printf ("test_insert_and_remove_unsafe ()\n\n");

	DoubleList *list = dlist_init (free, integer_comparator);

	printf ("Insert 10 numbers:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_at_end_unsafe (list, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nRemove 5 from the end:\n");
	for (int i = 0; i < 5; i++) {
		free (dlist_remove_end_unsafe (list));
	}

	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\n");
	printf ("Remove 2 from the start:\n");
	for (int i = 0; i < 2; i++) {
		free (dlist_remove_start_unsafe (list));
	}

	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}
	printf ("\n\n");

	dlist_delete (list);

	printf ("----------------------------------------\n");

	return 0;

}

#pragma region

#pragma region get

static int dlist_test_get_at (void) {

	printf ("dlist_get_at ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	Integer *two = (Integer *) dlist_get_at (dlist, 2);
	printf ("two %d\n", two->value);

	Integer *four = (Integer *) dlist_get_at (dlist, 4);
	printf ("four %d\n", four->value);

	Integer *seven = (Integer *) dlist_get_at (dlist, 7);
	printf ("seven %d\n", seven->value);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region traverse

static void test_traverse_method (void *list_element_data, void *method_args) {

	printf ("%4d", ((Integer *) list_element_data)->value);

}

static void *test_traverse_method_thread (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		dlist_traverse (list, test_traverse_method, NULL);
		printf ("\n\n");
	}

}

static int dlist_test_traverse (void) {

	printf ("dlist_traverse ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	dlist_traverse (dlist, test_traverse_method, NULL);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_traverse_threads (void) {

	printf ("dlist_traverse () THREADS\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	// create 4 threads
	const unsigned int N_THREADS = 4;
	pthread_t threads[N_THREADS];

 	(void) pthread_create (&threads[0], NULL, test_traverse_method_thread, dlist);
 	(void) pthread_create (&threads[1], NULL, test_traverse_method_thread, dlist);
 	(void) pthread_create (&threads[2], NULL, test_traverse_method_thread, dlist);
 	(void) pthread_create (&threads[3], NULL, test_traverse_method_thread, dlist);
 	(void) pthread_join (threads[0], NULL);
 	(void) pthread_join (threads[1], NULL);
 	(void) pthread_join (threads[2], NULL);
 	(void) pthread_join (threads[3], NULL);

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region sort

static int dlist_test_sort (void) {

	printf ("dlist_sort ()\n");

	DoubleList *list = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 100; i++) {
		integer->value = integer_new (rand () % 99 + 1);
		dlist_insert_after (list, dlist_start (list), integer);
	}

	dlist_sort (list, NULL);
	
	for (ListElement *le = dlist_start (list); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (list);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region threads

static void *test_thread_add (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		// add ten items at the list end
		for (unsigned int i = 0; i < 100; i++) {
			Integer *integer = (Integer *) malloc (sizeof (Integer));
			// integer->value = rand () % 99 + 1;
			integer->value = i;
			dlist_insert_after (list, dlist_end (list), integer);
		}
	}

}

static void *test_thread_remove (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		// remove 5 items from the start of the list
		for (unsigned int i = 0; i < 50; i++) {
			void *integer = dlist_remove_element (list, dlist_start (list));
			if (integer) free (integer);
		}
	}

}

static void *test_thread_search (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		// get 10 random values from the list
		for (unsigned int i = 0; i < 10; i++) {
			Integer *integer = (Integer *) malloc (sizeof (Integer));
			integer->value = rand () % 99 + 1;

			printf ("Searching: %d...\n", integer->value);
			Integer *search = (Integer *) dlist_search (list, integer, NULL);
			if (search) printf ("%d - %d\n", i + 1, search->value);

			free (integer);
		}
	}

}

static void *test_thread_get_element (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		// get 10 random values from the list
		for (unsigned int i = 0; i < 10; i++) {
			Integer *integer = (Integer *) malloc (sizeof (Integer));
			integer->value = rand () % 999 + 1;

			printf ("Getting element for: %d...\n", integer->value);
			ListElement *le = dlist_get_element (list, integer, NULL);
			Integer *search = (Integer *) le->data;
			if (search) printf ("%d - %d\n", i + 1, search->value);

			free (integer);
		}
	}

}

static void *test_thread_sort (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		dlist_sort (list, NULL);
	}

}

static int test_thread_safe (void) {

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	// create 4 threads
	const unsigned int N_THREADS = 4;
	pthread_t threads[N_THREADS];
	// for (unsigned int i = 0; i < N_THREADS; i++) {
	// 	pthread_create (&threads[i], NULL, test_thread_add, list);
	// }

	// // join the threads
	// for (unsigned int i = 0; i < N_THREADS; i++) {
	// 	pthread_join (threads[i], NULL);
	// }

	// 21/01/2020 -- 15:15 -- some times getting seg fault, other we get wrong list size,
	// and other times we are printing a wrong number of items
	// the correct values should be size: 10 and ten integers getting printed to the console
	pthread_create (&threads[0], NULL, test_thread_add, list);
	pthread_create (&threads[1], NULL, test_thread_add, list);
	// pthread_create (&threads[2], NULL, test_thread_remove, list);
	// pthread_create (&threads[3], NULL, test_thread_remove, list);

	for (unsigned int i = 0; i < 2; i++) {
		pthread_join (threads[i], NULL);
	}

	// get how many items are on the list, we expect 40 if all add ten items
	printf ("\nItems in list: %ld\n", dlist_size (list));
	Integer *integer = NULL;
	// for (ListElement *le = dlist_start (list); le; le = le->next) {
	// 	integer = (Integer *) le->data;
	// 	printf ("%6i", integer->value);
	// }

	// 22/01/2020 -- 3:14 -- this has no problem
	pthread_create (&threads[0], NULL, test_thread_add, list);
	pthread_create (&threads[1], NULL, test_thread_remove, list);
	pthread_create (&threads[2], NULL, test_thread_search, list);
	// pthread_create (&threads[2], NULL, test_thread_get_element, list);
	// pthread_create (&threads[3], NULL, test_thread_sort, list);
	pthread_join (threads[0], NULL);
	pthread_join (threads[1], NULL);
	pthread_join (threads[2], NULL);
	// pthread_join (threads[3], NULL);

	printf ("\nItems in list: %ld\n", dlist_size (list));
	dlist_sort (list, NULL);
	unsigned int i = 1;
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d - %3i\n", i, integer->value);
		i++;
	}

	// 21/01/2020 -- 15:09 -- we get a segfault every other time and NOT all items get inserted when that happens
	dlist_delete (list);

	return 0;

}

#pragma endregion

#pragma region other

static int dlist_test_to_array (void) {

	printf ("dlist_to_array ()\n");

	int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	size_t count = 0;
	void **array = dlist_to_array (dlist, &count);
	if (array) {
		for (ListElement *le = dlist_start (dlist); le; le = le->next) {
			printf ("%4d", ((Integer *) le->data)->value);
		}

		printf ("\n\nElements in array %ld\n", count);
		for (size_t idx = 0; idx < count; idx++) {
			printf ("%4d", ((Integer *) array[idx])->value);
		}

		// clear dlist to clear array
		dlist_clear (dlist);

		// clear array
		for (size_t idx = 0; idx < count; idx++) {
			free (array[idx]);
		}

		free (array);

		retval = 0;
	}

	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return retval;

}

static int dlist_test_copy (void) {

	printf ("dlist_copy ()\n");

	int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	DoubleList *copy = dlist_copy (dlist);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	for (ListElement *le = dlist_start (copy); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_clear (dlist);

	dlist_delete (copy);
	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return retval;

}

static int dlist_test_clone (void) {

	printf ("dlist_clone ()\n");

	int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	DoubleList *clone = dlist_clone (dlist, integer_clone);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	for (ListElement *le = dlist_start (clone); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (clone);
	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return retval;

}

static int dlist_test_split_half (void) {

	printf ("dlist_split_half ()\n");

	int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	DoubleList *split = dlist_split_half (dlist);

	printf ("\nFIRST half: %ld\n", dlist->size);
	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nSECOND half: %ld\n", split->size);
	for (ListElement *le = dlist_start (split); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\nInsert at end FIRST half:\n");
	for (int i = 0; i < 5; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	printf ("FIRST half: %ld\n", dlist->size);
	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nInsert at start SECOND half:\n");
	for (int i = 0; i < 5; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (split, NULL, integer);
	}

	printf ("SECOND half: %ld", split->size);
	for (ListElement *le = dlist_start (split); le; le = le->next) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);
	dlist_delete (split);

	retval = 0;

	printf ("\n\n----------------------------------------\n");

	return retval;

}

static bool test_split_by_condition_condition (
	const void *a, const void *b
) {

	Integer *integer_a = (Integer *) a;
	Integer *integer_b = (Integer *) b;

	if (integer_a->value < integer_b->value) return true;

	return false;

}

static int dlist_test_split_by_condition (void) {

	printf ("dlist_split_by_condition ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	ListElement *le = NULL;
	dlist_for_each (dlist, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	Integer match = { 5 };
	DoubleList *matches = dlist_split_by_condition (
		dlist, test_split_by_condition_condition, &match
	);

	printf ("\n\nAFTER split (size %ld):\n", dlist->size);
	dlist_for_each (dlist, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nMatches (size: %ld):\n", matches->size);
	dlist_for_each (matches, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (matches);
	dlist_delete (dlist);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_merge_two (void) {

	printf ("dlist_split_by_condition ()\n");

	DoubleList *one = dlist_init (free, integer_comparator);
	DoubleList *two = dlist_init (free, integer_comparator);

	printf ("Insert numbers from 0 to 9 into ONE:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (one, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (one, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nInsert numbers from 10 to 19 into TWO:\n");
	for (int i = 10; i < 20; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (two, integer);
	}

	dlist_for_each (two, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_merge_two (one, two);
	printf ("\n\nMerge both lists (%ld):\n", one->size);
	dlist_for_each (one, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nOne (%ld)\n", one->size);
	printf ("Two (%ld)\n", two->size);

	printf ("\n\n");

	dlist_delete (two);
	dlist_delete (one);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static bool test_merge_by_condition_condition (
	const void *a, const void *b
) {

	Integer *integer_a = (Integer *) a;
	Integer *integer_b = (Integer *) b;

	if (integer_a->value < integer_b->value) return true;

	return false;

}

static int dlist_test_merge_two_by_condition (void) {

	printf ("dlist_merge_two_by_condition ()\n");

	DoubleList *one = dlist_init (free, integer_comparator);
	DoubleList *two = dlist_init (free, integer_comparator);

	printf ("Insert numbers from 0 to 9 into ONE:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (one, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (one, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nInsert numbers from 0 to 9 into TWO:\n");
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (two, integer);
	}

	dlist_for_each (two, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nMerge both lists but ONLY elements < 5:\n");
	Integer match = { 5 };
	DoubleList *merge = dlist_merge_two_by_condition (
		one, two,
		test_merge_by_condition_condition, &match
	);

	printf ("One (%ld):\n", one->size);
	dlist_for_each (one, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nTwo (%ld):\n", two->size);
	dlist_for_each (two, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nMerge (%ld):\n", merge->size);
	dlist_for_each (merge, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (merge);
	dlist_delete (two);
	dlist_delete (one);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_merge_many (void) {

	printf ("dlist_merge_many ()\n");

	DoubleList *one = dlist_init (free, integer_comparator);
	DoubleList *two = dlist_init (free, integer_comparator);
	DoubleList *three = dlist_init (free, integer_comparator);
	DoubleList *four = dlist_init (free, integer_comparator);

	printf ("Insert numbers from 0 to 9 into ONE:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (one, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (one, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nInsert numbers from 10 to 19 into TWO:\n");
	for (int i = 10; i < 20; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (two, integer);
	}

	dlist_for_each (two, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nInsert numbers from 20 to 29 into THREE:\n");

	integer = NULL;
	for (int i = 20; i < 30; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (three, integer);
	}

	le = NULL;
	dlist_for_each (three, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nInsert numbers from 30 to 39 into FOUR:\n");

	integer = NULL;
	for (int i = 30; i < 40; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (four, integer);
	}

	le = NULL;
	dlist_for_each (four, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nCreating many dlists...\n");

	DoubleList *many = dlist_init (dlist_delete, NULL);
	dlist_insert_at_end_unsafe (many, one);
	dlist_insert_at_end_unsafe (many, two);
	dlist_insert_at_end_unsafe (many, three);
	dlist_insert_at_end_unsafe (many, four);

	DoubleList *merge = dlist_merge_many (many);
	printf ("Merge all lists:\n");
	dlist_for_each (merge, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nMerge (%ld)\n", merge->size);
	printf ("One (%ld)\n", one->size);
	printf ("Two (%ld)\n", two->size);
	printf ("Three (%ld)\n", three->size);
	printf ("Four (%ld)\n", four->size);

	dlist_delete (merge);
	dlist_delete (many);

	printf ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

int main (void) {

	srand ((unsigned) time (NULL));

	int res = 0;

	/*** main ***/

	res |= dlist_test_empty ();

	res |= dlist_test_delete_if_empty ();

	res |= dlist_test_reset ();

	/*** insert ***/

	res |= dlist_test_insert_before_at_start ();

	res |= dlist_test_insert_before_at_end ();

	res |= dlist_test_insert_before_at_middle ();

	res |= dlist_test_insert_before_at_start_unsafe ();

	res |= dlist_test_insert_before_at_end_unsafe ();

	res |= dlist_test_insert_after_at_start ();

	res |= dlist_test_insert_after_at_end ();

	res |= dlist_test_insert_after_unsafe_at_start ();

	res |= dlist_test_insert_after_unsafe_at_end ();

	res |= dlist_test_insert_at ();

	res |= dlist_test_insert_at_start ();

	res |= dlist_test_insert_at_start_unsafe ();

	res |= dlist_test_insert_at_end_unsafe ();

	res |= dlist_test_insert_in_order ();

	/*** remove ***/

	res |= dlist_test_remove ();

	res |= dlist_test_remove_start ();

	res |= dlist_test_remove_start_unsafe ();

	res |= dlist_test_remove_end ();

	res |= dlist_test_remove_end_unsafe ();

	res |= dlist_test_remove_by_condition ();

	/*** double ***/

	/*** get ***/

	res |= dlist_test_get_at ();

	/*** traverse ***/

	res |= dlist_test_traverse ();

	res |= dlist_test_traverse_threads ();

	/*** sort ***/

	res |= dlist_test_sort ();

	/*** other ***/

	res |= dlist_test_to_array ();

	res |= dlist_test_copy ();

	res |= dlist_test_clone ();

	res |= dlist_test_split_half ();

	res |= dlist_test_split_by_condition ();

	res |= dlist_test_merge_two_by_condition ();

	res |= dlist_test_merge_many ();

	return res;

}