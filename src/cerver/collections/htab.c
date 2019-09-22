#include <stdbool.h>

#include "cerver/collections/htab.h"

static size_t htab_generic_hash (const void *key, size_t key_size, size_t table_size) {

    size_t i;
    size_t sum = 0;
    unsigned char *k = (unsigned char *) key;
    for (i = 0; i < key_size; ++i)
        sum = (sum + (int)k[i]) % table_size;
    
    return sum;
}

static int htab_generic_compare (const void *k1, size_t s1, const void *k2, size_t s2) {

    if (!k1 || !s1 || !k2 || !s2) return -1;

    if (s1 != s2) return -1;

    return memcmp (k1, k2, s1);
}

static int htab_generic_copy (void **dst, const void *src, size_t sz) {

    if (!dst || !src || !sz) return -1;

    if (!*dst) *dst = malloc (sz);

    if (!*dst) return -1;

    memcpy (*dst, src, sz);

    return 0;
}

/*** htab nodes ***/

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

static void htab_node_delete (HtabNode *node, bool allow_copy, void (*destroy)(void *data)) {

    if (node) {
        // if (allow_copy) {
            if (node->val) {
                if (destroy) destroy (node->val);
                // else free (node->val);
                else if (allow_copy) free (node->val);
            }
        // }

        if (node->key) free (node->key);

        free (node);
    }

}

/*** Htab ***/

static void htab_delete (Htab *htab) {

    if (htab) {
        if (htab->table) free (htab->table);
        free (htab);
    }

}

static Htab *htab_new (unsigned int size) {

    Htab *htab = (Htab *) malloc (sizeof (Htab));
    if (htab) {
        memset (htab, 0, sizeof (Htab));
        htab->table = NULL;
        htab->hash_f = NULL;
        htab->compare_f = NULL;
        htab->kcopy_f = NULL;
        htab->vcopy_f = NULL;

        htab->size = HTAB_INIT_SIZE;

        htab->table = (HtabNode **) calloc (htab->size, sizeof (HtabNode *));
        if (htab->table) {
            for (size_t i = 0; i < htab->size; ++i) htab->table[i] = NULL;
        }

        else {
            htab_delete (htab);
            htab = NULL;
        }
    }

    return htab;

}

// creates a new htab
// size --> initial htab nodes size
// hash_f --> ptr to a custom hash function
// compare_f -> ptr to a custom value compare function
// kcopy_f --> ptr to a custom function to copy keys into the htab (generate a new copy)
// allow_copy --> select if you want to create a new copy of the values
// vcopy_f --> ptr to a custom function to copy values into the htab (generate a new copy)
// destroy --> custom function to destroy copied values or delete values when calling hatb_destroy
Htab *htab_init (unsigned int size, Hash hash_f, Compare compare_f, Copy kcopy_f, 
    bool allow_copy, Copy vcopy_f, void (*destroy)(void *data)) {

    Htab *ht = htab_new (size);

    if (ht) {
        ht->hash_f = hash_f ? hash_f : htab_generic_hash;
        ht->compare_f = compare_f ? compare_f : htab_generic_compare;
        ht->kcopy_f = kcopy_f ? kcopy_f : htab_generic_copy;

        ht->allow_copy = allow_copy;
        ht->vcopy_f = vcopy_f ? vcopy_f : htab_generic_copy;
        ht->destroy = destroy;

        ht->count = 0;
    }
    
    return ht;
    
}

// inserts a new value to the htab associated with its key
int htab_insert (Htab *ht, const void *key, size_t key_size, void *val, size_t val_size) {

    size_t index;
    HtabNode *node = NULL;
    
    if (!ht || !ht->hash_f || !key || !key_size || !val || !val_size || !ht->compare_f)
        return 1;

    index = ht->hash_f (key, key_size, ht->size);
    node = ht->table[index];

    if (node) {
        while (node->next && ht->compare_f (key, key_size, node->key, node->key_size))
            node = node->next;

        if (ht->compare_f (key, key_size, node->key, node->key_size)) {
            node->next = htab_node_new ();
            if (!node->next) return 1;
            node = node->next;
        }
    }

    else {
        node = htab_node_new ();
        if (!node) return 1;
        ht->table[index] = node;
    }
    
    node->key_size = key_size;
    node->val_size = val_size;
    node->key = malloc (node->key_size);
    if (!node->key) return 1;
    ht->kcopy_f (&node->key, key, node->key_size);

    if (ht->allow_copy) {
        node->val = malloc (node->val_size);
        ht->vcopy_f (&node->val, val, node->val_size);
    }

    // jsut point to the data
    else {
        node->val = val;
    }
    
    ++ht->count;

    return 0;

}

// returns a ptr to the data associated with the key
void *htab_get_data (Htab *ht, const void *key, size_t key_size) {

    if (ht) {
        size_t index;
        HtabNode *node = NULL;  

        index = ht->hash_f(key, key_size, ht->size);
        node = ht->table[index];

        while (node && node->key && node->val) {
            if (node->key_size == key_size) {
                if (!ht->compare_f (key, key_size, node->key, node->key_size)) {
                    return node->val;
                    // ht->vcopy_f(val, node->val, node->val_size);
                    // *val_size = node->val_size;
                    // return 0;
                }

                else node = node->next;
            }

            else node = node->next;
        }
    }

    return NULL;

}

int htab_get (Htab *ht, const void *key, size_t key_size, void **val, 
    size_t *val_size) {

    size_t index;
    HtabNode *node = NULL;

    if (!ht || !key || !ht->compare_f || !val || !val_size)
        return -1;

    index = ht->hash_f(key, key_size, ht->size);
    node = ht->table[index];

    while (node && node->key && node->val) {
        if (node->key_size == key_size) {
            if (!ht->compare_f(key, key_size, node->key, node->key_size)) {
                ht->vcopy_f(val, node->val, node->val_size);
                *val_size = node->val_size;
                return 0;
            }

            else node = node->next;
        }

        else node = node->next;
    }

    *val = NULL;
    *val_size = 0;
    return -1;
} 

bool htab_contains_key (Htab *ht, const void *key, size_t key_size) {

    size_t index;
    HtabNode *node = NULL;

    if (!ht || !key || !ht->compare_f) return -1;

    index = ht->hash_f(key, key_size, ht->size);
    node = ht->table[index];

    while (node && node->key && node->val) {
        if (node->key_size == key_size) {
            if (!ht->compare_f(key, key_size, node->key, node->key_size)) {
                return true;
            }

            else node = node->next;
        }

        else node = node->next;
    }

    return false;

}

// removes the data associated with the key from the htab
int htab_remove (Htab *ht, const void *key, size_t key_size) {

    size_t index;
    HtabNode *node = NULL, *prev = NULL;

    if (!ht || !key || !ht->compare_f) return 1;

    index = ht->hash_f (key, key_size, ht->size);
    node = ht->table[index];
    prev = NULL;
    while (node) {
        if (!ht->compare_f (key, key_size, node->key, node->key_size)) {
            if (!prev) ht->table[index] = ht->table[index]->next;
            else prev->next = node->next;

            htab_node_delete (node, ht->allow_copy, ht->destroy);
            --ht->count;

            return 0;
        }
        prev = node;
        node = node->next;
    }

    return 1;

}

int htab_cleanup (Htab *ht) {

    if (ht) {
        HtabNode *node = NULL;

        if (!ht->table || !ht->size) return 1;

        for (size_t i = 0; i < ht->size; ++i) {
            if (ht->table[i]) {
                node = ht->table[i];
                while (node) {
                    if (node->key) free (node->key);
                    if (node->val) free (node->val);

                    node = node->next;
                }
            }
        }

        return 0;
    }

    else return 1;

}

void htab_destroy (Htab *ht) {

    if (ht) {
        if (ht->table) {
            HtabNode *node = NULL;
            for (size_t i = 0; i < ht->size; i++) {
                if (ht->table[i]) {
                    node = ht->table[i];
                    while (node) {
                        // if (ht->allow_copy) {
                            if (node->val) {
                                if (ht->destroy) ht->destroy (node->val);
                                // else free (node->val);
                                else if (ht->allow_copy) free (node->val);
                            }
                        // }

                        if (node->key) free (node->key);

                        node->key = NULL;
                        node->val = NULL;

                        node = node->next;
                    }

                    free (node);
                }
            }
        }
        
        htab_delete (ht);
    }

}