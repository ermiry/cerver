#ifndef _CERVER_HTTP_JSON_VALUE_H_
#define _CERVER_HTTP_JSON_VALUE_H_

#include "cerver/config.h"

#include "cerver/http/json/hashtable.h"
#include "cerver/http/json/internal.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define json_to_object(json_)  container_of(json_, json_object_t, json)
#define json_to_array(json_)   container_of(json_, json_array_t, json)
#define json_to_string(json_)  container_of(json_, json_string_t, json)
#define json_to_real(json_)    container_of(json_, json_real_t, json)
#define json_to_integer(json_) container_of(json_, json_integer_t, json)

CERVER_PUBLIC void json_object_seed (size_t seed);

CERVER_PUBLIC size_t json_object_size (const json_t *object);

CERVER_PUBLIC json_t *json_object_get (
	const json_t *object, const char *key
) CERVER_ATTRS((warn_unused_result));

CERVER_PUBLIC int json_object_set_new (
	json_t *object, const char *key, json_t *value
);

CERVER_PUBLIC int json_object_set_new_nocheck (
	json_t *object, const char *key, json_t *value
);

CERVER_PUBLIC int json_object_del (
	json_t *object, const char *key
);

CERVER_PUBLIC int json_object_clear (
	json_t *object
);

CERVER_PUBLIC int json_object_update (
	json_t *object, json_t *other
);

CERVER_PUBLIC int json_object_update_existing (
	json_t *object, json_t *other
);

CERVER_PUBLIC int json_object_update_missing (
	json_t *object, json_t *other
);

CERVER_PUBLIC int json_object_update_recursive (
	json_t *object, json_t *other
);

CERVER_PUBLIC void *json_object_iter (
	json_t *object
);

CERVER_PUBLIC void *json_object_iter_at (
	json_t *object, const char *key
);

CERVER_PUBLIC void *json_object_key_to_iter (
	const char *key
);

CERVER_PUBLIC void *json_object_iter_next (
	json_t *object, void *iter
);

CERVER_PUBLIC const char *json_object_iter_key (
	void *iter
);

CERVER_PUBLIC json_t *json_object_iter_value (
	void *iter
);

CERVER_PUBLIC int json_object_iter_set_new (
	json_t *object, void *iter, json_t *value
);

#define json_object_foreach(object, key, value)                                          \
	for (key = json_object_iter_key(json_object_iter(object));                           \
		 key && (value = json_object_iter_value(json_object_key_to_iter(key)));          \
		 key = json_object_iter_key(                                                     \
			 json_object_iter_next(object, json_object_key_to_iter(key))))

#define json_object_foreach_safe(object, n, key, value)                                  \
	for (key = json_object_iter_key(json_object_iter(object)),                           \
		n = json_object_iter_next(object, json_object_key_to_iter(key));                 \
		 key && (value = json_object_iter_value(json_object_key_to_iter(key)));          \
		 key = json_object_iter_key(n),                                                  \
		n = json_object_iter_next(object, json_object_key_to_iter(key)))

#define json_array_foreach(array, index, value)                                          \
	for (index = 0;                                                                      \
		 index < json_array_size(array) && (value = json_array_get(array, index));       \
		 index++)

CERVER_PUBLIC int json_object_set (
	json_t *object, const char *key, json_t *value
);

CERVER_PUBLIC int json_object_set_nocheck (
	json_t *object, const char *key,
	json_t *value
);

CERVER_PUBLIC int json_object_iter_set (
	json_t *object, void *iter, json_t *value
);

CERVER_PUBLIC int json_object_update_new (
	json_t *object, json_t *other
);

CERVER_PUBLIC int json_object_update_existing_new (
	json_t *object, json_t *other
);

CERVER_PUBLIC int json_object_update_missing_new (
	json_t *object, json_t *other
);

CERVER_PUBLIC size_t json_array_size (
	const json_t *array
);

CERVER_PUBLIC json_t *json_array_get (
	const json_t *array, size_t index
) CERVER_ATTRS((warn_unused_result));

CERVER_PUBLIC int json_array_set_new (
	json_t *array, size_t index, json_t *value
);

CERVER_PUBLIC int json_array_append_new (
	json_t *array, json_t *value
);

CERVER_PUBLIC int json_array_insert_new (
	json_t *array, size_t index, json_t *value
);

CERVER_PUBLIC int json_array_remove (
	json_t *array, size_t index
);

CERVER_PUBLIC int json_array_clear (
	json_t *array
);

CERVER_PUBLIC int json_array_extend (
	json_t *array, json_t *other
);

CERVER_PUBLIC int json_array_set (
	json_t *array, size_t ind, json_t *value
);

CERVER_PUBLIC int json_array_append (
	json_t *array, json_t *value
);

CERVER_PUBLIC int json_array_insert (
	json_t *array, size_t ind, json_t *value
);

CERVER_PUBLIC const char *json_string_value (const json_t *string);

CERVER_PUBLIC size_t json_string_length (const json_t *string);

CERVER_PUBLIC json_int_t json_integer_value (const json_t *integer);

CERVER_PUBLIC double json_real_value (const json_t *real);

CERVER_PUBLIC double json_number_value (const json_t *json);

CERVER_PUBLIC int json_string_set (
	json_t *string, const char *value
);

CERVER_PUBLIC int json_string_setn (
	json_t *string, const char *value, size_t len
);

CERVER_PUBLIC int json_string_set_nocheck (
	json_t *string, const char *value
);

CERVER_PUBLIC int json_string_setn_nocheck (
	json_t *string, const char *value, size_t len
);

CERVER_PUBLIC int json_integer_set (
	json_t *integer, json_int_t value
);

CERVER_PUBLIC int json_real_set (
	json_t *real, double value
);

CERVER_PUBLIC json_t *json_object (void);

CERVER_PUBLIC json_t *json_array (void);

CERVER_PUBLIC json_t *json_string (
	const char *value
);

CERVER_PUBLIC json_t *json_stringn (
	const char *value, size_t len
);

CERVER_PUBLIC json_t *json_string_nocheck (
	const char *value
);

CERVER_PUBLIC json_t *json_stringn_nocheck (
	const char *value, size_t len
);

CERVER_PUBLIC json_t *json_integer (json_int_t value);

CERVER_PUBLIC json_t *json_real (double value);

CERVER_PUBLIC json_t *json_true (void);

CERVER_PUBLIC json_t *json_false (void);

#define json_boolean(val) ((val) ? json_true() : json_false())

CERVER_PUBLIC json_t *json_null (void);

#ifdef __cplusplus
}
#endif

#endif