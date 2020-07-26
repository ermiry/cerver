#include <stdlib.h>
#include <string.h>

#include <bson/bson.h>

#include "cerver/types/estring.h"
#include "cerver/collections/dlist.h"

#include "cerver/http/json.h"

// uses the reference to a value, do not free the value, it will be free when the list gets destroy
JsonKeyValue *json_key_value_new (void) {

    JsonKeyValue *jkvp = (JsonKeyValue *) malloc (sizeof (JsonKeyValue));
    if (jkvp) {
        jkvp->key = NULL;
        jkvp->value = NULL;
    }

    return jkvp;

}

JsonKeyValue *json_key_value_create (const char *key, const void *value, ValueType value_type) {

    JsonKeyValue *jkvp = NULL;

    if (key && value) {
        jkvp = (JsonKeyValue *) malloc (sizeof (JsonKeyValue));
        if (jkvp) {
            jkvp->key = estring_new (key);
            jkvp->value = (void *) value;
            jkvp->valueType = value_type;
        }
    }

    return jkvp;

}

void json_key_value_delete (void *ptr) {

    if (ptr) {
        JsonKeyValue *jkvp = (JsonKeyValue *) ptr;

        if (jkvp->key) estring_delete (jkvp->key);
        if (jkvp->valueType == VALUE_TYPE_STRING) estring_delete ((estring *) jkvp->key);

        free (jkvp);
    }

}

static void json_append_value (bson_t *doc, const JsonKeyValue *jkvp) {

    if (doc && jkvp) {
        switch (jkvp->valueType) {
            case VALUE_TYPE_INT: bson_append_int32 (doc, jkvp->key->str, jkvp->key->len, *(int32_t *) jkvp->value); break;
            case VALUE_TYPE_DOUBLE: bson_append_double (doc, jkvp->key->str, jkvp->key->len, *(double *) jkvp->value); break;
            case VALUE_TYPE_STRING: {
                estring *str = (estring *) jkvp->value;
                bson_append_utf8 (doc, jkvp->key->str, jkvp->key->len, str->str, str->len);
            } 
            break;

            default: break;
        }
    }

}

// creates a json string with the passed jkvp
char *json_create_with_one_pair (JsonKeyValue *jkvp, size_t *len) {

    char *retval = NULL;

    if (jkvp) {
        bson_t *doc = bson_new ();

        if (doc) {
            json_append_value (doc, jkvp);
            retval = bson_as_json (doc, len);
            bson_destroy (doc);
        } 
        
    }

    return retval;

}

// creates a json string with a list of jkvps
char *json_create_with_pairs (DoubleList *pairs, size_t *len) {

    char *retval = NULL;

    if (pairs) {
        bson_t *doc = bson_new ();

        if (doc) {
            JsonKeyValue *jkvp = NULL;
            for (ListElement *le = dlist_start (pairs); le; le = le->next) {
                jkvp = (JsonKeyValue *) le->data;
                json_append_value (doc, jkvp);
            }

            retval = bson_as_json (doc, len);
            bson_destroy (doc);
        }
    }

    return retval;

}

const void *json_pairs_get_value (DoubleList *pairs, const char *key, ValueType *value_type) {

    const void *value = NULL;

    if (pairs) {
        JsonKeyValue *jkvp = NULL;
        for (ListElement *le = dlist_start (pairs); le; le = le->next) {
            jkvp = (JsonKeyValue *) le->data;
            if (!strcmp (jkvp->key->str, key)) {
                value = jkvp->value;
                *value_type = jkvp->valueType;
                break;
            }
        }
    }

    return value;

}