#ifndef VECTOR_H
#define VECTOR_H

#include <stdlib.h>

#define VECTOR_INITIAL_N_ALLOCATED  1

typedef struct Vector {

	void *array;
	size_t elements;
	size_t elementSize;
	size_t allocated;

} Vector;


extern void vector_init (Vector *vector, size_t elementSize);

#endif