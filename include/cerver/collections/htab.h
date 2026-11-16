/**
 * @file cerver/collections/htab.h
 * @brief A hash table: values (pointers) indexed by keys of bytes.
 */

#ifndef _COLLECTIONS_HTAB_H_
#define _COLLECTIONS_HTAB_H_

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

/**
 * @defgroup collections_htab Hash table
 * @ingroup collections
 * @brief A hash table with chained buckets: the keys are copied, the values are pointers that the table keeps.
 * @{
 */

/** @brief The buckets of a new table when htab_create () gets 0. */
#define HTAB_DEFAULT_INIT_SIZE				32

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A key with its value, in the chain of a bucket.
 */
typedef struct HtabNode {

	/** @brief The next node of the bucket. */
	struct HtabNode *next;
	/** @brief The copy of the key. */
	void *key;
	/** @brief The size in bytes of the key. */
	size_t key_size;
	/** @brief The value, the table keeps the pointer. */
	void *val;
	/** @brief The size of the value that was inserted, only stored. */
	size_t val_size;

} HtabNode;

/**
 * @brief A bucket: the chain of the nodes whose keys have the same hash.
 */
typedef struct HtabBucket {

	/** @brief The first node, the last one that was inserted. */
	struct HtabNode *start;
	/** @brief The nodes of the bucket. */
	size_t count;

} HtabBucket;

/**
 * @brief A hash table.
 *
 * @threadsafety The methods take its mutex, except htab_print ().
 */
typedef struct Htab {

	/** @brief The buckets. */
	HtabBucket **table;

	/** @brief The number of buckets, a power of 2. */
	size_t size;
	/** @brief The values inside the table. */
	size_t count;

	/** @brief Hashes a key to a bucket, it must return a value below table_size. */
	size_t (*hash)(
		const void *key, size_t key_size, size_t table_size
	);

	// int (*compare)(const void *k1, size_t s1, const void *k2, size_t s2);
	/** @brief Allocates the copy of a key, NULL to copy its bytes. */
	void *(*key_create)(const void *);
	/** @brief Releases the copy of a key, NULL to use free (). */
	void (*key_delete)(void *);
	/** @brief Compares two keys, NULL to compare their sizes and their bytes. */
	int (*key_compare)(
		const void *one, const void *two
	);

	/** @brief Releases a value in htab_destroy (), NULL to keep it. */
	void (*delete_data)(void *data);

	/** @brief Protects the table. */
	pthread_mutex_t *mutex;

} Htab;

/**
 * @brief Sets the method that allocates the copy of a key.
 *
 * @param[in,out] htab the table, NULL is ignored
 * @param[in] key_create gets the original key and returns its copy; NULL copies the bytes of the key
 * @note A table with a key_create method never grows, as its keys can not be hashed again.
 */
extern void htab_set_key_create (
	Htab *htab, void *(*key_create)(const void *)
);

/**
 * @brief Sets the method that releases the copy of a key.
 *
 * @param[in,out] htab the table, NULL is ignored
 * @param[in] key_delete gets the copy of the key; NULL uses free ()
 */
extern void htab_set_key_delete (
	Htab *htab, void (*key_delete)(void *)
);

/**
 * @brief Sets the method that compares two keys, to compare them by specific fields.
 *
 * @param[in,out] htab the table, NULL is ignored
 * @param[in] key_compare returns 0 when the keys are equal; NULL compares their sizes and their bytes
 * @note With a key_compare method, the sizes of the keys are not compared.
 */
extern void htab_set_key_comparator (
	Htab *htab,
	int (*key_compare)(const void *one, const void *two)
);

/**
 * @brief Creates an empty table.
 *
 * The table doubles its buckets when it is 3/4 full, except when a key_create method is set.
 *
 * @param[in] size the buckets, more buckets are less collisions; rounded up to a power of 2, 0 uses
 * #HTAB_DEFAULT_INIT_SIZE
 * @param[in] hash hashes a key to a bucket, NULL for the default one (FNV-1a)
 * @param[in] delete_data releases the values in htab_destroy (), NULL to keep them
 * @return the new table, released with htab_destroy (); NULL if it could not be allocated
 */
extern Htab *htab_create (
	size_t size,
	size_t (*hash)(const void *key, size_t key_size, size_t table_size),
	void (*delete_data)(void *data)
);

/**
 * @brief Returns the number of values inside the table.
 *
 * @param[in] htab the table
 * @return the values, 0 for a NULL table
 */
extern size_t htab_size (Htab *htab);

/**
 * @brief Returns whether the table is empty.
 *
 * @param[in] htab the table
 * @return true if it has no values or it is NULL
 */
extern bool htab_is_empty (Htab *htab);

/**
 * @brief Returns whether the table has values.
 *
 * @param[in] htab the table
 * @return true if it has at least one value, false if it is empty or NULL
 */
extern bool htab_is_not_empty (Htab *htab);

/**
 * @brief Returns whether a key has a value.
 *
 * @param[in] ht the table
 * @param[in] key the key
 * @param[in] key_size the size in bytes of the key
 * @return true if the key has a value, false if it does not or an argument is NULL or 0
 */
extern bool htab_contains_key (
	Htab *ht, const void *key, size_t key_size
);

/**
 * @brief Inserts a value associated with its key; the key is copied.
 *
 * @param[in,out] ht the table
 * @param[in] key the key
 * @param[in] key_size the size in bytes of the key
 * @param[in] val the value, the table keeps the pointer
 * @param[in] val_size the size of the value, only stored; it can not be 0
 * @retval 0 the value was inserted
 * @retval 1 a NULL or 0 argument, the key already has a value, or the node could not be allocated
 */
extern int htab_insert (
	Htab *ht,
	const void *key, size_t key_size,
	void *val, size_t val_size
);

/**
 * @brief Returns the value associated with a key.
 *
 * @param[in] ht the table
 * @param[in] key the key
 * @param[in] key_size the size in bytes of the key
 * @return the value, NULL if the key has none or an argument is NULL or 0
 * @warning The value is returned after the mutex is released: another thread can remove it; use
 * htab_get_apply () to use the value with the mutex.
 */
extern void *htab_get (
	Htab *ht, const void *key, size_t key_size
);

/**
 * @brief Runs a method with the value associated with a key while the mutex of the table is held, so another
 * thread can not remove it in between.
 *
 * @param[in] ht the table
 * @param[in] key the key
 * @param[in] key_size the size in bytes of the key
 * @param[in] method gets the value and the method args; it must not change the same table
 * @param[in] method_args the second argument of the method
 * @retval 0 the key was found and the method ran
 * @retval 1 the key has no value or an argument is NULL or 0
 * @since 1.9b-2
 */
extern unsigned int htab_get_apply (
	Htab *ht, const void *key, size_t key_size,
	void (*method)(void *data, void *method_args), void *method_args
);

/**
 * @brief Removes a key from the table and returns its value, which is not released.
 *
 * @param[in,out] ht the table
 * @param[in] key the key
 * @param[in] key_size the size in bytes of the key
 * @return the value, to be released by the caller; NULL if the key has none
 */
extern void *htab_remove (
	Htab *ht, const void *key, size_t key_size
);

/**
 * @brief Releases the table, its keys and, with the delete_data method, its values.
 *
 * @param[in] ht the table, it can be NULL
 */
extern void htab_destroy (Htab *ht);

/**
 * @brief Prints the buckets and the nodes of the table, for debugging and testing.
 *
 * @param[in] htab the table
 * @warning It only works when both the keys and the values are ints, and it does not take the mutex.
 */
extern void htab_print (Htab *htab);

#ifdef __cplusplus
}
#endif

/** @} */

#endif
