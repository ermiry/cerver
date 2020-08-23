#ifndef _CERVER_ESTRING_H_
#define _CERVER_ESTRING_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

typedef struct String {

	unsigned int len;
	char *str;

} String;

CERVER_PUBLIC String *str_new (const char *str);

CERVER_PUBLIC void str_delete (void *str_ptr);

CERVER_PUBLIC String *str_create (const char *format, ...);

CERVER_PUBLIC int str_compare (const String *s1, const String *s2);

CERVER_PUBLIC int str_comparator (const void *a, const void *b);

CERVER_PUBLIC void str_copy (String *to, String *from);

CERVER_PUBLIC void str_replace (String *old, const char *str);

// concatenates two strings into a new one
CERVER_PUBLIC String *str_concat (String *s1, String *s2);

// appends a char to the end of the string
// reallocates the same string
CERVER_PUBLIC void str_append_char (String *s, const char c);

// appends a c string at the end of the string
// reallocates the same string
CERVER_PUBLIC void str_append_c_string (String *s, const char *c_str);

CERVER_PUBLIC void str_to_upper (String *string);

CERVER_PUBLIC void str_to_lower (String *string);

CERVER_PUBLIC char **str_split (String *string, const char delim, int *n_tokens);

CERVER_PUBLIC void str_remove_char (String *string, char garbage);

// removes the last char from a string
CERVER_PUBLIC void str_remove_last_char (String *string);

// check if a str (to_find) is inside str
// returns 0 on exact match
// returns 1 if it match the letters but len is different
// returns -1 if no match
CERVER_PUBLIC int str_contains (String *str, char *to_find);

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
	char str[64];

} SStringS;

// serialized str (medium)
typedef struct SStringM {

	u16 len;
	char str[128];

} SStringM;

// serialized str (large)
typedef struct SStringL {

	u16 len;
	char str[256];

} SStringL;

// serialized str (extra large)
typedef struct SStringXL {

	u16 len;
	char str[512];

} SStringXL;

// returns a ptr to a serialized str
CERVER_PUBLIC void *str_selialize (String *str, SStringSize size);

#endif