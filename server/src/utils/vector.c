#include <assert.h>

#include "utils/vector.h"

void vector_allocate (Vector *vector, size_t n_allocated) {

	assert (n_allocated > 0 && n_allocated >= vector->elements);

	vector->allocated = n_allocated;
	vector->array = realloc (vector->array, n_allocated * vector->elementSize);

}

void vector_init(Vector *vector, size_t elementSize) {

	assert (elementSize > 0);

	vector->elements = 0;
	vector->elementSize = elementSize;
	vector->array = NULL;

	vector_allocate (vector, VECTOR_INITIAL_N_ALLOCATED);

}

void vector_ensure_allocated (Vector *vector, size_t n_elems) {

	if (vector->allocated < n_elems) {
		size_t stretched_n_elems = vector->allocated * VECTOR_STRETCH_FACTOR;
		if (stretched_n_elems >= n_elems) vector_allocate (vector, stretched_n_elems);
		else vector_allocate (vector, n_elems);
	}

}

void vector_resize(Vector *vector, size_t n_elems) {

	vector_ensure_allocated (vector, n_elems);
	vector->elements = n_elems;

}

void *vector_elem_ptr(Vector *vector, size_t i_elem) {

	return (char *) vector->array + (i_elem * vector->elementSize);

}

void *vector_get (Vector *vector, size_t i_elem) {

	assert (i_elem <= vector->elements);

	return vector_elem_ptr (vector, i_elem);

}

void vector_set (Vector *vector, size_t i_elem, const void *elem) {

	assert (i_elem <= vector->elements);
	memcpy (vector_elem_ptr( vector, i_elem), elem, vector->elementSize);

}

void vector_push (Vector *vector, const void *elem) {

	vector_resize (vector, vector->elements + 1);
	vector_set (vector, vector->elements - 1, elem);

}

void vector_pop (Vector *vector) {

	assert (vector->elements > 0);
	vector->elements--;
}

void vector_insert (Vector *vector, size_t i_new_elem, const void *new_elem) {

	assert (i_new_elem <= vector->elements);

	size_t n_elems_after = vector->elements - i_new_elem;

	vector_resize(vector, vector->elements + 1);
	memmove (vector_elem_ptr (vector, i_new_elem + 1),
	        vector_elem_ptr (vector, i_new_elem),
	        n_elems_after * vector->elementSize);

	vector_set (vector, i_new_elem, new_elem);

}

void vector_delete(Vector *vector, size_t i_elem) {

	assert (i_elem < vector->elements);

	size_t n_elems_after = vector->elements - 1 - i_elem;
	memmove (vector_elem_ptr (vector, i_elem),
	        vector_elem_ptr (vector, i_elem + 1),
	        n_elems_after * vector->elementSize);

	vector_resize (vector, vector->elements - 1);

}
