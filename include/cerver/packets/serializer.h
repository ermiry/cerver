/**
 * @file cerver/packets/serializer.h
 * @brief The serializer of the objects of the packets: their schemas and their codec.
 *
 * An object is encoded as a CBOR map (RFC 8949) with the id of each field as its key, so any CBOR library can read
 * it; a peer ignores the fields that it does not know and gives their default to the fields that are missing.
 */

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

/**
 * @defgroup packets_serializer_types Serializer types
 * @ingroup packets
 * @brief The types of the fields and the results of the codec.
 * @{
 */

/**
 * @brief The X-macro table of the types of the fields: the number and the name of each one.
 */
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

/**
 * @brief The type of a field, one `SERIALIZER_TYPE_<name>` for each row of #SERIALIZER_TYPE_MAP.
 */
typedef enum SerializerType {

	#define XX(num, name) SERIALIZER_TYPE_##name = num,
	SERIALIZER_TYPE_MAP (XX)
	#undef XX

} SerializerType;

/**
 * @brief Returns the name of a type.
 *
 * @param[in] type the type
 * @return the name of its row in #SERIALIZER_TYPE_MAP, or the one of SERIALIZER_TYPE_NONE for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *serializer_type_string (
	const SerializerType type
);

/**
 * @brief The X-macro table of the results of the codec: the number, the name and the string of each one.
 */
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

/**
 * @brief The result of the codec, one `SERIALIZER_RESULT_<name>` for each row of #SERIALIZER_RESULT_MAP.
 */
typedef enum SerializerResult {

	#define XX(num, name, string) SERIALIZER_RESULT_##name = num,
	SERIALIZER_RESULT_MAP (XX)
	#undef XX

} SerializerResult;

/**
 * @brief Returns the string of a result.
 *
 * @param[in] result the result
 * @return the string of its row in #SERIALIZER_RESULT_MAP, or the one of SERIALIZER_RESULT_ARGUMENTS for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *serializer_result_string (
	const SerializerResult result
);

/** @} */

/**
 * @defgroup packets_serializer_schema Serializer schema
 * @ingroup packets
 * @brief The schema of an object: its fields and its limits.
 * @{
 */

/** @brief The default biggest encoded object, a schema can lower it. */
#define SERIALIZER_DEFAULT_MAX_SIZE				65536
/** @brief The default deepest nesting of an object, a schema can lower it. */
#define SERIALIZER_DEFAULT_MAX_DEPTH			8
/** @brief The default most elements of an array or a map, a schema can lower it. */
#define SERIALIZER_DEFAULT_MAX_ELEMENTS			1024

/** @brief The fields of a schema: the ids go from 1 to this value. */
#define SERIALIZER_FIELDS_MAX					64

/** @brief The key of the version of the schema in every object. */
#define SERIALIZER_VERSION_KEY					0

/** @brief A field that must be in the object. */
#define SERIALIZER_REQUIRED						true
/** @brief A field that takes its default when it is missing. */
#define SERIALIZER_OPTIONAL						false

/**
 * @brief The value of a field that is missing; every member is set from the default of the field.
 */
typedef struct SerializerDefault {

	/** @brief The default as a signed integer. */
	i64 integer;
	/** @brief The default as an unsigned integer. */
	u64 natural;
	/** @brief The default as a floating point number. */
	double number;

} SerializerDefault;

struct SerializerSchema;

/**
 * @brief A field of a schema, made by the `SERIALIZER_FIELD_*` macros.
 */
typedef struct SerializerField {

	/** @brief The id of the field, its key in the map. */
	u32 id;
	/** @brief The type of the field. */
	SerializerType type;
	/** @brief The name of the member. */
	const char *name;

	/** @brief The position of the member in the struct. */
	size_t offset;
	/** @brief The size of the member, the size of a single element for an array. */
	size_t size;

	/** @brief The capacity of a text (with its NUL), a bytes or an array, the maximum length of an arena text or bytes. */
	size_t capacity;

	/** @brief The position of the u32 member that counts the bytes or the elements, for bytes and arrays. */
	size_t count_offset;

	/** @brief The field must be in the object. */
	bool required;
	/** @brief The value of the field when it is missing. */
	SerializerDefault default_value;

	/** @brief The smallest value that an enum takes. */
	i64 minimum;
	/** @brief The biggest value that an enum takes. */
	i64 maximum;

	/** @brief The type of the elements of an array. */
	SerializerType element_type;

	/** @brief The schema of an object, or of the elements of an array of objects. */
	const struct SerializerSchema *schema;

} SerializerField;

/**
 * @brief The schema of an object, made by SERIALIZER_DEFINE () or SERIALIZER_DEFINE_LIMITS ().
 */
typedef struct SerializerSchema {

	/** @brief The name of the struct. */
	const char *name;

	/** @brief The version: a new field does not change it, a change that breaks the older peers does. */
	u32 version;

	/** @brief The size of the struct of the object. */
	size_t size;

	/** @brief The fields. */
	const SerializerField *fields;
	/** @brief The number of fields. */
	size_t n_fields;

	/** @brief The biggest encoded object. */
	size_t max_size;
	/** @brief The deepest nesting. */
	u32 max_depth;
	/** @brief The most elements of an array or a map. */
	u32 max_elements;

} SerializerSchema;

/** @} */

/**
 * @defgroup packets_serializer_define Serializer definitions
 * @ingroup packets
 * @brief The macros that make the struct and the schema of an object from a single list of fields.
 *
 * A schema is described once with a list of fields, that generates its struct and its table:
 *
 * @code
 * #define CREDENTIALS_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)
 * 	STRING(T, 1, TEXT, username, CREDENTIALS_USERNAME_SIZE, SERIALIZER_REQUIRED)
 * 	SCALAR(T, 2, U32, attempts, SERIALIZER_OPTIONAL, 0)
 * @endcode
 *
 * SERIALIZER_DECLARE (Credentials, credentials, CREDENTIALS_FIELDS) in a header and
 * SERIALIZER_DEFINE (Credentials, credentials, 1, CREDENTIALS_FIELDS) in a source give the struct Credentials and the
 * schema credentials_schema. The rows of the list are:
 *
 * - `SCALAR(T, id, kind, name, required, default)`: BOOL, U8 ... I64, F32, F64
 * - `ENUM(T, id, name, required, default, minimum, maximum)`: an i32 member
 * - `STRING(T, id, kind, name, capacity, required)`: TEXT, BYTES, TEXT_ARENA, BYTES_ARENA
 * - `ARRAY(T, id, kind, name, capacity, required)`: an array of scalars of kind
 * - `OBJECT(T, id, Type, schema, name, required)`: a nested object
 * - `OBJECTS(T, id, Type, schema, name, capacity, required)`: an array of objects
 *
 * Bytes and arrays have a u32 member named `<name>_count` with the number of bytes or elements.
 * @{
 */

/** @brief The C type of a BOOL field. */
#define SERIALIZER_C_TYPE_BOOL					bool
/** @brief The C type of an U8 field. */
#define SERIALIZER_C_TYPE_U8					u8
/** @brief The C type of an U16 field. */
#define SERIALIZER_C_TYPE_U16					u16
/** @brief The C type of an U32 field. */
#define SERIALIZER_C_TYPE_U32					u32
/** @brief The C type of an U64 field. */
#define SERIALIZER_C_TYPE_U64					u64
/** @brief The C type of an I8 field. */
#define SERIALIZER_C_TYPE_I8					i8
/** @brief The C type of an I16 field. */
#define SERIALIZER_C_TYPE_I16					i16
/** @brief The C type of an I32 field. */
#define SERIALIZER_C_TYPE_I32					i32
/** @brief The C type of an I64 field. */
#define SERIALIZER_C_TYPE_I64					i64
/** @brief The C type of a F32 field. */
#define SERIALIZER_C_TYPE_F32					float
/** @brief The C type of a F64 field. */
#define SERIALIZER_C_TYPE_F64					double

/**
 * @brief The member of a TEXT field: a char array with its NUL.
 *
 * @param name the name of the member
 * @param capacity the size of the array
 */
#define SERIALIZER_MEMBER_TEXT(name, capacity)					char name[capacity];
/**
 * @brief The members of a BYTES field: a u8 array and its count.
 *
 * @param name the name of the member
 * @param capacity the size of the array
 */
#define SERIALIZER_MEMBER_BYTES(name, capacity)					u8 name[capacity]; u32 name##_count;
/**
 * @brief The member of a TEXT_ARENA field: a pointer to a text in the arena of the decode.
 *
 * @param name the name of the member
 * @param capacity the maximum length, not used by the member
 */
#define SERIALIZER_MEMBER_TEXT_ARENA(name, capacity)			char *name;
/**
 * @brief The members of a BYTES_ARENA field: a pointer to bytes in the arena of the decode and their count.
 *
 * @param name the name of the member
 * @param capacity the maximum length, not used by the members
 */
#define SERIALIZER_MEMBER_BYTES_ARENA(name, capacity)			u8 *name; u32 name##_count;

/**
 * @brief The position of the count of a TEXT field: it does not have one.
 *
 * @param T the struct
 * @param name the name of the member
 */
#define SERIALIZER_COUNT_OFFSET_TEXT(T, name)					0
/**
 * @brief The position of the count of a BYTES field.
 *
 * @param T the struct
 * @param name the name of the member
 */
#define SERIALIZER_COUNT_OFFSET_BYTES(T, name)					offsetof (T, name##_count)
/**
 * @brief The position of the count of a TEXT_ARENA field: it does not have one.
 *
 * @param T the struct
 * @param name the name of the member
 */
#define SERIALIZER_COUNT_OFFSET_TEXT_ARENA(T, name)				0
/**
 * @brief The position of the count of a BYTES_ARENA field.
 *
 * @param T the struct
 * @param name the name of the member
 */
#define SERIALIZER_COUNT_OFFSET_BYTES_ARENA(T, name)			offsetof (T, name##_count)

/**
 * @brief The member of a SCALAR row in the struct.
 *
 * @param T the struct
 * @param id the id of the field
 * @param kind the type, BOOL, U8 ... I64, F32 or F64
 * @param name the name of the member
 * @param required whether the field is required
 * @param value the default
 */
#define SERIALIZER_STRUCT_SCALAR(T, id, kind, name, required, value)						\
	SERIALIZER_C_TYPE_##kind name;

/**
 * @brief The member of an ENUM row in the struct: an i32.
 *
 * @param T the struct
 * @param id the id of the field
 * @param name the name of the member
 * @param required whether the field is required
 * @param value the default
 * @param minimum the smallest value
 * @param maximum the biggest value
 */
#define SERIALIZER_STRUCT_ENUM(T, id, name, required, value, minimum, maximum)				\
	i32 name;

/**
 * @brief The members of a STRING row in the struct.
 *
 * @param T the struct
 * @param id the id of the field
 * @param kind TEXT, BYTES, TEXT_ARENA or BYTES_ARENA
 * @param name the name of the member
 * @param capacity the capacity
 * @param required whether the field is required
 */
#define SERIALIZER_STRUCT_STRING(T, id, kind, name, capacity, required)					\
	SERIALIZER_MEMBER_##kind(name, capacity)

/**
 * @brief The members of an ARRAY row in the struct: the array and its count.
 *
 * @param T the struct
 * @param id the id of the field
 * @param kind the type of the elements
 * @param name the name of the member
 * @param capacity the size of the array
 * @param required whether the field is required
 */
#define SERIALIZER_STRUCT_ARRAY(T, id, kind, name, capacity, required)						\
	SERIALIZER_C_TYPE_##kind name[capacity]; u32 name##_count;

/**
 * @brief The member of an OBJECT row in the struct.
 *
 * @param T the struct
 * @param id the id of the field
 * @param Type the struct of the nested object
 * @param schema the schema of the nested object
 * @param name the name of the member
 * @param required whether the field is required
 */
#define SERIALIZER_STRUCT_OBJECT(T, id, Type, schema, name, required)						\
	Type name;

/**
 * @brief The members of an OBJECTS row in the struct: the array of objects and its count.
 *
 * @param T the struct
 * @param id the id of the field
 * @param Type the struct of the objects
 * @param schema the schema of the objects
 * @param name the name of the member
 * @param capacity the size of the array
 * @param required whether the field is required
 */
#define SERIALIZER_STRUCT_OBJECTS(T, id, Type, schema, name, capacity, required)			\
	Type name[capacity]; u32 name##_count;

/**
 * @brief The default of a field, in every member of a SerializerDefault.
 *
 * @param value the default
 */
#define SERIALIZER_DEFAULT_VALUE(value)														\
	{ .integer = (i64) (value), .natural = (u64) (value), .number = (double) (value) }

/**
 * @brief The SerializerField of a SCALAR row.
 *
 * @param T the struct
 * @param field_id the id of the field
 * @param kind the type, BOOL, U8 ... I64, F32 or F64
 * @param field_name the name of the member
 * @param field_required whether the field is required
 * @param value the default
 */
#define SERIALIZER_FIELD_SCALAR(T, field_id, kind, field_name, field_required, value) {		\
	.id = field_id, .type = SERIALIZER_TYPE_##kind, .name = #field_name,					\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name),				\
	.capacity = 0, .count_offset = 0,														\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (value),			\
	.minimum = 0, .maximum = 0,																\
	.element_type = SERIALIZER_TYPE_NONE, .schema = NULL									\
},

/**
 * @brief The SerializerField of an ENUM row.
 *
 * @param T the struct
 * @param field_id the id of the field
 * @param field_name the name of the member
 * @param field_required whether the field is required
 * @param value the default
 * @param low the smallest value
 * @param high the biggest value
 */
#define SERIALIZER_FIELD_ENUM(T, field_id, field_name, field_required, value, low, high) {	\
	.id = field_id, .type = SERIALIZER_TYPE_ENUM, .name = #field_name,						\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name),				\
	.capacity = 0, .count_offset = 0,														\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (value),			\
	.minimum = low, .maximum = high,														\
	.element_type = SERIALIZER_TYPE_NONE, .schema = NULL									\
},

/**
 * @brief The SerializerField of a STRING row.
 *
 * @param T the struct
 * @param field_id the id of the field
 * @param kind TEXT, BYTES, TEXT_ARENA or BYTES_ARENA
 * @param field_name the name of the member
 * @param field_capacity the capacity
 * @param field_required whether the field is required
 */
#define SERIALIZER_FIELD_STRING(T, field_id, kind, field_name, field_capacity, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_##kind, .name = #field_name,						\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name),					\
	.capacity = field_capacity,																	\
	.count_offset = SERIALIZER_COUNT_OFFSET_##kind(T, field_name),								\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),					\
	.minimum = 0, .maximum = 0,																	\
	.element_type = SERIALIZER_TYPE_NONE, .schema = NULL										\
},

/**
 * @brief The SerializerField of an ARRAY row.
 *
 * @param T the struct
 * @param field_id the id of the field
 * @param kind the type of the elements
 * @param field_name the name of the member
 * @param field_capacity the size of the array
 * @param field_required whether the field is required
 */
#define SERIALIZER_FIELD_ARRAY(T, field_id, kind, field_name, field_capacity, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_ARRAY, .name = #field_name,							\
	.offset = offsetof (T, field_name), .size = sizeof (((T *) 0)->field_name[0]),				\
	.capacity = field_capacity, .count_offset = offsetof (T, field_name##_count),				\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),					\
	.minimum = 0, .maximum = 0,																	\
	.element_type = SERIALIZER_TYPE_##kind, .schema = NULL										\
},

/**
 * @brief The SerializerField of an OBJECT row.
 *
 * @param T the struct
 * @param field_id the id of the field
 * @param Type the struct of the nested object
 * @param field_schema the schema of the nested object
 * @param field_name the name of the member
 * @param field_required whether the field is required
 */
#define SERIALIZER_FIELD_OBJECT(T, field_id, Type, field_schema, field_name, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_OBJECT, .name = #field_name,						\
	.offset = offsetof (T, field_name), .size = sizeof (Type),									\
	.capacity = 0, .count_offset = 0,															\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),					\
	.minimum = 0, .maximum = 0,																	\
	.element_type = SERIALIZER_TYPE_NONE, .schema = &field_schema								\
},

/**
 * @brief The SerializerField of an OBJECTS row.
 *
 * @param T the struct
 * @param field_id the id of the field
 * @param Type the struct of the objects
 * @param field_schema the schema of the objects
 * @param field_name the name of the member
 * @param field_capacity the size of the array
 * @param field_required whether the field is required
 */
#define SERIALIZER_FIELD_OBJECTS(T, field_id, Type, field_schema, field_name, field_capacity, field_required) {	\
	.id = field_id, .type = SERIALIZER_TYPE_ARRAY, .name = #field_name,											\
	.offset = offsetof (T, field_name), .size = sizeof (Type),													\
	.capacity = field_capacity, .count_offset = offsetof (T, field_name##_count),								\
	.required = field_required, .default_value = SERIALIZER_DEFAULT_VALUE (0),									\
	.minimum = 0, .maximum = 0,																					\
	.element_type = SERIALIZER_TYPE_OBJECT, .schema = &field_schema												\
},

/**
 * @brief The struct of an object and the declaration of its schema, for a header.
 *
 * @param Name the struct
 * @param prefix the prefix of the schema, `<prefix>_schema`
 * @param FIELDS the list of fields
 */
#define SERIALIZER_DECLARE(Name, prefix, FIELDS)											\
	typedef struct Name {																	\
		FIELDS(Name, SERIALIZER_STRUCT_SCALAR, SERIALIZER_STRUCT_ENUM,						\
			SERIALIZER_STRUCT_STRING, SERIALIZER_STRUCT_ARRAY,								\
			SERIALIZER_STRUCT_OBJECT, SERIALIZER_STRUCT_OBJECTS)							\
	} Name;																					\
	CERVER_EXPORT const SerializerSchema prefix##_schema;

/**
 * @brief The table of the fields and the schema of an object, with its own limits, for a source.
 *
 * @param Name the struct
 * @param prefix the prefix of the schema
 * @param schema_version the version of the schema
 * @param FIELDS the list of fields
 * @param size_limit the biggest encoded object
 * @param depth_limit the deepest nesting
 * @param elements_limit the most elements of an array or a map
 */
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

/**
 * @brief The table of the fields and the schema of an object, with the default limits, for a source.
 *
 * @param Name the struct
 * @param prefix the prefix of the schema
 * @param schema_version the version of the schema
 * @param FIELDS the list of fields
 */
#define SERIALIZER_DEFINE(Name, prefix, schema_version, FIELDS)												\
	SERIALIZER_DEFINE_LIMITS (																				\
		Name, prefix, schema_version, FIELDS,																\
		SERIALIZER_DEFAULT_MAX_SIZE, SERIALIZER_DEFAULT_MAX_DEPTH, SERIALIZER_DEFAULT_MAX_ELEMENTS			\
	)

/** @} */

/**
 * @defgroup packets_serializer_codec Serializer codec
 * @ingroup packets
 * @brief Encoding and decoding the objects of a schema.
 * @{
 */

/**
 * @brief Sets every field of an object to its default.
 *
 * @param[in] schema the schema
 * @param[out] object the object
 */
CERVER_EXPORT void serializer_object_init (
	const SerializerSchema *schema, void *object
);

/**
 * @brief Returns the bytes of the encoded object.
 *
 * @param[in] schema the schema
 * @param[in] object the object
 * @return the bytes, 0 for a NULL argument or if the object can not be encoded
 */
CERVER_EXPORT size_t serializer_encoded_size (
	const SerializerSchema *schema, const void *object
);

/**
 * @brief Encodes an object in a buffer, without allocating memory.
 *
 * @param[in] schema the schema
 * @param[in] object the object
 * @param[out] buffer the buffer
 * @param[in] size the size of the buffer
 * @param[out] written the bytes of the encoded object, 0 on error; it can be NULL
 * @return SERIALIZER_RESULT_NONE when it was encoded, SERIALIZER_RESULT_ARGUMENTS for a NULL argument,
 * SERIALIZER_RESULT_BUFFER for a buffer that is too small, or what is wrong with the object
 */
CERVER_EXPORT SerializerResult serializer_encode (
	const SerializerSchema *schema, const void *object,
	void *buffer, const size_t size, size_t *written
);

/**
 * @brief Decodes and validates an object in a buffer, that must have nothing after it.
 *
 * The fields that are missing take their default and the unknown fields are ignored.
 *
 * @param[in] schema the schema
 * @param[in] buffer the encoded object
 * @param[in] size the size of the buffer
 * @param[in,out] arena gives the memory of the arena fields; it can be NULL for a schema without them
 * @param[out] object the object, only written when the result is SERIALIZER_RESULT_NONE
 * @return SERIALIZER_RESULT_NONE when it was decoded, SERIALIZER_RESULT_ARGUMENTS for a NULL argument,
 * SERIALIZER_RESULT_SIZE for a buffer bigger than the max size of the schema, or what is wrong with the object
 */
CERVER_EXPORT SerializerResult serializer_decode (
	const SerializerSchema *schema,
	const void *buffer, const size_t size,
	Arena *arena, void *object
);

/**
 * @brief Returns the schemas of the objects that cerver sends, used by the fuzzer.
 *
 * @param[out] count the number of schemas, it can be NULL
 * @return the static array of the schemas
 */
CERVER_PUBLIC const SerializerSchema *const *serializer_schemas (size_t *count);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
