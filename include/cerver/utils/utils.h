#ifndef _CERVER_UTILS_H_
#define _CERVER_UTILS_H_

/*** Random ***/

// init psuedo random generator based on our seed
extern void random_set_seed (unsigned int seed);

// gets a random int in a range of values
extern int random_int_in_range (int min, int max);

/*** C strings ***/

extern char *c_string_create (const char *stringWithFormat, ...);

extern char **c_string_split (char *str, const char delim);

#endif