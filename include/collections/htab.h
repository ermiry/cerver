#ifndef _HTAB_H_
#define _HTAB_H_

#include <stdlib.h>
#include <string.h>

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
    Copy vcopy_f;

} Htab;

extern Htab *htab_init (int size, Hash hash_f, Compare compare_f, Copy kcopy_f, Copy vcopy_f);
extern int htab_cleanup (Htab *ht);
extern void htab_destroy (Htab *ht);

extern int htab_insert (Htab *ht, const void *key, size_t ksz, const void *val, size_t vsz);
extern int htab_get (Htab *ht, const void *key, size_t ksz, void **val, size_t *vsz);
extern bool htab_contains_key (Htab *ht, const void *key, size_t key_size);
extern int htab_remove (Htab *ht, const void *key, size_t key_size);

extern void *htab_getData (Htab *ht, const void *key, size_t key_size, size_t *val_size);

#endif