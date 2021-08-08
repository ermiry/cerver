#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <time.h>

#include "data.h"

static unsigned int next_id = 0;

Data *data_new (void) {

	Data *data = (Data *) malloc (sizeof (Data));
	if (data) {
		(void) memset (data, 0, sizeof (Data));
	}

	return data;

}

void data_delete (void *data_ptr) {

	if (data_ptr) {
		free (data_ptr);
	}

}

void data_set_name (Data *data, const char *name) {

	if (data && name) {
		(void) strncpy (data->name, name, DATA_NAME_SIZE - 1);
		data->name_len = strlen (data->name);
	}

}

void data_set_value (Data *data, const char *value) {

	if (data && value) {
		(void) strncpy (data->value, value, DATA_VALUE_SIZE - 1);
		data->value_len = strlen (data->value);
	}

}

Data *data_create (const char *name, const char *value) {

	Data *data = data_new ();
	if (data) {
		data->id = next_id;
		next_id += 1;

		data_set_name (data, name);
		data_set_value (data, value);

		data->date = time (NULL);
	}

	return data;

}

void data_print (const Data *data) {

	if (data) {
		(void) printf ("Data: \n");
		(void) printf ("\tid: %u\n", data->id);
		(void) printf ("\tName: %s\n", data->name);
		(void) printf ("\tValue: %s\n", data->value);
	}

}
