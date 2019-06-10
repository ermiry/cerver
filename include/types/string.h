#ifndef _TYPES_STRING_H_
#define _TYPES_STRING_H_

typedef struct String {

    unsigned int len;
    char *str;

} String;

extern String *str_new (const char *str);
extern String *str_create (const char *format, ...);
extern void str_delete (String *string);

extern void str_copy (String *to, String *from);
extern void str_concat (String *des, String *s1, String *s2);

extern void str_to_upper (String *string);
extern void str_to_lower (String *string);

extern int str_compare (const String *s1, const String *s2);

extern char **str_split (String *string, const char delim, int *n_tokens);

extern void str_remove_char (String *string, char *garbage);

// check if a string (to_find) is inside string
// returns 0 on exact match
// returns 1 if it match the letters but len is different
// returns -1 if no match
extern int str_contains (String *string, char *to_find);

#endif