/**
 * @internal
 * @file cerver/json/internal.h
 * @brief What the files of the JSON of cerver share: the layout of a value and of a document, the cursor of a
 * reader and the methods of the strings and of the numbers.
 */

#ifndef _CERVER_JSON_INTERNAL_H_
#define _CERVER_JSON_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#include "cerver/json/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup json_internal Internal
 * @ingroup json
 * @brief The layout of the values and the methods that the readers and the writer share.
 * @{
 */

/** @brief The bits of the tag of a value that have its type; the rest have its length. */
#define JSON_TAG_TYPE_BITS					8

/** @brief The mask of the type in the tag of a value. */
#define JSON_TAG_TYPE_MASK					((u64) ((1 << JSON_TAG_TYPE_BITS) - 1))

/** @brief The biggest length that the tag of a value can have. */
#define JSON_TAG_LENGTH_MAX					(UINT64_MAX >> JSON_TAG_TYPE_BITS)

/** @brief The bit of json_characters of a space, a tab, a line feed and a carriage return. */
#define JSON_CHARACTER_WHITESPACE			(1 << 0)

/** @brief The bit of json_characters of a decimal digit. */
#define JSON_CHARACTER_DIGIT				(1 << 1)

/**
 * @brief The bit of json_characters of a byte that the plain part of a string stops at: its quote, a backslash,
 * a control character and a byte that is not ASCII.
 */
#define JSON_CHARACTER_STOP					(1 << 2)

/** @brief The bytes that the text of a number written by json_number_write_real () can have. */
#define JSON_NUMBER_REAL_SIZE				(1 << 5)

/** @brief The bytes that the text of a number written by json_number_write_unsigned () can have. */
#define JSON_NUMBER_INTEGER_SIZE			(1 << 5)

/** @brief An integer of 128 bits without sign, for the products of two integers of 64 bits. */
__extension__ typedef unsigned __int128 JsonWide;

/**
 * @brief An object with more members than these has a table of its keys by their hash; one with fewer is
 * searched in its directory, one member after the other.
 */
#define JSON_OBJECT_HASHED					(1 << 5)

/** @brief The slots of the table of an object for each of its members, a power of two: half of them stay empty. */
#define JSON_OBJECT_TABLE_RATIO				2

/**
 * @brief The slots that a search in the table of an object walks at most: a table that needs more for one of
 * its keys is not used, so keys chosen to share their slots only cost a search in the directory.
 */
#define JSON_OBJECT_TABLE_PROBES			(1 << 6)

/** @brief The bytes of the signature of a key: the first ones of the key. */
#define JSON_MEMBER_SIGNATURE_SIZE			4

/** @brief The bits of a byte. */
#define JSON_BYTE_BITS						8

/** @brief The signatures of the directory of an object that fit in a value, the ones that a search compares at once. */
#define JSON_MEMBER_BLOCK					(sizeof (CerverJsonValue) / sizeof (u32))

/**
 * @brief Where an array or an object ends and, for an object, where its directory is.
 */
typedef struct JsonLinks {

	/**
	 * @brief The values from an array or an object to the value after it, its directory and its table included;
	 * while a reader has it open, the values back to the array or the object that has it, 0 at the top.
	 */
	u32 span;

	/**
	 * @brief The values from an object to its directory, 0 for one without members; while a reader has it open,
	 * where its members start in the directory that the reader keeps.
	 */
	u32 directory;

} JsonLinks;

/**
 * @brief A member of an object while a reader has it open: where its key is and a signature to compare it without
 * reading it.
 *
 * When the object is closed they become its directory, in the memory of the values after its last member: first
 * the signature of each member, in their order, and then the values from the object to each key, both as words of
 * 32 bits and each part with zeros up to a whole value. A search compares the signatures, that are contiguous,
 * #JSON_MEMBER_BLOCK at a time from the last member to the first one, so it does not have to walk the values of
 * the object and the last member of a key is the first one found. An object with more than #JSON_OBJECT_HASHED
 * members has after its directory a table of 32 bits slots, each one the position of a member in the directory
 * plus one (0 when it is empty), and after its last slot a word that is 0 when the table is not used.
 */
typedef struct JsonMember {

	/** @brief The values from the object to the key. */
	u32 offset;

	/** @brief The first bytes of the key, see json_member_signature (). */
	u32 signature;

} JsonMember;

/**
 * @brief A value: 16 bytes, followed in memory by the values that it has inside.
 *
 * The values of a document are contiguous in the order of the text. After an array come its elements, and after
 * an object the key and the value of each member, each one with everything that it has inside, and then its
 * directory (see JsonMember); a value that is not an array nor an object is followed by its next sibling.
 */
struct CerverJsonValue {

	/**
	 * @brief The type in the low #JSON_TAG_TYPE_BITS bits; in the rest, the bytes of a string, the elements of
	 * an array or the members of an object.
	 */
	u64 tag;

	/** @brief What the value has, by its type. */
	union {

		/** @brief The value of an integer. */
		i64 integer;

		/** @brief The value of an unsigned; 1 for a true and 0 for a false and for a null. */
		u64 natural;

		/** @brief The value of a real. */
		double real;

		/** @brief The bytes of a string, with a NUL after them. */
		const char *string;

		/** @brief Where an array or an object ends, and the directory of an object. */
		JsonLinks links;

	} content;

};

/**
 * @brief A document: the values of a text and where they live.
 */
struct CerverJsonDocument {

	/** @brief The arena that has the document, its text and its values. */
	Arena *arena;

	/** @brief The document created its arena and releases it. */
	bool owns_arena;

	/** @brief The value at the top of the text, the first of the values. */
	const CerverJsonValue *root;

	/** @brief How many values the text has. */
	size_t n_values;

};

/**
 * @brief The position of a reader in its text.
 *
 * The text can be written by the reader, has a NUL at its end and #CERVER_JSON_PADDING bytes set to 0 after it,
 * so a read of a few bytes ahead never leaves it.
 */
typedef struct JsonCursor {

	/** @brief The first byte of the text. */
	u8 *start;

	/** @brief The next byte to read. */
	u8 *position;

	/** @brief The NUL at the end of the text. */
	u8 *end;

	/** @brief The `CERVER_JSON_PARSE_<name>` flags. */
	unsigned int flags;

} JsonCursor;

/**
 * @brief A number that was read.
 */
typedef struct JsonNumber {

	/** @brief #CERVER_JSON_TYPE_INTEGER, #CERVER_JSON_TYPE_UNSIGNED or #CERVER_JSON_TYPE_REAL. */
	CerverJsonType type;

	/** @brief Its value, by its type. */
	union {

		/** @brief The value of an integer. */
		i64 integer;

		/** @brief The value of an unsigned. */
		u64 natural;

		/** @brief The value of a real. */
		double real;

	} value;

} JsonNumber;

/** @brief The class of each byte, as `JSON_CHARACTER_<name>` bits. */
CERVER_HIDDEN const u8 json_characters[1 << 8];

/**
 * @brief Returns the type of a value.
 *
 * @param[in] value the value
 * @return its type
 */
static inline CerverJsonType json_value_type (const CerverJsonValue *value) {

	return (CerverJsonType) (value->tag & JSON_TAG_TYPE_MASK);

}

/**
 * @brief Returns the length of a value: the bytes of a string, the elements of an array, the members of an
 * object.
 *
 * @param[in] value the value
 * @return its length
 */
static inline size_t json_value_length (const CerverJsonValue *value) {

	return (size_t) (value->tag >> JSON_TAG_TYPE_BITS);

}

/**
 * @brief Checks that a value is an array or an object.
 *
 * @param[in] value the value
 * @return true for an array and for an object
 */
static inline bool json_value_is_container (const CerverJsonValue *value) {

	return json_value_type (value) >= CERVER_JSON_TYPE_ARRAY;

}

/**
 * @brief Returns the value that follows a value and everything that it has inside.
 *
 * @param[in] value the value
 * @return its next sibling, or the first value after the array or the object that has it
 */
static inline const CerverJsonValue *json_value_next (const CerverJsonValue *value) {

	return value + (json_value_is_container (value) ? value->content.links.span : 1);

}

/**
 * @brief Returns the signature of a key: its first #JSON_MEMBER_SIGNATURE_SIZE bytes, with zeros after a key
 * that is shorter.
 *
 * Two keys with different signatures are different; two with the same one are compared byte by byte. It does
 * not need the length of a key that ends with a NUL, see json_member_signature_string ().
 *
 * @param[in] key the bytes of the key
 * @param[in] length how many
 * @return the signature
 */
static inline u32 json_member_signature (const char *key, const size_t length) {

	u32 signature = 0;

	for (size_t i = 0; (i < JSON_MEMBER_SIGNATURE_SIZE) && (i < length); i++) {
		signature |= ((u32) (u8) key[i]) << (i * JSON_BYTE_BITS);
	}

	return signature;

}

/**
 * @brief Returns the signature of a key that ends with a NUL, without its length.
 *
 * @param[in] key the key
 * @param[out] taken the bytes of the key that the signature has: fewer than #JSON_MEMBER_SIGNATURE_SIZE are the
 * length of the key
 * @return the signature, the one of json_member_signature () with the length of the key
 */
static inline u32 json_member_signature_string (const char *key, size_t *taken) {

	u32 signature = 0;

	size_t i = 0;
	for (; (i < JSON_MEMBER_SIGNATURE_SIZE) && key[i]; i++) {
		signature |= ((u32) (u8) key[i]) << (i * JSON_BYTE_BITS);
	}

	*taken = i;

	return signature;

}

/**
 * @brief Returns the values that a part of the directory of an object takes in the memory of the values: its
 * signatures or the places of its keys.
 *
 * @param[in] count the members of the object
 * @return the values, each one with room for #JSON_MEMBER_BLOCK members
 */
static inline size_t json_object_part_values (const size_t count) {

	return (count + JSON_MEMBER_BLOCK - 1) / JSON_MEMBER_BLOCK;

}

/**
 * @brief Returns the values that the directory of an object takes in the memory of the values.
 *
 * @param[in] count the members of the object
 * @return the values of its two parts
 */
static inline size_t json_object_directory_values (const size_t count) {

	return json_object_part_values (count) + json_object_part_values (count);

}

/**
 * @brief Returns the signatures of the directory of an object that has members.
 *
 * @param[in] object the object
 * @return the signature of each member in their order, with zeros up to a whole value
 */
static inline const u32 *json_object_signatures (const CerverJsonValue *object) {

	return (const u32 *) (object + object->content.links.directory);

}

/**
 * @brief Returns where the keys of an object that has members are.
 *
 * @param[in] object the object
 * @param[in] count its members
 * @return the values from the object to the key of each member, in their order
 */
static inline const u32 *json_object_offsets (const CerverJsonValue *object, const size_t count) {

	return (const u32 *) (object + object->content.links.directory + json_object_part_values (count));

}

/**
 * @brief Returns the slots of the table of an object with more than #JSON_OBJECT_HASHED members.
 *
 * @param[in] count the members of the object
 * @return the slots, the first power of two with #JSON_OBJECT_TABLE_RATIO for each member
 */
static inline size_t json_object_table_slots (const size_t count) {

	size_t slots = JSON_OBJECT_HASHED * JSON_OBJECT_TABLE_RATIO;
	while (slots < (count * JSON_OBJECT_TABLE_RATIO)) slots <<= 1;

	return slots;

}

/**
 * @brief Returns the values that the table of an object takes in the memory of the values, with the word after
 * its last slot.
 *
 * @param[in] slots the slots of the table
 * @return the values
 */
static inline size_t json_object_table_values (const size_t slots) {

	return (((slots + 1) * sizeof (u32)) + sizeof (CerverJsonValue) - 1) / sizeof (CerverJsonValue);

}

/**
 * @brief Returns the table of an object with more than #JSON_OBJECT_HASHED members.
 *
 * @param[in] object the object
 * @param[in] count its members
 * @return its table, after its directory
 */
static inline const u32 *json_object_table (const CerverJsonValue *object, const size_t count) {

	return (const u32 *) (object + object->content.links.directory + json_object_directory_values (count));

}

/**
 * @brief Returns the hash of a key, for the table of an object.
 *
 * It depends on a seed that the process takes from the random bytes of the kernel the first time, so the keys
 * that share a slot can not be chosen from outside.
 *
 * @param[in] key the bytes of the key
 * @param[in] length how many
 * @return the hash
 */
CERVER_HIDDEN u64 json_member_hash (
	const char *key, const size_t length
);

/**
 * @brief Sets the slots that a search in the table of an object walks at most, for the documents that are read
 * after it. For the internal methods of cerver and their tests: with one slot almost no table can be used, and
 * the objects with many members are searched in their directories.
 *
 * @param[in] probes the slots, 0 for #JSON_OBJECT_TABLE_PROBES
 * @warning It changes a value of the process without any synchronization.
 */
CERVER_PRIVATE void cerver_json_object_table_probes_set (
	const size_t probes
);

/**
 * @brief Moves a cursor to its next byte that is not whitespace.
 *
 * @param[in,out] cursor the cursor
 */
static inline void json_cursor_skip_whitespace (JsonCursor *cursor) {

	while (json_characters[*cursor->position] & JSON_CHARACTER_WHITESPACE) cursor->position += 1;

}

/**
 * @brief Returns the error of a byte that a reader did not expect.
 *
 * @param[in] cursor the cursor, at the byte
 * @return #CERVER_JSON_ERROR_END at the end of the text, #CERVER_JSON_ERROR_CHARACTER before it
 */
static inline CerverJsonErrorCode json_cursor_unexpected (const JsonCursor *cursor) {

	return (cursor->position >= cursor->end) ? CERVER_JSON_ERROR_END : CERVER_JSON_ERROR_CHARACTER;

}

/**
 * @brief Sets the error of a reader.
 *
 * @param[out] error the error, it can be NULL
 * @param[in] code the code
 * @param[in] offset the position in the text
 */
CERVER_HIDDEN void json_error_set (
	CerverJsonError *error, const CerverJsonErrorCode code, const size_t offset
);

/**
 * @brief Fills the limits of a call with the defaults for the ones that are 0.
 *
 * @param[in] limits the limits of the caller, it can be NULL
 * @return the limits to use
 */
CERVER_HIDDEN CerverJsonLimits json_limits_resolve (
	const CerverJsonLimits *limits
);

/**
 * @brief Prepares the text of a reader that copies it: the copy in the arena, with its NUL and its padding.
 *
 * @param[out] cursor the cursor, at the start of the copy
 * @param[in,out] arena the arena
 * @param[in] text the text
 * @param[in] length the bytes of the text
 * @param[in] flags the flags of the reader
 * @return true, false when the memory could not be allocated
 */
CERVER_HIDDEN bool json_cursor_copy (
	JsonCursor *cursor, Arena *arena,
	const char *text, const size_t length, const unsigned int flags
);

/**
 * @brief Prepares the text of a reader that works in situ: its padding is set to 0.
 *
 * @param[out] cursor the cursor, at the start of the text
 * @param[in,out] text the text, with its padding
 * @param[in] length the bytes of the text
 * @param[in] flags the flags of the reader
 */
CERVER_HIDDEN void json_cursor_in_situ (
	JsonCursor *cursor,
	char *text, const size_t length, const unsigned int flags
);

/**
 * @brief Returns the first byte from a position that the plain part of a string stops at.
 *
 * @param[in] position a position inside a string
 * @return the first byte with #JSON_CHARACTER_STOP; the NUL at the end of the text is one
 */
CERVER_HIDDEN u8 *json_scan_string (
	u8 *position
);

/**
 * @brief Returns how many bytes at the start of some bytes are the plain part of a string, without reading any
 * byte after them: the scan of a writer, whose strings have a length and no padding.
 *
 * @param[in] bytes the bytes
 * @param[in] length how many
 * @return the bytes before the first one with #JSON_CHARACTER_STOP, the length when there is none
 */
CERVER_HIDDEN size_t json_scan_plain (
	const u8 *bytes, const size_t length
);

/**
 * @brief Returns where the plain part of a string stops, with the scan that the readers use or with the one
 * that goes byte by byte, the reference of the others. For the internal methods of cerver and their tests.
 *
 * @param[in] text a text with a byte that stops it (its NUL is one) and #CERVER_JSON_PADDING bytes after it
 * @param[in] reference true for the scan byte by byte
 * @return the position of the first byte with #JSON_CHARACTER_STOP
 */
CERVER_PRIVATE size_t cerver_json_scan_string_stop (
	const char *text, const bool reference
);

/**
 * @brief Reads a string in situ: its escapes are replaced and a NUL is written after it.
 *
 * @param[in,out] cursor the cursor, after the quote that starts the string; when it returns, after the quote
 * that ends it, or at the byte of the error
 * @param[out] string the bytes of the string, inside the text
 * @param[out] length the bytes of the string
 * @return #CERVER_JSON_ERROR_NONE, or the error of the string
 */
CERVER_HIDDEN CerverJsonErrorCode json_text_string (
	JsonCursor *cursor, const char **string, size_t *length
);

/**
 * @brief Checks that a buffer is valid UTF-8.
 *
 * @param[in] bytes the bytes
 * @param[in] length how many
 * @return true when every character is complete, has its shortest form and is not a surrogate nor over U+10FFFF
 */
CERVER_HIDDEN bool json_text_is_valid (
	const u8 *bytes, const size_t length
);

/**
 * @brief Returns the bytes of a character that starts a sequence that is not ASCII, after it checks them.
 *
 * @param[in] bytes the first byte of the character; the bytes after it are read until one that does not belong
 * @param[in] available the bytes that can be read
 * @return the bytes of the character, from 2 to 4; 0 when it is not valid UTF-8
 */
CERVER_HIDDEN size_t json_text_character (
	const u8 *bytes, const size_t available
);

/**
 * @brief Reads a number.
 *
 * @param[in,out] cursor the cursor, at the minus or the first digit of the number; when it returns, after the
 * number, or at the byte of the error
 * @param[out] number the number
 * @return #CERVER_JSON_ERROR_NONE, #CERVER_JSON_ERROR_NUMBER for a number that the grammar does not have, or
 * #CERVER_JSON_ERROR_RANGE for one that does not fit
 */
CERVER_HIDDEN CerverJsonErrorCode json_number_read (
	JsonCursor *cursor, JsonNumber *number
);

/**
 * @brief Writes an integer without sign with its decimal digits.
 *
 * @param[out] buffer a buffer of #JSON_NUMBER_INTEGER_SIZE bytes
 * @param[in] value the integer
 * @return the bytes that were written, without a NUL
 */
CERVER_HIDDEN size_t json_number_write_unsigned (
	char *buffer, const u64 value
);

/**
 * @brief Writes an integer with its sign and its decimal digits.
 *
 * @param[out] buffer a buffer of #JSON_NUMBER_INTEGER_SIZE bytes
 * @param[in] value the integer
 * @return the bytes that were written, without a NUL
 */
CERVER_HIDDEN size_t json_number_write_integer (
	char *buffer, const i64 value
);

/**
 * @brief Writes a real with the least digits that read back the same double, always with a fraction or an
 * exponent.
 *
 * @param[out] buffer a buffer of #JSON_NUMBER_REAL_SIZE bytes
 * @param[in] value the real, not a NaN nor an infinite
 * @return the bytes that were written, without a NUL; 0 for a NaN and for an infinite
 */
CERVER_HIDDEN size_t json_number_write_real (
	char *buffer, const double value
);

/** @brief The first power of ten of json_number_powers. */
#define JSON_NUMBER_POWERS_FIRST			(-342)

/** @brief The last power of ten of json_number_powers. */
#define JSON_NUMBER_POWERS_LAST				324

/** @brief How many powers of ten json_number_powers has. */
#define JSON_NUMBER_POWERS_SIZE				(JSON_NUMBER_POWERS_LAST - JSON_NUMBER_POWERS_FIRST + 1)

/**
 * @brief The 128 most significant bits of each power of five, from 5^#JSON_NUMBER_POWERS_FIRST to
 * 5^#JSON_NUMBER_POWERS_LAST: the high word and the low word of each one.
 *
 * A power that is not negative is cut to its 128 bits (the ones up to 5^55 are exact); a negative one is the
 * 128 bits of 2^b / 5^-q plus one, for the b that leaves its first bit set, cut again when it was computed with
 * more bits (the ones below 5^-27). It is the table of the algorithm of Eisel and Lemire, generated by
 * benchmarks/tables.py.
 */
CERVER_HIDDEN const u64 json_number_powers[JSON_NUMBER_POWERS_SIZE][2];

/**
 * @brief Returns the two words of a power of json_number_powers. For the internal methods of cerver and their
 * tests.
 *
 * @param[in] power the power, from #JSON_NUMBER_POWERS_FIRST to #JSON_NUMBER_POWERS_LAST
 * @param[out] high its high word
 * @param[out] low its low word
 */
CERVER_PRIVATE void cerver_json_number_power (
	const int power, u64 *high, u64 *low
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
