/**
 * @file cerver/types/types.h
 * @brief The integer types, the types of a value and the function pointers that cerver uses everywhere.
 */

#ifndef _CERVER_TYPES_H_
#define _CERVER_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup types Types
 * @brief The base types of cerver and its String.
 */

/**
 * @defgroup types_base Base types
 * @ingroup types
 * @brief The fixed size integers, the types of a value and the function pointers.
 * @{
 */

/** @brief An unsigned integer of 8 bits. */
typedef uint8_t u8;
/** @brief An unsigned integer of 16 bits. */
typedef uint16_t u16;
/** @brief An unsigned integer of 32 bits. */
typedef uint32_t u32;
/** @brief An unsigned integer of 64 bits. */
typedef uint64_t u64;

/** @brief A signed integer of 8 bits. */
typedef int8_t i8;
/** @brief A signed integer of 16 bits. */
typedef int16_t i16;
/** @brief A signed integer of 32 bits. */
typedef int32_t i32;
/** @brief A signed integer of 64 bits. */
typedef int64_t i64;

/** @brief An unsigned char. */
typedef unsigned char uchar;
/** @brief An unsigned char that holds an ASCII character. */
typedef unsigned char ascii_char;

/**
 * @brief The type of a value, like the values of the claims of a JWT.
 */
typedef enum {

	/** @brief A value without a type. */
	CERVER_TYPE_UNDEFINED	= 0x00,
	/** @brief A double. */
	CERVER_TYPE_DOUBLE		= 0x01,
	/** @brief A char. */
	CERVER_TYPE_CHAR		= 0x02,
	/** @brief A UTF-8 string. */
	CERVER_TYPE_UTF8		= 0x03,
	/** @brief An array. */
	CERVER_TYPE_ARRAY		= 0x04,
	/** @brief A boolean. */
	CERVER_TYPE_BOOL		= 0x08,
	/** @brief A date and a time. */
	CERVER_TYPE_DATE_TIME	= 0x09,
	/** @brief A null value. */
	CERVER_TYPE_NULL		= 0x0A,
	/** @brief A signed integer of 32 bits. */
	CERVER_TYPE_INT32		= 0x10,
	/** @brief A timestamp. */
	CERVER_TYPE_TIMESTAMP	= 0x11,
	/** @brief A signed integer of 64 bits. */
	CERVER_TYPE_INT64		= 0x12

} cerver_type_t;

/** @brief A function that takes no argument and returns a value. */
typedef u8 (*Func)(void);

/** @brief A function that takes an argument and does not return a value, like a destroy or a handler method. */
typedef void (*Action)(void *);

/** @brief A function that takes an argument and returns a value, like the authenticate method of a cerver. */
typedef u8 (*delegate)(void *);

/** @brief A function that takes an argument and returns a pointer, like the work of a thread. */
typedef void *(*Work)(void *);

/**
 * @brief Compares two elements: a negative value if the first goes before the second, 0 if they are equal and a
 * positive value if it goes after.
 */
typedef int (*Comparator)(const void *, const void *);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
