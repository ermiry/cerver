#ifndef _CERVER_STRING_H_
#define _CERVER_STRING_H_

#include "cerver/types/types.h"

typedef struct String {

    unsigned int len;
    char *str;

} String;

extern String *str_new (const char *str);
extern String *str_create (const char *format, ...);
extern void str_delete (void *str_ptr);

extern void str_copy (String *to, String *from);
extern void str_concat (String *des, String *s1, String *s2);

extern void str_to_upper (String *string);
extern void str_to_lower (String *string);

extern int str_compare (const String *s1, const String *s2);
extern int str_comparator (const void *a, const void *b);

extern char **str_split (String *string, const char delim, int *n_tokens);

extern void str_remove_char (String *string, char garbage);

// check if a string (to_find) is inside string
// returns 0 on exact match
// returns 1 if it match the letters but len is different
// returns -1 if no match
extern int str_contains (String *string, char *to_find);

/*** serialization ***/

typedef enum SStringSize {

    SS_SMALL = 64,
    SS_MEDIUM = 128,
    SS_LARGE = 256,
    SS_EXTRA_LARGE = 512

} SStringSize;

// serialized string (small)
typedef struct SStringS {

    u16 len;
    char string[64];

} SStringS;

// serialized string (medium)
typedef struct SStringM {

    u16 len;
    char string[128];

} SStringM;

// serialized string (large)
typedef struct SStringL {

    u16 len;
    char string[256];

} SStringL;

// serialized string (extra large)
typedef struct SStringXL {

    u16 len;
    char string[512];

} SStringXL;

#endif