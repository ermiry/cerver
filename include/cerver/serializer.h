#ifndef _CERVER_SERIALIZER_H_
#define _CERVER_SERIALIZER_H_

#include <stdlib.h>
#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef i32 SRelativePtr;

struct _SArray {

    i32 n_elems;
    SRelativePtr begin;

};

typedef struct _SArray SArray;

CERVER_PUBLIC void s_ptr_to_relative (
    void *relative_ptr, void *pointer
);

CERVER_PUBLIC void *s_relative_to_ptr (
    void *relative_ptr
);

CERVER_PUBLIC bool s_relative_valid (
    void *relative_ptr, void *valid_begin, void *valid_end
);

CERVER_PUBLIC bool s_array_valid (
    void *array, size_t elem_size, void *valid_begin, void *valid_end
);

CERVER_PUBLIC void s_array_init (
    void *array, void *begin, size_t n_elems
);

#ifdef __cplusplus
}
#endif

#endif