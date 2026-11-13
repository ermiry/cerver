/*
 * Copyright (c) 2010-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _CERVER_HTTP_JSON_CONFIG_H_
#define _CERVER_HTTP_JSON_CONFIG_H_

// the integers of the json are long long, read with strtoll ()
#define JSON_INTEGER_IS_LONG_LONG				1

// the deepest nesting of arrays and objects that a parse accepts
#define JSON_PARSER_MAX_DEPTH					2048

// the bytes of values that a parse can allocate for each byte that it reads,
// a document of small objects takes about 35 and [{}, {}, ...] about 67
#define JSON_PARSER_DEFAULT_MEMORY_FACTOR		(1 << 6)

// what every parse can allocate before it reads anything
#define JSON_PARSER_MEMORY_MINIMUM				(1 << 12)

// the dumps stop at the same depth that the parser accepts
#define JSON_DUMP_MAX_DEPTH						JSON_PARSER_MAX_DEPTH

// the digits of a real when its dump does not set a precision,
// enough for a double to be read back as the same value
#define JSON_REAL_DEFAULT_PRECISION				17

#endif
