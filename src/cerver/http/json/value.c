/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <math.h>

#include <stddef.h>
#include <stdint.h>

#include "cerver/config.h"

#include "cerver/http/json/config.h"
#include "cerver/http/json/hashtable.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/private.h"
#include "cerver/http/json/utf.h"
#include "cerver/http/json/value.h"

#include "cerver/utils/utils.h"

// the entries that a new array has room for
#define JSON_ARRAY_INITIAL_SIZE			8

// an array grows to the double of its size, or to what it needs
#define JSON_ARRAY_GROWTH_FACTOR		2

// true, false and null are never deleted
#define JSON_SINGLETON_REFCOUNT			((size_t) -1)

static json_t *json_value_deep_copy (const json_t *json, hashtable_t *parents);

static inline void json_init (json_t *json, json_type type) {

	json->type = type;
	json->refcount = 1;

}

int jsonp_loop_check (
	hashtable_t *parents, const json_t *json,
	char *key, size_t key_size
) {

	(void) snprintf (key, key_size, "%p", (void *) json);
	if (hashtable_get (parents, key)) return -1;

	return hashtable_set (parents, key, json_null ());

}

#pragma region object

json_t *json_object (void) {

	json_object_t *object = (json_object_t *) jsonp_malloc (sizeof (json_object_t));
	if (!object) return NULL;

	// a random seed if nobody set one before, it only reads the seed after that
	json_object_seed (0);

	json_init (&object->json, JSON_OBJECT);

	if (hashtable_init (&object->hashtable)) {
		jsonp_free (object);
		return NULL;
	}

	return &object->json;

}

static void json_delete_object (json_object_t *object) {

	hashtable_close (&object->hashtable);
	jsonp_free (object);

}

size_t json_object_size (const json_t *json) {

	if (!json_is_object (json)) return 0;

	return json_to_object (json)->hashtable.size;

}

json_t *json_object_get (const json_t *json, const char *key) {

	if (!key || !json_is_object (json)) return NULL;

	json_object_t *object = json_to_object (json);

	return (json_t *) hashtable_get (&object->hashtable, key);

}

int json_object_set_new_nocheck (
	json_t *json, const char *key, json_t *value
) {

	if (!value) return -1;

	if (!key || !json_is_object (json) || (json == value)) {
		json_decref (value);
		return -1;
	}

	json_object_t *object = json_to_object (json);

	if (hashtable_set (&object->hashtable, key, value)) {
		json_decref (value);
		return -1;
	}

	return 0;

}

int json_object_set_new (json_t *json, const char *key, json_t *value) {

	if (!key || !utf8_check_string (key, strlen (key))) {
		json_decref (value);
		return -1;
	}

	return json_object_set_new_nocheck (json, key, value);

}

int json_object_del (json_t *json, const char *key) {

	if (!key || !json_is_object (json)) return -1;

	json_object_t *object = json_to_object (json);

	return hashtable_del (&object->hashtable, key);

}

int json_object_clear (json_t *json) {

	if (!json_is_object (json)) return -1;

	json_object_t *object = json_to_object (json);
	hashtable_clear (&object->hashtable);

	return 0;

}

int json_object_update (json_t *object, json_t *other) {

	if (!json_is_object (object) || !json_is_object (other)) return -1;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach (other, key, value) {
		if (json_object_set_nocheck (object, key, value)) return -1;
	}

	return 0;

}

int json_object_update_existing (json_t *object, json_t *other) {

	if (!json_is_object (object) || !json_is_object (other)) return -1;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach (other, key, value) {
		if (json_object_get (object, key)) (void) json_object_set_nocheck (object, key, value);
	}

	return 0;

}

int json_object_update_missing (json_t *object, json_t *other) {

	if (!json_is_object (object) || !json_is_object (other)) return -1;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach (other, key, value) {
		if (!json_object_get (object, key)) (void) json_object_set_nocheck (object, key, value);
	}

	return 0;

}

// a value that is an object in both is updated with its own values,
// any other one is replaced
static int json_object_update_recursive_value (
	json_t *object, const char *key, json_t *value, hashtable_t *parents
);

static int json_object_update_recursive_internal (
	json_t *object, json_t *other, hashtable_t *parents
) {

	if (!json_is_object (object) || !json_is_object (other)) return -1;

	char loop_key[JSON_LOOP_KEY_LENGTH];
	if (jsonp_loop_check (parents, other, loop_key, sizeof (loop_key))) return -1;

	int retval = 0;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach (other, key, value) {
		retval = json_object_update_recursive_value (object, key, value, parents);
		if (retval) break;
	}

	(void) hashtable_del (parents, loop_key);

	return retval;

}

static int json_object_update_recursive_value (
	json_t *object, const char *key, json_t *value, hashtable_t *parents
) {

	json_t *current = json_object_get (object, key);

	return (json_is_object (current) && json_is_object (value)) ?
		json_object_update_recursive_internal (current, value, parents) :
		json_object_set_nocheck (object, key, value);

}

int json_object_update_recursive (json_t *object, json_t *other) {

	hashtable_t parents;
	if (hashtable_init (&parents)) return -1;

	const int retval = json_object_update_recursive_internal (object, other, &parents);

	hashtable_close (&parents);

	return retval;

}

void *json_object_iter (json_t *json) {

	if (!json_is_object (json)) return NULL;

	json_object_t *object = json_to_object (json);

	return hashtable_iter (&object->hashtable);

}

void *json_object_iter_at (json_t *json, const char *key) {

	if (!key || !json_is_object (json)) return NULL;

	json_object_t *object = json_to_object (json);

	return hashtable_iter_at (&object->hashtable, key);

}

void *json_object_iter_next (json_t *json, void *iter) {

	if (!json_is_object (json) || !iter) return NULL;

	json_object_t *object = json_to_object (json);

	return hashtable_iter_next (&object->hashtable, iter);

}

const char *json_object_iter_key (void *iter) {

	return iter ? (const char *) hashtable_iter_key (iter) : NULL;

}

json_t *json_object_iter_value (void *iter) {

	return iter ? (json_t *) hashtable_iter_value (iter) : NULL;

}

int json_object_iter_set_new (json_t *json, void *iter, json_t *value) {

	if (!json_is_object (json) || !iter || !value) {
		json_decref (value);
		return -1;
	}

	hashtable_iter_set (iter, value);

	return 0;

}

void *json_object_key_to_iter (const char *key) {

	return key ? hashtable_key_to_iter (key) : NULL;

}

static int json_object_equal (const json_t *first, const json_t *second) {

	if (json_object_size (first) != json_object_size (second)) return 0;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach ((json_t *) first, key, value) {
		if (!json_equal (value, json_object_get (second, key))) return 0;
	}

	return 1;

}

static json_t *json_object_copy (json_t *object) {

	json_t *result = json_object ();
	if (!result) return NULL;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach (object, key, value) {
		(void) json_object_set_nocheck (result, key, value);
	}

	return result;

}

// the values of the object copied into the result,
// json_object_foreach () can not be used because the object is const
// returns 0 on success, -1 on error
static int json_object_deep_copy_values (
	json_t *result, const json_t *object, hashtable_t *parents
) {

	void *iter = json_object_iter ((json_t *) object);
	while (iter) {
		const char *key = json_object_iter_key (iter);
		const json_t *value = json_object_iter_value (iter);

		if (json_object_set_new_nocheck (result, key, json_value_deep_copy (value, parents))) return -1;

		iter = json_object_iter_next ((json_t *) object, iter);
	}

	return 0;

}

static json_t *json_object_deep_copy (const json_t *object, hashtable_t *parents) {

	char loop_key[JSON_LOOP_KEY_LENGTH];
	if (jsonp_loop_check (parents, object, loop_key, sizeof (loop_key))) return NULL;

	json_t *result = json_object ();
	if (result && json_object_deep_copy_values (result, object, parents)) {
		json_decref (result);
		result = NULL;
	}

	(void) hashtable_del (parents, loop_key);

	return result;

}

CERVER_INLINE int json_object_set (
	json_t *object, const char *key, json_t *value
) {

	return json_object_set_new (
		object, key, json_incref (value)
	);

}

CERVER_INLINE int json_object_set_nocheck (
	json_t *object, const char *key,
	json_t *value
) {

	return json_object_set_new_nocheck (
		object, key, json_incref (value)
	);

}

CERVER_INLINE int json_object_iter_set (
	json_t *object, void *iter, json_t *value
) {

	return json_object_iter_set_new (
		object, iter, json_incref (value)
	);

}

CERVER_INLINE int json_object_update_new (
	json_t *object, json_t *other
) {

	const int retval = json_object_update (object, other);
	json_decref (other);

	return retval;

}

CERVER_INLINE int json_object_update_existing_new (
	json_t *object, json_t *other
) {

	const int retval = json_object_update_existing (object, other);
	json_decref (other);

	return retval;

}

CERVER_INLINE int json_object_update_missing_new (
	json_t *object, json_t *other
) {

	const int retval = json_object_update_missing (object, other);
	json_decref (other);

	return retval;

}

#pragma endregion

#pragma region array

json_t *json_array (void) {

	json_array_t *array = (json_array_t *) jsonp_malloc (sizeof (json_array_t));
	if (!array) return NULL;

	json_init (&array->json, JSON_ARRAY);

	array->entries = 0;
	array->size = JSON_ARRAY_INITIAL_SIZE;

	array->table = (json_t **) jsonp_malloc (array->size * sizeof (json_t *));
	if (!array->table) {
		jsonp_free (array);
		return NULL;
	}

	return &array->json;

}

static void json_delete_array (json_array_t *array) {

	for (size_t index = 0; index < array->entries; index++) {
		json_decref (array->table[index]);
	}

	jsonp_free (array->table);
	jsonp_free (array);

}

size_t json_array_size (const json_t *json) {

	if (!json_is_array (json)) return 0;

	return json_to_array (json)->entries;

}

json_t *json_array_get (const json_t *json, size_t index) {

	if (!json_is_array (json)) return NULL;

	json_array_t *array = json_to_array (json);
	if (index >= array->entries) return NULL;

	return array->table[index];

}

int json_array_set_new (json_t *json, size_t index, json_t *value) {

	if (!value) return -1;

	if (!json_is_array (json) || (json == value)) {
		json_decref (value);
		return -1;
	}

	json_array_t *array = json_to_array (json);

	if (index >= array->entries) {
		json_decref (value);
		return -1;
	}

	json_decref (array->table[index]);
	array->table[index] = value;

	return 0;

}

static void json_array_move (
	json_array_t *array, size_t destination, size_t source, size_t count
) {

	(void) memmove (&array->table[destination], &array->table[source], count * sizeof (json_t *));

}

static void json_array_copy_entries (
	json_t **destination, size_t destination_position,
	json_t **source, size_t source_position,
	size_t count
) {

	(void) memcpy (&destination[destination_position], &source[source_position], count * sizeof (json_t *));

}

// makes room for amount more entries, the old table is released if copy is set,
// if not, the caller moves its entries and releases it
// returns the old table, the same one if it had room, NULL on error
static json_t **json_array_grow (json_array_t *array, size_t amount, int copy) {

	if ((array->entries + amount) <= array->size) return array->table;

	json_t **old_table = array->table;

	// a size whose table does not fit in a size_t is an error, not a smaller table
	if (amount > ((SIZE_MAX / sizeof (json_t *)) - array->size)) return NULL;

	size_t new_size = MAX (array->size + amount, array->size * JSON_ARRAY_GROWTH_FACTOR);
	if (new_size > (SIZE_MAX / sizeof (json_t *))) new_size = array->size + amount;

	json_t **new_table = (json_t **) jsonp_malloc (new_size * sizeof (json_t *));
	if (!new_table) return NULL;

	array->size = new_size;
	array->table = new_table;

	if (copy) {
		json_array_copy_entries (array->table, 0, old_table, 0, array->entries);
		jsonp_free (old_table);
		return array->table;
	}

	return old_table;

}

int json_array_append_new (json_t *json, json_t *value) {

	if (!value) return -1;

	if (!json_is_array (json) || (json == value)) {
		json_decref (value);
		return -1;
	}

	json_array_t *array = json_to_array (json);

	if (!json_array_grow (array, 1, 1)) {
		json_decref (value);
		return -1;
	}

	array->table[array->entries] = value;
	array->entries += 1;

	return 0;

}

int json_array_insert_new (json_t *json, size_t index, json_t *value) {

	if (!value) return -1;

	if (!json_is_array (json) || (json == value)) {
		json_decref (value);
		return -1;
	}

	json_array_t *array = json_to_array (json);

	if (index > array->entries) {
		json_decref (value);
		return -1;
	}

	json_t **old_table = json_array_grow (array, 1, 0);
	if (!old_table) {
		json_decref (value);
		return -1;
	}

	if (old_table != array->table) {
		json_array_copy_entries (array->table, 0, old_table, 0, index);
		json_array_copy_entries (array->table, index + 1, old_table, index, array->entries - index);
		jsonp_free (old_table);
	}

	else {
		json_array_move (array, index + 1, index, array->entries - index);
	}

	array->table[index] = value;
	array->entries += 1;

	return 0;

}

int json_array_remove (json_t *json, size_t index) {

	if (!json_is_array (json)) return -1;

	json_array_t *array = json_to_array (json);
	if (index >= array->entries) return -1;

	json_decref (array->table[index]);

	// the last entry has nothing after it to move
	if (index < (array->entries - 1)) {
		json_array_move (array, index, index + 1, array->entries - index - 1);
	}

	array->entries -= 1;

	return 0;

}

int json_array_clear (json_t *json) {

	if (!json_is_array (json)) return -1;

	json_array_t *array = json_to_array (json);

	for (size_t index = 0; index < array->entries; index++) {
		json_decref (array->table[index]);
	}

	array->entries = 0;

	return 0;

}

int json_array_extend (json_t *json, json_t *other_json) {

	if (!json_is_array (json) || !json_is_array (other_json)) return -1;

	json_array_t *array = json_to_array (json);
	json_array_t *other = json_to_array (other_json);

	if (!json_array_grow (array, other->entries, 1)) return -1;

	for (size_t index = 0; index < other->entries; index++) {
		(void) json_incref (other->table[index]);
	}

	json_array_copy_entries (array->table, array->entries, other->table, 0, other->entries);

	array->entries += other->entries;

	return 0;

}

static int json_array_equal (const json_t *first, const json_t *second) {

	const size_t size = json_array_size (first);
	if (size != json_array_size (second)) return 0;

	for (size_t index = 0; index < size; index++) {
		if (!json_equal (json_array_get (first, index), json_array_get (second, index))) return 0;
	}

	return 1;

}

static json_t *json_array_copy (json_t *array) {

	json_t *result = json_array ();
	if (!result) return NULL;

	for (size_t index = 0; index < json_array_size (array); index++) {
		(void) json_array_append (result, json_array_get (array, index));
	}

	return result;

}

// returns 0 on success, -1 on error
static int json_array_deep_copy_values (
	json_t *result, const json_t *array, hashtable_t *parents
) {

	for (size_t index = 0; index < json_array_size (array); index++) {
		if (json_array_append_new (result, json_value_deep_copy (json_array_get (array, index), parents))) return -1;
	}

	return 0;

}

static json_t *json_array_deep_copy (const json_t *array, hashtable_t *parents) {

	char loop_key[JSON_LOOP_KEY_LENGTH];
	if (jsonp_loop_check (parents, array, loop_key, sizeof (loop_key))) return NULL;

	json_t *result = json_array ();
	if (result && json_array_deep_copy_values (result, array, parents)) {
		json_decref (result);
		result = NULL;
	}

	(void) hashtable_del (parents, loop_key);

	return result;

}

CERVER_INLINE int json_array_set (
	json_t *array, size_t index, json_t *value
) {

	return json_array_set_new (array, index, json_incref (value));

}

CERVER_INLINE int json_array_append (
	json_t *array, json_t *value
) {

	return json_array_append_new (array, json_incref (value));

}

CERVER_INLINE int json_array_insert (
	json_t *array, size_t index, json_t *value
) {

	return json_array_insert_new (array, index, json_incref (value));

}

#pragma endregion

#pragma region string

// the string takes the value when it owns it, or a copy of it
static json_t *json_string_create (const char *value, size_t length, int own) {

	if (!value) return NULL;

	char *copy = own ? (char *) value : jsonp_strndup (value, length);
	if (!copy) return NULL;

	json_string_t *string = (json_string_t *) jsonp_malloc (sizeof (json_string_t));
	if (!string) {
		jsonp_free (copy);
		return NULL;
	}

	json_init (&string->json, JSON_STRING);
	string->value = copy;
	string->length = length;

	return &string->json;

}

json_t *json_string_nocheck (const char *value) {

	if (!value) return NULL;

	return json_string_create (value, strlen (value), 0);

}

json_t *json_stringn_nocheck (const char *value, size_t length) {

	return json_string_create (value, length, 0);

}

// private, the string takes the value
json_t *jsonp_stringn_nocheck_own (const char *value, size_t length) {

	return json_string_create (value, length, 1);

}

json_t *json_string (const char *value) {

	if (!value) return NULL;

	return json_stringn (value, strlen (value));

}

json_t *json_stringn (const char *value, size_t length) {

	if (!value || !utf8_check_string (value, length)) return NULL;

	return json_stringn_nocheck (value, length);

}

const char *json_string_value (const json_t *json) {

	if (!json_is_string (json)) return NULL;

	return json_to_string (json)->value;

}

size_t json_string_length (const json_t *json) {

	if (!json_is_string (json)) return 0;

	return json_to_string (json)->length;

}

int json_string_set_nocheck (json_t *json, const char *value) {

	if (!value) return -1;

	return json_string_setn_nocheck (json, value, strlen (value));

}

int json_string_setn_nocheck (json_t *json, const char *value, size_t length) {

	if (!json_is_string (json) || !value) return -1;

	char *copy = jsonp_strndup (value, length);
	if (!copy) return -1;

	json_string_t *string = json_to_string (json);
	jsonp_free (string->value);
	string->value = copy;
	string->length = length;

	return 0;

}

int json_string_set (json_t *json, const char *value) {

	if (!value) return -1;

	return json_string_setn (json, value, strlen (value));

}

int json_string_setn (json_t *json, const char *value, size_t length) {

	if (!value || !utf8_check_string (value, length)) return -1;

	return json_string_setn_nocheck (json, value, length);

}

static void json_delete_string (json_string_t *string) {

	jsonp_free (string->value);
	jsonp_free (string);

}

static int json_string_equal (const json_t *first, const json_t *second) {

	const json_string_t *first_string = json_to_string (first);
	const json_string_t *second_string = json_to_string (second);

	return (first_string->length == second_string->length)
		&& !memcmp (first_string->value, second_string->value, first_string->length);

}

static json_t *json_string_copy (const json_t *json) {

	const json_string_t *string = json_to_string (json);

	return json_stringn_nocheck (string->value, string->length);

}

// the string of a formatted length that fits in an int, that is valid UTF-8
CERVER_PRINTF_FORMAT (2, 0) static json_t *json_vsprintf_length (
	const int length, const char *format, va_list arguments
) {

	// a format error is negative, and the null of INT_MAX bytes does not fit in an int
	if ((length < 0) || (length == INT_MAX)) return NULL;

	char *buffer = (char *) jsonp_malloc ((size_t) length + 1);
	if (!buffer) return NULL;

	(void) vsnprintf (buffer, (size_t) length + 1, format, arguments);
	if (!utf8_check_string (buffer, (size_t) length)) {
		jsonp_free (buffer);
		return NULL;
	}

	return jsonp_stringn_nocheck_own (buffer, (size_t) length);

}

json_t *json_vsprintf (const char *format, va_list arguments) {

	va_list copy;
	va_copy (copy, arguments);

	const int length = vsnprintf (NULL, 0, format, arguments);

	json_t *json = length ? json_vsprintf_length (length, format, copy) : json_string ("");

	va_end (copy);

	return json;

}

json_t *json_sprintf (const char *format, ...) {

	va_list arguments;

	va_start (arguments, format);
	json_t *result = json_vsprintf (format, arguments);
	va_end (arguments);

	return result;

}

#pragma endregion

#pragma region integer

json_t *json_integer (json_int_t value) {

	json_integer_t *integer = (json_integer_t *) jsonp_malloc (sizeof (json_integer_t));
	if (!integer) return NULL;

	json_init (&integer->json, JSON_INTEGER);
	integer->value = value;

	return &integer->json;

}

json_int_t json_integer_value (const json_t *json) {

	if (!json_is_integer (json)) return 0;

	return json_to_integer (json)->value;

}

int json_integer_set (json_t *json, json_int_t value) {

	if (!json_is_integer (json)) return -1;

	json_to_integer (json)->value = value;

	return 0;

}

static void json_delete_integer (json_integer_t *integer) {

	jsonp_free (integer);

}

static int json_integer_equal (const json_t *first, const json_t *second) {

	return json_integer_value (first) == json_integer_value (second);

}

static json_t *json_integer_copy (const json_t *integer) {

	return json_integer (json_integer_value (integer));

}

#pragma endregion

#pragma region real

json_t *json_real (double value) {

	if (isnan (value) || isinf (value)) return NULL;

	json_real_t *real = (json_real_t *) jsonp_malloc (sizeof (json_real_t));
	if (!real) return NULL;

	json_init (&real->json, JSON_REAL);
	real->value = value;

	return &real->json;

}

double json_real_value (const json_t *json) {

	if (!json_is_real (json)) return 0;

	return json_to_real (json)->value;

}

int json_real_set (json_t *json, double value) {

	if (!json_is_real (json) || isnan (value) || isinf (value)) return -1;

	json_to_real (json)->value = value;

	return 0;

}

static void json_delete_real (json_real_t *real) {

	jsonp_free (real);

}

// exact, like upstream: float_compare () takes floats, so a double out of their range
// was infinite (and never equal) and close doubles were equal
static int json_real_equal (const json_t *first, const json_t *second) {

	const double first_value = json_real_value (first);
	const double second_value = json_real_value (second);

	return !((first_value < second_value) || (first_value > second_value));

}

static json_t *json_real_copy (const json_t *real) {

	return json_real (json_real_value (real));

}

#pragma endregion

#pragma region number

double json_number_value (const json_t *json) {

	double value = 0.0;

	if (json_is_integer (json)) value = (double) json_integer_value (json);
	else if (json_is_real (json)) value = json_real_value (json);

	return value;

}

#pragma endregion

#pragma region simple

json_t *json_true (void) {

	static json_t the_true = { JSON_TRUE, JSON_SINGLETON_REFCOUNT };

	return &the_true;

}

json_t *json_false (void) {

	static json_t the_false = { JSON_FALSE, JSON_SINGLETON_REFCOUNT };

	return &the_false;

}

json_t *json_null (void) {

	static json_t the_null = { JSON_NULL, JSON_SINGLETON_REFCOUNT };

	return &the_null;

}

#pragma endregion

#pragma region delete

// true, false and null are never deleted
void json_delete (json_t *json) {

	if (!json) return;

	switch (json_typeof (json)) {
		case JSON_OBJECT: json_delete_object (json_to_object (json)); break;
		case JSON_ARRAY: json_delete_array (json_to_array (json)); break;
		case JSON_STRING: json_delete_string (json_to_string (json)); break;
		case JSON_INTEGER: json_delete_integer (json_to_integer (json)); break;
		case JSON_REAL: json_delete_real (json_to_real (json)); break;

		default: break;
	}

}

#pragma endregion

#pragma region equal

int json_equal (const json_t *first, const json_t *second) {

	if (!first || !second) return 0;

	if (json_typeof (first) != json_typeof (second)) return 0;

	// true, false and null are singletons
	if (first == second) return 1;

	int equal = 0;

	switch (json_typeof (first)) {
		case JSON_OBJECT: equal = json_object_equal (first, second); break;
		case JSON_ARRAY: equal = json_array_equal (first, second); break;
		case JSON_STRING: equal = json_string_equal (first, second); break;
		case JSON_INTEGER: equal = json_integer_equal (first, second); break;
		case JSON_REAL: equal = json_real_equal (first, second); break;

		default: break;
	}

	return equal;

}

#pragma endregion

#pragma region copy

json_t *json_copy (json_t *json) {

	if (!json) return NULL;

	json_t *copy = NULL;

	switch (json_typeof (json)) {
		case JSON_OBJECT: copy = json_object_copy (json); break;
		case JSON_ARRAY: copy = json_array_copy (json); break;
		case JSON_STRING: copy = json_string_copy (json); break;
		case JSON_INTEGER: copy = json_integer_copy (json); break;
		case JSON_REAL: copy = json_real_copy (json); break;

		case JSON_TRUE:
		case JSON_FALSE:
		case JSON_NULL:
			copy = json;
			break;

		default: break;
	}

	return copy;

}

json_t *json_deep_copy (const json_t *json) {

	hashtable_t parents;
	if (hashtable_init (&parents)) return NULL;

	json_t *copy = json_value_deep_copy (json, &parents);

	hashtable_close (&parents);

	return copy;

}

// the values that are not objects nor arrays are copied as a shallow copy
static json_t *json_value_deep_copy (
	const json_t *json, hashtable_t *parents
) {

	if (!json) return NULL;

	json_t *copy = NULL;

	switch (json_typeof (json)) {
		case JSON_OBJECT: copy = json_object_deep_copy (json, parents); break;
		case JSON_ARRAY: copy = json_array_deep_copy (json, parents); break;
		case JSON_STRING: copy = json_string_copy (json); break;
		case JSON_INTEGER: copy = json_integer_copy (json); break;
		case JSON_REAL: copy = json_real_copy (json); break;

		case JSON_TRUE:
		case JSON_FALSE:
		case JSON_NULL:
			copy = (json_t *) json;
			break;

		default: break;
	}

	return copy;

}

#pragma endregion
