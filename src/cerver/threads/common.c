#include <stdlib.h>
#include <string.h>

#include "cerver/threads/common.h"

bsem *bsem_new (void) {

    bsem *binary_sem = (bsem *) malloc (sizeof (bsem));
    if (binary_sem) memset (binary_sem, 0, sizeof (bsem));
    return binary_sem;

}

void bsem_delete (void *bsem_ptr) { if (bsem_ptr) free (bsem_ptr); }