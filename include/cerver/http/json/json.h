#ifndef _CERVER_HTTP_JSON_H_
#define _CERVER_HTTP_JSON_H_

#include <stdlib.h>
#include <stdio.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "cerver/config.h"

#include "cerver/http/json/config.h"
#include "cerver/http/json/types.h"
#include "cerver/http/json/value.h"

#ifdef __cplusplus
extern "C" {
#endif

#define json_typeof(json)     ((json)->type)
#define json_is_object(json)  ((json) && json_typeof(json) == JSON_OBJECT)
#define json_is_array(json)   ((json) && json_typeof(json) == JSON_ARRAY)
#define json_is_string(json)  ((json) && json_typeof(json) == JSON_STRING)
#define json_is_integer(json) ((json) && json_typeof(json) == JSON_INTEGER)
#define json_is_real(json)    ((json) && json_typeof(json) == JSON_REAL)
#define json_is_number(json)  (json_is_integer(json) || json_is_real(json))
#define json_is_true(json)    ((json) && json_typeof(json) == JSON_TRUE)
#define json_is_false(json)   ((json) && json_typeof(json) == JSON_FALSE)
#define json_boolean_value    json_is_true
#define json_is_boolean(json) (json_is_true(json) || json_is_false(json))
#define json_is_null(json)    ((json) && json_typeof(json) == JSON_NULL)

#pragma region errors

#define JSON_ERROR_TEXT_LENGTH          256
#define JSON_ERROR_TEXT_LENGTH_MAX      512
#define JSON_ERROR_SOURCE_LENGTH        128

typedef struct json_error_t {

	int line;
	int column;
	int position;
	char source[JSON_ERROR_SOURCE_LENGTH];
	char text[JSON_ERROR_TEXT_LENGTH];

} json_error_t;

enum json_error_code {

	json_error_unknown,
	json_error_out_of_memory,
	json_error_stack_overflow,
	json_error_cannot_open_file,
	json_error_invalid_argument,
	json_error_invalid_utf8,
	json_error_premature_end_of_input,
	json_error_end_of_input_expected,
	json_error_invalid_syntax,
	json_error_invalid_format,
	json_error_wrong_type,
	json_error_null_character,
	json_error_null_value,
	json_error_null_byte_in_key,
	json_error_duplicate_key,
	json_error_numeric_overflow,
	json_error_item_not_found,
	json_error_index_out_of_range

};

static CERVER_INLINE enum json_error_code json_error_code (
	const json_error_t *e
) {

	return (enum json_error_code) e->text[JSON_ERROR_TEXT_LENGTH - 1];

}

extern void jsonp_error_init (
	json_error_t *error, const char *source
);

extern void jsonp_error_set_source (
	json_error_t *error, const char *source
);

extern void jsonp_error_set (
	json_error_t *error, int line, int column, size_t position,
	enum json_error_code code, const char *msg, ...
);

extern void jsonp_error_vset (
	json_error_t *error, int line, int column, size_t position,
	enum json_error_code code, const char *msg, va_list ap
);

#pragma endregion

#pragma region pack

extern json_t *json_pack (
	const char *fmt, ...
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_pack_ex (
	json_error_t *error, size_t flags,
	const char *fmt, ...
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_vpack_ex (
	json_error_t *error, size_t flags,
	const char *fmt, va_list ap
) CERVER_ATTRS((warn_unused_result));

#pragma endregion

#pragma region unpack

#define JSON_VALIDATE_ONLY 	0x1
#define JSON_STRICT        	0x2

extern int json_unpack (
	json_t *root, const char *fmt, ...
);

extern int json_unpack_ex (
	json_t *root, json_error_t *error, size_t flags,
	const char *fmt, ...
);

extern int json_vunpack_ex (
	json_t *root, json_error_t *error, size_t flags,
	const char *fmt, va_list ap
);

#pragma endregion

#pragma region load

#define JSON_REJECT_DUPLICATES		0x1
#define JSON_DISABLE_EOF_CHECK		0x2
#define JSON_DECODE_ANY				0x4
#define JSON_DECODE_INT_AS_REAL		0x8
#define JSON_ALLOW_NUL				0x10

typedef size_t (*json_load_callback_t) (
	void *buffer, size_t buflen, void *data
);

extern json_t *json_loads (
	const char *input, size_t flags, json_error_t *error
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_loadb (
	const char *buffer, size_t buflen, size_t flags, json_error_t *error
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_loadf (
	FILE *input, size_t flags, json_error_t *error
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_loadfd (
	int input, size_t flags, json_error_t *error
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_load_file (
	const char *path, size_t flags, json_error_t *error
) CERVER_ATTRS((warn_unused_result));

extern json_t *json_load_callback (
	json_load_callback_t callback, void *data, size_t flags, json_error_t *error
) CERVER_ATTRS((warn_unused_result));

#pragma endregion

#pragma region dump

#define JSON_MAX_INDENT				0x1F
#define JSON_INDENT(n)				((n)&JSON_MAX_INDENT)
#define JSON_COMPACT				0x20
#define JSON_ENSURE_ASCII			0x40
#define JSON_SORT_KEYS				0x80
#define JSON_PRESERVE_ORDER			0x100
#define JSON_ENCODE_ANY				0x200
#define JSON_ESCAPE_SLASH			0x400
#define JSON_REAL_PRECISION(n)		(((n)&0x1F) << 11)
#define JSON_EMBED					0x10000

typedef int (*json_dump_callback_t) (
	const char *buffer, size_t size, void *data
);

extern char *json_dumps (
	const json_t *json, size_t flags
) CERVER_ATTRS((warn_unused_result));

extern size_t json_dumpb (
	const json_t *json, char *buffer, size_t size, size_t flags
);

extern int json_dumpf (
	const json_t *json, FILE *output, size_t flags
);

extern int json_dumpfd (
	const json_t *json, int output, size_t flags
);

extern int json_dump_file (
	const json_t *json, const char *path, size_t flags
);

extern int json_dump_callback (
	const json_t *json, json_dump_callback_t callback,
	void *data, size_t flags
);

#pragma endregion

#pragma region print

extern void json_print (json_t *root);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif