#ifndef _CERVER_TESTS_COLLECTIONS_DATA_H_
#define _CERVER_TESTS_COLLECTIONS_DATA_H_

typedef struct Data {

	unsigned int idx;
	unsigned int value;

} Data;

extern Data *data_new (
	const unsigned int idx, const unsigned int value
);

extern void data_delete (void *data_ptr);

extern int data_comparator (
	const void *a, const void *b
);

extern void data_print (Data *data);

#endif