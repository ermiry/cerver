/**
 * @file cerver/json/value.h
 * @brief The values of a document of the JSON of cerver: their type, their content, the elements of an array and
 * the members of an object.
 */

#ifndef _CERVER_JSON_VALUE_H_
#define _CERVER_JSON_VALUE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/json/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup json_value Values
 * @ingroup json
 * @brief What a value of a document has.
 *
 * Every method takes a NULL value and a value of another type, and answers its empty result for them (NULL, 0 or
 * false), so the result of a search can be given to the next method without a check in the middle. Every method
 * is safe from any thread: a document is never changed.
 * @{
 */

/**
 * @brief Returns the type of a value.
 *
 * @param[in] value the value
 * @return its type, CERVER_JSON_TYPE_NONE for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT CerverJsonType cerver_json_type (
	const CerverJsonValue *value
);

/**
 * @brief Checks that a value is a null.
 *
 * @param[in] value the value
 * @return true for a null, false for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_is_null (
	const CerverJsonValue *value
);

/**
 * @brief Checks that a value is a boolean.
 *
 * @param[in] value the value
 * @return true for a true and for a false, false for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_is_boolean (
	const CerverJsonValue *value
);

/**
 * @brief Checks that a value is a number of any of the three types.
 *
 * @param[in] value the value
 * @return true for an integer, an unsigned and a real, false for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_is_number (
	const CerverJsonValue *value
);

/**
 * @brief Checks that a value is a string.
 *
 * @param[in] value the value
 * @return true for a string, false for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_is_string (
	const CerverJsonValue *value
);

/**
 * @brief Checks that a value is an array.
 *
 * @param[in] value the value
 * @return true for an array, false for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_is_array (
	const CerverJsonValue *value
);

/**
 * @brief Checks that a value is an object.
 *
 * @param[in] value the value
 * @return true for an object, false for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_is_object (
	const CerverJsonValue *value
);

/**
 * @brief Returns the value of a boolean.
 *
 * @param[in] value the value
 * @return true for a true; false for a false, for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_boolean_value (
	const CerverJsonValue *value
);

/**
 * @brief Returns the value of a number that fits in 64 bits with sign.
 *
 * @param[in] value the value
 * @return the value of an integer; 0 for an unsigned (it does not fit), for a real, for another type and for a
 * NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT i64 cerver_json_integer_value (
	const CerverJsonValue *value
);

/**
 * @brief Returns the value of a number that fits in 64 bits without sign.
 *
 * @param[in] value the value
 * @return the value of an unsigned and of an integer that is not negative; 0 for a negative integer, for a real,
 * for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT u64 cerver_json_unsigned_value (
	const CerverJsonValue *value
);

/**
 * @brief Returns the value of a real.
 *
 * @param[in] value the value
 * @return the value of a real; 0 for an integer, for an unsigned, for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT double cerver_json_real_value (
	const CerverJsonValue *value
);

/**
 * @brief Returns the value of a number of any of the three types as a double.
 *
 * @param[in] value the value
 * @return the value of a real, or the closest double to an integer or to an unsigned; 0 for another type and for
 * a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT double cerver_json_number_value (
	const CerverJsonValue *value
);

/**
 * @brief Returns the bytes of a string.
 *
 * @param[in] value the value
 * @return the string, in UTF-8, without its escapes and with a NUL after it, valid while its document is; NULL
 * for another type and for a NULL value
 * @warning A string can have a NUL inside (an escaped one in its text): its length is the one of
 * cerver_json_string_length (), unless the text was read with #CERVER_JSON_PARSE_REJECT_NUL.
 * @since 3.0b-6
 */
CERVER_EXPORT const char *cerver_json_string_value (
	const CerverJsonValue *value
);

/**
 * @brief Returns the length of a string.
 *
 * @param[in] value the value
 * @return the bytes of the string, without the NUL after it; 0 for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT size_t cerver_json_string_length (
	const CerverJsonValue *value
);

/**
 * @brief Returns how many elements an array has.
 *
 * @param[in] array the array
 * @return its elements; 0 for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT size_t cerver_json_array_size (
	const CerverJsonValue *array
);

/**
 * @brief Returns an element of an array by its position.
 *
 * It walks the elements before it: an array is read in order with a CerverJsonArrayIterator.
 *
 * @param[in] array the array
 * @param[in] index the position of the element, from 0
 * @return the element; NULL for a position that the array does not have, for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT const CerverJsonValue *cerver_json_array_get (
	const CerverJsonValue *array, const size_t index
);

/**
 * @brief The position of a walk through the elements of an array, created by cerver_json_array_iterator ().
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonArrayIterator {

	/** @brief The element that the next call returns. */
	const CerverJsonValue *next;

	/** @brief The elements that are left. */
	size_t remaining;

} CerverJsonArrayIterator;

/**
 * @brief Starts a walk through the elements of an array, in the order of its text.
 *
 * @param[in] array the array
 * @return the iterator; one without elements for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT CerverJsonArrayIterator cerver_json_array_iterator (
	const CerverJsonValue *array
);

/**
 * @brief Returns the next element of an array.
 *
 * @param[in,out] iterator the iterator
 * @return the element; NULL when there are no more and for a NULL iterator
 * @since 3.0b-6
 */
CERVER_EXPORT const CerverJsonValue *cerver_json_array_iterator_next (
	CerverJsonArrayIterator *iterator
);

/**
 * @brief Returns how many members an object has.
 *
 * @param[in] object the object
 * @return its members, a key that is more than once counted each time; 0 for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT size_t cerver_json_object_size (
	const CerverJsonValue *object
);

/**
 * @brief Returns the value of a member of an object by its key.
 *
 * An object keeps a signature of each of its keys, and one with many members a table of them by their hash: a
 * search does not walk the values of the object, and its cost does not grow with the members of a big one.
 *
 * @param[in] object the object
 * @param[in] key the key, ended by a NUL
 * @return the value of the member, the last one when the key is more than once; NULL for a key that the object
 * does not have, for a NULL key, for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT const CerverJsonValue *cerver_json_object_get (
	const CerverJsonValue *object, const char *key
);

/**
 * @brief Works as cerver_json_object_get (), with a key that is given by its length.
 *
 * @param[in] object the object
 * @param[in] key the key, it can have a NUL inside
 * @param[in] length the bytes of the key
 * @return the value of the member, like cerver_json_object_get ()
 * @since 3.0b-6
 */
CERVER_EXPORT const CerverJsonValue *cerver_json_object_find (
	const CerverJsonValue *object, const char *key, const size_t length
);

/**
 * @brief The position of a walk through the members of an object, created by cerver_json_object_iterator ().
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonObjectIterator {

	/** @brief The key of the member that the next call returns. */
	const CerverJsonValue *next;

	/** @brief The members that are left. */
	size_t remaining;

} CerverJsonObjectIterator;

/**
 * @brief Starts a walk through the members of an object, in the order of its text.
 *
 * @param[in] object the object
 * @return the iterator; one without members for another type and for a NULL value
 * @since 3.0b-6
 */
CERVER_EXPORT CerverJsonObjectIterator cerver_json_object_iterator (
	const CerverJsonValue *object
);

/**
 * @brief Returns the next member of an object.
 *
 * @param[in,out] iterator the iterator
 * @param[out] key the key of the member, a string; it can be NULL
 * @param[out] value the value of the member; it can be NULL
 * @return true when a member was returned; false when there are no more and for a NULL iterator, with the key
 * and the value as they were
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_object_iterator_next (
	CerverJsonObjectIterator *iterator,
	const CerverJsonValue **key, const CerverJsonValue **value
);

/**
 * @brief Checks that two values are equal.
 *
 * Two numbers are equal when they have the same type and the same value (an integer is never equal to a real);
 * two arrays, when their elements are equal one by one; two objects, when they have the same amount of members
 * and each key of one has an equal value in the other, in any order (for a key that is more than once, its last
 * value).
 *
 * @param[in] first a value
 * @param[in] second another value, of the same document or of another one
 * @return true when they are equal; false when they are not and when any of them is NULL
 * @note It searches each key of an object in the other one, and in the object itself for its last value.
 * @warning It calls itself for each level of nested arrays and objects: two documents that were read with a
 * limit of levels far over #CERVER_JSON_LIMIT_DEPTH can need more stack than their thread has.
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_equal (
	const CerverJsonValue *first, const CerverJsonValue *second
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
