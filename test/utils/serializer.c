#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include <cerver/serializer.h>

#include "../test.h"

// the relative pointers and the arrays of the serialized structures,
// that are valid only inside the buffer that holds them

#define SERIALIZER_TEST_VALUES			4

typedef struct SerializerTestBuffer {

	SArray array;
	SRelativePtr value_ptr;
	i32 values[SERIALIZER_TEST_VALUES];

} SerializerTestBuffer;

static void test_serializer_relative (void) {

	SerializerTestBuffer buffer;
	(void) memset (&buffer, 0, sizeof (SerializerTestBuffer));

	char *begin = (char *) &buffer;
	char *end = begin + sizeof (SerializerTestBuffer);

	s_ptr_to_relative (&buffer.value_ptr, &buffer.values[1]);
	test_check_ptr_eq (s_relative_to_ptr (&buffer.value_ptr), &buffer.values[1]);
	test_check_bool_eq (s_relative_valid (&buffer.value_ptr, begin, end), true, NULL);

	// a pointer outside the buffer is not valid
	test_check_bool_eq (s_relative_valid (&buffer.value_ptr, begin, (char *) &buffer.values[1]), false, NULL);

}

static void test_serializer_array (void) {

	SerializerTestBuffer buffer;
	(void) memset (&buffer, 0, sizeof (SerializerTestBuffer));

	char *begin = (char *) &buffer;
	char *end = begin + sizeof (SerializerTestBuffer);

	s_array_init (&buffer.array, buffer.values, SERIALIZER_TEST_VALUES);
	test_check_int_eq (buffer.array.n_elems, SERIALIZER_TEST_VALUES, NULL);
	test_check_ptr_eq (s_relative_to_ptr ((char *) &buffer.array + offsetof (SArray, begin)), buffer.values);

	test_check_bool_eq (s_array_valid (&buffer.array, sizeof (i32), begin, end), true, NULL);

	// the last element is outside a shorter buffer
	test_check_bool_eq (s_array_valid (&buffer.array, sizeof (i32), begin, end - sizeof (i32)), false, NULL);

	// an empty array is always valid
	s_array_init (&buffer.array, buffer.values, 0);
	test_check_bool_eq (s_array_valid (&buffer.array, sizeof (i32), begin, begin), true, NULL);

}

void utils_tests_serializer (void) {

	(void) printf ("Testing UTILS serializer...\n");

	test_serializer_relative ();
	test_serializer_array ();

	(void) printf ("Done!\n");

}
