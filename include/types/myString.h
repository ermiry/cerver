#ifndef _MY_STRING_H_
#define _MY_STRING_H_

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

#endif