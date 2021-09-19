#ifndef _TEST_APP_DATA_H_
#define _TEST_APP_DATA_H_

#include <time.h>

#define DATA_NAME_SIZE		64
#define DATA_VALUE_SIZE		128

typedef struct Data {

	unsigned int id;

	unsigned int name_len;
	char name[DATA_NAME_SIZE];

	unsigned int value_len;
	char value[DATA_VALUE_SIZE];

	// when the value was created
	time_t date;

} Data;

extern Data *data_new (void);

extern void data_delete (void *data_ptr);

extern void data_set_name (
	Data *data, const char *name
);

extern void data_set_value (
	Data *data, const char *value
);

extern Data *data_create (
	const char *name, const char *value
);

extern void data_print (const Data *data);

#endif