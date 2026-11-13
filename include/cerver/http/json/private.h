/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See thirdparty.md for details.
 */

#ifndef _CERVER_HTTP_JSON_PRIVATE_H_
#define _CERVER_HTTP_JSON_PRIVATE_H_

#include <stdarg.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/json/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JSON_INTEGER_FORMAT			"lld"

typedef long long json_int_t;

// the reference counts are atomic, so every read only function is thread safe,
// they are never called directly, json_incref () and json_decref () use them
#define JSON_INTERNAL_INCREF(json)	__atomic_add_fetch (&json->refcount, 1, __ATOMIC_ACQUIRE)
#define JSON_INTERNAL_DECREF(json)	__atomic_sub_fetch (&json->refcount, 1, __ATOMIC_RELEASE)

#define container_of(ptr_, type_, member_)                                               \
	((type_ *)((char *)ptr_ - offsetof (type_, member_)))

struct json_t;

struct hashtable_t;

// creates a string that takes the buffer of the value
CERVER_HIDDEN struct json_t *jsonp_stringn_nocheck_own (
	const char *value, size_t length
);

CERVER_HIDDEN int jsonp_dtostr (
	char *buffer, size_t size, double value, int prec
);

/* Wrappers for custom memory functions */
CERVER_HIDDEN void *jsonp_malloc (
	size_t size
) CERVER_ATTRS ((warn_unused_result));

CERVER_HIDDEN void jsonp_free (void *ptr);

CERVER_HIDDEN char *jsonp_strdup (
	const char *str
) CERVER_ATTRS ((warn_unused_result));

CERVER_HIDDEN char *jsonp_strndup (
	const char *str, size_t len
) CERVER_ATTRS ((warn_unused_result));

// the key of a value in the check of circular references:
// "0x", two hex characters for each byte of a pointer and the terminator
#define JSON_LOOP_KEY_LENGTH			(2 + (sizeof (struct json_t *) * 2) + 1)

CERVER_HIDDEN int jsonp_loop_check (
	struct hashtable_t *parents,
	const struct json_t *json, char *key, size_t key_size
);

CERVER_PUBLIC struct json_t *json_sprintf (
	const char *format, ...
) CERVER_ATTRS ((warn_unused_result, format (printf, 1, 2)));

CERVER_PUBLIC struct json_t *json_vsprintf (
	const char *format, va_list arguments
) CERVER_ATTRS ((warn_unused_result, format (printf, 1, 0)));

CERVER_PUBLIC int json_equal (
	const struct json_t *value1, const struct json_t *value2
);

CERVER_PUBLIC struct json_t *json_copy (
	struct json_t *value
) CERVER_ATTRS ((warn_unused_result));

CERVER_PUBLIC struct json_t *json_deep_copy (
	const struct json_t *value
) CERVER_ATTRS ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
