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

// va_copy is a C99 feature. In C89 implementations, it's sometimes
// available as __va_copy. If not, memcpy() should do the trick
#ifndef va_copy
	#ifdef __va_copy
		#define va_copy __va_copy
	#else
		#define va_copy(a, b) memcpy (&(a), &(b), sizeof (va_list))
	#endif
#endif

#endif
