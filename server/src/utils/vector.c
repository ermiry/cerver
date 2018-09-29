#include "utils/vector.h"

void vector_allocate (Vector *vector, size_t n_allocated) {

	assert (n_allocated > 0 && n_allocated >= vector->elements);

	vector->allocated = n_allocated;
	vector->array = realloc (vector->array, n_allocated * vector->elementSize);

}

void vector_init (Vector *vector, size_t elementSize) {

	assert (elementSize > 0);

	vector->elements = 0;
	vector->elementSize = elementSize;
	vector->array = NULL;

	vector_allocate (vector, VECTOR_INITIAL_N_ALLOCATED);

}