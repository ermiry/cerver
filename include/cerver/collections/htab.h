#ifndef _COLLECTIONS_HTAB_H_
#define _COLLECTIONS_HTAB_H_

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define HTAB_INIT_SIZE      7

typedef size_t (*Hash)(const void *key, size_t key_size, size_t table_size);
typedef int (*Compare)(const void *k1, size_t s1, const void *k2, size_t s2);
typedef int (*Copy)(void **dst, const void *src, size_t sz);

typedef struct HtabNode {

    struct HtabNode *next;
    void *key;
    size_t key_size;
    void *val;
    size_t val_size;

} HtabNode;

typedef struct Htab {

    HtabNode **table;
    size_t size;
    size_t count;

    Hash hash_f;
    Compare compare_f;
    Copy kcopy_f;

    bool allow_copy;
    Copy vcopy_f;
    void (*destroy)(void *data);

} Htab;

// creates a new htab
// size --> initial htab nodes size
// hash_f --> ptr to a custom hash function
// compare_f -> ptr to a custom value compare function
// kcopy_f --> ptr to a custom function to copy keys into the htab (generate a new copy)
// allow_copy --> select if you want to create a new copy of the values
// vcopy_f --> ptr to a custom function to copy values into the htab (generate a new copy)
// destroy --> custom function to destroy copied values
extern Htab *htab_init (unsigned int size, Hash hash_f, Compare compare_f, Copy kcopy_f, 
    bool allow_copy, Copy vcopy_f, void (*destroy)(void *data));

// inserts a new value to the htab associated with its key
extern int htab_insert (Htab *ht, const void *key, size_t key_size, 
    void *val, size_t val_size);

// returns a ptr to the data associated with the key
extern void *htab_get_data (Htab *ht, const void *key, size_t key_size);

// removes the data associated with the key from the htab
extern int htab_remove (Htab *ht, const void *key, size_t key_size);

extern bool htab_contains_key (Htab *ht, const void *key, size_t key_size);

// destroys the htb and all of its data
extern void htab_destroy (Htab *ht);

// extern int htab_get (Htab *ht, const void *key, size_t ksz, void **val, size_t *vsz);

#endif