#ifndef _COLLECTIONS_HTAB_H_
#define _COLLECTIONS_HTAB_H_

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#define HTAB_DEFAULT_INIT_SIZE				32

typedef struct HtabNode {

	struct HtabNode *next;
	void *key;
	size_t key_size;
	void *val;
	size_t val_size;

} HtabNode;

typedef struct HtabBucket {

	struct HtabNode *start;
	size_t count;

} HtabBucket;

typedef struct Htab {

	HtabBucket **table;

	size_t size;
	size_t count;

	size_t (*hash)(const void *key, size_t key_size, size_t table_size);

	// int (*compare)(const void *k1, size_t s1, const void *k2, size_t s2);
	void *(*key_create)(const void *);
	void (*key_delete)(void *);
	int (*key_compare)(const void *one, const void *two);

	// method to delete the data
	void (*delete_data)(void *data);

	pthread_mutex_t *mutex;

} Htab;

// sets a method to correctly create (allocate) a new key
// your original key data is passed as the argument to this method
// if not set, a genreic internal method will be called instead
extern void htab_set_key_create (Htab *htab, void *(*key_create)(const void *));

// sets a method to correctly delete (free) your previous allocated key
// a ptr to the allocated key if passed for you to correctly handle it
// if not set, free will be used as default
extern void htab_set_key_delete (Htab *htab, void (*key_delete)(void *));

// sets a method to correctly compare keys
// usefull if you want to compare your keys (data) by specific fields
// if not set, a generic method will be used instead
extern void htab_set_key_comparator (Htab *htab,
	int (*key_compare)(const void *one, const void *two));

// creates a new htab
// size - how many buckets do you want - more buckets = less collisions
// hash - custom method to hash the key for insertion, NULL for default
// delete_data - custom method to delete your data, NULL for no delete when htab gets destroyed
extern Htab *htab_create (size_t size,
	size_t (*hash)(const void *key, size_t key_size, size_t table_size),
	void (*delete_data)(void *data)
);

// returns the current number of elements inside the htab
extern size_t htab_size (Htab *htab);

// returns true if its empty (size == 0)
extern bool htab_is_empty (Htab *htab);

// returns true if its NOT empty (size > 0)
extern bool htab_is_not_empty (Htab *htab);

// returns true if there is at least 1 data associated with the key
// returns false if their is none
extern bool htab_contains_key (Htab *ht, const void *key, size_t key_size);

// inserts a new value to the htab associated with its key
// returns 0 on success, 1 on error
extern int htab_insert (
	Htab *ht,
	const void *key, size_t key_size,
	void *val, size_t val_size
);

// returns a ptr to the data associated with the key
// returns NULL if no data was found
extern void *htab_get (Htab *ht, const void *key, size_t key_size);

// removes the data associated with the key from the htab
extern void *htab_remove (Htab *ht, const void *key, size_t key_size);

// destroys the htb and all of its data
extern void htab_destroy (Htab *ht);

// prints the htab - buckets - nodes
// currently only works if both keys and values are int
// used for debugging and testing
extern void htab_print (Htab *htab);

#endif