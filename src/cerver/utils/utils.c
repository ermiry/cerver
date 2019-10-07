#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <ctype.h>
#include <stdarg.h>

#include "cerver/utils/utils.h"

/*** misc ***/

bool system_is_little_endian (void) {

    unsigned int x = 0x76543210;
    char *c = (char *) &x;
    if (*c == 0x10) return true;
    else return false;

}

/*** math ***/

int clamp_int (int val, int min, int max) {

    const int t = val < min ? min : val;
    return t > max ? max : t;

}

int abs_int (int value) { return value > 0 ? value : (value * -1); }

float lerp (float first, float second, float by) { return first * (1 - by) + second * by; }

/*** random ***/

// init psuedo random generator based on our seed
void random_set_seed (unsigned int seed) { srand (seed); }

int random_int_in_range (int min, int max) {

    int low = 0, high = 0;

    if (min < max) {
        low = min;
        high = max + 1;
    }

    else {
        low = max + 1;
        high = min;
    }

    return (rand () % (high - low)) + low;

}

/*** converters ***/

// convert a string representing a hex to a string
int xtoi (char *hexString) {

    int i = 0;

    if ((*hexString == '0') && (*(hexString + 1) == 'x')) hexString += 2;

    while (*hexString) {
        char c = toupper (*hexString++);
        if ((c < '0') || (c > 'F') || ((c > '9') && (c < 'A'))) break;
        c -= '0';
        if (c > 9) c-= 7;
        i = (i << 4) + c;
    }

    return i;

}

char *itoa (int i, char *b) {

    char const digit[] = "0123456789";
    char *p = b;

    if (i < 0) {
        *p++ = '-';
        i *= -1;
    }

    int shifter = i;
    do { //Move to where representation ends
        ++p;
        shifter = shifter / 10;
    } while (shifter);

    *p = '\0';
    do { //Move back, inserting digits as u go
        *--p = digit [i % 10];
        i = i / 10;
    } while (i);

    return b;

}

/*** c strings ***/

// creates a new c string with the desired format, as in printf
char *c_string_create (const char *format, ...) {

    char *fmt;

    if (format != NULL) fmt = strdup (format);
    else fmt = strdup ("");

    va_list argp;
    va_start (argp, format);
    char oneChar[1];
    int len = vsnprintf (oneChar, 1, fmt, argp);
    if (len < 1) return NULL;
    va_end (argp);

    char *str = (char *) calloc (len + 1, sizeof (char));
    if (!str) return NULL;

    va_start (argp, format);
    vsnprintf (str, len + 1, fmt, argp);
    va_end (argp);

    free (fmt);

    return str;

}

// splits a c string into tokens based on a delimiter
char **c_string_split (char *string, const char delim, int *n_tokens) {

    char **result = 0;
    size_t count = 0;
    char *temp = string;
    char *last = 0;
    char dlm[2];
    dlm[0] = delim;
    dlm[1] = 0;

    // count how many elements will be extracted
    while (*temp) {
        if (delim == *temp) {
            count++;
            last = temp;
        }

        temp++;
    }

    count += last < (string + strlen (string) - 1);

    count++;

    result = (char **) calloc (count, sizeof (char *));
    if (n_tokens) *n_tokens = count;

    if (result) {
        size_t idx = 0;
        char *token = strtok (string, dlm);

        while (token) {
            // assert (idx < count);
            *(result + idx++) = strdup (token);
            token = strtok (0, dlm);
        }

        // assert (idx == count - 1);
        *(result + idx) = 0;
    }

    return result;

}

// removes all the matchings chars from a given string
void c_string_remove_char (char *str, char garbage) {

    char *src, *dst;
    for (src = dst = str; *src != '\0'; src++) {
        *dst = *src;
        if (*dst != garbage) dst++;
    }
    *dst = '\0';
    
}

// copies a c string into another one previuosly allocated
void c_string_copy (char *to, const char *from) {

    if (to && from) {
        while (*from) *to++ = *from++;
        
        *to = '\0';
    }

}

// revers a c string
char *c_string_reverse (char *str) {

    if (str) {
        char reverse[20];
        int len = strlen (str);
        short int end = len - 1;
        short int begin = 0;
        for ( ; begin < len; begin++) {
            reverse[begin] = str[end];
            end--;
        }

        reverse[begin] = '\0';

        char *retval = (char *) calloc (len + 1, sizeof (char));
        if (retval) c_string_copy (retval, reverse);

        return retval;
    }

    return NULL;

}