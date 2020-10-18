#ifndef JANSSON_PRIVATE_H
#define JANSSON_PRIVATE_H

#include <stddef.h>

#include "cerver/http/json/json.h"
#include "cerver/http/json/config.h"
#include "cerver/http/json/hashtable.h"

#define container_of(ptr_, type_, member_)                                               \
	((type_ *)((char *)ptr_ - offsetof(type_, member_)))

/* On some platforms, max() may already be defined */
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/* va_copy is a C99 feature. In C89 implementations, it's sometimes
   available as __va_copy. If not, memcpy() should do the trick. */
#ifndef va_copy
#ifdef __va_copy
#define va_copy __va_copy
#else
#define va_copy(a, b) memcpy(&(a), &(b), sizeof(va_list))
#endif
#endif

typedef struct {

	json_t json;
	struct hashtable_t hashtable;

} json_object_t;

typedef struct {

	json_t json;
	size_t size;
	size_t entries;
	json_t **table;

} json_array_t;

typedef struct {

	json_t json;
	char *value;
	size_t length;

} json_string_t;

typedef struct {

	json_t json;
	double value;

} json_real_t;

typedef struct {

	json_t json;
	json_int_t value;

} json_integer_t;

#define json_to_object(json_)  container_of(json_, json_object_t, json)
#define json_to_array(json_)   container_of(json_, json_array_t, json)
#define json_to_string(json_)  container_of(json_, json_string_t, json)
#define json_to_real(json_)    container_of(json_, json_real_t, json)
#define json_to_integer(json_) container_of(json_, json_integer_t, json)

/* Create a string by taking ownership of an existing buffer */
extern json_t *jsonp_stringn_nocheck_own (const char *value, size_t len);

/* Error message formatting */
extern void jsonp_error_init (json_error_t *error, const char *source);

extern void jsonp_error_set_source (json_error_t *error, const char *source);

extern void jsonp_error_set (json_error_t *error, int line, int column, size_t position,
	enum json_error_code code, const char *msg, ...);

extern void jsonp_error_vset (json_error_t *error, int line, int column, size_t position,
	enum json_error_code code, const char *msg, va_list ap);

/* Locale independent string<->double conversions */
extern int jsonp_strtod (strbuffer_t *strbuffer, double *out);

extern int jsonp_dtostr (char *buffer, size_t size, double value, int prec);

/* Wrappers for custom memory functions */
extern void *jsonp_malloc (size_t size) JANSSON_ATTRS((warn_unused_result));

extern void jsonp_free (void *ptr);

extern char *jsonp_strndup (const char *str, size_t length) JANSSON_ATTRS((warn_unused_result));

extern char *jsonp_strdup (const char *str) JANSSON_ATTRS((warn_unused_result));

extern char *jsonp_strndup (const char *str, size_t len) JANSSON_ATTRS((warn_unused_result));

/* Circular reference check*/
/* Space for "0x", double the sizeof a pointer for the hex and a terminator. */
#define LOOP_KEY_LEN (2 + (sizeof(json_t *) * 2) + 1)

extern int jsonp_loop_check (struct hashtable_t *parents, const json_t *json, char *key, size_t key_size);

#endif