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

#pragma region end

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

#pragma endregion

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

static int test_traverse (void) {

	int retval = 0;

	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 100; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		// integer->value = rand () % 99 + 1;
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	// create 4 threads
	const unsigned int N_THREADS = 4;
	pthread_t threads[N_THREADS];

	pthread_create (&threads[0], NULL, test_traverse_method_thread, list);
	pthread_create (&threads[1], NULL, test_traverse_method_thread, list);
	pthread_create (&threads[2], NULL, test_traverse_method_thread, list);
	pthread_create (&threads[3], NULL, test_traverse_method_thread, list);
	pthread_join (threads[0], NULL);
	pthread_join (threads[1], NULL);
	pthread_join (threads[2], NULL);
	pthread_join (threads[3], NULL);

	// retval |= dlist_traverse (list, test_traverse_method, NULL);

	dlist_delete (list);

	return retval;

}

static int test_sort (void) {

	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 100; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = rand () % 99 + 1;
		dlist_insert_after (list, dlist_start (list), integer);
	}

	dlist_sort (list, NULL);
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3i", integer->value);
	}

	dlist_delete (list);

	return 0;

}

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

static int test_get_at (void) {

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 100; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	// get 10 random values from the list
	for (unsigned int i = 0; i < 10; i++) {
		unsigned int idx = rand () % 999 + 1;

		printf ("Getting element at idx: %d... ", idx);
		// ListElement *le = dlist_get_element_at (list, idx);
		// if (le) {
		// 	Integer *integer = (Integer *) le->data;
		// 	printf ("%3d\n", integer->value);
		// }

		Integer *integer = (Integer *) dlist_get_at (list, idx);
		if (integer) printf ("%3d\n", integer->value);
	}

	dlist_delete (list);

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

static int test_array (void) {

	int retval = 1;

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 10; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	printf ("Elements in list: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	size_t count = 0;
	void **array = dlist_to_array (list, &count);
	if (array) {
		dlist_clear (list);

		printf ("Elements in array: \n");
		for (size_t idx = 0; idx < count; idx++) {
			printf ("%3d ", ((Integer *) array[idx])->value);
		}

		printf ("\n\nList is %ld long\n", list->size);
		printf ("Array is %ld long\n", count);

		// clear array
		for (size_t idx = 0; idx < count; idx++) {
			free (array[idx]);
		}

		free (array);

		retval = 0;
	}

	dlist_delete (list);

	return retval;

}

static int test_empty (void) {

	int retval = 1;

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	if (dlist_is_empty (list)) printf ("List is empty\n");
	if (dlist_is_not_empty (list)) printf ("List is not empty\n");

	for (int i = 0; i < 10; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	printf ("Elements in list: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	if (dlist_is_empty (list)) printf ("List is empty\n");
	if (dlist_is_not_empty (list)) printf ("List is not empty\n");

	dlist_delete (list);

	retval = 0;

	return retval;

}

static int test_copy (void) {

	int retval = 1;

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 10; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	DoubleList *copy = dlist_copy (list);

	printf ("Elements in original list: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	printf ("Elements in copied list: \n");
	for (ListElement *le = dlist_start (copy); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	dlist_delete (list);
	dlist_clear (copy);
	dlist_delete (copy);

	retval = 0;

	return retval;

}


static void *integer_clone (const void *original) {

	Integer *cloned_integer = NULL;

	if (original) {
		cloned_integer = integer_new (((Integer *) original)->value);
	}

	return cloned_integer;

}

static int test_clone (void) {

	int retval = 1;

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 10; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	DoubleList *clone = dlist_clone (list, integer_clone);

	printf ("Elements in original list: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	printf ("Elements in cloned list: \n");
	for (ListElement *le = dlist_start (clone); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	dlist_delete (list);
	dlist_delete (clone);

	retval = 0;

	return retval;

}

static int test_split_half (void) {

	int retval = 1;

	// create a global list
	DoubleList *list = dlist_init (NULL, integer_comparator);

	for (int i = 0; i < 3; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	printf ("ORIGINAL half size: %ld\n", list->size);
	printf ("Elements in ORIGINAL list: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	DoubleList *split = dlist_split_half (list);

	printf ("\nFIRST half size: %ld\n", list->size);
	printf ("Elements in FIRST half: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");

	printf ("\nSECOND half size: %ld\n", split->size);
	printf ("Elements in SECOND half: \n");
	for (ListElement *le = dlist_start (split); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");
	printf ("\n");

	printf ("Insert at end FIRST half:\n");
	for (int i = 0; i < 5; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_after (list, dlist_end (list), integer);
	}

	printf ("FIRST half size: %ld\n", list->size);
	printf ("Elements in FIRST half: \n");
	for (ListElement *le = dlist_start (list); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");
	printf ("\n");
	printf ("Insert at start SECOND half:\n");
	for (int i = 0; i < 5; i++) {
		Integer *integer = (Integer *) malloc (sizeof (Integer));
		integer->value = i;
		dlist_insert_before (split, NULL, integer);
	}

	printf ("SECOND half size: %ld\n", split->size);
	printf ("Elements in SECOND half: \n");
	for (ListElement *le = dlist_start (split); le != NULL; le = le->next) {
		Integer *integer = (Integer *) le->data;
		printf ("%3d ", integer->value);
	}

	printf ("\n");
	printf ("\n");

	dlist_delete (list);
	dlist_delete (split);

	retval = 0;

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

static int test_split_by_condition (void) {

	printf ("\ntest_split_by_condition ()\n");

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

	Integer match = { 5 };
	DoubleList *matches = dlist_split_by_condition (
		list, test_split_by_condition_condition, &match
	);

	printf ("\n\nAFTER split (size %ld):\n", list->size);
	dlist_for_each (list, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\nMatches (size: %ld):\n", matches->size);
	dlist_for_each (matches, le) {
		printf ("%4d", ((Integer *) le->data)->value);
	}

	printf ("\n\n");

	dlist_delete (matches);
	dlist_delete (list);

	printf ("----------------------------------------\n");

	return 0;

}

static int test_merge_two (void) {

	printf ("\ntest_merge_two ()\n");

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

	printf ("----------------------------------------\n");

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

static int test_merge_two_by_condition (void) {

	printf ("\ntest_merge_two_by_condition ()\n");

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

	printf ("\n\n");

	dlist_delete (merge);
	dlist_delete (two);
	dlist_delete (one);

	printf ("----------------------------------------\n");

	return 0;

}

static int test_merge_many (void) {

	printf ("\ntest_merge_many ()\n");

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

	printf ("\n\n");

	dlist_delete (merge);
	dlist_delete (many);

	printf ("----------------------------------------\n");

	return 0;

}

int main (void) {

	srand ((unsigned) time (NULL));

	int res = 0;

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

	return res;

}