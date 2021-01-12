#ifndef _CERVER_TYPES_H_
#define _CERVER_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef unsigned char uchar;
typedef unsigned char ascii_char;

typedef enum {

	CERVER_TYPE_UNDEFINED	= 0x00,
	CERVER_TYPE_DOUBLE		= 0x01,
	CERVER_TYPE_CHAR		= 0x02,
	CERVER_TYPE_UTF8		= 0x03,
	CERVER_TYPE_ARRAY		= 0x04,
	CERVER_TYPE_BOOL		= 0x08,
	CERVER_TYPE_DATE_TIME	= 0x09,
	CERVER_TYPE_NULL		= 0x0A,
	CERVER_TYPE_INT32		= 0x10,
	CERVER_TYPE_TIMESTAMP	= 0x11,
	CERVER_TYPE_INT64		= 0x12

} cerver_type_t;

// takes no argument and returns a value (int)
typedef u8 (*Func)(void);

// takes an argument and does not return a value
typedef void (*Action)(void *);

// takes an argument and returns a value (int)
typedef u8 (*delegate)(void *);

// takes an argument and returns a value generic value ptr
typedef void *(*Work)(void *);

typedef int (*Comparator)(const void *, const void *);

#ifdef __cplusplus
}
#endif

#endif