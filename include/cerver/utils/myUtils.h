#ifndef MY_UTILS_H
#define MY_UTILS_H

extern char *createString (const char *stringWithFormat, ...);

// init psuedo random generator based on our seed
extern void random_set_seed (unsigned int seed);

// gets a random int in a range of values
extern int random_int_in_range (int min, int max);

#endif