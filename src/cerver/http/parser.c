#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/collections/dlist.h"
#include "cerver/http/parser.h"

#pragma region Query

static KeyValuePair *key_value_pair_new (void) {

    KeyValuePair *kvp = (KeyValuePair *) malloc (sizeof (KeyValuePair));
    if (kvp) kvp->key = kvp->value = NULL;
    return kvp;

}

static void key_value_pair_delete (void *ptr) {

    if (ptr) {
        KeyValuePair *kvp = (KeyValuePair *) ptr;
        if (kvp->key) free (kvp->key);
        if (kvp->value) free (kvp->value);

        free (kvp);
    }

}

static KeyValuePair *key_value_pair_create (const char *keyFirst, const char *keyAfter, 
    const char *valueFirst, const char *valueAfter) {

    KeyValuePair *kvp = NULL;

    if (keyFirst && keyAfter && valueFirst && valueAfter) {
        const int keyLen = (int) (keyAfter - keyFirst);
        const int valueLen = (int) (valueAfter - valueFirst);

        kvp = key_value_pair_new ();
        if (kvp) {
            kvp->key = (char *) calloc (keyLen + 1, sizeof (char));
            memcpy (kvp->key, keyFirst, keyLen * sizeof (char));
            kvp->value = (char *) calloc (valueLen + 1, sizeof (char));
            memcpy (kvp->value, valueFirst, valueLen * sizeof (char));
        }
    }

    return kvp;

}

DoubleList *http_parse_query_into_pairs (const char *first, const char *last) {

    DoubleList *pairs = NULL;

    if (first && last) {
        pairs = dlist_init (key_value_pair_delete, NULL);

        const char *walk = first;
        const char *keyFirst = first;
        const char *keyAfter = NULL;
        const char *valueFirst = NULL;
        const char *valueAfter = NULL;

        /* Parse query string */
        for (; walk < last; walk++) {
            switch (*walk) {
                case '&': {
                    if (valueFirst != NULL) valueAfter = walk;
                    else keyAfter = walk;

                    KeyValuePair *kvp = key_value_pair_create (keyFirst, keyAfter, valueFirst, valueAfter);
                    if (kvp) dlist_insert_after (pairs, dlist_end (pairs), kvp);
                
                    if (walk + 1 < last) keyFirst = walk + 1;
                    else keyFirst = NULL;
                    
                    keyAfter = NULL;
                    valueFirst = NULL;
                    valueAfter = NULL;
                } break;

                case '=': {
                    if (keyAfter == NULL) {
                        keyAfter = walk;
                        if (walk + 1 <= last) {
                            valueFirst = walk + 1;
                            valueAfter = walk + 1;
                        }
                    }
                } break;

                default: break;
            }
        }

        if (valueFirst != NULL) valueAfter = walk;
        else keyAfter = walk;

        KeyValuePair *kvp = key_value_pair_create (keyFirst, keyAfter, valueFirst, valueAfter);
        if (kvp) dlist_insert_after (pairs, dlist_end (pairs), kvp);
    }

    return pairs;

}

const char *http_query_pairs_get_value (DoubleList *pairs, const char *key) {

    const char *value = NULL;

    if (pairs) {
        KeyValuePair *kvp = NULL;
        for (ListElement *le = dlist_start (pairs); le; le = le->next) {
            kvp = (KeyValuePair *) le->data;
            if (!strcmp (kvp->key, key)) {
                value = kvp->value;
                break;
            }
        }
    }

    return value;

}

char *http_strip_path_from_query (char *str) {

    if (str) {
        unsigned int len = strlen (str);
        unsigned int idx = 0;
        for (; idx < len; idx++) {
            if (str[idx] == '?') break;
        } 
        
        idx++;
        int newlen = len - idx;
        char *query = (char *) calloc (newlen, sizeof (char));
        int j = 0;
        for (unsigned int i = idx; i < len; i++) {
            query[j] = str[i];
            j++;
        } 

        return query;
    }

    return NULL;

}

#pragma endregion