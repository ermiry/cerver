#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/errors.h"
#include "cerver/files.h"
#include "cerver/serializer.h"
#include "cerver/sessions.h"

#include "cerver/collections/arena.h"

// the major types of CBOR that the serializer uses
#define SERIALIZER_MAJOR_UNSIGNED				0
#define SERIALIZER_MAJOR_NEGATIVE				1
#define SERIALIZER_MAJOR_BYTES					2
#define SERIALIZER_MAJOR_TEXT					3
#define SERIALIZER_MAJOR_ARRAY					4
#define SERIALIZER_MAJOR_MAP					5
#define SERIALIZER_MAJOR_TAG					6
#define SERIALIZER_MAJOR_SIMPLE					7

#define SERIALIZER_MAJOR_SHIFT					5
#define SERIALIZER_INFO_MASK					0x1f

// the additional information of the first byte of an item
#define SERIALIZER_INFO_DIRECT_MAX				23
#define SERIALIZER_INFO_ONE_BYTE				24
#define SERIALIZER_INFO_TWO_BYTES				25
#define SERIALIZER_INFO_FOUR_BYTES				26
#define SERIALIZER_INFO_EIGHT_BYTES				27
#define SERIALIZER_INFO_INDEFINITE				31

#define SERIALIZER_SIMPLE_FALSE					20
#define SERIALIZER_SIMPLE_TRUE					21
#define SERIALIZER_SIMPLE_NULL					22
#define SERIALIZER_SIMPLE_UNDEFINED				23

#define SERIALIZER_ONE_BYTE_MAX					0xffu
#define SERIALIZER_TWO_BYTES_MAX				0xffffu
#define SERIALIZER_FOUR_BYTES_MAX				0xffffffffu

#define SERIALIZER_BITS_PER_BYTE				8

// the items of each element of an array and of each entry of a map
#define SERIALIZER_ARRAY_ITEMS					1
#define SERIALIZER_MAP_ITEMS					2

// the parts of an IEEE 754 single and half
#define SERIALIZER_FLOAT_SIGN_SHIFT				31
#define SERIALIZER_FLOAT_EXPONENT_SHIFT			23
#define SERIALIZER_FLOAT_EXPONENT_MASK			0xffu
#define SERIALIZER_FLOAT_MANTISSA_MASK			0x7fffffu
#define SERIALIZER_FLOAT_IMPLICIT_BIT			0x800000u
#define SERIALIZER_FLOAT_BIAS					127

#define SERIALIZER_HALF_SIGN_SHIFT				15
#define SERIALIZER_HALF_EXPONENT_SHIFT			10
#define SERIALIZER_HALF_EXPONENT_MASK			0x1fu
#define SERIALIZER_HALF_MANTISSA_MASK			0x3ffu
#define SERIALIZER_HALF_BIAS					15
#define SERIALIZER_HALF_EXPONENT_MAX			15
#define SERIALIZER_HALF_EXPONENT_MIN			(-14)
#define SERIALIZER_HALF_SUBNORMAL_MIN			(-24)
#define SERIALIZER_HALF_MANTISSA_DROP			13
#define SERIALIZER_HALF_MANTISSA_IMPLICIT		1024
#define SERIALIZER_HALF_NORMAL_SHIFT			25
#define SERIALIZER_HALF_INFINITY				0x7c00u
#define SERIALIZER_HALF_NAN						0x7e00u

// the bytes of a UTF-8 sequence
#define SERIALIZER_UTF8_CONTINUATION_MASK		0xc0u
#define SERIALIZER_UTF8_CONTINUATION			0x80u
#define SERIALIZER_UTF8_ASCII_MAX				0x7fu
#define SERIALIZER_UTF8_TWO_MIN					0xc2u
#define SERIALIZER_UTF8_TWO_MAX					0xdfu
#define SERIALIZER_UTF8_THREE_MIN				0xe0u
#define SERIALIZER_UTF8_THREE_MAX				0xefu
#define SERIALIZER_UTF8_FOUR_MIN				0xf0u
#define SERIALIZER_UTF8_FOUR_MAX				0xf4u
#define SERIALIZER_UTF8_OVERLONG_THREE			0xe0u
#define SERIALIZER_UTF8_SURROGATES				0xedu
#define SERIALIZER_UTF8_OVERLONG_FOUR			0xf0u
#define SERIALIZER_UTF8_OUT_OF_RANGE			0xf4u
#define SERIALIZER_UTF8_SECOND_LOW_MIN			0xa0u
#define SERIALIZER_UTF8_SECOND_HIGH_MAX			0x9fu
#define SERIALIZER_UTF8_SECOND_FOUR_LOW_MIN		0x90u
#define SERIALIZER_UTF8_SECOND_FOUR_HIGH_MAX	0x8fu
#define SERIALIZER_UTF8_TWO_LENGTH				2
#define SERIALIZER_UTF8_THREE_LENGTH			3
#define SERIALIZER_UTF8_FOUR_LENGTH				4

#pragma region strings

const char *serializer_type_string (const SerializerType type) {

	switch (type) {
		#define XX(num, name) case SERIALIZER_TYPE_##name: return #name;
		SERIALIZER_TYPE_MAP (XX)
		#undef XX
	}

	return serializer_type_string (SERIALIZER_TYPE_NONE);

}

const char *serializer_result_string (const SerializerResult result) {

	switch (result) {
		#define XX(num, name, string) case SERIALIZER_RESULT_##name: return string;
		SERIALIZER_RESULT_MAP (XX)
		#undef XX
	}

	return serializer_result_string (SERIALIZER_RESULT_ARGUMENTS);

}

#pragma endregion

#pragma region members

// the members of a struct are read and written by their size,
// the buffers of the encoded object are never aligned
static u64 serializer_member_read_unsigned (const void *member, const size_t size) {

	u64 value = 0;

	switch (size) {
		case sizeof (u8): { u8 v; (void) memcpy (&v, member, size); value = v; } break;
		case sizeof (u16): { u16 v; (void) memcpy (&v, member, size); value = v; } break;
		case sizeof (u32): { u32 v; (void) memcpy (&v, member, size); value = v; } break;
		case sizeof (u64): { u64 v; (void) memcpy (&v, member, size); value = v; } break;
		default: break;
	}

	return value;

}

static i64 serializer_member_read_signed (const void *member, const size_t size) {

	i64 value = 0;

	switch (size) {
		case sizeof (i8): { i8 v; (void) memcpy (&v, member, size); value = v; } break;
		case sizeof (i16): { i16 v; (void) memcpy (&v, member, size); value = v; } break;
		case sizeof (i32): { i32 v; (void) memcpy (&v, member, size); value = v; } break;
		case sizeof (i64): { i64 v; (void) memcpy (&v, member, size); value = v; } break;
		default: break;
	}

	return value;

}

static void serializer_member_write_unsigned (void *member, const size_t size, const u64 value) {

	switch (size) {
		case sizeof (u8): { u8 v = (u8) value; (void) memcpy (member, &v, size); } break;
		case sizeof (u16): { u16 v = (u16) value; (void) memcpy (member, &v, size); } break;
		case sizeof (u32): { u32 v = (u32) value; (void) memcpy (member, &v, size); } break;
		case sizeof (u64): { u64 v = value; (void) memcpy (member, &v, size); } break;
		default: break;
	}

}

static void serializer_member_write_signed (void *member, const size_t size, const i64 value) {

	switch (size) {
		case sizeof (i8): { i8 v = (i8) value; (void) memcpy (member, &v, size); } break;
		case sizeof (i16): { i16 v = (i16) value; (void) memcpy (member, &v, size); } break;
		case sizeof (i32): { i32 v = (i32) value; (void) memcpy (member, &v, size); } break;
		case sizeof (i64): { i64 v = value; (void) memcpy (member, &v, size); } break;
		default: break;
	}

}

static u32 serializer_member_read_count (const void *object, const SerializerField *field) {

	u32 count = 0;
	(void) memcpy (&count, (const char *) object + field->count_offset, sizeof (u32));

	return count;

}

static void serializer_member_write_count (void *object, const SerializerField *field, const u32 count) {

	(void) memcpy ((char *) object + field->count_offset, &count, sizeof (u32));

}

// the biggest unsigned value that a member of the size takes
static u64 serializer_unsigned_max (const size_t size) {

	return (size >= sizeof (u64)) ? UINT64_MAX
		: ((u64) 1 << (size * SERIALIZER_BITS_PER_BYTE)) - 1;

}

// the biggest signed value that a member of the size takes, the smallest is -max - 1
static u64 serializer_signed_max (const size_t size) {

	return serializer_unsigned_max (size) >> 1;

}

static bool serializer_type_is_unsigned (const SerializerType type) {

	return (type == SERIALIZER_TYPE_U8) || (type == SERIALIZER_TYPE_U16)
		|| (type == SERIALIZER_TYPE_U32) || (type == SERIALIZER_TYPE_U64);

}

static bool serializer_type_is_signed (const SerializerType type) {

	return (type == SERIALIZER_TYPE_I8) || (type == SERIALIZER_TYPE_I16)
		|| (type == SERIALIZER_TYPE_I32) || (type == SERIALIZER_TYPE_I64)
		|| (type == SERIALIZER_TYPE_ENUM);

}

static bool serializer_type_is_float (const SerializerType type) {

	return (type == SERIALIZER_TYPE_F32) || (type == SERIALIZER_TYPE_F64);

}

#pragma endregion

#pragma region floats

static u32 serializer_float_bits (const float value) {

	u32 bits = 0;
	(void) memcpy (&bits, &value, sizeof (u32));

	return bits;

}

static float serializer_float_from_bits (const u32 bits) {

	float value = 0;
	(void) memcpy (&value, &bits, sizeof (float));

	return value;

}

static u64 serializer_double_bits (const double value) {

	u64 bits = 0;
	(void) memcpy (&bits, &value, sizeof (u64));

	return bits;

}

static double serializer_double_from_bits (const u64 bits) {

	double value = 0;
	(void) memcpy (&value, &bits, sizeof (double));

	return value;

}

// a double that a float holds without losing precision
static bool serializer_double_fits_float (const double value) {

	return isnan (value)
		|| (serializer_double_bits ((double) (float) value) == serializer_double_bits (value));

}

// a half that is a subnormal, the bits that are dropped must be zero
static bool serializer_half_subnormal (
	const u32 sign, const int exponent, const u32 mantissa, u16 *half
) {

	bool exact = false;

	const u32 full = SERIALIZER_FLOAT_IMPLICIT_BIT | mantissa;
	const unsigned int shift = (unsigned int) (-exponent - 1);

	if (!(full & ((1u << shift) - 1))) {
		*half = (u16) ((sign << SERIALIZER_HALF_SIGN_SHIFT) | (full >> shift));
		exact = true;
	}

	return exact;

}

// a half that is a normal number, the bits of the mantissa that are dropped must be zero
static bool serializer_half_normal (
	const u32 sign, const int exponent, const u32 mantissa, u16 *half
) {

	bool exact = false;

	if (!(mantissa & ((1u << SERIALIZER_HALF_MANTISSA_DROP) - 1))) {
		*half = (u16) (
			(sign << SERIALIZER_HALF_SIGN_SHIFT)
			| ((u32) (exponent + SERIALIZER_HALF_BIAS) << SERIALIZER_HALF_EXPONENT_SHIFT)
			| (mantissa >> SERIALIZER_HALF_MANTISSA_DROP)
		);

		exact = true;
	}

	return exact;

}

// the half that represents a float exactly
// returns true when there is one
static bool serializer_float_to_half (const float value, u16 *half) {

	bool exact = false;

	const u32 bits = serializer_float_bits (value);
	const u32 sign = bits >> SERIALIZER_FLOAT_SIGN_SHIFT;
	const u32 biased = (bits >> SERIALIZER_FLOAT_EXPONENT_SHIFT) & SERIALIZER_FLOAT_EXPONENT_MASK;
	const u32 mantissa = bits & SERIALIZER_FLOAT_MANTISSA_MASK;
	const int exponent = (int) biased - SERIALIZER_FLOAT_BIAS;

	if (isnan (value)) {
		*half = SERIALIZER_HALF_NAN;
		exact = true;
	}

	else if (isinf (value)) {
		*half = (u16) ((sign << SERIALIZER_HALF_SIGN_SHIFT) | SERIALIZER_HALF_INFINITY);
		exact = true;
	}

	else if (!biased && !mantissa) {
		*half = (u16) (sign << SERIALIZER_HALF_SIGN_SHIFT);
		exact = true;
	}

	else if ((exponent >= SERIALIZER_HALF_EXPONENT_MIN) && (exponent <= SERIALIZER_HALF_EXPONENT_MAX)) {
		exact = serializer_half_normal (sign, exponent, mantissa, half);
	}

	else if ((exponent >= SERIALIZER_HALF_SUBNORMAL_MIN) && (exponent < SERIALIZER_HALF_EXPONENT_MIN)) {
		exact = serializer_half_subnormal (sign, exponent, mantissa, half);
	}

	return exact;

}

static double serializer_half_to_double (const u16 half) {

	const u32 exponent = ((u32) half >> SERIALIZER_HALF_EXPONENT_SHIFT) & SERIALIZER_HALF_EXPONENT_MASK;
	const u32 mantissa = (u32) half & SERIALIZER_HALF_MANTISSA_MASK;

	double value = 0;

	if (!exponent) {
		value = ldexp ((double) mantissa, SERIALIZER_HALF_SUBNORMAL_MIN);
	}

	else if (exponent == SERIALIZER_HALF_EXPONENT_MASK) {
		value = mantissa ? (double) NAN : (double) INFINITY;
	}

	else {
		value = ldexp (
			(double) (mantissa + SERIALIZER_HALF_MANTISSA_IMPLICIT),
			(int) exponent - SERIALIZER_HALF_NORMAL_SHIFT
		);
	}

	return (half >> SERIALIZER_HALF_SIGN_SHIFT) ? -value : value;

}

#pragma endregion

#pragma region text

// the bytes after the first one of a UTF-8 sequence are all continuations,
// and the second one has the range that avoids overlongs, surrogates and values above U+10FFFF
static bool serializer_utf8_second_is_valid (const u8 first, const u8 second) {

	bool valid = true;

	switch (first) {
		case SERIALIZER_UTF8_OVERLONG_THREE: valid = (second >= SERIALIZER_UTF8_SECOND_LOW_MIN); break;
		case SERIALIZER_UTF8_SURROGATES: valid = (second <= SERIALIZER_UTF8_SECOND_HIGH_MAX); break;
		case SERIALIZER_UTF8_OVERLONG_FOUR: valid = (second >= SERIALIZER_UTF8_SECOND_FOUR_LOW_MIN); break;
		case SERIALIZER_UTF8_OUT_OF_RANGE: valid = (second <= SERIALIZER_UTF8_SECOND_FOUR_HIGH_MAX); break;
		default: break;
	}

	return valid;

}

// the length of the sequence that starts with the byte, 0 if it can not start one
static size_t serializer_utf8_length (const u8 first) {

	size_t length = 0;

	if (first <= SERIALIZER_UTF8_ASCII_MAX) length = 1;
	else if ((first >= SERIALIZER_UTF8_TWO_MIN) && (first <= SERIALIZER_UTF8_TWO_MAX)) length = SERIALIZER_UTF8_TWO_LENGTH;
	else if ((first >= SERIALIZER_UTF8_THREE_MIN) && (first <= SERIALIZER_UTF8_THREE_MAX)) length = SERIALIZER_UTF8_THREE_LENGTH;
	else if ((first >= SERIALIZER_UTF8_FOUR_MIN) && (first <= SERIALIZER_UTF8_FOUR_MAX)) length = SERIALIZER_UTF8_FOUR_LENGTH;

	return length;

}

static bool serializer_utf8_sequence_is_valid (const u8 *text, const size_t length) {

	bool valid = serializer_utf8_second_is_valid (text[0], text[1]);

	for (size_t i = 1; valid && (i < length); i++) {
		valid = ((text[i] & SERIALIZER_UTF8_CONTINUATION_MASK) == SERIALIZER_UTF8_CONTINUATION);
	}

	return valid;

}

// a text is valid UTF-8 without NUL characters
static SerializerResult serializer_text_check (const u8 *text, const size_t size) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	size_t i = 0;
	while ((result == SERIALIZER_RESULT_NONE) && (i < size)) {
		const size_t length = serializer_utf8_length (text[i]);

		if (!text[i]) result = SERIALIZER_RESULT_NUL;
		else if (!length || (length > (size - i))) result = SERIALIZER_RESULT_UTF8;
		else if ((length > 1) && !serializer_utf8_sequence_is_valid (text + i, length)) result = SERIALIZER_RESULT_UTF8;
		else i += length;
	}

	return result;

}

#pragma endregion

#pragma region defaults

static void serializer_field_init (const SerializerField *field, char *member);

static void serializer_scalar_init (
	const SerializerField *field, const SerializerType type, char *member
) {

	if (type == SERIALIZER_TYPE_BOOL) {
		const bool value = (field->default_value.integer != 0);
		(void) memcpy (member, &value, sizeof (bool));
	}

	else if (serializer_type_is_unsigned (type)) {
		serializer_member_write_unsigned (member, field->size, field->default_value.natural);
	}

	else if (serializer_type_is_signed (type)) {
		serializer_member_write_signed (member, field->size, field->default_value.integer);
	}

	else if (type == SERIALIZER_TYPE_F32) {
		const float value = (float) field->default_value.number;
		(void) memcpy (member, &value, sizeof (float));
	}

	else if (type == SERIALIZER_TYPE_F64) {
		(void) memcpy (member, &field->default_value.number, sizeof (double));
	}

}

static void serializer_objects_init (const SerializerSchema *schema, char *object) {

	for (size_t i = 0; i < schema->n_fields; i++) {
		const SerializerField *field = &schema->fields[i];
		serializer_field_init (field, object + field->offset);

		if (field->count_offset) {
			serializer_member_write_count (object, field, 0);
		}
	}

}

static void serializer_field_init (const SerializerField *field, char *member) {

	switch (field->type) {
		case SERIALIZER_TYPE_TEXT:
		case SERIALIZER_TYPE_BYTES:
			(void) memset (member, 0, field->size);
			break;

		case SERIALIZER_TYPE_TEXT_ARENA:
		case SERIALIZER_TYPE_BYTES_ARENA:
			(void) memset (member, 0, sizeof (void *));
			break;

		case SERIALIZER_TYPE_ARRAY:
			(void) memset (member, 0, field->size * field->capacity);
			break;

		case SERIALIZER_TYPE_OBJECT:
			serializer_objects_init (field->schema, member);
			break;

		default:
			serializer_scalar_init (field, field->type, member);
			break;
	}

}

// sets every field of the object to its default
void serializer_object_init (
	const SerializerSchema *schema, void *object
) {

	if (schema && object) {
		(void) memset (object, 0, schema->size);
		serializer_objects_init (schema, (char *) object);
	}

}

#pragma endregion

#pragma region encode

// the encoder only counts the bytes when it has no buffer
typedef struct SerializerWriter {

	u8 *buffer;
	size_t size;
	size_t position;

	const SerializerSchema *schema;

} SerializerWriter;

static void serializer_write_bytes (
	SerializerWriter *writer, const void *data, const size_t length
) {

	if (writer->buffer && (length <= (writer->size - writer->position)) && (writer->position <= writer->size)) {
		if (length) (void) memcpy (writer->buffer + writer->position, data, length);
	}

	writer->position += length;

}

static void serializer_write_byte (SerializerWriter *writer, const u8 byte) {

	serializer_write_bytes (writer, &byte, sizeof (u8));

}

// writes the value in network byte order in the bytes that follow the first one
static void serializer_write_big_endian (
	SerializerWriter *writer, const u64 value, const size_t length
) {

	u8 bytes[sizeof (u64)];
	for (size_t i = 0; i < length; i++) {
		bytes[i] = (u8) (value >> ((length - 1 - i) * SERIALIZER_BITS_PER_BYTE));
	}

	serializer_write_bytes (writer, bytes, length);

}

// the first byte of an item and its argument, in the smallest size that takes it
static void serializer_write_head (
	SerializerWriter *writer, const u8 major, const u64 value
) {

	const u8 type = (u8) (major << SERIALIZER_MAJOR_SHIFT);

	if (value <= SERIALIZER_INFO_DIRECT_MAX) {
		serializer_write_byte (writer, (u8) (type | value));
	}

	else if (value <= SERIALIZER_ONE_BYTE_MAX) {
		serializer_write_byte (writer, type | SERIALIZER_INFO_ONE_BYTE);
		serializer_write_big_endian (writer, value, sizeof (u8));
	}

	else if (value <= SERIALIZER_TWO_BYTES_MAX) {
		serializer_write_byte (writer, type | SERIALIZER_INFO_TWO_BYTES);
		serializer_write_big_endian (writer, value, sizeof (u16));
	}

	else if (value <= SERIALIZER_FOUR_BYTES_MAX) {
		serializer_write_byte (writer, type | SERIALIZER_INFO_FOUR_BYTES);
		serializer_write_big_endian (writer, value, sizeof (u32));
	}

	else {
		serializer_write_byte (writer, type | SERIALIZER_INFO_EIGHT_BYTES);
		serializer_write_big_endian (writer, value, sizeof (u64));
	}

}

static void serializer_write_signed (SerializerWriter *writer, const i64 value) {

	if (value >= 0) serializer_write_head (writer, SERIALIZER_MAJOR_UNSIGNED, (u64) value);
	else serializer_write_head (writer, SERIALIZER_MAJOR_NEGATIVE, (u64) (-(value + 1)));

}

static void serializer_write_simple_float (
	SerializerWriter *writer, const u8 info, const u64 bits, const size_t length
) {

	serializer_write_byte (writer, (u8) ((SERIALIZER_MAJOR_SIMPLE << SERIALIZER_MAJOR_SHIFT) | info));
	serializer_write_big_endian (writer, bits, length);

}

// a float in the shortest of half and single that keeps its value
static void serializer_write_float (SerializerWriter *writer, const float value) {

	u16 half = 0;

	if (serializer_float_to_half (value, &half)) {
		serializer_write_simple_float (writer, SERIALIZER_INFO_TWO_BYTES, half, sizeof (u16));
	}

	else {
		serializer_write_simple_float (writer, SERIALIZER_INFO_FOUR_BYTES, serializer_float_bits (value), sizeof (u32));
	}

}

// a double in the shortest of half, single and double that keeps its value
static void serializer_write_double (SerializerWriter *writer, const double value) {

	if (serializer_double_fits_float (value)) {
		serializer_write_float (writer, (float) value);
	}

	else {
		serializer_write_simple_float (writer, SERIALIZER_INFO_EIGHT_BYTES, serializer_double_bits (value), sizeof (u64));
	}

}

static SerializerResult serializer_encode_object (
	SerializerWriter *writer, const SerializerSchema *schema,
	const char *object, const u32 depth
);

static SerializerResult serializer_encode_signed (
	SerializerWriter *writer, const SerializerField *field,
	const SerializerType type, const char *member
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	const i64 value = serializer_member_read_signed (member, field->size);

	if ((type == SERIALIZER_TYPE_ENUM) && ((value < field->minimum) || (value > field->maximum))) {
		result = SERIALIZER_RESULT_RANGE;
	}

	else {
		serializer_write_signed (writer, value);
	}

	return result;

}

static SerializerResult serializer_encode_scalar (
	SerializerWriter *writer, const SerializerField *field,
	const SerializerType type, const char *member
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	if (type == SERIALIZER_TYPE_BOOL) {
		bool value = false;
		(void) memcpy (&value, member, sizeof (bool));

		serializer_write_byte (writer, (u8) (
			(SERIALIZER_MAJOR_SIMPLE << SERIALIZER_MAJOR_SHIFT)
			| (value ? SERIALIZER_SIMPLE_TRUE : SERIALIZER_SIMPLE_FALSE)
		));
	}

	else if (serializer_type_is_unsigned (type)) {
		serializer_write_head (
			writer, SERIALIZER_MAJOR_UNSIGNED,
			serializer_member_read_unsigned (member, field->size)
		);
	}

	else if (serializer_type_is_signed (type)) {
		result = serializer_encode_signed (writer, field, type, member);
	}

	else if (type == SERIALIZER_TYPE_F32) {
		float value = 0;
		(void) memcpy (&value, member, sizeof (float));
		serializer_write_float (writer, value);
	}

	else if (type == SERIALIZER_TYPE_F64) {
		double value = 0;
		(void) memcpy (&value, member, sizeof (double));
		serializer_write_double (writer, value);
	}

	else {
		result = SERIALIZER_RESULT_ARGUMENTS;
	}

	return result;

}

static SerializerResult serializer_encode_text_bytes (
	SerializerWriter *writer, const char *text, const size_t length
) {

	const SerializerResult result = serializer_text_check ((const u8 *) text, length);

	if (result == SERIALIZER_RESULT_NONE) {
		serializer_write_head (writer, SERIALIZER_MAJOR_TEXT, length);
		serializer_write_bytes (writer, text, length);
	}

	return result;

}

// a text must end with a NUL inside its member
static SerializerResult serializer_encode_text (
	SerializerWriter *writer, const SerializerField *field, const char *member
) {

	const size_t length = strnlen (member, field->size);

	return (length < field->size)
		? serializer_encode_text_bytes (writer, member, length)
		: SERIALIZER_RESULT_LENGTH;

}

static SerializerResult serializer_encode_text_arena (
	SerializerWriter *writer, const SerializerField *field, const char *member
) {

	const char *text = NULL;
	(void) memcpy (&text, member, sizeof (char *));

	const size_t length = text ? strnlen (text, field->capacity + 1) : 0;

	return (length <= field->capacity)
		? serializer_encode_text_bytes (writer, text, length)
		: SERIALIZER_RESULT_LENGTH;

}

static SerializerResult serializer_encode_bytes (
	SerializerWriter *writer, const SerializerField *field,
	const char *object, const char *member
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	const u32 count = serializer_member_read_count (object, field);

	const u8 *bytes = (const u8 *) member;
	if (field->type == SERIALIZER_TYPE_BYTES_ARENA) {
		(void) memcpy (&bytes, member, sizeof (u8 *));
	}

	if (count > field->capacity) result = SERIALIZER_RESULT_LENGTH;
	else if (count && !bytes) result = SERIALIZER_RESULT_ARGUMENTS;
	else {
		serializer_write_head (writer, SERIALIZER_MAJOR_BYTES, count);
		serializer_write_bytes (writer, bytes, count);
	}

	return result;

}

static SerializerResult serializer_encode_array (
	SerializerWriter *writer, const SerializerField *field,
	const char *object, const char *member, const u32 depth
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	const u32 count = serializer_member_read_count (object, field);

	if (count > field->capacity) result = SERIALIZER_RESULT_LENGTH;
	else if (count > writer->schema->max_elements) result = SERIALIZER_RESULT_ELEMENTS;
	else {
		serializer_write_head (writer, SERIALIZER_MAJOR_ARRAY, count);

		for (u32 i = 0; (i < count) && (result == SERIALIZER_RESULT_NONE); i++) {
			const char *element = member + ((size_t) i * field->size);

			result = (field->element_type == SERIALIZER_TYPE_OBJECT)
				? serializer_encode_object (writer, field->schema, element, depth + 1)
				: serializer_encode_scalar (writer, field, field->element_type, element);
		}
	}

	return result;

}

static SerializerResult serializer_encode_field (
	SerializerWriter *writer, const SerializerField *field,
	const char *object, const u32 depth
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	const char *member = object + field->offset;

	switch (field->type) {
		case SERIALIZER_TYPE_TEXT: result = serializer_encode_text (writer, field, member); break;
		case SERIALIZER_TYPE_TEXT_ARENA: result = serializer_encode_text_arena (writer, field, member); break;

		case SERIALIZER_TYPE_BYTES:
		case SERIALIZER_TYPE_BYTES_ARENA:
			result = serializer_encode_bytes (writer, field, object, member);
			break;

		case SERIALIZER_TYPE_ARRAY: result = serializer_encode_array (writer, field, object, member, depth); break;

		case SERIALIZER_TYPE_OBJECT:
			result = serializer_encode_object (writer, field->schema, member, depth + 1);
			break;

		default: result = serializer_encode_scalar (writer, field, field->type, member); break;
	}

	return result;

}

// every object is a map with its version and all of its fields, in the order of the schema
static SerializerResult serializer_encode_object (
	SerializerWriter *writer, const SerializerSchema *schema,
	const char *object, const u32 depth
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	if (depth > writer->schema->max_depth) result = SERIALIZER_RESULT_DEPTH;
	else if (!schema || (schema->n_fields > SERIALIZER_FIELDS_MAX)) result = SERIALIZER_RESULT_ARGUMENTS;
	else {
		serializer_write_head (writer, SERIALIZER_MAJOR_MAP, (u64) schema->n_fields + 1);

		serializer_write_head (writer, SERIALIZER_MAJOR_UNSIGNED, SERIALIZER_VERSION_KEY);
		serializer_write_head (writer, SERIALIZER_MAJOR_UNSIGNED, schema->version);

		for (size_t i = 0; (i < schema->n_fields) && (result == SERIALIZER_RESULT_NONE); i++) {
			serializer_write_head (writer, SERIALIZER_MAJOR_UNSIGNED, schema->fields[i].id);
			result = serializer_encode_field (writer, &schema->fields[i], object, depth);
		}
	}

	return result;

}

static SerializerResult serializer_encode_internal (
	const SerializerSchema *schema, const void *object,
	SerializerWriter *writer
) {

	SerializerResult result = serializer_encode_object (writer, schema, (const char *) object, 1);

	if ((result == SERIALIZER_RESULT_NONE) && (writer->position > schema->max_size)) {
		result = SERIALIZER_RESULT_SIZE;
	}

	return result;

}

// returns the number of bytes of the encoded object, 0 if it can not be encoded
size_t serializer_encoded_size (
	const SerializerSchema *schema, const void *object
) {

	size_t size = 0;

	if (schema && object) {
		SerializerWriter writer = { NULL, 0, 0, schema };

		if (serializer_encode_internal (schema, object, &writer) == SERIALIZER_RESULT_NONE) {
			size = writer.position;
		}
	}

	return size;

}

// encodes the object in the buffer without allocating memory
// written takes the number of bytes of the encoded object, it can be NULL
SerializerResult serializer_encode (
	const SerializerSchema *schema, const void *object,
	void *buffer, const size_t size, size_t *written
) {

	SerializerResult result = SERIALIZER_RESULT_ARGUMENTS;

	if (schema && object && buffer) {
		SerializerWriter writer = { (u8 *) buffer, size, 0, schema };

		result = serializer_encode_internal (schema, object, &writer);

		if ((result == SERIALIZER_RESULT_NONE) && (writer.position > size)) {
			result = SERIALIZER_RESULT_BUFFER;
		}

		if (written) *written = (result == SERIALIZER_RESULT_NONE) ? writer.position : 0;
	}

	return result;

}

#pragma endregion

#pragma region decode

// the decoder reads the buffer twice: the first pass validates everything
// and counts the memory of the arena fields without writing, the second one writes the object,
// so a buffer that is not valid never changes the object
typedef struct SerializerReader {

	const u8 *data;
	size_t size;
	size_t position;

	const SerializerSchema *schema;

	bool writing;

	// the memory of the arena fields, counted by the first pass and taken by the second one
	size_t arena_size;
	char *arena_memory;
	size_t arena_used;

} SerializerReader;

typedef struct SerializerHead {

	u8 major;
	u8 info;
	u64 value;

} SerializerHead;

static SerializerResult serializer_read_big_endian (
	SerializerReader *reader, const size_t length, u64 *value
) {

	SerializerResult result = SERIALIZER_RESULT_TRUNCATED;

	if (length <= (reader->size - reader->position)) {
		*value = 0;
		for (size_t i = 0; i < length; i++) {
			*value = (*value << SERIALIZER_BITS_PER_BYTE) | reader->data[reader->position + i];
		}

		reader->position += length;
		result = SERIALIZER_RESULT_NONE;
	}

	return result;

}

// the bytes of the argument of an item, 0 when it has no argument
static size_t serializer_head_argument_length (const u8 info) {

	size_t length = 0;

	switch (info) {
		case SERIALIZER_INFO_ONE_BYTE: length = sizeof (u8); break;
		case SERIALIZER_INFO_TWO_BYTES: length = sizeof (u16); break;
		case SERIALIZER_INFO_FOUR_BYTES: length = sizeof (u32); break;
		case SERIALIZER_INFO_EIGHT_BYTES: length = sizeof (u64); break;
		default: break;
	}

	return length;

}

// reads the first byte of an item and its argument,
// tags, indefinite lengths and the reserved values are not supported
static SerializerResult serializer_read_head (SerializerReader *reader, SerializerHead *head) {

	SerializerResult result = SERIALIZER_RESULT_TRUNCATED;

	if (reader->position < reader->size) {
		const u8 byte = reader->data[reader->position++];

		head->major = (u8) (byte >> SERIALIZER_MAJOR_SHIFT);
		head->info = (u8) (byte & SERIALIZER_INFO_MASK);
		head->value = head->info;

		const size_t length = serializer_head_argument_length (head->info);

		if (head->major == SERIALIZER_MAJOR_TAG) result = SERIALIZER_RESULT_UNSUPPORTED;
		else if (length) result = serializer_read_big_endian (reader, length, &head->value);
		else if (head->info > SERIALIZER_INFO_DIRECT_MAX) result = SERIALIZER_RESULT_UNSUPPORTED;
		else result = SERIALIZER_RESULT_NONE;
	}

	return result;

}

static SerializerResult serializer_read_head_major (
	SerializerReader *reader, const u8 major, SerializerHead *head
) {

	SerializerResult result = serializer_read_head (reader, head);

	if ((result == SERIALIZER_RESULT_NONE) && (head->major != major)) {
		result = SERIALIZER_RESULT_TYPE;
	}

	return result;

}

// the bytes of a text or bytes item must be in the buffer
static SerializerResult serializer_read_payload (
	SerializerReader *reader, const u64 length, const u8 **payload
) {

	SerializerResult result = SERIALIZER_RESULT_TRUNCATED;

	if (length <= (u64) (reader->size - reader->position)) {
		*payload = reader->data + reader->position;
		reader->position += (size_t) length;
		result = SERIALIZER_RESULT_NONE;
	}

	return result;

}

static SerializerResult serializer_skip_item (SerializerReader *reader, const u32 depth);

// the elements of an array have one item each, the entries of a map have two
static SerializerResult serializer_skip_items (
	SerializerReader *reader, const u64 count, const u64 items_per_element, const u32 depth
) {

	SerializerResult result = (count > reader->schema->max_elements)
		? SERIALIZER_RESULT_ELEMENTS : SERIALIZER_RESULT_NONE;

	for (u64 i = 0; (i < (count * items_per_element)) && (result == SERIALIZER_RESULT_NONE); i++) {
		result = serializer_skip_item (reader, depth);
	}

	return result;

}

// a simple value can be false, true, null, undefined or a float
static SerializerResult serializer_skip_simple (const SerializerHead *head) {

	return (
		((head->info >= SERIALIZER_SIMPLE_FALSE) && (head->info <= SERIALIZER_SIMPLE_UNDEFINED))
		|| (head->info >= SERIALIZER_INFO_TWO_BYTES)
	) ? SERIALIZER_RESULT_NONE : SERIALIZER_RESULT_UNSUPPORTED;

}

// the value of a field that the schema does not know, its structure is validated
static SerializerResult serializer_skip_item (SerializerReader *reader, const u32 depth) {

	SerializerHead head = { 0 };
	SerializerResult result = (depth > reader->schema->max_depth)
		? SERIALIZER_RESULT_DEPTH : serializer_read_head (reader, &head);

	const u8 *payload = NULL;

	if (result == SERIALIZER_RESULT_NONE) {
		switch (head.major) {
			case SERIALIZER_MAJOR_BYTES:
			case SERIALIZER_MAJOR_TEXT:
				result = serializer_read_payload (reader, head.value, &payload);
				break;

			case SERIALIZER_MAJOR_ARRAY:
				result = serializer_skip_items (reader, head.value, SERIALIZER_ARRAY_ITEMS, depth + 1);
				break;

			case SERIALIZER_MAJOR_MAP:
				result = serializer_skip_items (reader, head.value, SERIALIZER_MAP_ITEMS, depth + 1);
				break;

			case SERIALIZER_MAJOR_SIMPLE: result = serializer_skip_simple (&head); break;

			default: break;
		}
	}

	return result;

}

static SerializerResult serializer_decode_unsigned (
	SerializerReader *reader, const size_t size, char *member
) {

	SerializerHead head = { 0 };
	SerializerResult result = serializer_read_head_major (reader, SERIALIZER_MAJOR_UNSIGNED, &head);

	if ((result == SERIALIZER_RESULT_NONE) && (head.value > serializer_unsigned_max (size))) {
		result = SERIALIZER_RESULT_RANGE;
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		serializer_member_write_unsigned (member, size, head.value);
	}

	return result;

}

// a negative item n is -1 - n, it fits when n is not bigger than the biggest positive value
static SerializerResult serializer_decode_signed_value (
	const SerializerHead *head, const size_t size, i64 *value
) {

	SerializerResult result = SERIALIZER_RESULT_TYPE;

	if ((head->major == SERIALIZER_MAJOR_UNSIGNED) || (head->major == SERIALIZER_MAJOR_NEGATIVE)) {
		result = (head->value > serializer_signed_max (size))
			? SERIALIZER_RESULT_RANGE : SERIALIZER_RESULT_NONE;
	}

	if (result == SERIALIZER_RESULT_NONE) {
		*value = (head->major == SERIALIZER_MAJOR_UNSIGNED)
			? (i64) head->value : (-1 - (i64) head->value);
	}

	return result;

}

static SerializerResult serializer_decode_signed (
	SerializerReader *reader, const SerializerField *field,
	const SerializerType type, char *member
) {

	SerializerHead head = { 0 };
	i64 value = 0;

	SerializerResult result = serializer_read_head (reader, &head);
	if (result == SERIALIZER_RESULT_NONE) {
		result = serializer_decode_signed_value (&head, field->size, &value);
	}

	if (
		(result == SERIALIZER_RESULT_NONE) && (type == SERIALIZER_TYPE_ENUM)
		&& ((value < field->minimum) || (value > field->maximum))
	) {
		result = SERIALIZER_RESULT_RANGE;
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		serializer_member_write_signed (member, field->size, value);
	}

	return result;

}

static SerializerResult serializer_decode_bool (SerializerReader *reader, char *member) {

	SerializerHead head = { 0 };
	SerializerResult result = serializer_read_head_major (reader, SERIALIZER_MAJOR_SIMPLE, &head);

	if (
		(result == SERIALIZER_RESULT_NONE)
		&& (head.info != SERIALIZER_SIMPLE_FALSE) && (head.info != SERIALIZER_SIMPLE_TRUE)
	) {
		result = SERIALIZER_RESULT_TYPE;
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		const bool value = (head.info == SERIALIZER_SIMPLE_TRUE);
		(void) memcpy (member, &value, sizeof (bool));
	}

	return result;

}

// the value of a half, a single or a double
static SerializerResult serializer_decode_float_value (
	const SerializerHead *head, double *value
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	switch (head->info) {
		case SERIALIZER_INFO_TWO_BYTES: *value = serializer_half_to_double ((u16) head->value); break;
		case SERIALIZER_INFO_FOUR_BYTES: *value = (double) serializer_float_from_bits ((u32) head->value); break;
		case SERIALIZER_INFO_EIGHT_BYTES: *value = serializer_double_from_bits (head->value); break;
		default: result = SERIALIZER_RESULT_TYPE; break;
	}

	return result;

}

// a float field takes a double only when it keeps its value
static SerializerResult serializer_decode_float (
	SerializerReader *reader, const SerializerType type, char *member
) {

	SerializerHead head = { 0 };
	double value = 0;

	SerializerResult result = serializer_read_head_major (reader, SERIALIZER_MAJOR_SIMPLE, &head);
	if (result == SERIALIZER_RESULT_NONE) result = serializer_decode_float_value (&head, &value);

	if (
		(result == SERIALIZER_RESULT_NONE) && (type == SERIALIZER_TYPE_F32)
		&& !serializer_double_fits_float (value)
	) {
		result = SERIALIZER_RESULT_RANGE;
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		if (type == SERIALIZER_TYPE_F32) {
			const float single = (float) value;
			(void) memcpy (member, &single, sizeof (float));
		}

		else {
			(void) memcpy (member, &value, sizeof (double));
		}
	}

	return result;

}

static SerializerResult serializer_decode_scalar (
	SerializerReader *reader, const SerializerField *field,
	const SerializerType type, char *member
) {

	SerializerResult result = SERIALIZER_RESULT_ARGUMENTS;

	if (type == SERIALIZER_TYPE_BOOL) result = serializer_decode_bool (reader, member);
	else if (serializer_type_is_unsigned (type)) result = serializer_decode_unsigned (reader, field->size, member);
	else if (serializer_type_is_signed (type)) result = serializer_decode_signed (reader, field, type, member);
	else if (serializer_type_is_float (type)) result = serializer_decode_float (reader, type, member);

	return result;

}

// reads a text or bytes item that is not longer than maximum
static SerializerResult serializer_decode_string_payload (
	SerializerReader *reader, const u8 major, const u64 maximum,
	const u8 **payload, size_t *length
) {

	SerializerHead head = { 0 };
	SerializerResult result = serializer_read_head_major (reader, major, &head);

	if ((result == SERIALIZER_RESULT_NONE) && (head.value > maximum)) result = SERIALIZER_RESULT_LENGTH;
	if (result == SERIALIZER_RESULT_NONE) result = serializer_read_payload (reader, head.value, payload);

	if ((result == SERIALIZER_RESULT_NONE) && (major == SERIALIZER_MAJOR_TEXT)) {
		result = serializer_text_check (*payload, (size_t) head.value);
	}

	*length = (size_t) head.value;

	return result;

}

// a text in its member, ended with a NUL
static SerializerResult serializer_decode_text (
	SerializerReader *reader, const SerializerField *field, char *member
) {

	const u8 *payload = NULL;
	size_t length = 0;

	const SerializerResult result = serializer_decode_string_payload (
		reader, SERIALIZER_MAJOR_TEXT, (u64) field->size - 1, &payload, &length
	);

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		(void) memset (member, 0, field->size);
		(void) memcpy (member, payload, length);
	}

	return result;

}

static SerializerResult serializer_decode_bytes (
	SerializerReader *reader, const SerializerField *field,
	char *object, char *member
) {

	const u8 *payload = NULL;
	size_t length = 0;

	const SerializerResult result = serializer_decode_string_payload (
		reader, SERIALIZER_MAJOR_BYTES, field->capacity, &payload, &length
	);

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		(void) memset (member, 0, field->size);
		(void) memcpy (member, payload, length);
		serializer_member_write_count (object, field, (u32) length);
	}

	return result;

}

// the first pass counts the memory, the second one takes it from the block of the arena
static char *serializer_arena_take (SerializerReader *reader, const size_t size) {

	char *memory = NULL;

	if (reader->writing) {
		memory = reader->arena_memory + reader->arena_used;
		reader->arena_used += size;
	}

	else {
		reader->arena_size += size;
	}

	return memory;

}

// an arena text or bytes points to its copy in the arena, an empty one is NULL
static SerializerResult serializer_decode_arena (
	SerializerReader *reader, const SerializerField *field,
	char *object, char *member
) {

	const bool text = (field->type == SERIALIZER_TYPE_TEXT_ARENA);

	const u8 *payload = NULL;
	size_t length = 0;

	const SerializerResult result = serializer_decode_string_payload (
		reader, text ? SERIALIZER_MAJOR_TEXT : SERIALIZER_MAJOR_BYTES,
		field->capacity, &payload, &length
	);

	if ((result == SERIALIZER_RESULT_NONE) && length) {
		char *copy = serializer_arena_take (reader, text ? length + 1 : length);

		if (copy) {
			(void) memcpy (copy, payload, length);
			if (text) copy[length] = '\0';
		}

		if (reader->writing) (void) memcpy (member, &copy, sizeof (char *));
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing && !text) {
		serializer_member_write_count (object, field, (u32) length);
	}

	return result;

}

static SerializerResult serializer_decode_object (
	SerializerReader *reader, const SerializerSchema *schema,
	char *object, const u32 depth
);

static SerializerResult serializer_decode_array (
	SerializerReader *reader, const SerializerField *field,
	char *object, char *member, const u32 depth
) {

	SerializerHead head = { 0 };
	SerializerResult result = serializer_read_head_major (reader, SERIALIZER_MAJOR_ARRAY, &head);

	if ((result == SERIALIZER_RESULT_NONE) && (head.value > field->capacity)) result = SERIALIZER_RESULT_LENGTH;
	else if ((result == SERIALIZER_RESULT_NONE) && (head.value > reader->schema->max_elements)) result = SERIALIZER_RESULT_ELEMENTS;

	for (u64 i = 0; (i < head.value) && (result == SERIALIZER_RESULT_NONE); i++) {
		// the first pass has no object
		char *element = member ? member + ((size_t) i * field->size) : NULL;

		result = (field->element_type == SERIALIZER_TYPE_OBJECT)
			? serializer_decode_object (reader, field->schema, element, depth + 1)
			: serializer_decode_scalar (reader, field, field->element_type, element);
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		serializer_member_write_count (object, field, (u32) head.value);
	}

	return result;

}

static SerializerResult serializer_decode_field (
	SerializerReader *reader, const SerializerField *field,
	char *object, const u32 depth
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	char *member = object ? object + field->offset : NULL;

	switch (field->type) {
		case SERIALIZER_TYPE_TEXT: result = serializer_decode_text (reader, field, member); break;
		case SERIALIZER_TYPE_BYTES: result = serializer_decode_bytes (reader, field, object, member); break;

		case SERIALIZER_TYPE_TEXT_ARENA:
		case SERIALIZER_TYPE_BYTES_ARENA:
			result = serializer_decode_arena (reader, field, object, member);
			break;

		case SERIALIZER_TYPE_ARRAY: result = serializer_decode_array (reader, field, object, member, depth); break;

		case SERIALIZER_TYPE_OBJECT:
			result = serializer_decode_object (reader, field->schema, member, depth + 1);
			break;

		default: result = serializer_decode_scalar (reader, field, field->type, member); break;
	}

	return result;

}

static const SerializerField *serializer_schema_field (
	const SerializerSchema *schema, const u64 id, size_t *index
) {

	const SerializerField *field = NULL;

	for (size_t i = 0; (i < schema->n_fields) && !field; i++) {
		if (schema->fields[i].id == id) {
			field = &schema->fields[i];
			*index = i;
		}
	}

	return field;

}

// the version of the schema, a different one breaks the object
static SerializerResult serializer_decode_version (
	SerializerReader *reader, const SerializerSchema *schema
) {

	SerializerHead head = { 0 };
	SerializerResult result = serializer_read_head_major (reader, SERIALIZER_MAJOR_UNSIGNED, &head);

	if ((result == SERIALIZER_RESULT_NONE) && (head.value != schema->version)) {
		result = SERIALIZER_RESULT_VERSION;
	}

	return result;

}

// the fields that were seen in an object, to find the duplicates and the missing ones
typedef struct SerializerPresence {

	u64 fields;
	bool version;

} SerializerPresence;

static SerializerResult serializer_decode_entry (
	SerializerReader *reader, const SerializerSchema *schema,
	char *object, const u32 depth, SerializerPresence *presence
) {

	SerializerHead key = { 0 };
	SerializerResult result = serializer_read_head (reader, &key);

	size_t index = 0;
	const SerializerField *field = NULL;

	if ((result == SERIALIZER_RESULT_NONE) && (key.major != SERIALIZER_MAJOR_UNSIGNED)) result = SERIALIZER_RESULT_KEY;

	else if ((result == SERIALIZER_RESULT_NONE) && (key.value == SERIALIZER_VERSION_KEY)) {
		result = presence->version ? SERIALIZER_RESULT_DUPLICATE : serializer_decode_version (reader, schema);
		presence->version = true;
	}

	else if (result == SERIALIZER_RESULT_NONE) {
		field = serializer_schema_field (schema, key.value, &index);

		if (!field) result = serializer_skip_item (reader, depth + 1);
		else if (presence->fields & ((u64) 1 << index)) result = SERIALIZER_RESULT_DUPLICATE;
		else {
			presence->fields |= ((u64) 1 << index);
			result = serializer_decode_field (reader, field, object, depth);
		}
	}

	return result;

}

static SerializerResult serializer_decode_missing (
	const SerializerSchema *schema, const SerializerPresence *presence
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	for (size_t i = 0; (i < schema->n_fields) && (result == SERIALIZER_RESULT_NONE); i++) {
		if (schema->fields[i].required && !(presence->fields & ((u64) 1 << i))) {
			result = SERIALIZER_RESULT_MISSING;
		}
	}

	return result;

}

static SerializerResult serializer_decode_object (
	SerializerReader *reader, const SerializerSchema *schema,
	char *object, const u32 depth
) {

	SerializerHead head = { 0 };
	SerializerResult result = SERIALIZER_RESULT_NONE;

	if (depth > reader->schema->max_depth) result = SERIALIZER_RESULT_DEPTH;
	else if (!schema || (schema->n_fields > SERIALIZER_FIELDS_MAX)) result = SERIALIZER_RESULT_ARGUMENTS;
	else result = serializer_read_head_major (reader, SERIALIZER_MAJOR_MAP, &head);

	if ((result == SERIALIZER_RESULT_NONE) && (head.value > reader->schema->max_elements)) {
		result = SERIALIZER_RESULT_ELEMENTS;
	}

	if ((result == SERIALIZER_RESULT_NONE) && reader->writing) {
		serializer_objects_init (schema, object);
	}

	SerializerPresence presence = { 0, false };
	for (u64 i = 0; (i < head.value) && (result == SERIALIZER_RESULT_NONE); i++) {
		result = serializer_decode_entry (reader, schema, object, depth, &presence);
	}

	if (result == SERIALIZER_RESULT_NONE) result = serializer_decode_missing (schema, &presence);

	return result;

}

static SerializerResult serializer_decode_pass (
	SerializerReader *reader, const SerializerSchema *schema, char *object
) {

	reader->position = 0;

	SerializerResult result = serializer_decode_object (reader, schema, object, 1);

	if ((result == SERIALIZER_RESULT_NONE) && (reader->position != reader->size)) {
		result = SERIALIZER_RESULT_TRAILING;
	}

	return result;

}

// the second pass takes all the memory of the arena fields in a single block
static SerializerResult serializer_decode_write (
	SerializerReader *reader, const SerializerSchema *schema,
	Arena *arena, char *object
) {

	SerializerResult result = SERIALIZER_RESULT_NONE;

	if (reader->arena_size) {
		reader->arena_memory = arena ? (char *) arena_alloc (arena, reader->arena_size) : NULL;
		if (!reader->arena_memory) result = SERIALIZER_RESULT_ARENA;
	}

	if (result == SERIALIZER_RESULT_NONE) {
		reader->writing = true;
		result = serializer_decode_pass (reader, schema, object);
	}

	return result;

}

// decodes and validates the object in the buffer, that must have nothing after it,
// the fields that are missing take their default, the unknown fields are ignored
// the arena gives the memory of the arena fields, it can be NULL for a schema without them
// the object is only written when the result is SERIALIZER_RESULT_NONE
SerializerResult serializer_decode (
	const SerializerSchema *schema,
	const void *buffer, const size_t size,
	Arena *arena, void *object
) {

	SerializerResult result = SERIALIZER_RESULT_ARGUMENTS;

	if (schema && buffer && object) {
		SerializerReader reader = {
			.data = (const u8 *) buffer, .size = size, .position = 0,
			.schema = schema,
			.writing = false,
			.arena_size = 0, .arena_memory = NULL, .arena_used = 0
		};

		result = (size > schema->max_size)
			? SERIALIZER_RESULT_SIZE
			: serializer_decode_pass (&reader, schema, NULL);

		if (result == SERIALIZER_RESULT_NONE) {
			result = serializer_decode_write (&reader, schema, arena, (char *) object);
		}
	}

	return result;

}

#pragma endregion

#pragma region schemas

static const SerializerSchema *const serializer_cerver_schemas[] = {
	&cerver_summary_schema,
	&error_message_schema,
	&file_header_schema,
	&session_token_schema
};

// returns the schemas of the objects that cerver sends, used by the fuzzer
const SerializerSchema *const *serializer_schemas (size_t *count) {

	if (count) *count = sizeof (serializer_cerver_schemas) / sizeof (SerializerSchema *);

	return serializer_cerver_schemas;

}

#pragma endregion
