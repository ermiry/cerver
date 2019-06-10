#include <stdbool.h>

#include "collections/htab.h"

size_t htab_generic_hash (const void *key, size_t key_size, size_t table_size) {

    size_t i;
    size_t sum = 0;
    unsigned char *k = (unsigned char *) key;
    for (i = 0; i < key_size; ++i)
        sum = (sum + (int)k[i]) % table_size;
    
    return sum;
}

int htab_generic_compare (const void *k1, size_t s1, const void *k2, size_t s2) {

    if (!k1 || !s1 || !k2 || !s2) return -1;

    if (s1 != s2) return -1;

    return memcmp (k1, k2, s1);
}

int htab_generic_copy (void **dst, const void *src, size_t sz) {

    if (!dst || !src || !sz) return -1;

    if (!*dst) *dst = malloc (sz);

    if (!*dst) return -1;

    memcpy (*dst, src, sz);

    return 0;
}

void htab_node_init (HtabNode *node) {

    if (node) {
        node->key = NULL;
        node->val = NULL;
        node->next = NULL;
        node->key_size = 0;
        node->val_size = 0;
    }

}

void htab_node_cleanup (HtabNode *node) {

    if (node) {
        free(node->key);
        free(node->val);
        node->key = NULL;
        node->val = NULL;
        node->next = NULL;
        node->key_size = 0;
        node->val_size = 0;
    }

}

// TODO: pass destroy function
Htab *htab_init (int table_size, Hash hash_f, Compare compare_f, Copy kcopy_f, 
    Copy vcopy_f) {

    Htab *ht = (Htab *) malloc (sizeof (Htab));

    if (ht) {
        ht->hash_f = hash_f ? hash_f : htab_generic_hash;
        ht->compare_f = compare_f ? compare_f : htab_generic_compare;
        ht->kcopy_f = kcopy_f ? kcopy_f : htab_generic_copy;
        ht->vcopy_f = vcopy_f ? vcopy_f : htab_generic_copy;
        ht->count = 0;

        ht->size = table_size <= 0 ? HTAB_INIT_SIZE : table_size;
        ht->table = (HtabNode **) calloc (ht->size, sizeof (HtabNode *));

        for (size_t i = 0; i < ht->size; ++i) ht->table[i] = NULL;
    }
    
    return ht;
    
}

int htab_insert (Htab *ht, const void *key, size_t key_size, const void *val, 
    size_t val_size) {

    size_t index;
    HtabNode *node = NULL;
    
    if (!ht || !ht->hash_f || !key || !key_size || !val || !val_size || !ht->compare_f)
        return 1;

    index = ht->hash_f(key, key_size, ht->size);
    node = ht->table[index];

    if (node) {
        while (node->next && ht->compare_f(key, key_size, node->key, node->key_size))
            node = node->next;

        if (ht->compare_f(key, key_size, node->key, node->key_size)) {
            node->next = (HtabNode *) malloc (sizeof(HtabNode));
            node = node->next;
            htab_node_init(node);
        }
    }

    else {
        node = (HtabNode *) malloc (sizeof(HtabNode));
        if (!node) return 1;
        htab_node_init(node);
        ht->table[index] = node;
    }
    
    node->key_size = key_size;
    node->val_size = val_size;
    node->key = malloc(node->key_size);
    node->val = malloc(node->val_size);

    if (!node->key || !node->val) return 1;

    ht->kcopy_f(&node->key, key, node->key_size);
    ht->vcopy_f(&node->val, val, node->val_size);
    ++ht->count;

    return 0;

}

void *htab_getData (Htab *ht, const void *key, size_t key_size, size_t *val_size) {

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

int htab_remove (Htab *ht, const void *key, size_t key_size) {

    size_t index;
    HtabNode *node = NULL, *prev = NULL;

    if (!ht || !key || !ht->compare_f) return -1;

    index = ht->hash_f(key, key_size, ht->size);
    node = ht->table[index];
    prev = NULL;
    while (node) {
        if (!ht->compare_f(key, key_size, node->key, node->key_size)) {
            if (!prev) ht->table[index] = ht->table[index]->next;
            else prev->next = node->next;

            htab_node_cleanup(node);
            free(node);
            --ht->count;

            return 0;
        }
        prev = node;
        node = node->next;
    }

    return -1;

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
        HtabNode *node = NULL;
        for (size_t i = 0; i < ht->size; i++) {
            if (ht->table[i]) {
                node = ht->table[i];
                while (node) {
                    if (node->key) free (node->key);
                    if (node->val) free (node->val);

                    node = node->next;
                }

                free (node);
            }
        }

        free (ht);
    }

}