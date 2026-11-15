#include <stdbool.h>
#include <stdlib.h>

#include <cerver/collections/dlist.h>

#include "../test.h"
#include "check.h"

static void collections_check_dlist_empty (const DoubleList *dlist) {

	test_check_null_ptr (dlist->start);
	test_check_null_ptr (dlist->end);

}

static void collections_check_dlist_forwards (
	const DoubleList *dlist, const size_t expected_size
) {

	size_t count = 0;
	const ListElement *last = NULL;
	for (const ListElement *le = dlist->start; le; le = le->next) {
		test_check (le->prev == last, "a prev link does not point to the previous element");

		last = le;
		count++;

		test_check (count <= expected_size, "the dlist has more elements than its size");
	}

	test_check (last == dlist->end, "the last element is not the dlist end");
	test_check (count == expected_size, "the forwards count does not match the size");

}

static void collections_check_dlist_backwards (
	const DoubleList *dlist, const size_t expected_size
) {

	size_t count = 0;
	const ListElement *last = NULL;
	for (const ListElement *le = dlist->end; le; le = le->prev) {
		test_check (le->next == last, "a next link does not point to the next element");

		last = le;
		count++;

		test_check (count <= expected_size, "the dlist has more elements than its size");
	}

	test_check (last == dlist->start, "the first element is not the dlist start");
	test_check (count == expected_size, "the backwards count does not match the size");

}

void collections_check_dlist (
	const DoubleList *dlist, const size_t expected_size
) {

	test_check_ptr (dlist);
	test_check (dlist->size == expected_size, "the dlist size does not match");

	if (expected_size) {
		test_check_ptr (dlist->start);
		test_check_ptr (dlist->end);
		test_check_null_ptr (dlist->start->prev);
		test_check_null_ptr (dlist->end->next);

		collections_check_dlist_forwards (dlist, expected_size);
		collections_check_dlist_backwards (dlist, expected_size);
	}

	else {
		collections_check_dlist_empty (dlist);
	}

}
