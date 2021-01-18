#include <stdlib.h>
#include <stdio.h>

#include "data.h"

Data *data_new (
	const unsigned int idx, const unsigned int value
) {

	Data *data = (Data *) malloc (sizeof (Data));
	if (data) {
		data->idx = idx;
		data->value = value;
	}

	return data;

}

void data_delete (void *data_ptr) {

	if (data_ptr) free (data_ptr);

}

int data_comparator (const void *a, const void *b) {

	Data *data_a = (Data *) a;
	Data *data_b = (Data *) b;

	if (data_a->idx < data_b->idx) return -1;
	else if (data_a->idx == data_b->idx) return 0;
	return 1;

}

void data_print (Data *data) {

	(void) printf (
		"idx: %u - value: %u\n",
		data->idx, data->value
	);
	
}