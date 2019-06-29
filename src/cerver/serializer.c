#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include "cerver/types/types.h"

#include "cerver/serializer.h"

void s_ptr_to_relative (void *relative_ptr, void *pointer) {

	SRelativePtr result = (char *) pointer - (char *) relative_ptr;
	memcpy (relative_ptr, &result, sizeof (result));

}

void *s_relative_to_ptr (void *relative_ptr) {

	SRelativePtr offset;
	memcpy (&offset, relative_ptr, sizeof (offset));
	return (char *) relative_ptr + offset;

}

bool s_relative_valid (void *relative_ptr, void *valid_begin, void *valid_end) {

	void *pointee = s_relative_to_ptr (relative_ptr);
	return pointee >= valid_begin && pointee < valid_end;
    
}

bool s_array_valid (void *array, size_t elem_size, void *valid_begin, void *valid_end) {

	void *begin = s_relative_to_ptr ((char *) array + offsetof (SArray, begin));

	SArray array_;
	memcpy (&array_, array, sizeof(array_));
	void *end = (char *) begin + elem_size * array_.n_elems;

	return array_.n_elems == 0
		|| (begin >= valid_begin && begin < valid_end &&
		    end >= valid_begin && end <= valid_end);

}

void s_array_init (void *array, void *begin, size_t n_elems) {

	SArray result;
	result.n_elems = n_elems;
	memcpy (array, &result, sizeof (SArray));
	s_ptr_to_relative ((char *) array + offsetof (SArray, begin), begin);

}