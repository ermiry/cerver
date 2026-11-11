#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

#include <cerver/serializer.h>

#include <cerver/collections/arena.h>

#include "../test.h"

// the objects are maps with the id of each field as its key,
// encoded and validated with the schema of the object, compatible with CBOR (RFC 8949)

#define SERIALIZER_TEST_TEXT_SIZE			32
#define SERIALIZER_TEST_LABEL_SIZE			16
#define SERIALIZER_TEST_BYTES_SIZE			8
#define SERIALIZER_TEST_VALUES				4
#define SERIALIZER_TEST_ITEMS				3
#define SERIALIZER_TEST_ARENA_MAX			64
#define SERIALIZER_TEST_ENUM_MAX			3
#define SERIALIZER_TEST_DEFAULT				42
#define SERIALIZER_TEST_NEW_DEFAULT			7
#define SERIALIZER_TEST_BUFFER_SIZE			1024
#define SERIALIZER_TEST_GARBAGE				0xa5
#define SERIALIZER_TEST_DEPTH				9

// a map of two entries, the version 1 and the key of the field 1, before the value of a vector
static const u8 serializer_test_prefix[] = { 0xa2, 0x00, 0x01, 0x01 };

#pragma region schemas

#define VECTOR_UNSIGNED_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, U64, value, SERIALIZER_REQUIRED, 0)

#define VECTOR_SIGNED_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, I64, value, SERIALIZER_REQUIRED, 0)

#define VECTOR_DOUBLE_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, F64, value, SERIALIZER_REQUIRED, 0)

#define VECTOR_FLOAT_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, F32, value, SERIALIZER_REQUIRED, 0)

#define VECTOR_TEXT_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	STRING(T, 1, TEXT, value, SERIALIZER_TEST_TEXT_SIZE, SERIALIZER_REQUIRED)

#define VECTOR_BOOL_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	SCALAR(T, 1, BOOL, value, SERIALIZER_REQUIRED, false)

#define VECTOR_SMALL_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, U8, value, SERIALIZER_REQUIRED, 0)

#define VECTOR_TINY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	SCALAR(T, 1, I8, value, SERIALIZER_REQUIRED, 0)

#define TEST_ITEM_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	SCALAR(T, 1, U16, id, SERIALIZER_REQUIRED, 0)									\
	STRING(T, 2, TEXT, label, SERIALIZER_TEST_LABEL_SIZE, SERIALIZER_OPTIONAL)

#define TEST_OBJECT_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)							\
	SCALAR(T, 1, BOOL, flag, SERIALIZER_REQUIRED, false)											\
	SCALAR(T, 2, U8, small, SERIALIZER_OPTIONAL, 0)													\
	SCALAR(T, 3, U16, medium, SERIALIZER_OPTIONAL, 0)												\
	SCALAR(T, 4, U32, large, SERIALIZER_OPTIONAL, 0)												\
	SCALAR(T, 5, U64, huge, SERIALIZER_OPTIONAL, 0)													\
	SCALAR(T, 6, I8, tiny, SERIALIZER_OPTIONAL, 0)													\
	SCALAR(T, 7, I16, short_value, SERIALIZER_OPTIONAL, 0)											\
	SCALAR(T, 8, I32, integer, SERIALIZER_OPTIONAL, 0)												\
	SCALAR(T, 9, I64, long_value, SERIALIZER_OPTIONAL, 0)											\
	SCALAR(T, 10, F32, single, SERIALIZER_OPTIONAL, 0)												\
	SCALAR(T, 11, F64, number, SERIALIZER_OPTIONAL, 0)												\
	ENUM(T, 12, kind, SERIALIZER_OPTIONAL, 0, 0, SERIALIZER_TEST_ENUM_MAX)							\
	STRING(T, 13, TEXT, name, SERIALIZER_TEST_TEXT_SIZE, SERIALIZER_REQUIRED)						\
	STRING(T, 14, BYTES, raw, SERIALIZER_TEST_BYTES_SIZE, SERIALIZER_OPTIONAL)						\
	STRING(T, 15, TEXT_ARENA, note, SERIALIZER_TEST_ARENA_MAX, SERIALIZER_OPTIONAL)					\
	STRING(T, 16, BYTES_ARENA, blob, SERIALIZER_TEST_ARENA_MAX, SERIALIZER_OPTIONAL)					\
	ARRAY(T, 17, U32, values, SERIALIZER_TEST_VALUES, SERIALIZER_OPTIONAL)							\
	OBJECT(T, 18, TestItem, test_item_schema, item, SERIALIZER_OPTIONAL)							\
	OBJECTS(T, 19, TestItem, test_item_schema, items, SERIALIZER_TEST_ITEMS, SERIALIZER_OPTIONAL)	\
	SCALAR(T, 20, U32, missing, SERIALIZER_OPTIONAL, SERIALIZER_TEST_DEFAULT)

#define TEST_OLD_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	SCALAR(T, 1, U32, first, SERIALIZER_REQUIRED, 0)

#define TEST_NEW_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	SCALAR(T, 1, U32, first, SERIALIZER_REQUIRED, 0)								\
	SCALAR(T, 2, U32, second, SERIALIZER_OPTIONAL, SERIALIZER_TEST_NEW_DEFAULT)

SERIALIZER_DECLARE (VectorUnsigned, vector_unsigned, VECTOR_UNSIGNED_FIELDS)
SERIALIZER_DEFINE (VectorUnsigned, vector_unsigned, 1, VECTOR_UNSIGNED_FIELDS)

SERIALIZER_DECLARE (VectorSigned, vector_signed, VECTOR_SIGNED_FIELDS)
SERIALIZER_DEFINE (VectorSigned, vector_signed, 1, VECTOR_SIGNED_FIELDS)

SERIALIZER_DECLARE (VectorDouble, vector_double, VECTOR_DOUBLE_FIELDS)
SERIALIZER_DEFINE (VectorDouble, vector_double, 1, VECTOR_DOUBLE_FIELDS)

SERIALIZER_DECLARE (VectorFloat, vector_float, VECTOR_FLOAT_FIELDS)
SERIALIZER_DEFINE (VectorFloat, vector_float, 1, VECTOR_FLOAT_FIELDS)

SERIALIZER_DECLARE (VectorText, vector_text, VECTOR_TEXT_FIELDS)
SERIALIZER_DEFINE (VectorText, vector_text, 1, VECTOR_TEXT_FIELDS)

SERIALIZER_DECLARE (VectorBool, vector_bool, VECTOR_BOOL_FIELDS)
SERIALIZER_DEFINE (VectorBool, vector_bool, 1, VECTOR_BOOL_FIELDS)

SERIALIZER_DECLARE (VectorSmall, vector_small, VECTOR_SMALL_FIELDS)
SERIALIZER_DEFINE (VectorSmall, vector_small, 1, VECTOR_SMALL_FIELDS)

SERIALIZER_DECLARE (VectorTiny, vector_tiny, VECTOR_TINY_FIELDS)
SERIALIZER_DEFINE (VectorTiny, vector_tiny, 1, VECTOR_TINY_FIELDS)

SERIALIZER_DECLARE (TestItem, test_item, TEST_ITEM_FIELDS)
SERIALIZER_DEFINE (TestItem, test_item, 1, TEST_ITEM_FIELDS)

SERIALIZER_DECLARE (TestObject, test_object, TEST_OBJECT_FIELDS)
SERIALIZER_DEFINE (TestObject, test_object, 1, TEST_OBJECT_FIELDS)

SERIALIZER_DECLARE (TestOld, test_old, TEST_OLD_FIELDS)
SERIALIZER_DEFINE (TestOld, test_old, 1, TEST_OLD_FIELDS)

SERIALIZER_DECLARE (TestNew, test_new, TEST_NEW_FIELDS)
SERIALIZER_DEFINE (TestNew, test_new, 1, TEST_NEW_FIELDS)

#pragma endregion

#pragma region vectors

// encodes the object, checks that its value has the bytes of the vector,
// and that it decodes back to the same object
static void serializer_vector_check (
	const SerializerSchema *schema, const void *object,
	const u8 *expected, const size_t length
) {

	u8 buffer[SERIALIZER_TEST_BUFFER_SIZE];
	size_t written = 0;

	test_check_unsigned_eq (
		serializer_encode (schema, object, buffer, SERIALIZER_TEST_BUFFER_SIZE, &written),
		SERIALIZER_RESULT_NONE, NULL
	);

	test_check_unsigned_eq (written, sizeof (serializer_test_prefix) + length, NULL);
	test_check_unsigned_eq (serializer_encoded_size (schema, object), written, NULL);
	test_check_int_eq (memcmp (buffer, serializer_test_prefix, sizeof (serializer_test_prefix)), 0, NULL);
	test_check_int_eq (memcmp (buffer + sizeof (serializer_test_prefix), expected, length), 0, NULL);

	void *decoded = calloc (1, schema->size);
	test_check_ptr (decoded);
	test_check_unsigned_eq (serializer_decode (schema, buffer, written, NULL, decoded), SERIALIZER_RESULT_NONE, NULL);
	test_check_int_eq (memcmp (decoded, object, schema->size), 0, NULL);
	free (decoded);

}

static void serializer_unsigned_vector (const u64 value, const u8 *expected, const size_t length) {

	VectorUnsigned object = { value };
	serializer_vector_check (&vector_unsigned_schema, &object, expected, length);

}

static void serializer_signed_vector (const i64 value, const u8 *expected, const size_t length) {

	VectorSigned object = { value };
	serializer_vector_check (&vector_signed_schema, &object, expected, length);

}

static void serializer_double_vector (const double value, const u8 *expected, const size_t length) {

	VectorDouble object = { value };
	serializer_vector_check (&vector_double_schema, &object, expected, length);

}

static void serializer_text_vector (const char *value, const u8 *expected, const size_t length) {

	VectorText object;
	(void) memset (&object, 0, sizeof (VectorText));
	(void) strncpy (object.value, value, SERIALIZER_TEST_TEXT_SIZE - 1);

	serializer_vector_check (&vector_text_schema, &object, expected, length);

}

#define SERIALIZER_UNSIGNED_VECTOR(value, ...) {								\
	const u8 bytes[] = { __VA_ARGS__ };											\
	serializer_unsigned_vector (value, bytes, sizeof (bytes));					\
}

#define SERIALIZER_SIGNED_VECTOR(value, ...) {									\
	const u8 bytes[] = { __VA_ARGS__ };											\
	serializer_signed_vector (value, bytes, sizeof (bytes));					\
}

#define SERIALIZER_DOUBLE_VECTOR(value, ...) {									\
	const u8 bytes[] = { __VA_ARGS__ };											\
	serializer_double_vector (value, bytes, sizeof (bytes));					\
}

#define SERIALIZER_TEXT_VECTOR(value, ...) {									\
	const u8 bytes[] = { __VA_ARGS__ };											\
	serializer_text_vector (value, bytes, sizeof (bytes));						\
}

// the integers of RFC 8949 appendix A, in the smallest size
static void test_serializer_vectors_integers (void) {

	SERIALIZER_UNSIGNED_VECTOR (0, 0x00);
	SERIALIZER_UNSIGNED_VECTOR (1, 0x01);
	SERIALIZER_UNSIGNED_VECTOR (10, 0x0a);
	SERIALIZER_UNSIGNED_VECTOR (23, 0x17);
	SERIALIZER_UNSIGNED_VECTOR (24, 0x18, 0x18);
	SERIALIZER_UNSIGNED_VECTOR (25, 0x18, 0x19);
	SERIALIZER_UNSIGNED_VECTOR (100, 0x18, 0x64);
	SERIALIZER_UNSIGNED_VECTOR (1000, 0x19, 0x03, 0xe8);
	SERIALIZER_UNSIGNED_VECTOR (1000000, 0x1a, 0x00, 0x0f, 0x42, 0x40);
	SERIALIZER_UNSIGNED_VECTOR (1000000000000ull, 0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00);
	SERIALIZER_UNSIGNED_VECTOR (UINT64_MAX, 0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);

	SERIALIZER_SIGNED_VECTOR (-1, 0x20);
	SERIALIZER_SIGNED_VECTOR (-10, 0x29);
	SERIALIZER_SIGNED_VECTOR (-100, 0x38, 0x63);
	SERIALIZER_SIGNED_VECTOR (-1000, 0x39, 0x03, 0xe7);
	SERIALIZER_SIGNED_VECTOR (INT64_MIN, 0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	SERIALIZER_SIGNED_VECTOR (INT64_MAX, 0x1b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);

}

// the floats of RFC 8949 appendix A, in the shortest size that keeps their value
static void test_serializer_vectors_floats (void) {

	SERIALIZER_DOUBLE_VECTOR (0.0, 0xf9, 0x00, 0x00);
	SERIALIZER_DOUBLE_VECTOR (-0.0, 0xf9, 0x80, 0x00);
	SERIALIZER_DOUBLE_VECTOR (1.0, 0xf9, 0x3c, 0x00);
	SERIALIZER_DOUBLE_VECTOR (1.1, 0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a);
	SERIALIZER_DOUBLE_VECTOR (1.5, 0xf9, 0x3e, 0x00);
	SERIALIZER_DOUBLE_VECTOR (65504.0, 0xf9, 0x7b, 0xff);
	SERIALIZER_DOUBLE_VECTOR (100000.0, 0xfa, 0x47, 0xc3, 0x50, 0x00);
	SERIALIZER_DOUBLE_VECTOR (3.4028234663852886e+38, 0xfa, 0x7f, 0x7f, 0xff, 0xff);
	SERIALIZER_DOUBLE_VECTOR (1.0e+300, 0xfb, 0x7e, 0x37, 0xe4, 0x3c, 0x88, 0x00, 0x75, 0x9c);
	SERIALIZER_DOUBLE_VECTOR (5.960464477539063e-8, 0xf9, 0x00, 0x01);
	SERIALIZER_DOUBLE_VECTOR (0.00006103515625, 0xf9, 0x04, 0x00);
	SERIALIZER_DOUBLE_VECTOR (-4.0, 0xf9, 0xc4, 0x00);
	SERIALIZER_DOUBLE_VECTOR (-4.1, 0xfb, 0xc0, 0x10, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66);
	SERIALIZER_DOUBLE_VECTOR ((double) INFINITY, 0xf9, 0x7c, 0x00);
	SERIALIZER_DOUBLE_VECTOR (-(double) INFINITY, 0xf9, 0xfc, 0x00);

	// a NaN is written as the canonical half and read as a NaN
	VectorDouble nan_object = { (double) NAN };
	u8 buffer[SERIALIZER_TEST_BUFFER_SIZE];
	size_t written = 0;
	test_check_unsigned_eq (
		serializer_encode (&vector_double_schema, &nan_object, buffer, SERIALIZER_TEST_BUFFER_SIZE, &written),
		SERIALIZER_RESULT_NONE, NULL
	);

	const u8 nan_bytes[] = { 0xf9, 0x7e, 0x00 };
	test_check_int_eq (memcmp (buffer + sizeof (serializer_test_prefix), nan_bytes, sizeof (nan_bytes)), 0, NULL);

	VectorDouble decoded = { 0 };
	test_check_unsigned_eq (serializer_decode (&vector_double_schema, buffer, written, NULL, &decoded), SERIALIZER_RESULT_NONE, NULL);
	test_check_true (isnan (decoded.value));

}

// the texts of RFC 8949 appendix A, and true and false
static void test_serializer_vectors_texts (void) {

	SERIALIZER_TEXT_VECTOR ("", 0x60);
	SERIALIZER_TEXT_VECTOR ("a", 0x61, 0x61);
	SERIALIZER_TEXT_VECTOR ("IETF", 0x64, 0x49, 0x45, 0x54, 0x46);
	SERIALIZER_TEXT_VECTOR ("\"\\", 0x62, 0x22, 0x5c);
	SERIALIZER_TEXT_VECTOR ("\xc3\xbc", 0x62, 0xc3, 0xbc);
	SERIALIZER_TEXT_VECTOR ("\xe6\xb0\xb4", 0x63, 0xe6, 0xb0, 0xb4);
	SERIALIZER_TEXT_VECTOR ("\xf0\x90\x85\x91", 0x64, 0xf0, 0x90, 0x85, 0x91);

	const u8 false_bytes[] = { 0xf4 };
	VectorBool false_object = { false };
	serializer_vector_check (&vector_bool_schema, &false_object, false_bytes, sizeof (false_bytes));

	const u8 true_bytes[] = { 0xf5 };
	VectorBool true_object = { true };
	serializer_vector_check (&vector_bool_schema, &true_object, true_bytes, sizeof (true_bytes));

}

#pragma endregion

#pragma region decode

// decodes the bytes into an object filled with garbage, that must stay untouched on an error
static SerializerResult serializer_decode_bytes (
	const SerializerSchema *schema, const u8 *bytes, const size_t size
) {

	u8 object[SERIALIZER_TEST_BUFFER_SIZE];
	(void) memset (object, SERIALIZER_TEST_GARBAGE, SERIALIZER_TEST_BUFFER_SIZE);

	u8 untouched[SERIALIZER_TEST_BUFFER_SIZE];
	(void) memset (untouched, SERIALIZER_TEST_GARBAGE, SERIALIZER_TEST_BUFFER_SIZE);

	const SerializerResult result = serializer_decode (schema, bytes, size, NULL, object);

	if (result != SERIALIZER_RESULT_NONE) {
		test_check_int_eq (memcmp (object, untouched, SERIALIZER_TEST_BUFFER_SIZE), 0, "the object changed");
	}

	return result;

}

#define SERIALIZER_DECODE_CHECK(schema, expected, ...) {											\
	const u8 bytes[] = { __VA_ARGS__ };																\
	test_check_unsigned_eq (serializer_decode_bytes (schema, bytes, sizeof (bytes)), expected, NULL);	\
}

// every way an object can be wrong is rejected, without changing the object
static void test_serializer_decode_rejects (void) {

	// bool = 2 and null in a bool
	SERIALIZER_DECODE_CHECK (&vector_bool_schema, SERIALIZER_RESULT_TYPE, 0xa2, 0x00, 0x01, 0x01, 0x02);
	SERIALIZER_DECODE_CHECK (&vector_bool_schema, SERIALIZER_RESULT_TYPE, 0xa2, 0x00, 0x01, 0x01, 0xf6);

	// a field twice, a version twice, and a missing required field
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_DUPLICATE, 0xa3, 0x00, 0x01, 0x01, 0x05, 0x01, 0x06);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_DUPLICATE, 0xa3, 0x00, 0x01, 0x00, 0x01, 0x01, 0x06);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_MISSING, 0xa1, 0x00, 0x01);

	// another version of the schema
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_VERSION, 0xa2, 0x00, 0x02, 0x01, 0x05);

	// a tag, an indefinite map, and a reserved additional information
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_UNSUPPORTED, 0xa2, 0x00, 0x01, 0x01, 0xc1, 0x05);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_UNSUPPORTED, 0xbf, 0x00, 0x01, 0xff);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_UNSUPPORTED, 0xa2, 0x00, 0x01, 0x01, 0x1c);

	// a buffer that ends in the middle of a value, bytes after the object, and a key that is not an id
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_TRUNCATED, 0xa2, 0x00, 0x01, 0x01, 0x19, 0x03);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_TRAILING, 0xa2, 0x00, 0x01, 0x01, 0x05, 0x00);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_KEY, 0xa2, 0x00, 0x01, 0x61, 0x61, 0x05);

	// an object that is not a map, and a value of another type
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_TYPE, 0x81, 0x05);
	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_TYPE, 0xa2, 0x00, 0x01, 0x01, 0x20);

	// a text with a NUL, invalid UTF-8, an overlong sequence, a surrogate, and a text longer than its field
	SERIALIZER_DECODE_CHECK (&vector_text_schema, SERIALIZER_RESULT_NUL, 0xa2, 0x00, 0x01, 0x01, 0x62, 0x61, 0x00);
	SERIALIZER_DECODE_CHECK (&vector_text_schema, SERIALIZER_RESULT_UTF8, 0xa2, 0x00, 0x01, 0x01, 0x62, 0xc3, 0x28);
	SERIALIZER_DECODE_CHECK (&vector_text_schema, SERIALIZER_RESULT_UTF8, 0xa2, 0x00, 0x01, 0x01, 0x62, 0xc0, 0x80);
	SERIALIZER_DECODE_CHECK (&vector_text_schema, SERIALIZER_RESULT_UTF8, 0xa2, 0x00, 0x01, 0x01, 0x63, 0xed, 0xa0, 0x80);
	SERIALIZER_DECODE_CHECK (&vector_text_schema, SERIALIZER_RESULT_LENGTH, 0xa2, 0x00, 0x01, 0x01, 0x78, 0x20);

	// numbers that do not fit in their fields
	SERIALIZER_DECODE_CHECK (&vector_small_schema, SERIALIZER_RESULT_RANGE, 0xa2, 0x00, 0x01, 0x01, 0x19, 0x01, 0x00);
	SERIALIZER_DECODE_CHECK (&vector_tiny_schema, SERIALIZER_RESULT_RANGE, 0xa2, 0x00, 0x01, 0x01, 0x38, 0x80);
	SERIALIZER_DECODE_CHECK (&vector_tiny_schema, SERIALIZER_RESULT_RANGE, 0xa2, 0x00, 0x01, 0x01, 0x18, 0x80);
	SERIALIZER_DECODE_CHECK (&vector_float_schema, SERIALIZER_RESULT_RANGE, 0xa2, 0x00, 0x01, 0x01, 0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a);

	// an unknown value nested deeper than the maximum, and an array with more elements than the maximum
	SERIALIZER_DECODE_CHECK (
		&vector_unsigned_schema, SERIALIZER_RESULT_DEPTH,
		0xa3, 0x00, 0x01, 0x01, 0x05, 0x09, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x00
	);

	SERIALIZER_DECODE_CHECK (&vector_unsigned_schema, SERIALIZER_RESULT_ELEMENTS, 0xa3, 0x00, 0x01, 0x01, 0x05, 0x09, 0x99, 0x04, 0x01);

	// an enum out of its range
	TestObject object;
	serializer_object_init (&test_object_schema, &object);
	object.kind = SERIALIZER_TEST_ENUM_MAX + 1;
	u8 buffer[SERIALIZER_TEST_BUFFER_SIZE];
	test_check_unsigned_eq (
		serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL),
		SERIALIZER_RESULT_RANGE, NULL
	);

	// a buffer bigger than the maximum size of an object
	u8 *big = (u8 *) calloc (SERIALIZER_DEFAULT_MAX_SIZE + 1, sizeof (u8));
	test_check_ptr (big);
	test_check_unsigned_eq (serializer_decode_bytes (&vector_unsigned_schema, big, SERIALIZER_DEFAULT_MAX_SIZE + 1), SERIALIZER_RESULT_SIZE, NULL);
	free (big);

}

// a value in more bytes than it needs, a float in a smaller size and an unknown field are taken
static void test_serializer_decode_accepts (void) {

	VectorUnsigned unsigned_object = { 0 };
	const u8 long_integer[] = { 0xa2, 0x00, 0x01, 0x01, 0x19, 0x00, 0x05 };
	test_check_unsigned_eq (serializer_decode (&vector_unsigned_schema, long_integer, sizeof (long_integer), NULL, &unsigned_object), SERIALIZER_RESULT_NONE, NULL);
	test_check_unsigned_eq (unsigned_object.value, 5, NULL);

	const u8 unknown_field[] = { 0xa3, 0x00, 0x01, 0x09, 0x63, 0x61, 0x62, 0x63, 0x01, 0x06 };
	test_check_unsigned_eq (serializer_decode (&vector_unsigned_schema, unknown_field, sizeof (unknown_field), NULL, &unsigned_object), SERIALIZER_RESULT_NONE, NULL);
	test_check_unsigned_eq (unsigned_object.value, 6, NULL);

	// the version can be missing
	const u8 without_version[] = { 0xa1, 0x01, 0x07 };
	test_check_unsigned_eq (serializer_decode (&vector_unsigned_schema, without_version, sizeof (without_version), NULL, &unsigned_object), SERIALIZER_RESULT_NONE, NULL);
	test_check_unsigned_eq (unsigned_object.value, 7, NULL);

	VectorDouble double_object = { 0 };
	const u8 single[] = { 0xa2, 0x00, 0x01, 0x01, 0xfa, 0x47, 0xc3, 0x50, 0x00 };
	test_check_unsigned_eq (serializer_decode (&vector_double_schema, single, sizeof (single), NULL, &double_object), SERIALIZER_RESULT_NONE, NULL);
	test_check (serializer_encoded_size (&vector_double_schema, &double_object) == sizeof (single), NULL);

}

#pragma endregion

#pragma region objects

static void serializer_test_object_fill (TestObject *object, char *note, u8 *blob) {

	serializer_object_init (&test_object_schema, object);

	object->flag = true;
	object->small = UINT8_MAX;
	object->medium = UINT16_MAX;
	object->large = UINT32_MAX;
	object->huge = UINT64_MAX;
	object->tiny = INT8_MIN;
	object->short_value = INT16_MIN;
	object->integer = INT32_MIN;
	object->long_value = INT64_MIN;
	object->single = 0.5f;
	object->number = 1.1;
	object->kind = SERIALIZER_TEST_ENUM_MAX;

	(void) strncpy (object->name, "cerver", SERIALIZER_TEST_TEXT_SIZE - 1);

	for (u8 i = 0; i < SERIALIZER_TEST_BYTES_SIZE; i++) object->raw[i] = i;
	object->raw_count = SERIALIZER_TEST_BYTES_SIZE;

	object->note = note;
	object->blob = blob;
	object->blob_count = SERIALIZER_TEST_BYTES_SIZE;

	for (u32 i = 0; i < SERIALIZER_TEST_VALUES; i++) object->values[i] = i * SERIALIZER_TEST_DEFAULT;
	object->values_count = SERIALIZER_TEST_VALUES;

	object->item.id = 1;
	(void) strncpy (object->item.label, "item", SERIALIZER_TEST_LABEL_SIZE - 1);

	for (u16 i = 0; i < SERIALIZER_TEST_ITEMS; i++) object->items[i].id = (u16) (i + 1);
	object->items_count = SERIALIZER_TEST_ITEMS;

}

// every type goes and comes back, the arena fields take their memory from the arena
static void test_serializer_object_round_trip (void) {

	char note[] = "a note in the arena";
	u8 blob[SERIALIZER_TEST_BYTES_SIZE] = { 9, 8, 7, 6, 5, 4, 3, 2 };

	TestObject object;
	serializer_test_object_fill (&object, note, blob);

	u8 buffer[SERIALIZER_TEST_BUFFER_SIZE];
	size_t written = 0;
	test_check_unsigned_eq (
		serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, &written),
		SERIALIZER_RESULT_NONE, NULL
	);

	test_check_unsigned_eq (serializer_encoded_size (&test_object_schema, &object), written, NULL);

	// a smaller buffer does not take it
	test_check_unsigned_eq (
		serializer_encode (&test_object_schema, &object, buffer, written - 1, NULL),
		SERIALIZER_RESULT_BUFFER, NULL
	);

	// the arena fields need an arena
	TestObject decoded;
	test_check_unsigned_eq (serializer_decode (&test_object_schema, buffer, written, NULL, &decoded), SERIALIZER_RESULT_ARENA, NULL);

	Arena *arena = arena_create (0);
	test_check_ptr (arena);
	test_check_unsigned_eq (serializer_decode (&test_object_schema, buffer, written, arena, &decoded), SERIALIZER_RESULT_NONE, NULL);

	test_check_true (decoded.flag);
	test_check_unsigned_eq (decoded.small, UINT8_MAX, NULL);
	test_check_unsigned_eq (decoded.medium, UINT16_MAX, NULL);
	test_check_unsigned_eq (decoded.large, UINT32_MAX, NULL);
	test_check (decoded.huge == UINT64_MAX, NULL);
	test_check_int_eq (decoded.tiny, INT8_MIN, NULL);
	test_check_int_eq (decoded.short_value, INT16_MIN, NULL);
	test_check_int_eq (decoded.integer, INT32_MIN, NULL);
	test_check (decoded.long_value == INT64_MIN, NULL);
	test_check_int_eq (memcmp (&decoded.single, &object.single, sizeof (float)), 0, NULL);
	test_check_int_eq (memcmp (&decoded.number, &object.number, sizeof (double)), 0, NULL);
	test_check_int_eq (decoded.kind, SERIALIZER_TEST_ENUM_MAX, NULL);
	test_check_str_eq (decoded.name, "cerver", NULL);
	test_check_unsigned_eq (decoded.raw_count, SERIALIZER_TEST_BYTES_SIZE, NULL);
	test_check_int_eq (memcmp (decoded.raw, object.raw, SERIALIZER_TEST_BYTES_SIZE), 0, NULL);
	test_check_str_eq (decoded.note, note, NULL);
	test_check (decoded.note != note, "the note was not copied to the arena");
	test_check_unsigned_eq (decoded.blob_count, SERIALIZER_TEST_BYTES_SIZE, NULL);
	test_check_int_eq (memcmp (decoded.blob, blob, SERIALIZER_TEST_BYTES_SIZE), 0, NULL);
	test_check_unsigned_eq (decoded.values_count, SERIALIZER_TEST_VALUES, NULL);
	test_check_int_eq (memcmp (decoded.values, object.values, sizeof (object.values)), 0, NULL);
	test_check_unsigned_eq (decoded.item.id, 1, NULL);
	test_check_str_eq (decoded.item.label, "item", NULL);
	test_check_unsigned_eq (decoded.items_count, SERIALIZER_TEST_ITEMS, NULL);
	test_check_unsigned_eq (decoded.items[SERIALIZER_TEST_ITEMS - 1].id, SERIALIZER_TEST_ITEMS, NULL);
	test_check_unsigned_eq (decoded.missing, SERIALIZER_TEST_DEFAULT, NULL);

	arena_delete (arena);

}

// the values that can not be encoded are errors, nothing is cut
static void test_serializer_encode_rejects (void) {

	TestObject object;
	serializer_test_object_fill (&object, NULL, NULL);
	object.blob_count = 0;

	u8 buffer[SERIALIZER_TEST_BUFFER_SIZE];

	// a text that fills its field without a NUL
	(void) memset (object.name, 'a', SERIALIZER_TEST_TEXT_SIZE);
	test_check_unsigned_eq (serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_LENGTH, NULL);
	object.name[0] = '\0';

	// more bytes and more elements than their fields
	object.raw_count = SERIALIZER_TEST_BYTES_SIZE + 1;
	test_check_unsigned_eq (serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_LENGTH, NULL);
	object.raw_count = 0;

	object.values_count = SERIALIZER_TEST_VALUES + 1;
	test_check_unsigned_eq (serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_LENGTH, NULL);
	object.values_count = 0;

	// an arena bytes field with a count and without bytes
	object.blob_count = 1;
	test_check_unsigned_eq (serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_ARGUMENTS, NULL);
	object.blob_count = 0;

	// invalid UTF-8
	object.name[0] = (char) 0xc3;
	object.name[1] = '\0';
	test_check_unsigned_eq (serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_UTF8, NULL);
	object.name[0] = '\0';

	test_check_unsigned_eq (serializer_encode (&test_object_schema, &object, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_NONE, NULL);
	test_check_unsigned_eq (serializer_encoded_size (NULL, &object), 0, NULL);
	test_check_unsigned_eq (serializer_encode (&test_object_schema, NULL, buffer, SERIALIZER_TEST_BUFFER_SIZE, NULL), SERIALIZER_RESULT_ARGUMENTS, NULL);

}

// a peer with a new field and a peer without it understand each other
static void test_serializer_versions (void) {

	u8 buffer[SERIALIZER_TEST_BUFFER_SIZE];
	size_t written = 0;

	TestNew newer = { 1, 2 };
	test_check_unsigned_eq (serializer_encode (&test_new_schema, &newer, buffer, SERIALIZER_TEST_BUFFER_SIZE, &written), SERIALIZER_RESULT_NONE, NULL);

	TestOld older = { 0 };
	test_check_unsigned_eq (serializer_decode (&test_old_schema, buffer, written, NULL, &older), SERIALIZER_RESULT_NONE, NULL);
	test_check_unsigned_eq (older.first, 1, NULL);

	older.first = 3;
	test_check_unsigned_eq (serializer_encode (&test_old_schema, &older, buffer, SERIALIZER_TEST_BUFFER_SIZE, &written), SERIALIZER_RESULT_NONE, NULL);

	test_check_unsigned_eq (serializer_decode (&test_new_schema, buffer, written, NULL, &newer), SERIALIZER_RESULT_NONE, NULL);
	test_check_unsigned_eq (newer.first, 3, NULL);
	test_check_unsigned_eq (newer.second, SERIALIZER_TEST_NEW_DEFAULT, NULL);

}

// the schemas of cerver can be listed for the fuzzer
static void test_serializer_cerver_schemas (void) {

	size_t count = 0;
	const SerializerSchema *const *schemas = serializer_schemas (&count);

	test_check_ptr (schemas);
	test_check (count > 0, NULL);

	for (size_t i = 0; i < count; i++) {
		test_check_ptr (schemas[i]);
		test_check (schemas[i]->n_fields <= SERIALIZER_FIELDS_MAX, NULL);
	}

	test_check_str_eq (serializer_result_string (SERIALIZER_RESULT_NONE), "The object was serialized", NULL);
	test_check_str_eq (serializer_type_string (SERIALIZER_TYPE_OBJECT), "OBJECT", NULL);

}

#pragma endregion

void utils_tests_serializer (void) {

	(void) printf ("Testing UTILS serializer...\n");

	test_serializer_vectors_integers ();
	test_serializer_vectors_floats ();
	test_serializer_vectors_texts ();
	test_serializer_decode_rejects ();
	test_serializer_decode_accepts ();
	test_serializer_object_round_trip ();
	test_serializer_encode_rejects ();
	test_serializer_versions ();
	test_serializer_cerver_schemas ();

	(void) printf ("Done!\n");

}
