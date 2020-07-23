#ifndef _CERVER_ESTRING_H_
#define _CERVER_ESTRING_H_

#include "cerver/types/types.h"

typedef struct estring {

    unsigned int len;
    char *str;

} estring;

extern estring *estring_new (const char *str);

extern void estring_delete (void *str_ptr);

extern estring *estring_create (const char *format, ...);

extern int estring_compare (const estring *s1, const estring *s2);

extern int estring_comparator (const void *a, const void *b);

extern void estring_copy (estring *to, estring *from);

extern void estring_replace (estring *old, const char *str);

// concatenates two strings into a new one
extern estring *estring_concat (estring *s1, estring *s2);

// appends a char to the end of the string
// reallocates the same string
extern void estring_append_char (estring *s, const char c);

// appends a c string at the end of the string
// reallocates the same string
extern void estring_append_c_string (estring *s, const char *c_str);

extern void estring_to_upper (estring *string);

extern void estring_to_lower (estring *string);

extern char **estring_split (estring *string, const char delim, int *n_tokens);

extern void estring_remove_char (estring *string, char garbage);

// removes the last char from a string
extern void estring_remove_last_char (estring *string);

// check if a str (to_find) is inside str
// returns 0 on exact match
// returns 1 if it match the letters but len is different
// returns -1 if no match
extern int estring_contains (estring *str, char *to_find);

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
extern void *estring_selialize (estring *str, SStringSize size);

#endif