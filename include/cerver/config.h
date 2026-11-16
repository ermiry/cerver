/**
 * @file cerver/config.h
 * @brief The configuration that every header of cerver shares: the feature test macros of the C library, the
 * visibility of the symbols and the helper macros.
 *
 * It is the first header of cerver that each part includes; an application only needs it through the other
 * headers.
 */

#ifndef _CERVER_CONFIG_H_
#define _CERVER_CONFIG_H_

// cerver is C11 (and C++11), so va_copy always comes from stdarg.h:
// the memcpy () that was used when this header came first copied
// the pointer that a va_list parameter is in x86-64, not the list,
// and every json_pack () and json_unpack () built with clang crashed
#include <stdarg.h>

/**
 * @defgroup config_features Feature test macros
 * @brief The GNU and POSIX extensions of the C library that cerver uses.
 *
 * They only work before the first system header, and most headers of cerver include their system headers before
 * this one: a program that includes cerver is built with `-D _GNU_SOURCE` (g++ defines it, gcc and clang do not,
 * not even with their gnu dialects), and without it this header stops the build with an error that says so. The
 * internal macros of glibc (`__USE_GNU`, `__USE_POSIX199309`) are never defined by hand.
 * @{
 */

#ifndef _GNU_SOURCE
	/** @brief Enables the GNU extensions of the C library, unless the build already defines it. */
	#define _GNU_SOURCE
#endif

#ifndef _XOPEN_SOURCE
	/** @brief Enables the X/Open 7 (POSIX.1-2008) declarations. */
	#define _XOPEN_SOURCE 700
#endif

/** @} */

#include <features.h>

#if defined (__GLIBC__) && !defined (__USE_GNU)
	#error "cerver needs _GNU_SOURCE before any system header: build with -D _GNU_SOURCE"
#endif

/**
 * @defgroup config_visibility Visibility
 * @brief The macros that every declaration of cerver starts with.
 * @{
 */

/** @brief A function or a variable of the API, to be used in the application. */
#define CERVER_EXPORT 			extern

/** @brief A function that is exported by libcerver.so but is meant for the internal methods of cerver. */
#define CERVER_PRIVATE			extern

/** @brief A function that is used by the private methods of cerver and that the application can also use. */
#define CERVER_PUBLIC 			extern

/**
 * @brief An internal function that is not exported by libcerver.so.
 *
 * A process that also loads another library with the same names (like the json of the system) never mixes them.
 */
#define CERVER_HIDDEN			extern __attribute__ ((visibility ("hidden")))

/**
 * @brief An exported function that the sources define as inline.
 *
 * In C++ an inline function is only emitted where it is used, so the exported functions defined with
 * CERVER_INLINE in the sources must be regular functions to be available to other translation units: in C++ it
 * expands to nothing.
 */
#ifdef __cplusplus
#define CERVER_INLINE
#else
#define CERVER_INLINE inline
#endif

/** @} */

/**
 * @defgroup config_helpers Helpers
 * @brief The macros of the initializers, the attributes and the arrays.
 * @{
 */

/**
 * @brief Sets every member of a struct to zero, valid both in C11 and in C++.
 *
 * C++ only takes the empty braces without a missing initializer warning, C11 takes `{ 0 }`.
 */
#ifdef __cplusplus
#define CERVER_ZERO_INITIALIZER		{ }
#else
#define CERVER_ZERO_INITIALIZER		{ 0 }
#endif

/**
 * @brief The compiler checks the arguments of the calls against the format of a printf () like function.
 *
 * @param format_index the position (from 1) of the format parameter
 * @param first_argument_index the position of the first variable argument, 0 for the functions that take a
 * va_list
 */
#define CERVER_PRINTF_FORMAT(format_index, first_argument_index)	\
	__attribute__ ((format (printf, format_index, first_argument_index)))

/**
 * @brief Marks a function declaration as deprecated.
 *
 * @param func the declaration of the function
 */
#define DEPRECATED(func) func __attribute__ ((deprecated))

/**
 * @brief Adds the attributes of GCC and clang to a declaration, nothing with other compilers.
 *
 * @param x the attributes, between parentheses
 */
#if defined (__GNUC__) || defined (__clang__)
#define CERVER_ATTRS(x) __attribute__ (x)
#else
#define CERVER_ATTRS(x)
#endif

#ifndef MIN
	/**
	 * @brief The smaller of two values; each argument is evaluated twice.
	 *
	 * @param a the first value
	 * @param b the second value
	 */
	#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
	/**
	 * @brief The larger of two values; each argument is evaluated twice.
	 *
	 * @param a the first value
	 * @param b the second value
	 */
	#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
	/**
	 * @brief The number of elements of an array (not of a pointer).
	 *
	 * @param a the array
	 */
	#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#endif

#ifndef ARRAY_ELEM_AT
	/**
	 * @brief The element of an array at an index, or a default value when the index is out of range.
	 *
	 * @param a the array
	 * @param i the index, compared as an unsigned int, so a negative index is out of range
	 * @param v the value when the index is out of range
	 */
	#define ARRAY_ELEM_AT(a, i, v) ((unsigned int) (i) < ARRAY_SIZE (a) ? (a)[(i)] : (v))
#endif

/** @} */

#endif
