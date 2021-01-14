#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/collections/avl.h>

#include "../test.h"

#include "data.h"

static AVLTree *test_avl_create (void) {

	AVLTree *avl = avl_init (data_comparator, data_delete);

	test_check_ptr (avl);
	test_check_null_ptr (avl->root);
	test_check_int_eq (avl->size, 0, NULL);
	test_check_ptr (avl->comparator);
	test_check_ptr (avl->destroy);
	test_check_ptr (avl->mutex);

	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));

	return avl;

}

// create a new avl and insert a single value
static void test_avl_int_insert_single (void) {

	AVLTree *avl = test_avl_create ();

	unsigned int idx = 0;
	unsigned int value = 1;
	Data *data = data_new (idx, value);

	unsigned int result = avl_insert_node (avl, data);

	test_check_unsigned_eq (result, 0, NULL);
	test_check_int_eq ((int) avl->size, 1, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	avl_delete (avl);

}

// create a new avl and insert multiple values
static void test_avl_int_insert_multiple (void) {

	AVLTree *avl = test_avl_create ();

	unsigned int value = 1000;
	Data *data = NULL;
	unsigned int result = 0;
	for (unsigned int i = 0; i < 10; i++) {
		data = data_new (i, value);
		value++;

		result = avl_insert_node (avl, data);

		test_check_unsigned_eq (result, 0, NULL);
	}

	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	avl_delete (avl);

}

// create a avl
// insert and remove a single value
static void test_avl_int_remove_single (void) {

	AVLTree *avl = test_avl_create ();

	unsigned int idx = 1;
	unsigned int value = 10;
	Data *data = data_new (idx, value);

	unsigned int result = avl_insert_node (avl, data);

	// after insertion
	test_check_unsigned_eq (result, 0, NULL);
	test_check_int_eq ((int) avl->size, 1, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	void *removed = NULL;

	// remove NULL value
	removed = avl_remove_node (avl, NULL);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 1, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	// remove bad value
	Data *bad_data_query = data_new (2, 10);
	removed = avl_remove_node (avl, bad_data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 1, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));
	data_delete (bad_data_query);

	// remove good value
	Data *good_data_query = data_new (1, 0);
	removed = avl_remove_node (avl, good_data_query);
	test_check_ptr (removed);
	test_check_unsigned_eq (((Data *) removed)->idx, 1, NULL);
	test_check_int_eq ((int) avl->size, 0, NULL);
	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));

	// delete removed data
	data_delete (removed);

	// remove good value again
	removed = avl_remove_node (avl, good_data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 0, NULL);
	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));
	data_delete (good_data_query);

	// remove unmatched value again
	Data *another_bad_data_query = data_new (10, 10);
	removed = avl_remove_node (avl, another_bad_data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 0, NULL);
	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));
	data_delete (another_bad_data_query);

	avl_delete (avl);

}

// create a avl
// insert and remove multiple values
static void test_avl_int_remove_multiple (void) {

	AVLTree *avl = test_avl_create ();

	unsigned int idx = 0;

	// insert 10 elements
	unsigned int value = 1000;
	Data *data = NULL;
	unsigned int result = 0;
	for (idx = 0; idx < 10; idx++) {
		data = data_new (idx, value);

		result = avl_insert_node (avl, data);

		test_check_int_eq (result, 0, NULL);

		value++;
	}

	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	void *removed = NULL;

	// remove NULL value
	removed = avl_remove_node (avl, NULL);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	// remove bad value
	Data *bad_data_query = data_new (20, 10);
	removed = avl_remove_node (avl, bad_data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));
	data_delete (bad_data_query);

	// remove good value
	Data *good_data_query = data_new (3, 0);
	removed = avl_remove_node (avl, good_data_query);
	test_check_ptr (removed);
	test_check_unsigned_eq (((Data *) removed)->idx, 3, NULL);
	test_check_int_eq ((int) avl->size, 9, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));
	data_delete (good_data_query);

	// delete removed data
	data_delete (removed);

	// remove another good value
	Data *another_good_data_query = data_new (8, 0);
	removed = avl_remove_node (avl, another_good_data_query);
	test_check_ptr (removed);
	test_check_unsigned_eq (((Data *) removed)->idx, 8, NULL);
	test_check_int_eq ((int) avl->size, 8, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));
	data_delete (another_good_data_query);

	// delete removed data
	data_delete (removed);

	// remove the remaining of the values
	Data data_query = { .idx = 0, .value = 0 };
	for (idx = 0; idx < 10; idx++) {
		data_query = (Data) { .idx = idx, .value = 0 };

		// the values that we removed before
		if ((idx == 3) || (idx == 8)) {
			removed = avl_remove_node (avl, &data_query);
			test_check_null_ptr (removed);
			test_check_false (avl_is_empty (avl));
			test_check_true (avl_is_not_empty (avl));
		}

		else {
			removed = avl_remove_node (avl, &data_query);
			test_check_ptr (removed);
			data_delete (removed);
		}
	}

	// try to remove a previous good value
	data_query = (Data) { .idx = 5, .value = 0 };
	removed = avl_remove_node (avl, &data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 0, NULL);
	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));

	// try again
	data_query = (Data) { .idx = 4, .value = 0 };
	removed = avl_remove_node (avl, &data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 0, NULL);
	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));

	// try to remove a bad value
	data_query = (Data) { .idx = 51, .value = 0 };
	removed = avl_remove_node (avl, &data_query);
	test_check_null_ptr (removed);
	test_check_int_eq ((int) avl->size, 0, NULL);
	test_check_true (avl_is_empty (avl));
	test_check_false (avl_is_not_empty (avl));

	// insert a new value
	Data *final_data = data_new (100, 100);
	result = avl_insert_node (avl, final_data);
	test_check_unsigned_eq (result, 0, NULL);
	test_check_int_eq ((int) avl->size, 1, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	avl_delete (avl);

}

static void test_avl_int_get_single (void) {

	AVLTree *avl = test_avl_create ();

	unsigned int idx = 0;

	// insert 10 elements
	unsigned int val = 1000;
	Data *data = NULL;
	unsigned int result = 0;
	for (idx = 0; idx < 10; idx++) {
		data = data_new (idx, val);

		result = avl_insert_node (avl, data);

		test_check_int_eq (result, 0, NULL);

		val++;
	}

	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	void *value = NULL;

	// get bad value
	Data data_query = { .idx = 321, .value = 123 };
	value = avl_get_node_data (avl, &data_query, NULL);
	test_check_null_ptr (value);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	// get good value
	data_query = (Data) { .idx = 6, .value = 0 };
	value = avl_get_node_data (avl, &data_query, NULL);
	test_check_ptr (value);
	test_check_unsigned_eq (((Data *) value)->idx, 6, NULL);
	test_check_unsigned_eq (((Data *) value)->value, 1006, NULL);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	// get another good value
	data_query = (Data) { .idx = 0, .value = 0 };
	value = avl_get_node_data (avl, &data_query, NULL);
	test_check_ptr (value);
	test_check_unsigned_eq (((Data *) value)->idx, 0, NULL);
	test_check_unsigned_eq (((Data *) value)->value, 1000, NULL);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	// get another good value
	data_query = (Data) { .idx = 1, .value = 0 };
	value = avl_get_node_data (avl, &data_query, NULL);
	test_check_ptr (value);
	test_check_unsigned_eq (((Data *) value)->idx, 1, NULL);
	test_check_unsigned_eq (((Data *) value)->value, 1001, NULL);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	// get another good value
	data_query = (Data) { .idx = 9, .value = 0 };
	value = avl_get_node_data (avl, &data_query, NULL);
	test_check_ptr (value);
	test_check_unsigned_eq (((Data *) value)->idx, 9, NULL);
	test_check_unsigned_eq (((Data *) value)->value, 1009, NULL);
	test_check_int_eq ((int) avl->size, 10, NULL);
	test_check_false (avl_is_empty (avl));
	test_check_true (avl_is_not_empty (avl));

	avl_delete (avl);

}

// TODO:
// static void test_avl_int_get_multple (void) {

// 	AVLTree *avl = test_avl_create ();

// 	avl_delete (avl);

// }

void collections_tests_avl (void) {

	(void) printf ("Testing COLLECTIONS avl...\n");

	test_avl_int_insert_single ();

	test_avl_int_insert_multiple ();

	test_avl_int_remove_single ();

	test_avl_int_remove_multiple ();

	test_avl_int_get_single ();

	// test_avl_int_get_multple ();

	(void) printf ("Done!\n");

}