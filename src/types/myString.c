#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

#include "types/myString.h"

static inline void char_copy (char *to, char *from) {

    while (*from) *to++ = *from++;
    *to = '\0';

}

String *str_new (const char *str) {

    String *string = (String *) malloc (sizeof (String));
    if (string) {
        memset (string, 0, sizeof (String));

        if (str) {
            string->len = strlen (str);
            string->str = (char *) calloc (string->len + 1, sizeof (char));
            char_copy (string->str, (char *) str);
        }

        else string->str = NULL;
    }

    return string;

}

String *str_create (const char *format, ...) {

    String *string = NULL;

    if (format) {
        char *fmt = strdup (format);

        va_list argp;
        va_start (argp, format);
        char oneChar[1];
        int len = vsnprintf (oneChar, 1, fmt, argp);
        va_end (argp);

        char *str = (char *) calloc (len + 1, sizeof (char));

        va_start (argp, format);
        vsnprintf (str, len + 1, fmt, argp);
        va_end (argp);

        string = str_new (str);

        free (str);
        free (fmt);
    }

    return string;

}

void str_delete (String *string) {

    if (string) {
        if (string->str) free (string->str);
        free (string);
    }

}

void str_copy (String *to, String *from) {

    if (to && from) {
        while (*from->str)
            *to->str++ = *from->str++;

        *to->str = '\0';
        to->len = from->len;
    }

}

void str_concat (String *des, String *s1, String *s2) {

    if (des && s1 && s2) {
        while (*s1->str) *des->str++ = *s1->str++;
        while (*s2->str) *des->str++ = *s2->str++;

        *des->str = '\0';

        des->len = s1->len + s2->len;
    }

}

void str_to_upper (String *string) {

    if (string) for (int i = 0; i < string->len; i++) string->str[i] = toupper (string->str[i]);

}

void str_to_lower (String *string) {

    if (string) for (int i = 0; i < string->len; i++) string->str[i] = tolower (string->str[i]);

}

int str_compare (const String *s1, const String *s2) { return strcmp (s1->str, s2->str); }