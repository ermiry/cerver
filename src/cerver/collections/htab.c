#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include "cerver/collections/htab.h"

#pragma region generic

static size_t htab_generic_hash (
	const void *key, size_t key_size, size_t table_size
) {

	size_t i;
	size_t sum = 0;
	unsigned char *k = (unsigned char *) key;
	for (i = 0; i < key_size; ++i)
		sum = (sum + (int)k[i]) % table_size;
	
	return sum;
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
		(void) memcpy (retval, key, key_size);
	}

	return retval;

}

static inline int htab_internal_key_compare (
	Htab *htab,
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
		while (bucket->count) {
			HtabNode *node = bucket->start;
			bucket->start = bucket->start->next;

			htab_node_delete (node, key_delete, delete_data);

			bucket->count--;
		}

		free (bucket);
	}

}

static void htab_delete (Htab *htab) {

	if (htab) {
		if (htab->table) free (htab->table);
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
	}

	return htab;

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
// hash - custom method to hash the key for insertion, NULL for default
// delete_data - custom method to delete your data, NULL for no delete when htab gets destroyed
Htab *htab_create (
	size_t size,
	size_t (*hash)(const void *key, size_t key_size, size_t table_size),
	void (*delete_data)(void *data)
) {

	Htab *htab = htab_new ();
	if (htab) {
		if (size > 0) {
			htab->size = size;
			
			htab->table = (HtabBucket **) calloc (htab->size, sizeof (HtabBucket));
			if (htab->table) {
				for (size_t i = 0; i < htab->size; i++)
					htab->table[i] = htab_bucket_new ();

				htab->hash = hash ? hash : htab_generic_hash;
				// htab->compare = compare ? compare : htab_generic_compare;

				htab->delete_data = delete_data;
			}

			htab->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			(void) pthread_mutex_init (htab->mutex, NULL);
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

		HtabNode *node = ht->table[index]->start;
		while (node && node->key && node->val) {
			if (node->key_size == key_size) {
				if (!htab_internal_key_compare (
					ht, 
					key, key_size, node->key, node->key_size
				)) {
					retval = true;
					break;
				}

				else node = node->next;
			}

			else node = node->next;
		}

		(void) pthread_mutex_unlock (ht->mutex);
	}

	return retval;

}

// inserts a new value to the htab associated with its key
// returns 0 on success, 1 on error
int htab_insert (
	Htab *ht,
	const void *key, size_t key_size,
	void *val, size_t val_size
) {

	int retval = 1;

	if (ht && ht->hash && key && key_size && val && val_size) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);
		// printf ("size: %ld\n", ht->size);
		// printf ("index: %ld\n", index);

		HtabBucket *bucket = ht->table[index];
		HtabNode *node = bucket->start;
		if (node) {
			while (
				node->next
				&& htab_internal_key_compare (
					ht, key, key_size, node->key, node->key_size
				)
			) node = node->next;

			if (htab_internal_key_compare (
				ht,
				key, key_size, node->key, node->key_size
			)) {
				node->next = htab_node_create (
					key, key_size, val, val_size,
					ht->key_create
				);
				
				if (node->next) {
					node = node->next;

					bucket->count += 1;
					ht->count += 1;

					retval = 0;
				}
			}
		}

		else {
			node = htab_node_create (
				key, key_size, val, val_size,
				ht->key_create
			);

			if (node) {
				bucket->start = node;

				bucket->count += 1;
				ht->count += 1;

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

	if (ht && key) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);
		HtabNode *node = ht->table[index]->start;  
		while (node && node->key && node->val) {
			if (node->key_size == key_size) {
				if (!htab_internal_key_compare (
					ht,
					key, key_size, node->key, node->key_size
				)) {
					retval = node->val;
					break;
				}

				else {
					node = node->next;
				} 
			}

			else {
				node = node->next;
			}
		}

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

	if (ht && key && ht->hash) {
		(void) pthread_mutex_lock (ht->mutex);

		size_t index = ht->hash (key, key_size, ht->size);
		HtabBucket *bucket = ht->table[index];
		HtabNode *node = bucket->start;
		HtabNode *prev = NULL;
		while (node) {
			if (!htab_internal_key_compare (
				ht, 
				key, key_size, node->key, node->key_size
			)) {
				if (!prev) ht->table[index]->start = ht->table[index]->start->next;
				else prev->next = node->next;

				retval = node->val;

				node->val = NULL;
				htab_node_delete (node, ht->key_delete, ht->delete_data);

				bucket->count--;
				ht->count--;

				break;
			}

			prev = node;
			node = node->next;
		}

		(void) pthread_mutex_unlock (ht->mutex);
	}

	return retval;

}

void htab_destroy (Htab *ht) {

	if (ht) {
		(void) pthread_mutex_lock (ht->mutex);

		if (ht->table) {
			for (size_t i = 0; i < ht->size; i++) {
				if (ht->table[i]) {
					htab_bucket_delete (
						ht->table[i], 
						ht->key_delete, 
						ht->delete_data
					);
				}
			}
		}

		(void) pthread_mutex_unlock (ht->mutex);
		(void) pthread_mutex_destroy (ht->mutex);
		free (ht->mutex);
		
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