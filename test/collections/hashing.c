#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/htab.h>

#include "../test.h"
#include "data.h"

// bugs that this suite pinned:
// - the hash was the sum of the key bytes and the table never grew
// - the table was allocated with sizeof (HtabBucket) instead of the pointer
// - the mutex was only created when size > 0 (a size of 0 crashed on lock)
// - a duplicated key failed without any difference from an error
// coverage holes:
// - a custom key_create keeps the table size fixed (its keys can not be hashed again)

#define HASHING_COLLISIONS					64
#define HASHING_GROWTH_KEYS					4096
#define HASHING_ROUNDED_SIZE				100
#define HASHING_ROUNDED_EXPECTED			128
#define HASHING_SOCKETS						256
#define HASHING_SOCKET_ROUNDS				4

// sends every key to the same bucket
static size_t hashing_collision_hash (
	const void *key, size_t key_size, size_t table_size
) {

	(void) key;
	(void) key_size;
	(void) table_size;

	return 0;

}

static void *hashing_key_create (const void *key) {

	unsigned int *created = (unsigned int *) malloc (sizeof (unsigned int));
	if (created) *created = *(const unsigned int *) key;

	return created;

}

static int hashing_insert_value (Htab *map, unsigned int key, unsigned int value) {

	return htab_insert (
		map,
		&key, sizeof (unsigned int),
		data_new (key, value), sizeof (Data)
	);

}

static Data *hashing_get_value (Htab *map, unsigned int key) {

	return (Data *) htab_get (map, &key, sizeof (unsigned int));

}

static void test_hashing_collisions (void) {

	Htab *map = htab_create (HTAB_DEFAULT_INIT_SIZE, hashing_collision_hash, data_delete);
	test_check_ptr (map);

	for (unsigned int i = 0; i < HASHING_COLLISIONS; i++) {
		test_check_int_eq (hashing_insert_value (map, i, i), 0, NULL);
	}

	test_check (map->table[0]->count == HASHING_COLLISIONS, NULL);

	// every key is found inside the same bucket
	for (unsigned int i = 0; i < HASHING_COLLISIONS; i++) {
		Data *data = hashing_get_value (map, i);
		test_check_ptr (data);
		test_check_unsigned_eq (data->idx, i, NULL);
	}

	// removing from the start, the middle and the end of the chain
	const unsigned int removes[] = { 0, HASHING_COLLISIONS / 2, HASHING_COLLISIONS - 1 };
	for (unsigned int i = 0; i < 3; i++) {
		unsigned int key = removes[i];
		Data *removed = (Data *) htab_remove (map, &key, sizeof (unsigned int));
		test_check_ptr (removed);
		test_check_unsigned_eq (removed->idx, key, NULL);
		data_delete (removed);

		test_check_false (htab_contains_key (map, &key, sizeof (unsigned int)));
	}

	test_check (htab_size (map) == (HASHING_COLLISIONS - 3), NULL);

	// the rest of the chain is still reachable
	for (unsigned int i = 1; i < HASHING_COLLISIONS - 1; i++) {
		if (i != HASHING_COLLISIONS / 2) {
			test_check_ptr (hashing_get_value (map, i));
		}
	}

	htab_destroy (map);

}

static void test_hashing_duplicates (void) {

	Htab *map = htab_create (HTAB_DEFAULT_INIT_SIZE, NULL, data_delete);

	test_check_int_eq (hashing_insert_value (map, 7, 1), 0, NULL);

	// the duplicated key is rejected and the first value is kept
	unsigned int key = 7;
	Data *duplicate = data_new (7, 2);
	test_check_int_eq (htab_insert (map, &key, sizeof (unsigned int), duplicate, sizeof (Data)), 1, NULL);
	data_delete (duplicate);

	test_check (htab_size (map) == 1, NULL);
	test_check_unsigned_eq (hashing_get_value (map, 7)->value, 1, NULL);

	htab_destroy (map);

}

static void test_hashing_growth (void) {

	Htab *map = htab_create (HTAB_DEFAULT_INIT_SIZE, NULL, data_delete);

	for (unsigned int i = 0; i < HASHING_GROWTH_KEYS; i++) {
		test_check_int_eq (hashing_insert_value (map, i, i), 0, NULL);
	}

	// the table grew and its size is still a power of 2
	test_check (map->size > HTAB_DEFAULT_INIT_SIZE, NULL);
	test_check ((map->size & (map->size - 1)) == 0, NULL);
	test_check ((map->count * 4) <= (map->size * 3), NULL);

	size_t total = 0;
	for (size_t i = 0; i < map->size; i++) total += map->table[i]->count;
	test_check (total == HASHING_GROWTH_KEYS, NULL);

	for (unsigned int i = 0; i < HASHING_GROWTH_KEYS; i++) {
		Data *data = hashing_get_value (map, i);
		test_check_ptr (data);
		test_check_unsigned_eq (data->value, i, NULL);
	}

	for (unsigned int i = 0; i < HASHING_GROWTH_KEYS; i++) {
		unsigned int key = i;
		data_delete (htab_remove (map, &key, sizeof (unsigned int)));
	}

	test_check_true (htab_is_empty (map));

	htab_destroy (map);

}

static void test_hashing_sizes (void) {

	Htab *zero = htab_create (0, NULL, data_delete);
	test_check_ptr (zero);
	test_check (zero->size == HTAB_DEFAULT_INIT_SIZE, NULL);
	test_check_int_eq (hashing_insert_value (zero, 1, 1), 0, NULL);
	test_check_ptr (hashing_get_value (zero, 1));
	htab_destroy (zero);

	Htab *rounded = htab_create (HASHING_ROUNDED_SIZE, NULL, data_delete);
	test_check (rounded->size == HASHING_ROUNDED_EXPECTED, NULL);
	htab_destroy (rounded);

	// the keys created by a custom method are not hashed again
	Htab *custom = htab_create (HTAB_DEFAULT_INIT_SIZE, NULL, data_delete);
	htab_set_key_create (custom, hashing_key_create);
	for (unsigned int i = 0; i < HASHING_GROWTH_KEYS; i++) {
		test_check_int_eq (hashing_insert_value (custom, i, i), 0, NULL);
	}

	test_check (custom->size == HTAB_DEFAULT_INIT_SIZE, NULL);
	for (unsigned int i = 0; i < HASHING_GROWTH_KEYS; i++) {
		test_check_ptr (hashing_get_value (custom, i));
	}

	htab_destroy (custom);

}

// the cerver maps sock fds to clients, and the kernel reuses fds right away
static void test_hashing_socket_reuse (void) {

	Htab *map = htab_create (HTAB_DEFAULT_INIT_SIZE, NULL, data_delete);

	for (unsigned int round = 0; round < HASHING_SOCKET_ROUNDS; round++) {
		for (unsigned int fd = 0; fd < HASHING_SOCKETS; fd++) {
			test_check_int_eq (hashing_insert_value (map, fd, round), 0, NULL);
		}

		for (unsigned int fd = 0; fd < HASHING_SOCKETS; fd++) {
			Data *data = hashing_get_value (map, fd);
			test_check_ptr (data);
			test_check_unsigned_eq (data->value, round, NULL);

			unsigned int key = fd;
			data_delete (htab_remove (map, &key, sizeof (unsigned int)));
		}

		test_check_true (htab_is_empty (map));
	}

	// a different key size is a different key
	unsigned int key = 1;
	test_check_int_eq (hashing_insert_value (map, key, 1), 0, NULL);
	test_check_null_ptr (htab_get (map, &key, sizeof (unsigned char)));

	htab_destroy (map);

}

void collections_tests_htab_hashing (void) {

	(void) printf ("Testing COLLECTIONS htab hashing...\n");

	test_hashing_collisions ();
	test_hashing_duplicates ();
	test_hashing_growth ();
	test_hashing_sizes ();
	test_hashing_socket_reuse ();

	(void) printf ("Done!\n");

}
