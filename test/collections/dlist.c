#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <pthread.h>

#include <cerver/collections/dlist.h>

#include <cerver/utils/log.h>

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

	cerver_log_raw ("dlist_is_empty () & dlist_is_not_empty ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	if (dlist_is_empty (dlist)) cerver_log_raw ("BEFORE - List is empty\n");
	if (dlist_is_not_empty (dlist)) cerver_log_raw ("BEFORE - List is not empty\n");

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	if (dlist_is_empty (dlist)) cerver_log_raw ("AFTER - List is empty\n");
	if (dlist_is_not_empty (dlist)) cerver_log_raw ("AFTER - List is not empty\n");

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_delete_if_empty (void) {

	int errors = 0;

	cerver_log_raw ("dlist_delete_if_empty ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	if (!dlist_delete_if_empty (dlist)) {
		cerver_log_raw ("\n\nDeleted dlist BUT it is not empty!");
		dlist = NULL;

		errors = 1;
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return errors;

}

static int dlist_test_reset (void) {

	int errors = 0;

	cerver_log_raw ("dlist_reset ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	// }

	dlist_reset (dlist);

	cerver_log_raw ("\ndlist size after reset: %ld\n", dlist->size);
	if (dlist->size) errors = 1;

	if (!dlist->start && !dlist->end) {
		cerver_log_raw ("\ndlist has NULL start & NULL end!\n");
	}

	else {
		cerver_log_raw ("\ndlist start or end are not NULL!\n");
		errors = 1;
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return errors;

}

#pragma endregion

#pragma region insert

static int dlist_test_insert_before_at_start (void) {

	cerver_log_raw ("dlist_insert_before () at START\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_end (void) {

	cerver_log_raw ("dlist_insert_before () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before (dlist, dlist_end (dlist), integer);

		// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
		// }

		// cerver_log_raw ("\n");
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_middle (void) {

	cerver_log_raw ("dlist_insert_before () at MIDDLE\n");

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
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_start_unsafe (void) {

	cerver_log_raw ("dlist_insert_before () at START UNSAFE\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_before_at_end_unsafe (void) {

	cerver_log_raw ("dlist_insert_before () at END UNSAFE\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_at_start (void) {

	cerver_log_raw ("dlist_insert_after () at START\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_at_end (void) {

	cerver_log_raw ("dlist_insert_after () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_unsafe_at_start (void) {

	cerver_log_raw ("dlist_insert_after_unsafe () at START\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_start (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_after_unsafe_at_end (void) {

	cerver_log_raw ("dlist_insert_after_unsafe () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at (void) {

	cerver_log_raw ("dlist_insert_at ()\n");

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
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_start (void) {

	cerver_log_raw ("dlist_insert_at_start ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_start (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_start_unsafe (void) {

	cerver_log_raw ("dlist_insert_at_start_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_start_unsafe (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_end (void) {

	cerver_log_raw ("dlist_insert_at_end ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_at_end_unsafe (void) {

	cerver_log_raw ("dlist_insert_at_end_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_insert_in_order (void) {

	cerver_log_raw ("dlist_test_insert_in_order ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	// test insert at
	cerver_log_raw ("\nInsert 100 random numbers\n");
	Integer *integer = NULL;
	for (unsigned int i = 0; i < 100; i++) {
		integer = integer_new (i);
		integer->value = rand () % 999 + 1;
		// cerver_log_raw ("%d\n", integer->value);
		dlist_insert_in_order (dlist, integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region remove

static int dlist_test_remove (void) {

	cerver_log_raw ("dlist_remove ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	// }

	Integer query = { 0 };
	for (unsigned int i = 0; i < 10; i++) {
		query.value = i;
		dlist_remove (dlist, &query, NULL) ;
	}

	cerver_log_raw ("dlist_remove () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_start (void) {

	cerver_log_raw ("dlist_remove_start ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_start (dlist);
	}

	cerver_log_raw ("dlist_remove_start () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_start_unsafe (void) {

	cerver_log_raw ("dlist_remove_start_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_start_unsafe (dlist);
	}

	cerver_log_raw ("dlist_remove_start_unsafe () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_end (void) {

	cerver_log_raw ("dlist_remove_end ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_end (dlist);
	}

	cerver_log_raw ("dlist_remove_end () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_end_unsafe (void) {

	cerver_log_raw ("dlist_remove_end_unsafe ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_at_end_unsafe (dlist, integer);
	}

	// for (ListElement *le = dlist_start (dlist); le; le = le->next) {
	// 	cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	// }

	for (unsigned int i = 0; i < 10; i++) {
		dlist_remove_end_unsafe (dlist);
	}

	cerver_log_raw ("dlist_remove_end_unsafe () dlist size after removed all: %ld", dlist->size);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

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

	cerver_log_raw ("dlist_remove_by_condition ()\n");

	DoubleList *list = dlist_init (integer_delete, integer_comparator);

	cerver_log_raw ("Insert 10 numbers:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	ListElement *le = NULL;
	dlist_for_each (list, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	Integer match = { 4 };
	unsigned int matches = dlist_remove_by_condition (
		list, dlist_test_remove_by_condition_less_than_condition, &match, true
	);

	cerver_log_raw ("\n\nRemoved %d elements smaller than 4:\n", matches);
	dlist_for_each (list, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	matches = dlist_remove_by_condition (
		list, dlist_test_remove_by_condition_greater_than_condition, &match, true
	);
	cerver_log_raw ("\n\nRemoved %d elements greater than 4:\n", matches);
	dlist_for_each (list, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (list);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_at (void) {

	cerver_log_raw ("dlist_insert_after_unsafe () at END\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	Integer *two = (Integer *) dlist_remove_at (dlist, 2);
	integer_delete (two);
	Integer *seven = (Integer *) dlist_remove_at (dlist, 7);
	integer_delete (seven);

	cerver_log_raw ("Removed at 2 & 7 -> dlist size: %ld\n", dlist->size);
	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region double

// TODO:
// static int test_insert_end_remove_start (void) {

// 	DoubleList *list = dlist_init (NULL, integer_comparator);

// 	for (int i = 0; i < 101; i++) {
// 		Integer *integer = (Integer *) malloc (sizeof (Integer));
// 		// integer->value = rand () % 99 + 1;
// 		integer->value = i;
// 		dlist_insert_after (list, dlist_end (list), integer);

// 		if (i < 100) {
// 			Integer *query = (Integer *) malloc (sizeof (int));
// 			query->value = i;

// 			free (dlist_remove (list, query, NULL));
// 			// free (dlist_remove_element (list, NULL));
// 		}
// 	}

// 	cerver_log_raw ("\nRemaining list item: %d -- size: %ld\n", ((Integer *) list->start->data)->value, list->size);

// 	dlist_delete (list);

// 	return 0;

// }

// static int test_insert_end_remove_end (void) {

// 	DoubleList *list = dlist_init (free, integer_comparator);

// 	for (int i = 0; i < 100; i++) {
// 		Integer *integer = (Integer *) malloc (sizeof (Integer));
// 		// integer->value = rand () % 99 + 1;
// 		integer->value = i;
// 		dlist_insert_after (list, dlist_end (list), integer);
// 	}

// 	for (int i = 0; i < 50; i++) {
// 		free (dlist_remove_element (list, list->end));
// 	}

// 	cerver_log_raw ("\nRemaining list item: %d -- size: %ld\n", ((Integer *) list->start->data)->value, list->size);

// 	dlist_delete (list);

// 	return 0;

// }

// static int test_insert_and_remove (void) {

// 	cerver_log_raw ("test_insert_and_remove ()\n");

// 	DoubleList *list = dlist_init (free, integer_comparator);

// 	cerver_log_raw ("Insert 10 numbers:\n");

// 	Integer *integer = NULL;
// 	for (int i = 0; i < 10; i++) {
// 		integer = (Integer *) malloc (sizeof (Integer));
// 		// integer->value = rand () % 99 + 1;
// 		integer->value = i;
// 		dlist_insert_after (list, dlist_end (list), integer);
// 	}

// 	ListElement *le = NULL;
// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}

// 	cerver_log_raw ("\n\nRemove 5 from the end:\n");
// 	for (int i = 0; i < 5; i++) {
// 		free (dlist_remove_element (list, list->end));
// 	}

// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}

// 	cerver_log_raw ("\n\n");
// 	cerver_log_raw ("Remove 2 from the start:\n");
// 	for (int i = 0; i < 2; i++) {
// 		free (dlist_remove_start (list));
// 	}

// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}
// 	cerver_log_raw ("\n\n");

// 	cerver_log_raw ("Remove 2 from the start again:\n");
// 	for (int i = 0; i < 2; i++) {
// 		free (dlist_remove_element (list, NULL));
// 	}

// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}
// 	cerver_log_raw ("\n\n");

// 	dlist_delete (list);

// 	cerver_log_raw ("----------------------------------------\n");

// 	return 0;

// }

// static int test_insert_and_remove_unsafe (void) {

// 	cerver_log_raw ("test_insert_and_remove_unsafe ()\n\n");

// 	DoubleList *list = dlist_init (free, integer_comparator);

// 	cerver_log_raw ("Insert 10 numbers:\n");

// 	Integer *integer = NULL;
// 	for (int i = 0; i < 10; i++) {
// 		integer = (Integer *) malloc (sizeof (Integer));
// 		// integer->value = rand () % 99 + 1;
// 		integer->value = i;
// 		dlist_insert_at_end_unsafe (list, integer);
// 	}

// 	ListElement *le = NULL;
// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}

// 	cerver_log_raw ("\n\nRemove 5 from the end:\n");
// 	for (int i = 0; i < 5; i++) {
// 		free (dlist_remove_end_unsafe (list));
// 	}

// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}

// 	cerver_log_raw ("\n\n");
// 	cerver_log_raw ("Remove 2 from the start:\n");
// 	for (int i = 0; i < 2; i++) {
// 		free (dlist_remove_start_unsafe (list));
// 	}

// 	dlist_for_each (list, le) {
// 		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
// 	}
// 	cerver_log_raw ("\n\n");

// 	dlist_delete (list);

// 	cerver_log_raw ("----------------------------------------\n");

// 	return 0;

// }

#pragma endregion

#pragma region get

static int dlist_test_get_at (void) {

	cerver_log_raw ("dlist_get_at ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	Integer *two = (Integer *) dlist_get_at (dlist, 2);
	cerver_log_raw ("two %d\n", two->value);

	Integer *four = (Integer *) dlist_get_at (dlist, 4);
	cerver_log_raw ("four %d\n", four->value);

	Integer *seven = (Integer *) dlist_get_at (dlist, 7);
	cerver_log_raw ("seven %d\n", seven->value);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region traverse

static void test_traverse_method (void *list_element_data, void *method_args) {

	cerver_log_raw ("%4d", ((Integer *) list_element_data)->value);

}

static void *test_traverse_method_thread (void *args) {

	if (args) {
		DoubleList *list = (DoubleList *) args;

		dlist_traverse (list, test_traverse_method, NULL);
		cerver_log_raw ("\n\n");
	}

	return NULL;

}

static int dlist_test_traverse (void) {

	cerver_log_raw ("dlist_traverse ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (dlist, dlist_end (dlist), integer);
	}

	dlist_traverse (dlist, test_traverse_method, NULL);

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_traverse_threads (void) {

	cerver_log_raw ("dlist_traverse () THREADS\n");

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

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region sort

static int dlist_test_sort (void) {

	cerver_log_raw ("dlist_sort ()\n");

	DoubleList *list = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 100; i++) {
		integer = integer_new (rand () % 99 + 1);
		dlist_insert_after (list, dlist_start (list), integer);
	}

	dlist_sort (list, NULL);
	
	for (ListElement *le = dlist_start (list); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (list);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region threads

static void *test_thread_add (void *args) {

	DoubleList *dlist = (DoubleList *) args;

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	return NULL;

}

static void *test_thread_remove (void *args) {

	DoubleList *dlist = (DoubleList *) args;

	for (unsigned int i = 0; i < 10; i++) {
		integer_delete (dlist_remove_element (dlist, dlist->end));
	}

	return NULL;

}

static int dlist_test_insert_threads (void) {

	cerver_log_raw ("dlist_test_insert_threads ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	// create 4 threads
	const unsigned int N_THREADS = 4;
	pthread_t threads[N_THREADS];

 	(void) pthread_create (&threads[0], NULL, test_thread_add, dlist);
 	(void) pthread_create (&threads[1], NULL, test_thread_add, dlist);
 	(void) pthread_create (&threads[2], NULL, test_thread_add, dlist);
 	(void) pthread_create (&threads[3], NULL, test_thread_add, dlist);
 	(void) pthread_join (threads[0], NULL);
 	(void) pthread_join (threads[1], NULL);
 	(void) pthread_join (threads[2], NULL);
 	(void) pthread_join (threads[3], NULL);

	cerver_log_raw ("Elements in dlist after 4 x 10 inserted: %ld\n\n", dlist->size);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_remove_threads (void) {

	cerver_log_raw ("dlist_test_remove_threads ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (unsigned int i = 0; i < 50; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	// create threads
	const unsigned int N_THREADS = 4;
	pthread_t threads[N_THREADS];

 	(void) pthread_create (&threads[0], NULL, test_thread_remove, dlist);
 	(void) pthread_create (&threads[1], NULL, test_thread_remove, dlist);
 	(void) pthread_create (&threads[2], NULL, test_thread_remove, dlist);
 	(void) pthread_create (&threads[3], NULL, test_thread_remove, dlist);
 	(void) pthread_join (threads[0], NULL);
 	(void) pthread_join (threads[1], NULL);
 	(void) pthread_join (threads[2], NULL);
 	(void) pthread_join (threads[3], NULL);

	cerver_log_raw ("Elements in dlist after 4 x 10 removed: %ld\n\n", dlist->size);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

#pragma region other

static int dlist_test_to_array (void) {

	cerver_log_raw ("dlist_to_array ()\n");

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
			cerver_log_raw ("%4d", ((Integer *) le->data)->value);
		}

		cerver_log_raw ("\n\nElements in array %ld\n", count);
		for (size_t idx = 0; idx < count; idx++) {
			cerver_log_raw ("%4d", ((Integer *) array[idx])->value);
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

	cerver_log_raw ("\n\n----------------------------------------\n");

	return retval;

}

static int dlist_test_copy (void) {

	cerver_log_raw ("dlist_copy ()\n");

	// int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	DoubleList *copy = dlist_copy (dlist);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	for (ListElement *le = dlist_start (copy); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_clear (dlist);

	dlist_delete (copy);
	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_clone (void) {

	cerver_log_raw ("dlist_clone ()\n");

	// int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	DoubleList *clone = dlist_clone (dlist, integer_clone);

	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	for (ListElement *le = dlist_start (clone); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (clone);
	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_split_half (void) {

	cerver_log_raw ("dlist_split_half ()\n");

	int retval = 1;

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	DoubleList *split = dlist_split_half (dlist);

	cerver_log_raw ("\nFIRST half: %ld\n", dlist->size);
	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nSECOND half: %ld\n", split->size);
	for (ListElement *le = dlist_start (split); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\nInsert at end FIRST half:\n");
	for (int i = 0; i < 5; i++) {
		integer = integer_new (i);
		dlist_insert_after_unsafe (dlist, dlist_end (dlist), integer);
	}

	cerver_log_raw ("FIRST half: %ld\n", dlist->size);
	for (ListElement *le = dlist_start (dlist); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nInsert at start SECOND half:\n");
	for (int i = 0; i < 5; i++) {
		integer = integer_new (i);
		dlist_insert_before_unsafe (split, NULL, integer);
	}

	cerver_log_raw ("SECOND half: %ld", split->size);
	for (ListElement *le = dlist_start (split); le; le = le->next) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (dlist);
	dlist_delete (split);

	retval = 0;

	cerver_log_raw ("\n\n----------------------------------------\n");

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

	cerver_log_raw ("dlist_split_by_condition ()\n");

	DoubleList *dlist = dlist_init (integer_delete, integer_comparator);

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = integer_new (i);
		dlist_insert_after (dlist, dlist_end (dlist), integer);
	}

	ListElement *le = NULL;
	dlist_for_each (dlist, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	Integer match = { 5 };
	DoubleList *matches = dlist_split_by_condition (
		dlist, test_split_by_condition_condition, &match
	);

	cerver_log_raw ("\n\nAFTER split (size %ld):\n", dlist->size);
	dlist_for_each (dlist, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nMatches (size: %ld):\n", matches->size);
	dlist_for_each (matches, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (matches);
	dlist_delete (dlist);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_merge_two (void) {

	cerver_log_raw ("dlist_merge_two ()\n");

	DoubleList *one = dlist_init (free, integer_comparator);
	DoubleList *two = dlist_init (free, integer_comparator);

	cerver_log_raw ("Insert numbers from 0 to 9 into ONE:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (one, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (one, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nInsert numbers from 10 to 19 into TWO:\n");
	for (int i = 10; i < 20; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (two, integer);
	}

	dlist_for_each (two, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_merge_two (one, two);
	cerver_log_raw ("\n\nMerge both lists (%ld):\n", one->size);
	dlist_for_each (one, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nOne (%ld)\n", one->size);
	cerver_log_raw ("Two (%ld)\n", two->size);

	dlist_delete (two);
	dlist_delete (one);

	cerver_log_raw ("\n\n----------------------------------------\n");

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

	cerver_log_raw ("dlist_merge_two_by_condition ()\n");

	DoubleList *one = dlist_init (free, integer_comparator);
	DoubleList *two = dlist_init (free, integer_comparator);

	cerver_log_raw ("Insert numbers from 0 to 9 into ONE:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (one, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (one, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nInsert numbers from 0 to 9 into TWO:\n");
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (two, integer);
	}

	dlist_for_each (two, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nMerge both lists but ONLY elements < 5:\n");
	Integer match = { 5 };
	DoubleList *merge = dlist_merge_two_by_condition (
		one, two,
		test_merge_by_condition_condition, &match
	);

	cerver_log_raw ("One (%ld):\n", one->size);
	dlist_for_each (one, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nTwo (%ld):\n", two->size);
	dlist_for_each (two, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nMerge (%ld):\n", merge->size);
	dlist_for_each (merge, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	dlist_delete (merge);
	dlist_delete (two);
	dlist_delete (one);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

static int dlist_test_merge_many (void) {

	cerver_log_raw ("dlist_merge_many ()\n");

	DoubleList *one = dlist_init (free, integer_comparator);
	DoubleList *two = dlist_init (free, integer_comparator);
	DoubleList *three = dlist_init (free, integer_comparator);
	DoubleList *four = dlist_init (free, integer_comparator);

	cerver_log_raw ("Insert numbers from 0 to 9 into ONE:\n");

	Integer *integer = NULL;
	for (int i = 0; i < 10; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (one, integer);
	}

	ListElement *le = NULL;
	dlist_for_each (one, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nInsert numbers from 10 to 19 into TWO:\n");
	for (int i = 10; i < 20; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (two, integer);
	}

	dlist_for_each (two, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nInsert numbers from 20 to 29 into THREE:\n");

	integer = NULL;
	for (int i = 20; i < 30; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (three, integer);
	}

	le = NULL;
	dlist_for_each (three, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nInsert numbers from 30 to 39 into FOUR:\n");

	integer = NULL;
	for (int i = 30; i < 40; i++) {
		integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_at_end_unsafe (four, integer);
	}

	le = NULL;
	dlist_for_each (four, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nCreating many dlists...\n");

	DoubleList *many = dlist_init (dlist_delete, NULL);
	dlist_insert_at_end_unsafe (many, one);
	dlist_insert_at_end_unsafe (many, two);
	dlist_insert_at_end_unsafe (many, three);
	dlist_insert_at_end_unsafe (many, four);

	DoubleList *merge = dlist_merge_many (many);
	cerver_log_raw ("Merge all lists:\n");
	dlist_for_each (merge, le) {
		cerver_log_raw ("%4d", ((Integer *) le->data)->value);
	}

	cerver_log_raw ("\n\nMerge (%ld)\n", merge->size);
	cerver_log_raw ("One (%ld)\n", one->size);
	cerver_log_raw ("Two (%ld)\n", two->size);
	cerver_log_raw ("Three (%ld)\n", three->size);
	cerver_log_raw ("Four (%ld)\n", four->size);

	dlist_delete (merge);
	dlist_delete (many);

	cerver_log_raw ("\n\n----------------------------------------\n");

	return 0;

}

#pragma endregion

int collections_tests_dlist (void) {

	(void) printf ("Testing COLLECTIONS dlist...\n");

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

	res |= dlist_test_insert_at_end ();

	res |= dlist_test_insert_at_end_unsafe ();

	res |= dlist_test_insert_in_order ();

	/*** remove ***/

	res |= dlist_test_remove ();

	res |= dlist_test_remove_start ();

	res |= dlist_test_remove_start_unsafe ();

	res |= dlist_test_remove_end ();

	res |= dlist_test_remove_end_unsafe ();

	res |= dlist_test_remove_by_condition ();

	res |= dlist_test_remove_at ();

	/*** double ***/

	// TODO:

	/*** get ***/

	res |= dlist_test_get_at ();

	/*** traverse ***/

	res |= dlist_test_traverse ();

	res |= dlist_test_traverse_threads ();

	/*** sort ***/

	res |= dlist_test_sort ();

	/*** threads ***/

	res |= dlist_test_insert_threads ();

	// TODO: segfault some times, gdb with no issues
	// res |= dlist_test_remove_threads ();

	/*** other ***/

	res |= dlist_test_to_array ();

	res |= dlist_test_copy ();

	res |= dlist_test_clone ();

	res |= dlist_test_split_half ();

	res |= dlist_test_split_by_condition ();

	res |= dlist_test_merge_two ();

	res |= dlist_test_merge_two_by_condition ();

	res |= dlist_test_merge_many ();

	(void) printf ("Done!\n");

	return res;

}