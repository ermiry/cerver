#ifndef _CERVER_SERIALIZER_H_
#define _CERVER_SERIALIZER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

// an object is encoded as a CBOR map (RFC 8949) with the id of each field as its key,
// so any CBOR library can read it, and a peer ignores the fields that it does not know
// and gives their default to the fields that are missing

#pragma region types

#define SERIALIZER_TYPE_MAP(XX)				\
	XX(0,	NONE)							\
	XX(1,	BOOL)							\
	XX(2,	U8)								\
	XX(3,	U16)							\
	XX(4,	U32)							\
	XX(5,	U64)							\
	XX(6,	I8)								\
	XX(7,	I16)							\
	XX(8,	I32)							\
	XX(9,	I64)							\
	XX(10,	F32)							\
	XX(11,	F64)							\
	XX(12,	ENUM)							\
	XX(13,	TEXT)							\
	XX(14,	BYTES)							\
	XX(15,	TEXT_ARENA)						\
	XX(16,	BYTES_ARENA)					\
	XX(17,	ARRAY)							\
	XX(18,	OBJECT)

typedef enum SerializerType {

	#define XX(num, name) SERIALIZER_TYPE_##name = num,
	SERIALIZER_TYPE_MAP (XX)
	#undef XX

} SerializerType;

CERVER_PUBLIC const char *serializer_type_string (
	const SerializerType type
);

#define SERIALIZER_RESULT_MAP(XX)																	\
	XX(0,	NONE,			"The object was serialized")											\
	XX(1,	ARGUMENTS,		"The arguments are not valid")											\
	XX(2,	TRUNCATED,		"The buffer ends in the middle of an item")								\
	XX(3,	SIZE,			"The object is bigger than its maximum size")							\
	XX(4,	DEPTH,			"The object is nested deeper than its maximum depth")					\
	XX(5,	ELEMENTS,		"An array or a map has more elements than the maximum")					\
	XX(6,	TYPE,			"A field has a value of another type")									\
	XX(7,	RANGE,			"A number does not fit in its field")									\
	XX(8,	LENGTH,			"A text, bytes or array is longer than its field")						\
	XX(9,	UTF8,			"A text is not valid UTF-8")											\
	XX(10,	NUL,			"A text has a NUL character")											\
	XX(11,	DUPLICATE,		"A field appears more than once")										\
	XX(12,	MISSING,		"A required field is missing")											\
	XX(13,	VERSION,		"The object has another version of the schema")							\
	XX(14,	KEY,			"A key of a map is not a field id")										\
	XX(15,	UNSUPPORTED,	"The item uses a feature of CBOR that cerver does not take")			\
	XX(16,	ARENA,			"A field needs memory from an arena")									\
	XX(17,	TRAILING,		"There are bytes after the object")										\
	XX(18,	BUFFER,			"The buffer is smaller than the encoded object")

typedef enum SerializerResult {

	#define XX(num, name, string) SERIALIZER_RESULT_##name = num,
	SERIALIZER_RESULT_MAP (XX)
	#undef XX

} SerializerResult;

CERVER_PUBLIC const char *serializer_result_string (
	const SerializerResult result
);

#pragma endregion

#pragma region schema

// the default limits of an object, a schema can lower them
#define SERIALIZER_DEFAULT_MAX_SIZE				65536
#define SERIALIZER_DEFAULT_MAX_DEPTH			8
#define SERIALIZER_DEFAULT_MAX_ELEMENTS			1024

// the fields of a schema, ids go from 1 to this value
#define SERIALIZER_FIELDS_MAX					64

// the key of the version of the schema in every object
#define SERIALIZER_VERSION_KEY					0

#define SERIALIZER_REQUIRED						true
#define SERIALIZER_OPTIONAL						false

// the value of a field that is missing, every member is set from the default of the field
typedef struct SerializerDefault {

	i64 integer;
	u64 natural;
	double number;

} SerializerDefault;

struct SerializerSchema;

typedef struct SerializerField {

	u32 id;
	SerializerType type;
	const char *name;

	// the position and the size of the member in the struct,
	// the size of a single element for an array
	size_t offset;
	size_t size;

	// the capacity of a text (with its NUL), a bytes or an array,
	// the maximum length of an arena text or bytes
	size_t capacity;

	// the u32 member that counts the bytes or the elements, for bytes and arrays
	size_t count_offset;

	bool required;
	SerializerDefault default_value;

	// the values that an enum takes
	i64 minimum;
	i64 maximum;

	// the type of the elements of an array
	SerializerType element_type;

	// the schema of an object, or of the elements of an array of objects
	const struct SerializerSchema *schema;

} SerializerField;

typedef struct SerializerSchema {

	const char *name;

	// a new field does not change it, a change that breaks the older peers does
	u32 version;

	// the size of the struct of the object
	size_t size;

	const SerializerField *fields;
	size_t n_fields;

	size_t max_size;
	u32 max_depth;
	u32 max_elements;

} SerializerSchema;

#pragma endregion

#pragma region define

// a schema is described once with a list of fields, that generates its struct and its table:
//
// #define CREDENTIALS_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)
// 	STRING(T, 1, TEXT, username, CREDENTIALS_USERNAME_SIZE, SERIALIZER_REQUIRED)
// 	SCALAR(T, 2, U32, attempts, SERIALIZER_OPTIONAL, 0)
//
// SERIALIZER_DECLARE (Credentials, credentials, CREDENTIALS_FIELDS) in a header,
// SERIALIZER_DEFINE (Credentials, credentials, 1, CREDENTIALS_FIELDS) in a source,
// gives the struct Credentials and the schema credentials_schema
//
// SCALAR(T, id, kind, name, required, default)						BOOL, U8 ... I64, F32, F64
// ENUM(T, id, name, required, default, minimum, maximum)			an i32 member
// STRING(T, id, kind, name, capacity, required)					TEXT, BYTES, TEXT_ARENA, BYTES_ARENA
// ARRAY(T, id, kind, name, capacity, required)						an array of scalars of kind
// OBJECT(T, id, Type, schema, name, required)						a nested object
// OBJECTS(T, id, Type, schema, name, capacity, required)			an array of objects
//
// bytes and arrays have a u32 member named <name>_count with the number of bytes or elements

#define SERIALIZER_C_TYPE_BOOL					bool
#define SERIALIZER_C_TYPE_U8					u8
#define SERIALIZER_C_TYPE_U16					u16
#define SERIALIZER_C_TYPE_U32					u32
#define SERIALIZER_C_TYPE_U64					u64
#define SERIALIZER_C_TYPE_I8					i8
#define SERIALIZER_C_TYPE_I16					i16
#define SERIALIZER_C_TYPE_I32					i32
#define SERIALIZER_C_TYPE_I64					i64
#define SERIALIZER_C_TYPE_F32					float
#define SERIALIZER_C_TYPE_F64					double

#define SERIALIZER_MEMBER_TEXT(name, capacity)					char name[capacity];
#define SERIALIZER_MEMBER_BYTES(name, capacity)					u8 name[capacity]; u32 name##_count;
#define SERIALIZER_MEMBER_TEXT_ARENA(name, capacity)			char *name;
#define SERIALIZER_MEMBER_BYTES_ARENA(name, capacity)			u8 *name; u32 name##_count;

#define SERIALIZER_COUNT_OFFSET_TEXT(T, name)					0
#define SERIALIZER_COUNT_OFFSET_BYTES(T, name)					offsetof (T, name##_count)
#define SERIALIZER_COUNT_OFFSET_TEXT_ARENA(T, name)				0
#define SERIALIZER_COUNT_OFFSET_BYTES_ARENA(T, name)			offsetof (T, name##_count)

#define SERIALIZER_STRUCT_SCALAR(T, id, kind, name, required, value)						\
	SERIALIZER_C_TYPE_##kind name;

#define SERIALIZER_STRUCT_ENUM(T, id, name, required, value, minimum, maximum)				\
	i32 name;

#define SERIALIZER_STRUCT_STRING(T, id, kind, name, capacity, required)					\
	SERIALIZER_MEMBER_##kind(name, capacity)

#define SERIALIZER_STRUCT_ARRAY(T, id, kind, name, capacity, required)						\
	SERIALIZER_C_TYPE_##kind name[capacity]; u32 name##_count;

#define SERIALIZER_STRUCT_OBJECT(T, id, Type, schema, name, required)						\
	Type name;

#define SERIALIZER_STRUCT_OBJECTS(T, id, Type, schema, name, capacity, required)			\
	Type name[capacity]; u32 name##_count;

#define SERIALIZER_DEFAULT_VALUE(value)														\
	{ .integer = (i64) (value), .natural = (u64) (value), .number = (double) (value) }

#define SERIALIZER_FIELD_SCALAR(T, field_id, kind, field_name, field_required, value) {		\
	.id = field_id, .type = SERIALIZER_TYPE_##kind, .name = #field_name,					\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name),				\
	.capacity = 0, .count_offset = 0,														\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (value),			\
	.minimum = 0, .maximum = 0,																\
	.element_type = SERIALIZER_TYPE_NONE, .schema = NULL									\
},

#define SERIALIZER_FIELD_ENUM(T, field_id, field_name, field_required, value, low, high) {	\
	.id = field_id, .type = SERIALIZER_TYPE_ENUM, .name = #field_name,						\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name),				\
	.capacity = 0, .count_offset = 0,														\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (value),			\
	.minimum = low, .maximum = high,														\
	.element_type = SERIALIZER_TYPE_NONE, .schema = NULL									\
},

#define SERIALIZER_FIELD_STRING(T, field_id, kind, field_name, field_capacity, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_##kind, .name = #field_name,						\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name),					\
	.capacity = field_capacity,																	\
	.count_offset = SERIALIZER_COUNT_OFFSET_##kind(T, field_name),								\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),					\
	.minimum = 0, .maximum = 0,																	\
	.element_type = SERIALIZER_TYPE_NONE, .schema = NULL										\
},

#define SERIALIZER_FIELD_ARRAY(T, field_id, kind, field_name, field_capacity, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_ARRAY, .name = #field_name,							\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name[0]),				\
	.capacity = field_capacity, .count_offset = offsetof (T, field_name##_count),				\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),					\
	.minimum = 0, .maximum = 0,																	\
	.element_type = SERIALIZER_TYPE_##kind, .schema = NULL										\
},

#define SERIALIZER_FIELD_OBJECT(T, field_id, Type, field_schema, field_name, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_OBJECT, .name = #field_name,						\
	.offset = offsetof (T, field_name), .size = sizeof (Type),									\
	.capacity = 0, .count_offset = 0,															\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),					\
	.minimum = 0, .maximum = 0,																	\
	.element_type = SERIALIZER_TYPE_NONE, .schema = &field_schema								\
},

#define SERIALIZER_FIELD_OBJECTS(T, field_id, Type, field_schema, field_name, field_capacity, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_ARRAY, .name = #field_name,											\
	.offset = offsetof (T, field_name), .size = sizeof (Type),													\
	.capacity = field_capacity, .count_offset = offsetof (T, field_name##_count),								\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),									\
	.minimum = 0, .maximum = 0,																					\
	.element_type = SERIALIZER_TYPE_OBJECT, .schema = &field_schema												\
},

// the struct of the object and the declaration of its schema
#define SERIALIZER_DECLARE(Name, prefix, FIELDS)											\
	typedef struct Name {																	\
		FIELDS(Name, SERIALIZER_STRUCT_SCALAR, SERIALIZER_STRUCT_ENUM,						\
			SERIALIZER_STRUCT_STRING, SERIALIZER_STRUCT_ARRAY,								\
			SERIALIZER_STRUCT_OBJECT, SERIALIZER_STRUCT_OBJECTS)							\
	} Name;																					\
	CERVER_EXPORT const SerializerSchema prefix##_schema;

// the table of the fields and the schema, with its own limits
#define SERIALIZER_DEFINE_LIMITS(Name, prefix, schema_version, FIELDS, size_limit, depth_limit, elements_limit)	\
	static const SerializerField prefix##_fields[] = {														\
		FIELDS(Name, SERIALIZER_FIELD_SCALAR, SERIALIZER_FIELD_ENUM,										\
			SERIALIZER_FIELD_STRING, SERIALIZER_FIELD_ARRAY,												\
			SERIALIZER_FIELD_OBJECT, SERIALIZER_FIELD_OBJECTS)												\
	};																										\
	const SerializerSchema prefix##_schema = {																\
		.name = #Name, .version = schema_version, .size = sizeof (Name),									\
		.fields = prefix##_fields, .n_fields = sizeof (prefix##_fields) / sizeof (SerializerField),			\
		.max_size = size_limit, .max_depth = depth_limit, .max_elements = elements_limit					\
	};

// the table of the fields and the schema, with the default limits
#define SERIALIZER_DEFINE(Name, prefix, schema_version, FIELDS)												\
	SERIALIZER_DEFINE_LIMITS (																				\
		Name, prefix, schema_version, FIELDS,																\
		SERIALIZER_DEFAULT_MAX_SIZE, SERIALIZER_DEFAULT_MAX_DEPTH, SERIALIZER_DEFAULT_MAX_ELEMENTS			\
	)

#pragma endregion

#pragma region codec

// sets every field of the object to its default
CERVER_EXPORT void serializer_object_init (
	const SerializerSchema *schema, void *object
);

// returns the number of bytes of the encoded object, 0 if it can not be encoded
CERVER_EXPORT size_t serializer_encoded_size (
	const SerializerSchema *schema, const void *object
);

// encodes the object in the buffer without allocating memory
// written takes the number of bytes of the encoded object, it can be NULL
CERVER_EXPORT SerializerResult serializer_encode (
	const SerializerSchema *schema, const void *object,
	void *buffer, const size_t size, size_t *written
);

// decodes and validates the object in the buffer, that must have nothing after it,
// the fields that are missing take their default, the unknown fields are ignored
// the arena gives the memory of the arena fields, it can be NULL for a schema without them
// the object is only written when the result is SERIALIZER_RESULT_NONE
CERVER_EXPORT SerializerResult serializer_decode (
	const SerializerSchema *schema,
	const void *buffer, const size_t size,
	Arena *arena, void *object
);

// returns the schemas of the objects that cerver sends, used by the fuzzer
CERVER_PUBLIC const SerializerSchema *const *serializer_schemas (size_t *count);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
