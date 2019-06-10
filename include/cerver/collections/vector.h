#ifndef _COLLECTIONS_VECTOR_H_
#define _COLLECTIONS_VECTOR_H_

#include <stdlib.h>

#define VECTOR_INITIAL_N_ALLOCATED  1
#define VECTOR_STRETCH_FACTOR 		2

typedef struct Vector {

	void *array;
	size_t elements;
	size_t elementSize;
	size_t allocated;

} Vector;

extern void vector_init (Vector *vector, size_t elementSize);
extern void *vector_get (Vector *vector, size_t i_elem);
extern void vector_push (Vector *vector, const void *elem);

#endif