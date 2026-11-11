#ifndef _CERVER_STRING_H_
#define _CERVER_STRING_H_

#include <stdarg.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

#define STRING_MIN_CAPACITY				(1 << 4)
#define STRING_GROWTH_FACTOR			2

typedef struct String {

	// the bytes allocated for str, including the '\0',
	// 0 if they are exactly len + 1
	size_t max_len;
	size_t len;
	char *str;

} String;

CERVER_PUBLIC String *str_new (const char *str);

CERVER_PUBLIC void str_delete (void *str_ptr);

CERVER_PUBLIC String *str_create (
	const char *format, ...
) CERVER_PRINTF_FORMAT (1, 2);

CERVER_PUBLIC String *str_allocate (
	const unsigned int max_len
);

CERVER_PUBLIC void str_set (
	String *str, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

CERVER_PUBLIC void str_set_with_args (
	String *str, const char *format, va_list args
) CERVER_PRINTF_FORMAT (2, 0);

CERVER_PUBLIC int str_compare (
	const String *s1, const String *s2
);

CERVER_PUBLIC int str_comparator (
	const void *a, const void *b
);

CERVER_PUBLIC void str_copy (
	String *to, const String *from
);

CERVER_PUBLIC void str_n_copy (
	String *to, const String *from, const size_t n
);

CERVER_PUBLIC String *str_clone (
	const String *original
);

CERVER_PUBLIC void str_replace (
	String *str, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

CERVER_PUBLIC void str_replace_with (
	String *str, const char *c_str
);

CERVER_PUBLIC void str_n_replace_with (
	String *str, const char *c_str, const size_t n
);

// concatenates two strings into a new one
CERVER_PUBLIC String *str_concat (
	const String *s1, const String *s2
);

// appends a char to the end of the string
// reallocates the same string
CERVER_PUBLIC void str_append_char (
	String *str, const char c
);

// appends a c string at the end of the string
// reallocates the same string
CERVER_PUBLIC void str_append_c_string (
	String *str, const char *c_str
);

// appends the first n chars of c_str at the end of the string
// reallocates the same string
CERVER_PUBLIC void str_append_n_from_c_string (
	String *str, const char *c_str, const size_t n
);

// creates a new string with exactly n bytes copied from bytes,
// bytes may contain '\0' and the result is always terminated with '\0'
CERVER_PUBLIC String *str_create_from_bytes (
	const char *bytes, const size_t n
);

// works as str_create_from_bytes () but the string and its bytes
// are allocated in the arena (a single allocation)
// the string is released with the arena, it must NOT be passed to str_delete ()
// nor grown with any of the append methods
CERVER_PUBLIC String *str_create_from_bytes_arena (
	struct Arena *arena, const char *bytes, const size_t n
);

// makes sure that the string has at least capacity bytes allocated
// (including the '\0'), so the next appends that fit do not reallocate it
// it never shrinks the string
// returns 0 on success, 1 on error (the string is kept unchanged)
CERVER_PUBLIC int str_reserve (
	String *str, const size_t capacity
);

// appends exactly n bytes at the end of the string,
// bytes may contain '\0' and the result is always terminated with '\0'
// the string grows geometrically, so many appends are not quadratic
// returns 0 on success, 1 on error (the string is kept unchanged)
CERVER_PUBLIC int str_append_bytes (
	String *str, const char *bytes, const size_t n
);

CERVER_PUBLIC void str_to_upper (String *string);

CERVER_PUBLIC void str_to_lower (String *string);

CERVER_PUBLIC char **str_split (
	String *string, const char delim, int *n_tokens
);

CERVER_PUBLIC void str_remove_char (
	String *string, const char garbage
);

// removes the last char from a string
CERVER_PUBLIC void str_remove_last_char (
	String *string
);

// check if a str (to_find) is inside str
// returns 0 on exact match
// returns 1 if it match the letters but len is different
// returns -1 if no match
CERVER_PUBLIC int str_contains (
	const String *str, const char *to_find
);

/*** serialization ***/

typedef enum SStringSize {

	SS_SMALL = 64,
	SS_MEDIUM = 128,
	SS_LARGE = 256,
	SS_EXTRA_LARGE = 512

} SStringSize;

// serialized str (small)
typedef struct SStringS {

	u16 len;
	char str[SS_SMALL];

} SStringS;

// serialized str (medium)
typedef struct SStringM {

	u16 len;
	char str[SS_MEDIUM];

} SStringM;

// serialized str (large)
typedef struct SStringL {

	u16 len;
	char str[SS_LARGE];

} SStringL;

// serialized str (extra large)
typedef struct SStringXL {

	u16 len;
	char str[SS_EXTRA_LARGE];

} SStringXL;

// returns a ptr to a serialized str
CERVER_PUBLIC void *str_selialize (
	const String *str, const SStringSize size
);

#ifdef __cplusplus
}
#endif

#endif