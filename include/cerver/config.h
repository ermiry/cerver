#ifndef _CERVER_CONFIG_H_
#define _CERVER_CONFIG_H_

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#ifndef __USE_GNU
	#define __USE_GNU
#endif

#ifndef __USE_POSIX199309
	#define __USE_POSIX199309
#endif

#ifndef _XOPEN_SOURCE
	#define _XOPEN_SOURCE 700
#endif

#include <features.h>

// to be used in the application
#define CERVER_EXPORT 			extern

// to be used by internal cerver methods
#define CERVER_PRIVATE			extern

// used by private methods but can be used by the application
#define CERVER_PUBLIC 			extern

// internal methods that are not exported by libcerver.so,
// so a process that also loads another library with the same names
// (like the json of the system) never mixes them
#define CERVER_HIDDEN			extern __attribute__ ((visibility ("hidden")))

// in C++ an inline function is only emitted where it is used,
// so the exported functions defined with CERVER_INLINE in the sources
// must be regular functions to be available to other translation units
#ifdef __cplusplus
#define CERVER_INLINE
#else
#define CERVER_INLINE inline
#endif

// sets every member of a struct to zero, valid both in C11 and in C++,
// that only takes the empty braces without a missing initializer warning
#ifdef __cplusplus
#define CERVER_ZERO_INITIALIZER		{ }
#else
#define CERVER_ZERO_INITIALIZER		{ 0 }
#endif

// the compiler checks the arguments of the calls against the format,
// 0 as the first argument index for the functions that take a va_list
#define CERVER_PRINTF_FORMAT(format_index, first_argument_index)	\
	__attribute__ ((format (printf, format_index, first_argument_index)))

#define DEPRECATED(func) func __attribute__ ((deprecated))

#if defined (__GNUC__) || defined (__clang__)
#define CERVER_ATTRS(x) __attribute__ (x)
#else
#define CERVER_ATTRS(x)
#endif

#ifndef MIN
	#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
	#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#endif

#ifndef ARRAY_ELEM_AT
	#define ARRAY_ELEM_AT(a, i, v) ((unsigned int) (i) < ARRAY_SIZE (a) ? (a)[(i)] : (v))
#endif

// cerver is C11 (and C++11), so va_copy always comes from stdarg.h:
// the memcpy () that was used when this header came first copied
// the pointer that a va_list parameter is in x86-64, not the list,
// and every json_pack () and json_unpack () built with clang crashed
#include <stdarg.h>

#endif
