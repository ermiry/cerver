#ifndef _CERVER_HTTP_JSON_H_
#define _CERVER_HTTP_JSON_H_

#include <stdlib.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dllist.h"

typedef struct JsonKeyValue {

    String *key;
    void *value;
    ValueType valueType;

} JsonKeyValue;

// uses the reference to a value, do not free the value, it will be free when the list gets destroy
extern JsonKeyValue *json_key_value_new (void);
extern JsonKeyValue *json_key_value_create (const char *key, const void *value, ValueType value_type);

extern void json_key_value_delete (void *ptr);

// creates a json string with the passed jkvp
extern char *json_create_with_one_pair (JsonKeyValue *jkvp, size_t *len);
// creates a json string with a list of jkvps
extern char *json_create_with_pairs (DoubleList *pairs, size_t *len);

// gets the value associated with a key
const void *json_pairs_get_value (DoubleList *pairs, const char *key, ValueType *value_type);

#endif