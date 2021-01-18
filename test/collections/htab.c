#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/collections/htab.h>

#include "../test.h"

#include "data.h"

static Htab *test_htab_create (void) {

	Htab *map = htab_create (
		HTAB_DEFAULT_INIT_SIZE,
		NULL,
		data_delete
	);

	test_check_ptr (map);
	test_check_int_eq ((int) map->size, HTAB_DEFAULT_INIT_SIZE, NULL);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));

	return map;

}

// create a new map and insert a single value
static void test_htab_int_insert_single (void) {

	Htab *map = test_htab_create ();

	unsigned int idx = 0;
	unsigned int value = 1;
	Data *data = data_new (idx, value);

	const void *key = &idx;
	int result = htab_insert (
		map,
		key, sizeof (unsigned int),
		data, sizeof (Data)
	);

	test_check_int_eq (result, 0, NULL);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// htab_print (map);

	htab_destroy (map);

}

// create a new map and insert multiple values
static void test_htab_int_insert_multiple (void) {

	Htab *map = test_htab_create ();

	unsigned int value = 1000;
	Data *data = NULL;	
	for (unsigned int i = 0; i < 10; i++) {
		const void *key = &i;

		data = data_new (i, value);
		value++;

		int result = htab_insert (
			map,
			key, sizeof (unsigned int),
			data, sizeof (Data)
		);

		test_check_int_eq (result, 0, NULL);
	}

	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// htab_print (map);

	htab_destroy (map);

}

// create a map
// insert and remove a single value
static void test_htab_int_remove_single (void) {

	Htab *map = test_htab_create ();

	unsigned int idx = 1;
	unsigned int value = 10;
	Data *data = data_new (idx, value);

	void *key = &idx;
	int result = htab_insert (
		map,
		key, sizeof (unsigned int),
		data, sizeof (Data)
	);

	// after insertion
	test_check_int_eq (result, 0, NULL);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	void *removed = NULL;

	// remove NULL value
	removed = htab_remove (map, NULL, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// remove bad value
	unsigned int bad_key = 2;
	key = &bad_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// remove good value
	unsigned int good_key = 1;
	key = &good_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_ptr (removed);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));
	data_delete (removed);

	// remove good value again
	key = &good_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));

	// remove unmatched value
	key = &bad_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));

	// remove unmatched value again
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));

	htab_destroy (map);

}

// create a map
// insert and remove multiple values
static void test_htab_int_remove_multiple (void) {

	Htab *map = test_htab_create ();

	unsigned int idx = 0;

	// insert 10 elements
	unsigned int value = 1000;
	Data *data = NULL;
	for (idx = 0; idx < 10; idx++) {
		const void *key = &idx;

		data = data_new (idx, value);

		int result = htab_insert (
			map,
			key, sizeof (unsigned int),
			data, sizeof (Data)
		);

		test_check_int_eq (result, 0, NULL);

		value++;
	}

	// check for insertions
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	void *key = 0;
	void *removed = NULL;

	// remove NULL value
	removed = htab_remove (map, NULL, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// remove bad value
	unsigned int bad_key = 20;
	key = &bad_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// remove good value
	unsigned int good_key = 2;
	key = &good_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_ptr (removed);
	test_check_int_eq ((int) map->count, 9, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));
	data_delete (removed);

	// remove another good value
	unsigned int another_good_key = 7;
	key = &another_good_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_ptr (removed);
	test_check_int_eq ((int) map->count, 8, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));
	data_delete (removed);

	// remove the remaining of the values
	for (idx = 0; idx < 10; idx++) {
		const void *key = &idx;

		// the values that we removed before
		if ((idx == 2) || (idx == 7)) {
			removed = htab_remove (map, key, sizeof (unsigned int));
			test_check_null_ptr (removed);
			test_check_false (htab_is_empty (map));
			test_check_true (htab_is_not_empty (map));
		}

		else {
			removed = htab_remove (map, key, sizeof (unsigned int));
			test_check_ptr (removed);
			data_delete (removed);
		}
	}

	// try to remove a previous good value 
	unsigned int prev_key = 4;
	key = &prev_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));

	// try to remove a bad value
	unsigned int another_bad_key = 100;
	key = &another_bad_key;
	removed = htab_remove (map, key, sizeof (unsigned int));
	test_check_null_ptr (removed);
	test_check_int_eq ((int) map->count, 0, NULL);
	test_check_true (htab_is_empty (map));
	test_check_false (htab_is_not_empty (map));

	// insert a new value
	unsigned int final_value = 18;
	key = &final_value;
	int result = htab_insert (
		map,
		key, sizeof (unsigned int),
		data, sizeof (Data)
	);
	test_check_int_eq (result, 0, NULL);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	htab_destroy (map);

}

static void test_htab_int_get_single (void) {

	Htab *map = test_htab_create ();

	unsigned int idx = 0;
	Data *data = data_new (idx, 1000);

	const void *key = &idx;
	int result = htab_insert (
		map,
		key, sizeof (unsigned int),
		data, sizeof (Data)
	);

	test_check_int_eq (result, 0, NULL);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	void *value = NULL;

	// get bad value
	unsigned int bad_key = 1;
	key = &bad_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_null_ptr (value);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// get good value
	unsigned int good_key = 0;
	key = &good_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_ptr (value);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// get good value again
	key = &good_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_ptr (value);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// get bad again
	key = &bad_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_null_ptr (value);
	test_check_int_eq ((int) map->count, 1, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// htab_print (map);

	htab_destroy (map);

}

static void test_htab_int_get_multple (void) {

	Htab *map = test_htab_create ();

	// insert multiple values
	unsigned int val = 1000;
	Data *data = NULL;	
	for (unsigned int i = 0; i < 10; i++) {
		const void *key = &i;

		data = data_new (i, val);
		val++;

		int result = htab_insert (
			map,
			key, sizeof (unsigned int),
			data, sizeof (Data)
		);

		test_check_int_eq (result, 0, NULL);
	}

	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	void *value = NULL;

	// get bad value
	unsigned int bad_key = 11;
	void *key = &bad_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_null_ptr (value);
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// get good value
	unsigned int good_key = 0;
	key = &good_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_ptr (value);
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// get another good value
	unsigned int another_good_key = 6;
	key = &another_good_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_ptr (value);
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// get another bad value
	unsigned int another_bad_key = 127;
	key = &another_bad_key;
	value = htab_get (map, key, sizeof (unsigned int));
	test_check_null_ptr (value);
	test_check_int_eq ((int) map->count, 10, NULL);
	test_check_false (htab_is_empty (map));
	test_check_true (htab_is_not_empty (map));

	// htab_print (map);

	htab_destroy (map);

}

void collections_tests_htab (void) {

	(void) printf ("Testing COLLECTIONS htab...\n");

	test_htab_int_insert_single ();

	test_htab_int_insert_multiple ();

	test_htab_int_remove_single ();

	test_htab_int_remove_multiple ();

	test_htab_int_get_single ();

	test_htab_int_get_multple ();

	(void) printf ("Done!\n");

}