#ifndef _CERVER_TESTS_COLLECTIONS_CHECK_H_
#define _CERVER_TESTS_COLLECTIONS_CHECK_H_

#include <stddef.h>

#include <cerver/collections/dlist.h>

// walks the dlist forwards and backwards and checks
// that the size, the start, the end and every prev and next link match
extern void collections_check_dlist (
	const DoubleList *dlist, const size_t expected_size
);

#endif
