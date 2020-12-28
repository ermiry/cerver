/*
 * Copyright (c) 2010-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 *
 * This file specifies a part of the site-specific configuration for
 * Jansson, namely those things that affect the public API in
 * jansson.h.
 *
 * The configure script copies this file to jansson_config.h and
 * replaces @var@ substitutions by values that fit your system. If you
 * cannot run the configure script, you can do the value substitution
 * by hand.
 */

#ifndef _CERVER_HTTP_JSON_CONFIG_H_
#define _CERVER_HTTP_JSON_CONFIG_H_

/* If your compiler supports the `long long` type and the strtoll()
   library function, JSON_INTEGER_IS_LONG_LONG is defined to 1,
   otherwise to 0. */
#define JSON_INTEGER_IS_LONG_LONG 1

/* If locale.h and localeconv() are available, define to 1,
   otherwise to 0. */
#define JSON_HAVE_LOCALECONV 1

/* If __atomic builtins are available they will be used to manage
   reference counts of json_t. */
#define JSON_HAVE_ATOMIC_BUILTINS 1

/* If __atomic builtins are not available we try using __sync builtins
   to manage reference counts of json_t. */
#define JSON_HAVE_SYNC_BUILTINS 1

/* Maximum recursion depth for parsing JSON input.
   This limits the depth of e.g. array-within-array constructions. */
#define JSON_PARSER_MAX_DEPTH 2048

#endif
