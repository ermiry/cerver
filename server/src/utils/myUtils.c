#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

char *createString (const char *stringWithFormat, ...) {

    char *fmt;

    if (stringWithFormat != NULL) fmt = strdup (stringWithFormat);
    else fmt = strdup ("");

    va_list argp;
    va_start (argp, stringWithFormat);
    char oneChar[1];
    int len = vsnprintf (oneChar, 1, fmt, argp);
    if (len < 1) return NULL;
    va_end (argp);

    char *str = (char *) calloc (len + 1, sizeof (char));
    if (!str) return NULL;

    va_start (argp, stringWithFormat);
    vsnprintf (str, len + 1, fmt, argp);
    va_end (argp);

    free (fmt);

    return str;

}