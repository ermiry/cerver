#ifndef _TYPES_H_
#define _TYPES_H_

#define EXIT_FAILURE    1

#define THREAD_OK       0

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef unsigned char asciiChar;

// takes no argument and returns a value (int)
typedef u8 (*Func)(void);
// takes an argument and does not return a value
typedef void (*Action)(void *);
// takes an argument and returns a value (int)
typedef u8 (*delegate)(void *);

// TODO: handle different data lengths
typedef enum ValueType {

    VALUE_TYPE_INT,
    VALUE_TYPE_DOUBLE,
    VALUE_TYPE_STRING,

} ValueType;

#endif