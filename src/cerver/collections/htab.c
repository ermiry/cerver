#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include "cerver/collections/htab.h"

// 64 bit FNV-1a parameters
static const uint64_t HTAB_FNV_OFFSET_BASIS = 14695981039346656037ULL;
static const uint64_t HTAB_FNV_PRIME = 1099511628211ULL;

// the table grows when count > size * numerator / denominator
static const size_t HTAB_LOAD_FACTOR_NUMERATOR = 3;
static const size_t HTAB_LOAD_FACTOR_DENOMINATOR = 4;

static const size_t HTAB_GROWTH_FACTOR = 2;

#pragma region generic

static size_t htab_generic_hash (
	const void *key, size_t key_size, size_t table_size
) {

	uint64_t hash = HTAB_FNV_OFFSET_BASIS;

	const unsigned char *bytes = (const unsigned char *) key;
	for (size_t i = 0; i < key_size; i++) {
		hash ^= (uint64_t) bytes[i];
		hash *= HTAB_FNV_PRIME;
	}

	return (size_t) (hash % (uint64_t) table_size);

}

static int htab_generic_compare (
	const void *k1, size_t s1, const void *k2, size_t s2
) {

	if (!k1 || !s1 || !k2 || !s2) return -1;

	if (s1 != s2) return -1;

	return memcmp (k1, k2, s1);
}

#pragma endregion

#pragma region internal

static inline void *htab_internal_key_create (
	void *(*key_create)(const void *),
	const void *key, size_t key_size
) {

	void *retval = NULL;

	if (key_create) retval = key_create (key);
	else {
		retval = malloc (key_size);
		if (retval) (void) memcpy (retval, key, key_size);
	}

	return retval;

}

static inline int htab_internal_key_compare (
	const Htab *htab,
	const void *k1, size_t s1, const void *k2, size_t s2
) {

	return htab->key_compare ?
		htab->key_compare (k1, k2) :
		htab_generic_compare (k1, s1, k2, s2);

}

static HtabNode *htab_node_new (void) {

	HtabNode *node = (HtabNode *) malloc (sizeof (HtabNode));
	if (node) {
		node->key = NULL;
		node->val = NULL;
		node->next = NULL;
		node->key_size = 0;
		node->val_size = 0;
	}

	return node;

}

static void htab_node_delete (
	HtabNode *node,
	void (*key_delete)(void *),
	void (*delete_data)(void *data)
) {

	if (node) {
		if (node->val) {
			if (delete_data) delete_data (node->val);
		}

		if (node->key) {
			if (key_delete) key_delete (node->key) ;
			else free (node->key);
		}

		free (node);
	}

}

static HtabNode *htab_node_create (
	const void *key, size_t key_size, void *val, size_t val_size,
	void *(*key_create)(void const *)
) {

	HtabNode *node = htab_node_new ();
	if (node) {
		node->key_size = key_size;
		node->key = htab_internal_key_create (key_create, key, key_size);

		node->val = val;
		node->val_size = val_size;

		if (!node->key) {
			free (node);
			node = NULL;
		}
	}

	return node;

}

static HtabBucket *htab_bucket_new (void) {

	HtabBucket *bucket = (HtabBucket *) malloc (sizeof (HtabBucket));
	if (bucket) {
		bucket->start = NULL;
		bucket->count = 0;
	}

	return bucket;

}

static void htab_bucket_delete (
	HtabBucket *bucket,
	void (*key_delete)(void *),
	void (*delete_data)(void *data)
) {

	if (bucket) {
		while (bucket->start) {
			HtabNode *node = bucket->start;
			bucket->start = bucket->start->next;

			htab_node_delete (node, key_delete, delete_data);
		}

		bucket->count = 0;

		free (bucket);
	}

}

static void htab_table_delete (
	HtabBucket **table, size_t size,
	void (*key_delete)(void *),
	void (*delete_data)(void *data)
) {

	if (table) {
		for (size_t i = 0; i < size; i++) {
			htab_bucket_delete (table[i], key_delete, delete_data);
		}

		free (table);
	}

}

static HtabBucket **htab_table_create (size_t size) {

	HtabBucket **table = (HtabBucket **) calloc (size, sizeof (HtabBucket *));
	if (table) {
		bool failed = false;
		for (size_t i = 0; i < size; i++) {
			table[i] = htab_bucket_new ();
			if (!table[i]) failed = true;
		}

		if (failed) {
			htab_table_delete (table, size, NULL, NULL);
			table = NULL;
		}
	}

	return table;

}

static void htab_delete (Htab *htab) {

	if (htab) {
		if (htab->mutex) {
			(void) pthread_mutex_destroy (htab->mutex);
			free (htab->mutex);
		}

		free (htab);
	}

}

static Htab *htab_new (void) {

	Htab *htab = (Htab *) malloc (sizeof (Htab));
	if (htab) {
		htab->table = NULL;

		htab->size = 0;
		htab->count = 0;

		htab->hash = NULL;

		htab->key_create = NULL;
		htab->key_delete = NULL;
		htab->key_compare = NULL;

		htab->delete_data = NULL;

		htab->mutex = NULL;
	}

	return htab;

}

// returns the smallest power of 2 that is >= size
static size_t htab_internal_size_round (size_t size) {

	size_t retval = 1;
	while (retval < size) retval <<= 1;

	return retval;

}

// searches the bucket for the key
// returns the matching node and sets prev to the node before it
static HtabNode *htab_internal_find (
	const Htab *htab, const HtabBucket *bucket,
	const void *key, size_t key_size,
	HtabNode **prev
) {

	HtabNode *retval = NULL;

	HtabNode *before = NULL;
	HtabNode *node = bucket->start;
	while (node) {
		if (
			(htab->key_compare || (node->key_size == key_size))
			&& !htab_internal_key_compare (
				htab, key, key_size, node->key, node->key_size
			)
		) {
			retval = node;
			break;
		}

		before = node;
		node = node->next;
	}

	if (prev) *prev = before;

	return retval;

}

// the stored keys can only be hashed again
// when they are a byte copy of the original keys
static inline bool htab_internal_can_grow (const Htab *htab) {

	return (htab->key_create == NULL);

}

static void htab_internal_move_nodes (
	Htab *htab, HtabBucket **table, size_t size
) {

	for (size_t i = 0; i < htab->size; i++) {
		HtabNode *node = htab->table[i]->start;
		while (node) {
			HtabNode *next = node->next;

			size_t index = htab->hash (node->key, node->key_size, size);
			node->next = table[index]->start;
			table[index]->start = node;
			table[index]->count += 1;

			node = next;
		}

		htab->table[i]->start = NULL;
		htab->table[i]->count = 0;
	}

}

// doubles the table size and moves every node to its new bucket
// if the new table can not be allocated, the current one is kept
static void htab_internal_grow (Htab *htab) {

	size_t new_size = htab->size * HTAB_GROWTH_FACTOR;

	HtabBucket **table = htab_table_create (new_size);
	if (table) {
		htab_internal_move_nodes (htab, table, new_size);

		htab_table_delete (htab->table, htab->size, NULL, NULL);

		htab->table = table;
		htab->size = new_size;
	}

}

static void htab_internal_check_grow (Htab *htab) {

	if (
		htab_internal_can_grow (htab)
		&& (htab->count * HTAB_LOAD_FACTOR_DENOMINATOR) > (htab->size * HTAB_LOAD_FACTOR_NUMERATOR)
	) {
		htab_internal_grow (htab);
	}

}

#pragma endregion

// sets a method to correctly create (allocate) a new key
// your original key data is passed as the argument to this method
// if not set, a genreic internal method will be called instead
void htab_set_key_create (
	Htab *htab, void *(*key_create)(const void *)
) {

	if (htab) {
		htab->key_create = key_create;
	}

}

// sets a method to correctly delete (free) your previous allocated key
// a ptr to the allocated key if passed for you to correctly handle it
// if not set, free will be used as default
void htab_set_key_delete (
	Htab *htab, void (*key_delete)(void *)
) {

	if (htab) {
		htab->key_delete = key_delete;
	}

}

// sets a method to correctly compare keys
// usefull if you want to compare your keys (data) by specific fields
// if not set, a generic method will be used instead
void htab_set_key_comparator (
	Htab *htab, int (*key_compare)(const void *one, const void *two)
) {

	if (htab) {
		htab->key_compare = key_compare;
	}

}

// creates a new htab
// size - how many buckets do you want - more buckets = less collisions
// it is rounded up to a power of 2, and 0 uses HTAB_DEFAULT_INIT_SIZE
// hash - custom method to hash the key for insertion, NULL for default
// delete_data - custom method to delete your data, NULL for no delete when htab gets destroyed
Htab *htab_create (
	size_t size,
	size_t (*hash)(const void *key, size_t key_size, size_t table_size),
	void (*delete_data)(void *data)
) {

	Htab *htab = htab_new ();
	if (htab) {
		htab->size = htab_internal_size_round (
			size ? size : HTAB_DEFAULT_INIT_SIZE
		);

		htab->hash = hash ? hash : htab_generic_hash;
		htab->delete_data = delete_data;

		htab->table = htab_table_create (htab->size);

		htab->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		if (htab->mutex) {
			(void) pthread_mutex_init (htab->mutex, NULL);
		}

		if (!htab->table || !htab->mutex) {
			htab_table_delete (htab->table, htab->size, NULL, NULL);
			htab_delete (htab);
			htab = NULL;
		}
	}

	return htab;

}

// returns the current number of elements inside the htab
size_t htab_size (Htab *htab) {

	size_t retval = 0;

	if (htab) {
		(void) pthread_mutex_lock (htab->mutex);

		retval = htab->count;

		(void) pthread_mutex_unlock (htab->mutex);
	}

	return retval;

}

// returns true if its empty (size == 0)
bool htab_is_empty (Htab *htab) {

	bool retval = true;

	if (htab) {
		(void) pthread_mutex_lock (htab->mutex);

		retval = (htab->count == 0);

		(void) pthread_mutex_unlock (htab->mutex);
	}

	return retval;

}

// returns true if its NOT empty (size > 0)
bool htab_is_not_empty (Htab *htab) {

	bool retval = false;

	if (htab) {
		(void) pthread_mutex_lock (htab->mutex);

		retval = (htab->count > 0);

		(void) pthread_mutex_unlock (htab->mutex);
	}

	return retval;

}

// returns true if there is at least 1 data associated with the key
// returns false if their is none
bool htab_contains_key (
	Htab *ht, const void *key, size_t key_size
) {

	bool retval = false;

	if (ht && key && key_size) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);

		retval = (htab_internal_find (
			ht, ht->table[index], key, key_size, NULL
		) != NULL);

		(void) pthread_mutex_unlock (ht->mutex);
	}

	return retval;

}

// inserts a new value to the htab associated with its key
// returns 0 on success, 1 on error or if the key already exists
int htab_insert (
	Htab *ht,
	const void *key, size_t key_size,
	void *val, size_t val_size
) {

	int retval = 1;

	if (ht && ht->hash && key && key_size && val && val_size) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);
		HtabBucket *bucket = ht->table[index];

		if (!htab_internal_find (ht, bucket, key, key_size, NULL)) {
			HtabNode *node = htab_node_create (
				key, key_size, val, val_size,
				ht->key_create
			);

			if (node) {
				node->next = bucket->start;
				bucket->start = node;

				bucket->count += 1;
				ht->count += 1;

				htab_internal_check_grow (ht);

				retval = 0;
			}
		}

		(void) pthread_mutex_unlock (ht->mutex);
	}

	return retval;

}

// returns a ptr to the data associated with the key
// returns NULL if no data was found
void *htab_get (
	Htab *ht, const void *key, size_t key_size
) {

	void *retval = NULL;

	if (ht && key && key_size) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);

		HtabNode *node = htab_internal_find (
			ht, ht->table[index], key, key_size, NULL
		);

		if (node) retval = node->val;

		(void) pthread_mutex_unlock (ht->mutex);
	}

	return retval;

}

// removes and returns the data associated with the key from the htab
// the data should be deleted by the user
// returns NULL if no data was found with the provided key
void *htab_remove (
	Htab *ht, const void *key, size_t key_size
) {

	void *retval = NULL;

	if (ht && key && key_size && ht->hash) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);
		HtabBucket *bucket = ht->table[index];

		HtabNode *prev = NULL;
		HtabNode *node = htab_internal_find (ht, bucket, key, key_size, &prev);
		if (node) {
			if (prev) prev->next = node->next;
			else bucket->start = node->next;

			retval = node->val;

			node->val = NULL;
			htab_node_delete (node, ht->key_delete, ht->delete_data);

			bucket->count--;
			ht->count--;
		}

		(void) pthread_mutex_unlock (ht->mutex);
	}

	return retval;

}

void htab_destroy (Htab *ht) {

	if (ht) {
		(void) pthread_mutex_lock (ht->mutex);

		htab_table_delete (
			ht->table, ht->size,
			ht->key_delete, ht->delete_data
		);

		ht->table = NULL;

		(void) pthread_mutex_unlock (ht->mutex);

		htab_delete (ht);
	}

}

static void htab_node_print (HtabNode *node) {

	if (node) {
		int *int_key = (int *) node->key;
		int *int_value = (int *) node->val;
		(void) printf ("\tKey %d - Value: %d\n", *int_key, *int_value);
	}

}

static void htab_bucket_print (
	HtabBucket *bucket, size_t idx
) {

	if (bucket) {
		(void) printf ("Bucket <%lu> count: %lu\n", idx, bucket->count);
		HtabNode *n = bucket->start;
		while (n) {
			htab_node_print (n);

			n = n->next;
		}
	}

}

void htab_print (Htab *htab) {

	if (htab) {
		(void) printf ("\n\n");
		(void) printf ("Htab's size: %lu\n", htab->size);
		(void) printf ("Htab's count: %lu\n", htab->count);

		for (size_t idx = 0; idx < htab->size; idx++) {
			htab_bucket_print (htab->table[idx], idx);
		}

		(void) printf ("\n\n");
	}

}
