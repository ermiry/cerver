/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See thirdparty.md for details.
 */

#ifndef _CERVER_HTTP_JSON_UTF_H_
#define _CERVER_HTTP_JSON_UTF_H_

#include <stddef.h>
#include <stdint.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

CERVER_HIDDEN int utf8_encode (
	int32_t codepoint, char *buffer, size_t *size
);

CERVER_HIDDEN size_t utf8_check_first (char byte);

CERVER_HIDDEN size_t utf8_check_full (
	const char *buffer, size_t size, int32_t *codepoint
);

CERVER_HIDDEN const char *utf8_iterate (
	const char *buffer, size_t bufsize, int32_t *codepoint
);

CERVER_HIDDEN int utf8_check_string (
	const char *string, size_t length
);

#ifdef __cplusplus
}
#endif

#endif
