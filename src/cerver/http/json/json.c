/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdint.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#include "cerver/config.h"

#include "cerver/http/json/config.h"
#include "cerver/http/json/hashtable.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/private.h"
#include "cerver/http/json/utf.h"
#include "cerver/http/json/value.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#pragma region memory

// the bytes that a parse can allocate for each byte that it reads, 0 has no limit,
// a body of [{}, {}, ...] takes about 60 bytes of objects for each one of its bytes
static size_t json_parser_memory_factor = JSON_PARSER_DEFAULT_MEMORY_FACTOR;

// the parse of this thread: the bytes that it can still allocate,
// and if it ran out of them, so its error says why
static __thread bool json_parse_limited = false;
static __thread bool json_parse_exhausted = false;
static __thread size_t json_parse_budget = 0;

void json_set_parser_memory_factor (size_t factor) {

	__atomic_store_n (&json_parser_memory_factor, factor, __ATOMIC_RELAXED);

}

static void json_parse_budget_start (void) {

	json_parse_limited = (__atomic_load_n (&json_parser_memory_factor, __ATOMIC_RELAXED) > 0);
	json_parse_exhausted = false;
	json_parse_budget = JSON_PARSER_MEMORY_MINIMUM;

}

static void json_parse_budget_end (void) {

	json_parse_limited = false;
	json_parse_budget = 0;

}

// every byte that the parse reads lets it allocate the factor more,
// so the memory of a document is bounded by its size
static void json_parse_budget_add (void) {

	if (json_parse_limited) {
		json_parse_budget += __atomic_load_n (&json_parser_memory_factor, __ATOMIC_RELAXED);
	}

}

// returns false if the parse has no room for the allocation
static bool json_parse_budget_take (size_t size) {

	bool taken = true;

	if (json_parse_limited) {
		if (size > json_parse_budget) {
			json_parse_exhausted = true;
			taken = false;
		}

		else {
			json_parse_budget -= size;
		}
	}

	return taken;

}

void *jsonp_malloc (size_t size) {

	return (size && json_parse_budget_take (size)) ? malloc (size) : NULL;

}

void jsonp_free (void *ptr) {

	if (ptr) free (ptr);

}

char *jsonp_strdup (const char *str) {

	return jsonp_strndup (str, strlen (str));

}

char *jsonp_strndup (const char *str, size_t len) {

	char *new_str = (char *) jsonp_malloc (len + 1);
	if (!new_str)
		return NULL;

	(void) memcpy (new_str, str, len);
	new_str[len] = '\0';
	return new_str;

}

#pragma endregion

#pragma region error

// the position of an error is an int in json_error_t,
// a document of more than INT_MAX bytes keeps the largest one
static int json_error_position (const size_t position) {

	return (position > (size_t) INT_MAX) ? INT_MAX : (int) position;

}

CERVER_INLINE enum json_error_code json_error_code (
	const json_error_t *e
) {

	return (enum json_error_code) e->text[JSON_ERROR_TEXT_LENGTH - 1];

}

void jsonp_error_init (
	json_error_t *error, const char *source
) {

	if (error) {
		error->text[0] = '\0';
		error->line = -1;
		error->column = -1;
		error->position = 0;
		if (source)
			jsonp_error_set_source (error, source);
		else
			error->source[0] = '\0';
	}

}

void jsonp_error_set_source (
	json_error_t *error, const char *source
) {

	// a long source is cut, the error never had a source without its end
	if (error && source) {
		(void) strncpy (error->source, source, JSON_ERROR_SOURCE_LENGTH - 1);
		error->source[JSON_ERROR_SOURCE_LENGTH - 1] = '\0';
	}

}

void jsonp_error_vset (
	json_error_t *error, int line, int column, size_t position,
	enum json_error_code code,
	const char *msg, va_list ap
) {

	if (!error)
		return;

	if (error->text[0] != '\0') {
		/* error already set */
		return;
	}

	error->line = line;
	error->column = column;
	error->position = json_error_position (position);

	(void) vsnprintf (error->text, JSON_ERROR_TEXT_LENGTH - 1, msg, ap);
	error->text[JSON_ERROR_TEXT_LENGTH - 2] = '\0';
	error->text[JSON_ERROR_TEXT_LENGTH - 1] = code;

}

void jsonp_error_set (
	json_error_t *error, int line, int column, size_t position,
	enum json_error_code code,
	const char *msg, ...
) {

	va_list ap;

	va_start (ap, msg);
	jsonp_error_vset (error, line, column, position, code, msg, ap);
	va_end (ap);

}

#pragma endregion

#pragma region strbuffer

#define STRBUFFER_MIN_SIZE 16
#define STRBUFFER_FACTOR   2
#define STRBUFFER_SIZE_MAX ((size_t)-1)

typedef struct strbuffer_t {

	char *value;
	size_t length; /* bytes used */
	size_t size;   /* bytes allocated */

} strbuffer_t;

static int strbuffer_init (strbuffer_t *strbuff) {

	strbuff->size = STRBUFFER_MIN_SIZE;
	strbuff->length = 0;

	strbuff->value = (char *) jsonp_malloc (strbuff->size);
	if (!strbuff->value)
		return -1;

	/* initialize to empty */
	strbuff->value[0] = '\0';
	return 0;

}

static void strbuffer_close (strbuffer_t *strbuff) {

	if (strbuff->value)
		jsonp_free (strbuff->value);

	strbuff->size = 0;
	strbuff->length = 0;
	strbuff->value = NULL;

}

static void strbuffer_clear (strbuffer_t *strbuff) {

	strbuff->length = 0;
	strbuff->value[0] = '\0';

}

static const char *strbuffer_value (
	const strbuffer_t *strbuff
) {
	
	return strbuff->value;
	
}

static char *strbuffer_steal_value (strbuffer_t *strbuff) {

	char *result = strbuff->value;
	strbuff->value = NULL;
	return result;

}

static int strbuffer_append_bytes (
	strbuffer_t *strbuff, const char *data, size_t size
) {

	if (size >= strbuff->size - strbuff->length) {
		size_t new_size;
		char *new_value;

		/* avoid integer overflow */
		if (strbuff->size > STRBUFFER_SIZE_MAX / STRBUFFER_FACTOR ||
			size > STRBUFFER_SIZE_MAX - 1 ||
			strbuff->length > STRBUFFER_SIZE_MAX - 1 - size)
			return -1;

		new_size = MAX (strbuff->size * STRBUFFER_FACTOR, strbuff->length + size + 1);

		new_value = (char *) jsonp_malloc (new_size);
		if (!new_value)
			return -1;

		(void) memcpy (new_value, strbuff->value, strbuff->length);

		jsonp_free (strbuff->value);
		strbuff->value = new_value;
		strbuff->size = new_size;
	}

	(void) memcpy (strbuff->value + strbuff->length, data, size);
	strbuff->length += size;
	strbuff->value[strbuff->length] = '\0';

	return 0;

}

static int strbuffer_append_byte (
	strbuffer_t *strbuff, char byte
) {

	return strbuffer_append_bytes (strbuff, &byte, 1);

}

static char strbuffer_pop (strbuffer_t *strbuff) {

	if (strbuff->length > 0) {
		char c = strbuff->value[--strbuff->length];
		strbuff->value[strbuff->length] = '\0';
		return c;
	}
	
	else return '\0';

}

#pragma endregion

#pragma region strconv

// the numbers of json always have a dot, whatever the locale of the process is:
// they are read and written with a "C" locale that is created once,
// localeconv () was not thread safe and a decimal point of two bytes
// ended the process in an assert ()
static locale_t json_numbers_locale = (locale_t) 0;
static pthread_once_t json_numbers_locale_once = PTHREAD_ONCE_INIT;

static void json_numbers_locale_create (void) {

	json_numbers_locale = newlocale (LC_NUMERIC_MASK, "C", (locale_t) 0);

}

static locale_t json_numbers_locale_get (void) {

	(void) pthread_once (&json_numbers_locale_once, json_numbers_locale_create);

	return json_numbers_locale;

}

// the lexer only gives numbers, a number that is not read whole is an error,
// an underflow gives zero or a subnormal value that is valid,
// only an overflow (an infinite value) is an error
static int jsonp_strtod (strbuffer_t *strbuffer, double *out) {

	const locale_t locale = json_numbers_locale_get ();
	if (!locale)
		return -1;

	char *end = NULL;

	errno = 0;
	const double value = strtod_l (strbuffer->value, &end, locale);

	if (end != (strbuffer->value + strbuffer->length))
		return -1;

	if ((errno == ERANGE) && isinf (value))
		return -1;

	*out = value;
	return 0;

}

// NaN and the infinite values are not json
static int jsonp_dtostr_format (
	char *buffer, size_t size, double value, int precision
) {

	const locale_t locale = json_numbers_locale_get ();
	if (!locale || !isfinite (value))
		return -1;

	const locale_t previous = uselocale (locale);

	const int ret = snprintf (buffer, size, "%.*g", precision, value);

	(void) uselocale (previous);

	return ret;

}

int jsonp_dtostr (
	char *buffer, size_t size, double value, int precision
) {

	int ret = 0;
	char *start = NULL, *end = NULL;
	size_t length = 0;

	if (precision == 0)
		precision = JSON_REAL_DEFAULT_PRECISION;

	ret = jsonp_dtostr_format (buffer, size, value, precision);
	if (ret < 0)
		return -1;

	length = (size_t)ret;
	if (length >= size)
		return -1;

	/* Make sure there's a dot or 'e' in the output. Otherwise
	   a real is converted to an integer when decoding */
	if (strchr (buffer, '.') == NULL && strchr (buffer, 'e') == NULL) {
		if (length + 3 >= size) {
			/* No space to append ".0" */
			return -1;
		}
		buffer[length] = '.';
		buffer[length + 1] = '0';
		buffer[length + 2] = '\0';
		length += 2;
	}

	/* Remove leading '+' from positive exponent. Also remove leading
	   zeros from exponents (added by some printf() implementations) */
	start = strchr (buffer, 'e');
	if (start) {
		start++;
		end = start + 1;

		if (*start == '-')
			start++;

		while (*end == '0')
			end++;

		if (end != start) {
			memmove (start, end, length - (size_t)(end - buffer));
			length -= (size_t)(end - start);
		}
	}

	return (int)length;

}

#pragma endregion

#pragma region dump

#define MAX_INTEGER_STR_LENGTH 100
#define MAX_REAL_STR_LENGTH    100

#define FLAGS_TO_INDENT(f)    ((f)&0x1F)
#define FLAGS_TO_PRECISION(f) (((f) >> 11) & 0x1F)

struct buffer {

	const size_t size;
	size_t used;
	char *data;

};

static int dump_to_strbuffer (
	const char *buffer, size_t size, void *data
) {

	return strbuffer_append_bytes ((strbuffer_t *)data, buffer, size);

}

static int dump_to_buffer (
	const char *buffer, size_t size, void *data
) {

	struct buffer *buf = (struct buffer *) data;

	if (buf->used + size <= buf->size)
		(void) memcpy (&buf->data[buf->used], buffer, size);

	buf->used += size;
	return 0;

}

static int dump_to_file (
	const char *buffer, size_t size, void *data
) {

	FILE *dest = (FILE *)data;
	if (fwrite (buffer, size, 1, dest) != 1)
		return -1;
	
	return 0;

}

static int dump_to_fd (
	const char *buffer, size_t size, void *data
) {

	// HAVE_UNISTD_H of upstream was never defined, so the dumps to a fd always failed
	int *dest = (int *)data;
	if (write (*dest, buffer, size) == (ssize_t)size)
		return 0;

	return -1;

}

/* 32 spaces (the maximum indentation size) */
static const char whitespace[] = "                                ";

static int dump_indent (
	size_t flags, int depth, int space, json_dump_callback_t dump,
	void *data
) {

	if (FLAGS_TO_INDENT (flags) > 0) {
		unsigned int ws_count = FLAGS_TO_INDENT (flags), n_spaces = depth * ws_count;

		if (dump ("\n", 1, data))
			return -1;

		while (n_spaces > 0) {
			int cur_n =
				n_spaces < sizeof whitespace - 1 ? n_spaces : sizeof whitespace - 1;

			if (dump (whitespace, cur_n, data))
				return -1;

			n_spaces -= cur_n;
		}
	}
	
	else if (space && !(flags & JSON_COMPACT)) {
		return dump (" ", 1, data);
	}

	return 0;

}

static int dump_string (
	const char *str, size_t len, json_dump_callback_t dump, void *data,
	size_t flags
) {

	const char *pos, *end, *lim;
	int32_t codepoint = 0;

	if (dump ("\"", 1, data))
		return -1;

	end = pos = str;
	lim = str + len;
	while (1) {
		const char *text;
		char seq[13];
		int length;

		while (end < lim) {
			end = utf8_iterate (pos, lim - pos, &codepoint);
			if (!end)
				return -1;

			/* mandatory escape or control char */
			if (codepoint == '\\' || codepoint == '"' || codepoint < 0x20)
				break;

			/* slash */
			if ((flags & JSON_ESCAPE_SLASH) && codepoint == '/')
				break;

			/* non-ASCII */
			if ((flags & JSON_ENSURE_ASCII) && codepoint > 0x7F)
				break;

			pos = end;
		}

		if (pos != str) {
			if (dump (str, pos - str, data))
				return -1;
		}

		if (end == pos)
			break;

		/* handle \, /, ", and control codes */
		length = 2;
		switch (codepoint) {
			case '\\':
				text = "\\\\";
				break;
			case '\"':
				text = "\\\"";
				break;
			case '\b':
				text = "\\b";
				break;
			case '\f':
				text = "\\f";
				break;
			case '\n':
				text = "\\n";
				break;
			case '\r':
				text = "\\r";
				break;
			case '\t':
				text = "\\t";
				break;
			case '/':
				text = "\\/";
				break;
			default: {
				/* codepoint is in BMP */
				if (codepoint < 0x10000) {
					snprintf (seq, sizeof (seq), "\\u%04X", (unsigned int)codepoint);
					length = 6;
				}

				/* not in BMP -> construct a UTF-16 surrogate pair */
				else {
					int32_t first, last;

					codepoint -= 0x10000;
					first = 0xD800 | ((codepoint & 0xffc00) >> 10);
					last = 0xDC00 | (codepoint & 0x003ff);

					snprintf (seq, sizeof (seq), "\\u%04X\\u%04X", (unsigned int)first,
							 (unsigned int)last);
					length = 12;
				}

				text = seq;
				break;
			}
		}

		if (dump (text, length, data))
			return -1;

		str = pos = end;
	}

	return dump ("\"", 1, data);

}

static int compare_keys (const void *key1, const void *key2) {

	return strcmp (*(const char **) key1, *(const char **) key2);

}

static int do_dump (
	const json_t *json, size_t flags, int depth, hashtable_t *parents,
	json_dump_callback_t dump, void *data
) {

	int embed = flags & JSON_EMBED;

	flags &= ~JSON_EMBED;

	// a tree built in code can be deeper than what the parser accepts,
	// its dump would end the stack of the thread
	if (!json || (depth > JSON_DUMP_MAX_DEPTH))
		return -1;

	switch (json_typeof (json)) {
		case JSON_NULL:
			return dump ("null", 4, data);

		case JSON_TRUE:
			return dump ("true", 4, data);

		case JSON_FALSE:
			return dump ("false", 5, data);

		case JSON_INTEGER: {
			char buffer[MAX_INTEGER_STR_LENGTH];
			int size;

			size = snprintf (buffer, MAX_INTEGER_STR_LENGTH, "%" JSON_INTEGER_FORMAT,
							json_integer_value (json));
			if (size < 0 || size >= MAX_INTEGER_STR_LENGTH)
				return -1;

			return dump (buffer, size, data);
		}

		case JSON_REAL: {
			char buffer[MAX_REAL_STR_LENGTH];
			int size;
			double value = json_real_value (json);

			size = jsonp_dtostr (
				buffer, MAX_REAL_STR_LENGTH, value,
				FLAGS_TO_PRECISION (flags)
			);

			if (size < 0)
				return -1;

			return dump (buffer, size, data);
		}

		case JSON_STRING:
			return dump_string (
				json_string_value (json), json_string_length (json), dump,
				data, flags
			);

		case JSON_ARRAY: {
			size_t n;
			size_t i;
			/* Space for "0x", double the sizeof a pointer for the hex and a
			 * terminator. */
			char key[2 + (sizeof (json) * 2) + 1];

			/* detect circular references */
			if (jsonp_loop_check (parents, json, key, sizeof (key)))
				return -1;

			n = json_array_size (json);

			if (!embed && dump ("[", 1, data))
				return -1;
			if (n == 0) {
				hashtable_del (parents, key);
				return embed ? 0 : dump ("]", 1, data);
			}
			if (dump_indent (flags, depth + 1, 0, dump, data))
				return -1;

			for (i = 0; i < n; ++i) {
				if (do_dump (
					json_array_get (json, i), flags, depth + 1, parents, dump,
					data
				))
					return -1;

				if (i < n - 1) {
					if (dump (",", 1, data) ||
						dump_indent (flags, depth + 1, 1, dump, data))
						return -1;
				} else {
					if (dump_indent (flags, depth, 0, dump, data))
						return -1;
				}
			}

			hashtable_del (parents, key);
			return embed ? 0 : dump ("]", 1, data);
		}

		case JSON_OBJECT: {
			void *iter;
			const char *separator;
			int separator_length;
			char loop_key[JSON_LOOP_KEY_LENGTH];

			if (flags & JSON_COMPACT) {
				separator = ":";
				separator_length = 1;
			} else {
				separator = ": ";
				separator_length = 2;
			}

			/* detect circular references */
			if (jsonp_loop_check (parents, json, loop_key, sizeof (loop_key)))
				return -1;

			iter = json_object_iter ((json_t *)json);

			if (!embed && dump ("{", 1, data))
				return -1;
			if (!iter) {
				hashtable_del (parents, loop_key);
				return embed ? 0 : dump ("}", 1, data);
			}
			if (dump_indent (flags, depth + 1, 0, dump, data))
				return -1;

			if (flags & JSON_SORT_KEYS) {
				const char **keys;
				size_t size, i;

				size = json_object_size (json);
				keys = (const char **) jsonp_malloc (size * sizeof (const char *));
				if (!keys)
					return -1;

				i = 0;
				while (iter) {
					keys[i] = json_object_iter_key (iter);
					iter = json_object_iter_next ((json_t *)json, iter);
					i++;
				}
				assert (i == size);

				qsort (keys, size, sizeof (const char *), compare_keys);

				for (i = 0; i < size; i++) {
					const char *key;
					json_t *value;

					key = keys[i];
					value = json_object_get (json, key);
					assert (value);

					dump_string (key, strlen (key), dump, data, flags);
					if (dump (separator, separator_length, data) ||
						do_dump (value, flags, depth + 1, parents, dump, data)) {
						jsonp_free (keys);
						return -1;
					}

					if (i < size - 1) {
						if (dump (",", 1, data) ||
							dump_indent (flags, depth + 1, 1, dump, data)) {
							jsonp_free (keys);
							return -1;
						}
					} else {
						if (dump_indent (flags, depth, 0, dump, data)) {
							jsonp_free (keys);
							return -1;
						}
					}
				}

				jsonp_free (keys);
			} else {
				/* Don't sort keys */

				while (iter) {
					void *next = json_object_iter_next ((json_t *)json, iter);
					const char *key = json_object_iter_key (iter);

					dump_string (key, strlen (key), dump, data, flags);
					if (dump (separator, separator_length, data) ||
						do_dump (json_object_iter_value (iter), flags, depth + 1, parents,
								dump, data))
						return -1;

					if (next) {
						if (dump (",", 1, data) ||
							dump_indent (flags, depth + 1, 1, dump, data))
							return -1;
					} else {
						if (dump_indent (flags, depth, 0, dump, data))
							return -1;
					}

					iter = next;
				}
			}

			hashtable_del (parents, loop_key);
			return embed ? 0 : dump ("}", 1, data);
		}

		default:
			/* not reached */
			return -1;
	}
}

char *json_dumps (const json_t *json, size_t flags) {

	strbuffer_t strbuff;
	char *result;

	if (strbuffer_init (&strbuff))
		return NULL;

	if (json_dump_callback (json, dump_to_strbuffer, (void *)&strbuff, flags))
		result = NULL;
	else
		result = jsonp_strdup (strbuffer_value (&strbuff));

	strbuffer_close (&strbuff);
	return result;

}

size_t json_dumpb (
	const json_t *json, char *buffer, size_t size, size_t flags
) {

	struct buffer buf = {size, 0, buffer};

	if (json_dump_callback (json, dump_to_buffer, (void *)&buf, flags))
		return 0;

	return buf.used;

}

int json_dumpf (
	const json_t *json, FILE *output, size_t flags
) {

	return json_dump_callback (json, dump_to_file, (void *) output, flags);

}

int json_dumpfd (const json_t *json, int output, size_t flags) {

	return json_dump_callback (json, dump_to_fd, (void *) &output, flags);

}

int json_dump_file (
	const json_t *json, const char *path, size_t flags
) {

	int result;

	FILE *output = fopen (path, "w");
	if (!output)
		return -1;

	result = json_dumpf (json, output, flags);

	if (fclose (output) != 0)
		return -1;

	return result;

}

int json_dump_callback (
	const json_t *json, json_dump_callback_t callback, void *data,
	size_t flags
) {

	int res;
	hashtable_t parents_set;

	if (!(flags & JSON_ENCODE_ANY)) {
		if (!json_is_array (json) && !json_is_object (json))
			return -1;
	}

	if (hashtable_init (&parents_set))
		return -1;
	res = do_dump (json, flags, 0, &parents_set, callback, data);
	hashtable_close (&parents_set);

	return res;

}

#pragma endregion

#pragma region load

#define STREAM_STATE_OK    0
#define STREAM_STATE_EOF   -1
#define STREAM_STATE_ERROR -2

#define TOKEN_INVALID -1
#define TOKEN_EOF     0
#define TOKEN_STRING  256
#define TOKEN_INTEGER 257
#define TOKEN_REAL    258
#define TOKEN_TRUE    259
#define TOKEN_FALSE   260
#define TOKEN_NULL    261

/* Locale independent versions of isxxx() functions */
#define l_isupper(c) ('A' <= (c) && (c) <= 'Z')
#define l_islower(c) ('a' <= (c) && (c) <= 'z')
#define l_isalpha(c) (l_isupper (c) || l_islower (c))
#define l_isdigit(c) ('0' <= (c) && (c) <= '9')
#define l_isxdigit(c)                                                                    \
	(l_isdigit (c) || ('A' <= (c) && (c) <= 'F') || ('a' <= (c) && (c) <= 'f'))

/* Read one byte from stream, convert to unsigned char, then int, and
   return. return EOF on end of file. This corresponds to the
   behaviour of fgetc(). */
typedef int (*get_func)(void *data);

typedef struct {

	get_func get;
	void *data;
	char buffer[5];
	size_t buffer_pos;
	int state;
	int line;
	int column, last_column;
	size_t position;

} stream_t;

typedef struct {

	stream_t stream;
	strbuffer_t saved_text;
	size_t flags;
	size_t depth;
	int token;
	union {
		struct {
			char *val;
			size_t len;
		} string;
		json_int_t integer;
		double real;
	} value;

} lex_t;

#define stream_to_lex(stream) container_of (stream, lex_t, stream)

// the errors of a parse

CERVER_PRINTF_FORMAT (4, 5) static void error_set (
	json_error_t *error, const lex_t *lex, enum json_error_code code,
	const char *msg, ...
) {
	va_list ap;
	char msg_text[JSON_ERROR_TEXT_LENGTH];
	char msg_with_context[JSON_ERROR_TEXT_LENGTH_MAX];

	int line = -1, col = -1;
	size_t pos = 0;
	const char *result = msg_text;

	if (!error)
		return;

	va_start (ap, msg);
	vsnprintf (msg_text, JSON_ERROR_TEXT_LENGTH, msg, ap);
	msg_text[JSON_ERROR_TEXT_LENGTH - 1] = '\0';
	va_end (ap);

	if (lex) {
		const char *saved_text = strbuffer_value (&lex->saved_text);

		line = lex->stream.line;
		col = lex->stream.column;
		pos = lex->stream.position;

		if (saved_text && saved_text[0]) {
			if (lex->saved_text.length <= 20) {
				(void) snprintf (
					msg_with_context, JSON_ERROR_TEXT_LENGTH_MAX - 1,
					"%s near '%s'", msg_text, saved_text
				);
				
				result = msg_with_context;
			}
		}
		
		else {
			if (code == json_error_invalid_syntax) {
				/* More specific error code for premature end of file. */
				code = json_error_premature_end_of_input;
			}

			if (lex->stream.state == STREAM_STATE_ERROR) {
				/* No context for UTF-8 decoding errors */
				result = msg_text;
			}
			
			else {
				(void) snprintf (
					msg_with_context, JSON_ERROR_TEXT_LENGTH_MAX - 1,
					"%s near end of file", msg_text
				);
				
				result = msg_with_context;
			}
		}
	}

	jsonp_error_set (error, line, col, pos, code, "%s", result);
}

// the lexer

static void stream_init (
	stream_t *stream, get_func get, void *data
) {

	stream->get = get;
	stream->data = data;
	stream->buffer[0] = '\0';
	stream->buffer_pos = 0;

	stream->state = STREAM_STATE_OK;
	stream->line = 1;
	stream->column = 0;
	stream->position = 0;

}

// the first byte of a character that takes more than one byte in UTF-8
#define STREAM_MULTI_BYTE_FIRST			0x80
#define STREAM_MULTI_BYTE_LAST			0xFF

// the stream can not be read after a byte that is not UTF-8
static int stream_decode_failed (
	stream_t *stream, json_error_t *error, const int c
) {

	stream->state = STREAM_STATE_ERROR;

	error_set (
		error, stream_to_lex (stream), json_error_invalid_utf8,
		"unable to decode byte 0x%x", (unsigned int) c
	);

	return STREAM_STATE_ERROR;

}

// reads the rest of a character of many bytes into the buffer
// returns true if it is valid UTF-8
static bool stream_read_multi_byte (stream_t *stream, const int c) {

	const size_t count = utf8_check_first ((char) c);
	if (!count) return false;

	assert (count >= 2);

	for (size_t index = 1; index < count; index++) {
		stream->buffer[index] = (char) stream->get (stream->data);
	}

	if (!utf8_check_full (stream->buffer, count, NULL)) return false;

	stream->buffer[count] = '\0';

	return true;

}

// the line and the column of the character that was read
static void stream_advance (stream_t *stream, const int c) {

	stream->position += 1;

	if (c == '\n') {
		stream->line += 1;
		stream->last_column = stream->column;
		stream->column = 0;
	}

	// the column of Unicode characters, only the first byte of each one counts
	else if (utf8_check_first ((char) c)) {
		stream->column += 1;
	}

}

static int stream_get (
	stream_t *stream, json_error_t *error
) {

	if (stream->state != STREAM_STATE_OK) return stream->state;

	if (!stream->buffer[stream->buffer_pos]) {
		const int c = stream->get (stream->data);
		if (c == EOF) {
			stream->state = STREAM_STATE_EOF;
			return STREAM_STATE_EOF;
		}

		json_parse_budget_add ();

		stream->buffer[0] = (char) c;
		stream->buffer_pos = 0;

		if ((STREAM_MULTI_BYTE_FIRST <= c) && (c <= STREAM_MULTI_BYTE_LAST)) {
			if (!stream_read_multi_byte (stream, c)) return stream_decode_failed (stream, error, c);
		}

		else {
			stream->buffer[1] = '\0';
		}
	}

	const int c = stream->buffer[stream->buffer_pos++];

	stream_advance (stream, c);

	return c;

}

static void stream_unget (stream_t *stream, int c) {
	if (c == STREAM_STATE_EOF || c == STREAM_STATE_ERROR)
		return;

	stream->position--;
	if (c == '\n') {
		stream->line--;
		stream->column = stream->last_column;
	} else if (utf8_check_first (c))
		stream->column--;

	assert (stream->buffer_pos > 0);
	stream->buffer_pos--;
	assert (stream->buffer[stream->buffer_pos] == c);
}

static int lex_get (lex_t *lex, json_error_t *error) {
	return stream_get (&lex->stream, error);
}

static void lex_save (lex_t *lex, int c) {
	
	(void) strbuffer_append_byte (&lex->saved_text, c);
	
}

static int lex_get_save (lex_t *lex, json_error_t *error) {

	int c = stream_get (&lex->stream, error);
	if (c != STREAM_STATE_EOF && c != STREAM_STATE_ERROR)
		lex_save (lex, c);
	
	return c;

}

static void lex_unget (lex_t *lex, int c) {
	
	stream_unget (&lex->stream, c);
	
}

static void lex_unget_unsave (lex_t *lex, int c) {

	if (c != STREAM_STATE_EOF && c != STREAM_STATE_ERROR) {
/* Since we treat warnings as errors, when assertions are turned
 * off the "d" variable would be set but never used. Which is
 * treated as an error by GCC.
 */
#ifndef NDEBUG
		char d;
#endif
		stream_unget (&lex->stream, c);
#ifndef NDEBUG
		d =
#endif
			strbuffer_pop (&lex->saved_text);
		assert (c == d);
	}

}

static void lex_save_cached (lex_t *lex) {

	while (lex->stream.buffer[lex->stream.buffer_pos] != '\0') {
		lex_save (lex, lex->stream.buffer[lex->stream.buffer_pos]);
		lex->stream.buffer_pos++;
		lex->stream.position++;
	}

}

static void lex_free_string (lex_t *lex) {

	jsonp_free (lex->value.string.val);
	lex->value.string.val = NULL;
	lex->value.string.len = 0;

}

/* assumes that str points to 'u' plus at least 4 valid hex digits */
static int32_t decode_unicode_escape (const char *str) {

	int i;
	int32_t value = 0;

	assert (str[0] == 'u');

	for (i = 1; i <= 4; i++) {
		char c = str[i];
		value <<= 4;
		if (l_isdigit (c))
			value += c - '0';
		else if (l_islower (c))
			value += c - 'a' + 10;
		else if (l_isupper (c))
			value += c - 'A' + 10;
		else
			return -1;
	}

	return value;

}

// the hex digits of a \uXXXX escape
#define LEX_UNICODE_ESCAPE_DIGITS		4

// the 'u' and the hex digits of a \uXXXX escape
#define LEX_UNICODE_ESCAPE_LENGTH		(1 + LEX_UNICODE_ESCAPE_DIGITS)

// the last byte that is a control character in a string
#define LEX_CONTROL_CHARACTER_LAST		0x1F

// the halves of a character beyond the basic plane in UTF-16 (RFC 8259 §7)
#define LEX_HIGH_SURROGATE_FIRST		0xD800
#define LEX_HIGH_SURROGATE_LAST			0xDBFF
#define LEX_LOW_SURROGATE_FIRST			0xDC00
#define LEX_LOW_SURROGATE_LAST			0xDFFF
#define LEX_SURROGATE_SHIFT				10
#define LEX_SURROGATE_BASE				0x10000

// a control character inside a string is an error
static bool lex_scan_string_control (lex_t *lex, const int c, json_error_t *error) {

	lex_unget_unsave (lex, c);

	if (c == '\n') {
		error_set (error, lex, json_error_invalid_syntax, "unexpected newline");
	}

	else {
		error_set (error, lex, json_error_invalid_syntax, "control character 0x%x", (unsigned int) c);
	}

	return false;

}

static bool lex_is_short_escape (const int c) {

	return (c == '"') || (c == '\\') || (c == '/') || (c == 'b')
		|| (c == 'f') || (c == 'n') || (c == 'r') || (c == 't');

}

// an escape after its '\\': \uXXXX or one of the short ones
// returns true if it is valid, with the character after it in c
static bool lex_scan_string_escape (lex_t *lex, int *c, json_error_t *error) {

	*c = lex_get_save (lex, error);

	if (*c == 'u') {
		*c = lex_get_save (lex, error);
		for (unsigned int digit = 0; digit < LEX_UNICODE_ESCAPE_DIGITS; digit++) {
			if (!l_isxdigit (*c)) {
				error_set (error, lex, json_error_invalid_syntax, "invalid escape");
				return false;
			}

			*c = lex_get_save (lex, error);
		}
	}

	else if (lex_is_short_escape (*c)) {
		*c = lex_get_save (lex, error);
	}

	else {
		error_set (error, lex, json_error_invalid_syntax, "invalid escape");
		return false;
	}

	return true;

}

// reads the string up to its closing quote into the saved text
// returns true if it is a valid string
static bool lex_scan_string_read (lex_t *lex, json_error_t *error) {

	int c = lex_get_save (lex, error);

	while (c != '"') {
		if (c == STREAM_STATE_ERROR) return false;

		if (c == STREAM_STATE_EOF) {
			error_set (error, lex, json_error_premature_end_of_input, "premature end of input");
			return false;
		}

		if ((0 <= c) && (c <= LEX_CONTROL_CHARACTER_LAST)) return lex_scan_string_control (lex, c, error);

		if (c == '\\') {
			if (!lex_scan_string_escape (lex, &c, error)) return false;
		}

		else {
			c = lex_get_save (lex, error);
		}
	}

	return true;

}

// an escape whose 4 characters are not hex digits, from its '\\'
static bool lex_unicode_escape_invalid (lex_t *lex, json_error_t *error, const char *escape) {

	error_set (error, lex, json_error_invalid_syntax, "invalid Unicode escape '%.6s'", escape);

	return false;

}

// a surrogate without its other half
static bool lex_unicode_value_invalid (lex_t *lex, json_error_t *error, const int32_t value) {

	error_set (error, lex, json_error_invalid_syntax, "invalid Unicode '\\u%04X'", (unsigned int) value);

	return false;

}

// a high surrogate followed by a value that is not a low one
static bool lex_unicode_pair_invalid (
	lex_t *lex, json_error_t *error, const int32_t first, const int32_t second
) {

	error_set (
		error, lex, json_error_invalid_syntax,
		"invalid Unicode '\\u%04X\\u%04X'", (unsigned int) first, (unsigned int) second
	);

	return false;

}

// the code point of a \uXXXX escape that p points to, with the 'u',
// or of two that form a surrogate pair, p is moved after them
// returns true if the code point is valid
static bool lex_decode_unicode (
	lex_t *lex, json_error_t *error, const char **p, int32_t *code_point
) {

	int32_t value = decode_unicode_escape (*p);
	if (value < 0) return lex_unicode_escape_invalid (lex, error, *p - 1);

	*p += LEX_UNICODE_ESCAPE_LENGTH;

	if ((LEX_LOW_SURROGATE_FIRST <= value) && (value <= LEX_LOW_SURROGATE_LAST)) {
		return lex_unicode_value_invalid (lex, error, value);
	}

	if ((LEX_HIGH_SURROGATE_FIRST <= value) && (value <= LEX_HIGH_SURROGATE_LAST)) {
		// a high surrogate must be followed by a low one
		if (((*p)[0] != '\\') || ((*p)[1] != 'u')) return lex_unicode_value_invalid (lex, error, value);

		*p += 1;

		const int32_t second = decode_unicode_escape (*p);
		if (second < 0) return lex_unicode_escape_invalid (lex, error, *p - 1);

		*p += LEX_UNICODE_ESCAPE_LENGTH;

		if ((second < LEX_LOW_SURROGATE_FIRST) || (second > LEX_LOW_SURROGATE_LAST)) {
			return lex_unicode_pair_invalid (lex, error, value, second);
		}

		value = ((value - LEX_HIGH_SURROGATE_FIRST) << LEX_SURROGATE_SHIFT)
			+ (second - LEX_LOW_SURROGATE_FIRST) + LEX_SURROGATE_BASE;
	}

	*code_point = value;

	return true;

}

// the byte of a short escape after its '\\'
static char lex_short_escape_value (const char escape) {

	char value = escape;

	switch (escape) {
		case 'b': value = '\b'; break;
		case 'f': value = '\f'; break;
		case 'n': value = '\n'; break;
		case 'r': value = '\r'; break;
		case 't': value = '\t'; break;

		// '"', '\\' and '/' are themselves
		default: break;
	}

	return value;

}

// the value of the saved string with its escapes decoded,
// it is never longer than its source: a short escape (2 bytes) gives 1 byte,
// a \uXXXX (6 bytes) at most 3, and a surrogate pair (12 bytes) 4
// returns true on success
static bool lex_scan_string_decode (lex_t *lex, json_error_t *error) {

	char *value = (char *) jsonp_malloc (lex->saved_text.length + 1);
	if (!value) return false;

	lex->value.string.val = value;

	// after the opening quote
	const char *p = strbuffer_value (&lex->saved_text) + 1;

	while (*p != '"') {
		if (*p != '\\') {
			*(value++) = *(p++);
		}

		else if (*(++p) == 'u') {
			int32_t code_point = 0;
			if (!lex_decode_unicode (lex, error, &p, &code_point)) return false;

			size_t length = 0;
			if (utf8_encode (code_point, value, &length)) assert (0);
			value += length;
		}

		else {
			*(value++) = lex_short_escape_value (*(p++));
		}
	}

	*value = '\0';
	lex->value.string.len = (size_t) (value - lex->value.string.val);

	return true;

}

static void lex_scan_string (lex_t *lex, json_error_t *error) {

	lex->value.string.val = NULL;
	lex->token = TOKEN_INVALID;

	if (lex_scan_string_read (lex, error) && lex_scan_string_decode (lex, error)) {
		lex->token = TOKEN_STRING;
	}

	else {
		lex_free_string (lex);
	}

}

// the integers of the json are long long
#define json_strtoint strtoll

// the base of the integers of the json text
#define LEX_INTEGER_BASE				10

// a part of a number that is not valid, never a byte nor a state of the stream
#define LEX_NUMBER_INVALID				-3

// reads digits while they come, returns the first character that is not one
static int lex_scan_digits (lex_t *lex, json_error_t *error) {

	int c = 0;

	do {
		c = lex_get_save (lex, error);
	} while (l_isdigit (c));

	return c;

}

// an integer that fits in a json_int_t
// returns 0 on success, -1 on error
static int lex_scan_integer (lex_t *lex, int c, json_error_t *error) {

	lex_unget_unsave (lex, c);

	const char *saved_text = strbuffer_value (&lex->saved_text);

	char *end = NULL;
	errno = 0;
	const json_int_t integer = json_strtoint (saved_text, &end, LEX_INTEGER_BASE);
	if (errno == ERANGE) {
		if (integer < 0) {
			error_set (error, lex, json_error_numeric_overflow, "too big negative integer");
		}

		else {
			error_set (error, lex, json_error_numeric_overflow, "too big integer");
		}

		return -1;
	}

	assert (end == saved_text + lex->saved_text.length);

	lex->token = TOKEN_INTEGER;
	lex->value.integer = integer;

	return 0;

}

// the digits after the dot of a real
// returns the character after them, or LEX_NUMBER_INVALID if there are none
static int lex_scan_fraction (lex_t *lex, json_error_t *error) {

	int c = lex_get (lex, error);
	if (!l_isdigit (c)) {
		lex_unget (lex, c);
		return LEX_NUMBER_INVALID;
	}

	lex_save (lex, c);

	return lex_scan_digits (lex, error);

}

// the exponent of a real, with its sign and its digits
// returns the character after them, or LEX_NUMBER_INVALID if there are no digits
static int lex_scan_exponent (lex_t *lex, json_error_t *error) {

	int c = lex_get_save (lex, error);
	if ((c == '+') || (c == '-')) c = lex_get_save (lex, error);

	if (!l_isdigit (c)) {
		lex_unget_unsave (lex, c);
		return LEX_NUMBER_INVALID;
	}

	return lex_scan_digits (lex, error);

}

// the integer part of a number: a single 0 or digits that do not start with 0
// returns the character after it, or LEX_NUMBER_INVALID if it is not valid
static int lex_scan_integer_part (lex_t *lex, int c, json_error_t *error) {

	if (c == '0') {
		c = lex_get_save (lex, error);
		if (l_isdigit (c)) {
			lex_unget_unsave (lex, c);
			return LEX_NUMBER_INVALID;
		}
	}

	else if (l_isdigit (c)) {
		c = lex_scan_digits (lex, error);
	}

	else {
		lex_unget_unsave (lex, c);
		return LEX_NUMBER_INVALID;
	}

	return c;

}

static int lex_scan_number (
	lex_t *lex, int c, json_error_t *error
) {

	lex->token = TOKEN_INVALID;

	if (c == '-') c = lex_get_save (lex, error);

	c = lex_scan_integer_part (lex, c, error);
	if (c == LEX_NUMBER_INVALID) return -1;

	if (!(lex->flags & JSON_DECODE_INT_AS_REAL) && (c != '.') && (c != 'E') && (c != 'e')) {
		return lex_scan_integer (lex, c, error);
	}

	if (c == '.') {
		c = lex_scan_fraction (lex, error);
		if (c == LEX_NUMBER_INVALID) return -1;
	}

	if ((c == 'E') || (c == 'e')) {
		c = lex_scan_exponent (lex, error);
		if (c == LEX_NUMBER_INVALID) return -1;
	}

	lex_unget_unsave (lex, c);

	double real = 0;
	if (jsonp_strtod (&lex->saved_text, &real)) {
		error_set (error, lex, json_error_numeric_overflow, "real number overflow");
		return -1;
	}

	lex->token = TOKEN_REAL;
	lex->value.real = real;

	return 0;

}

#define LEX_WORD_TRUE					"true"
#define LEX_WORD_FALSE					"false"
#define LEX_WORD_NULL					"null"

// the whole word is read, so its error is clearer
static void lex_scan_word (lex_t *lex, json_error_t *error) {

	int c = 0;
	do {
		c = lex_get_save (lex, error);
	} while (l_isalpha (c));

	lex_unget_unsave (lex, c);

	const char *saved_text = strbuffer_value (&lex->saved_text);

	if (!strcmp (saved_text, LEX_WORD_TRUE)) lex->token = TOKEN_TRUE;
	else if (!strcmp (saved_text, LEX_WORD_FALSE)) lex->token = TOKEN_FALSE;
	else if (!strcmp (saved_text, LEX_WORD_NULL)) lex->token = TOKEN_NULL;
	else lex->token = TOKEN_INVALID;

}

static int lex_scan (
	lex_t *lex, json_error_t *error
) {

	int c = 0;

	strbuffer_clear (&lex->saved_text);

	if (lex->token == TOKEN_STRING)
		lex_free_string (lex);

	do
		c = lex_get (lex, error);
	while (c == ' ' || c == '\t' || c == '\n' || c == '\r');

	if (c == STREAM_STATE_EOF) {
		lex->token = TOKEN_EOF;
		return lex->token;
	}

	if (c == STREAM_STATE_ERROR) {
		lex->token = TOKEN_INVALID;
		return lex->token;
	}

	lex_save (lex, c);

	if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',')
		lex->token = c;

	else if (c == '"')
		lex_scan_string (lex, error);

	// a number that fails keeps its invalid token
	else if (l_isdigit (c) || (c == '-')) {
		(void) lex_scan_number (lex, c, error);
	}

	else if (l_isalpha (c)) {
		lex_scan_word (lex, error);
	}

	// the rest of the UTF-8 sequence is saved, so its error is valid UTF-8
	else {
		lex_save_cached (lex);
		lex->token = TOKEN_INVALID;
	}

	return lex->token;

}

static char *lex_steal_string (
	lex_t *lex, size_t *out_len
) {

	char *result = NULL;
	if (lex->token == TOKEN_STRING) {
		result = lex->value.string.val;
		*out_len = lex->value.string.len;
		lex->value.string.val = NULL;
		lex->value.string.len = 0;
	}

	return result;

}

static int lex_init (
	lex_t *lex, get_func get, size_t flags, void *data
) {

	stream_init (&lex->stream, get, data);
	if (strbuffer_init (&lex->saved_text))
		return -1;

	lex->flags = flags;
	lex->token = TOKEN_INVALID;

	return 0;

}

static void lex_close (lex_t *lex) {

	if (lex->token == TOKEN_STRING)
		lex_free_string (lex);
	strbuffer_close (&lex->saved_text);

}

// the parser

static json_t *parse_value (
	lex_t *lex, size_t flags, json_error_t *error
);

// the key of a member: a string without NUL bytes that is not repeated if they are rejected
// returns the key, released with jsonp_free (), or NULL on error
static char *parse_object_key (
	lex_t *lex, size_t flags, json_error_t *error, const json_t *object
) {

	if (lex->token != TOKEN_STRING) {
		error_set (error, lex, json_error_invalid_syntax, "string or '}' expected");
		return NULL;
	}

	size_t length = 0;
	char *key = lex_steal_string (lex, &length);
	if (!key) return NULL;

	if (memchr (key, '\0', length)) {
		error_set (error, lex, json_error_null_byte_in_key, "NUL byte in object key not supported");
	}

	else if ((flags & JSON_REJECT_DUPLICATES) && json_object_get (object, key)) {
		error_set (error, lex, json_error_duplicate_key, "duplicate object key");
	}

	else {
		return key;
	}

	jsonp_free (key);

	return NULL;

}

// a "key": value of the object
// returns true if it was parsed and set
static bool parse_object_member (
	lex_t *lex, size_t flags, json_error_t *error, json_t *object
) {

	char *key = parse_object_key (lex, flags, error, object);
	if (!key) return false;

	bool parsed = false;

	lex_scan (lex, error);
	if (lex->token != ':') {
		error_set (error, lex, json_error_invalid_syntax, "':' expected");
	}

	else {
		lex_scan (lex, error);

		json_t *value = parse_value (lex, flags, error);
		parsed = value && !json_object_set_new_nocheck (object, key, value);
	}

	jsonp_free (key);

	return parsed;

}

static json_t *parse_object (
	lex_t *lex, size_t flags, json_error_t *error
) {

	json_t *object = json_object ();
	if (!object) return NULL;

	lex_scan (lex, error);
	if (lex->token == '}') return object;

	bool parsed = true;
	while (parsed) {
		parsed = parse_object_member (lex, flags, error, object);
		if (!parsed) break;

		lex_scan (lex, error);
		if (lex->token != ',') break;

		lex_scan (lex, error);
	}

	if (parsed && (lex->token != '}')) {
		error_set (error, lex, json_error_invalid_syntax, "'}' expected");
		parsed = false;
	}

	if (!parsed) {
		json_decref (object);
		object = NULL;
	}

	return object;

}

static json_t *parse_array (
	lex_t *lex, size_t flags, json_error_t *error
) {

	json_t *array = json_array ();
	if (!array) return NULL;

	lex_scan (lex, error);
	if (lex->token == ']') return array;

	bool parsed = true;
	while (lex->token) {
		json_t *element = parse_value (lex, flags, error);

		// the array releases the element when it fails to append it
		parsed = element && !json_array_append_new (array, element);
		if (!parsed) break;

		lex_scan (lex, error);
		if (lex->token != ',') break;

		lex_scan (lex, error);
	}

	if (parsed && (lex->token != ']')) {
		error_set (error, lex, json_error_invalid_syntax, "']' expected");
		parsed = false;
	}

	if (!parsed) {
		json_decref (array);
		array = NULL;
	}

	return array;

}

static json_t *parse_value (
	lex_t *lex, size_t flags, json_error_t *error
) {

	json_t *json;

	lex->depth++;
	if (lex->depth > JSON_PARSER_MAX_DEPTH) {
		error_set (error, lex, json_error_stack_overflow, "maximum parsing depth reached");
		return NULL;
	}

	switch (lex->token) {
		case TOKEN_STRING: {
			const char *value = lex->value.string.val;
			size_t len = lex->value.string.len;

			if (!(flags & JSON_ALLOW_NUL)) {
				if (memchr (value, '\0', len)) {
					error_set (error, lex, json_error_null_character,
							  "\\u0000 is not allowed without JSON_ALLOW_NUL");
					return NULL;
				}
			}

			json = jsonp_stringn_nocheck_own (value, len);
			lex->value.string.val = NULL;
			lex->value.string.len = 0;
			break;
		}

		case TOKEN_INTEGER: {
			json = json_integer (lex->value.integer);
			break;
		}

		case TOKEN_REAL: {
			json = json_real (lex->value.real);
			break;
		}

		case TOKEN_TRUE:
			json = json_true ();
			break;

		case TOKEN_FALSE:
			json = json_false ();
			break;

		case TOKEN_NULL:
			json = json_null ();
			break;

		case '{':
			json = parse_object (lex, flags, error);
			break;

		case '[':
			json = parse_array (lex, flags, error);
			break;

		case TOKEN_INVALID:
			error_set (error, lex, json_error_invalid_syntax, "invalid token");
			return NULL;

		default:
			error_set (error, lex, json_error_invalid_syntax, "unexpected token");
			return NULL;
	}

	if (!json)
		return NULL;

	lex->depth--;
	return json;

}

// a parse that ran out of its memory says why, whatever error its parser saw after that
static void parse_json_exhausted (lex_t *lex, json_error_t *error) {

	if (json_parse_exhausted) {
		if (error) error->text[0] = '\0';

		error_set (
			error, lex, json_error_out_of_memory,
			"the json needs more memory than its size allows"
		);
	}

}

static json_t *parse_json_value (
	lex_t *lex, size_t flags, json_error_t *error
) {

	json_t *result;

	lex->depth = 0;

	lex_scan (lex, error);
	if (!(flags & JSON_DECODE_ANY)) {
		if (lex->token != '[' && lex->token != '{') {
			error_set (error, lex, json_error_invalid_syntax, "'[' or '{' expected");
			return NULL;
		}
	}

	result = parse_value (lex, flags, error);
	if (!result)
		return NULL;

	if (!(flags & JSON_DISABLE_EOF_CHECK)) {
		lex_scan (lex, error);
		if (lex->token != TOKEN_EOF) {
			error_set (error, lex, json_error_end_of_input_expected,
					  "end of file expected");
			json_decref (result);
			return NULL;
		}
	}

	if (error) {
		/* Save the position even though there was no error */
		error->position = json_error_position (lex->stream.position);
	}

	return result;

}

// the memory of the values is limited by the size of the document
static json_t *parse_json (
	lex_t *lex, size_t flags, json_error_t *error
) {

	json_parse_budget_start ();

	json_t *result = parse_json_value (lex, flags, error);
	if (!result) parse_json_exhausted (lex, error);

	json_parse_budget_end ();

	return result;

}

typedef struct {

	const char *data;
	size_t pos;

} string_data_t;

static int string_get (void *data) {

	char c;
	string_data_t *stream = (string_data_t *)data;
	c = stream->data[stream->pos];
	if (c == '\0')
		return EOF;
	else {
		stream->pos++;
		return (unsigned char)c;
	}

}

json_t *json_loads (
	const char *string, size_t flags, json_error_t *error
) {

	lex_t lex;
	json_t *result;
	string_data_t stream_data;

	jsonp_error_init (error, "<string>");

	if (string == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	stream_data.data = string;
	stream_data.pos = 0;

	if (lex_init (&lex, string_get, flags, (void *)&stream_data))
		return NULL;

	result = parse_json (&lex, flags, error);

	lex_close (&lex);
	return result;

}

typedef struct {

	const char *data;
	size_t len;
	size_t pos;

} buffer_data_t;

static int buffer_get (void *data) {

	char c;
	buffer_data_t *stream = (buffer_data_t *) data;
	if (stream->pos >= stream->len)
		return EOF;

	c = stream->data[stream->pos];
	stream->pos++;
	return (unsigned char)c;

}

json_t *json_loadb (
	const char *buffer, size_t buflen, size_t flags, json_error_t *error
) {

	lex_t lex;
	json_t *result;
	buffer_data_t stream_data;

	jsonp_error_init (error, "<buffer>");

	if (buffer == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	stream_data.data = buffer;
	stream_data.pos = 0;
	stream_data.len = buflen;

	if (lex_init (&lex, buffer_get, flags, (void *)&stream_data))
		return NULL;

	result = parse_json (&lex, flags, error);

	lex_close (&lex);
	return result;

}

static int file_get_func (void *data) {

	return fgetc ((FILE *) data);

}

json_t *json_loadf (
	FILE *input, size_t flags, json_error_t *error
) {

	lex_t lex;
	const char *source;
	json_t *result;

	if (input == stdin)
		source = "<stdin>";
	else
		source = "<stream>";

	jsonp_error_init (error, source);

	if (input == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	if (lex_init (&lex, file_get_func, flags, input))
		return NULL;

	result = parse_json (&lex, flags, error);

	lex_close (&lex);
	return result;

}

static int fd_get_func (void *data) {

	uint8_t c = 0;
	if (read (*(int *) data, &c, 1) == 1)
		return (int) c;
	
	return EOF;

}

json_t *json_loadfd (
	int input, size_t flags, json_error_t *error
) {

	lex_t lex = CERVER_ZERO_INITIALIZER;
	const char *source;
	json_t *result;

	source = (input == STDIN_FILENO) ? "<stdin>" : "<stream>";

	jsonp_error_init (error, source);

	if (input < 0) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	if (lex_init (&lex, fd_get_func, flags, &input))
		return NULL;

	result = parse_json (&lex, flags, error);

	lex_close (&lex);
	return result;

}

json_t *json_load_file (
	const char *path, size_t flags, json_error_t *error
) {

	json_t *result;
	FILE *fp;

	jsonp_error_init (error, path);

	if (path == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	fp = fopen (path, "rb");
	if (!fp) {
		error_set (error, NULL, json_error_cannot_open_file, "unable to open %s: %s", path,
				  strerror (errno));
		return NULL;
	}

	result = json_loadf (fp, flags, error);

	fclose (fp);
	return result;

}

#define MAX_BUF_LEN 1024

typedef struct {

	char data[MAX_BUF_LEN];
	size_t len;
	size_t pos;
	json_load_callback_t callback;
	void *arg;

} callback_data_t;

static int callback_get (void *data) {

	char c;
	callback_data_t *stream = (callback_data_t *) data;

	if (stream->pos >= stream->len) {
		stream->pos = 0;
		stream->len = stream->callback (stream->data, MAX_BUF_LEN, stream->arg);
		if (stream->len == 0 || stream->len == (size_t)-1)
			return EOF;
	}

	c = stream->data[stream->pos];
	stream->pos++;
	return (unsigned char)c;

}

json_t *json_load_callback (
	json_load_callback_t callback, void *arg, size_t flags,
	json_error_t *error
) {

	lex_t lex = CERVER_ZERO_INITIALIZER;
	json_t *result = NULL;

	callback_data_t stream_data;

	memset (&stream_data, 0, sizeof (stream_data));
	stream_data.callback = callback;
	stream_data.arg = arg;

	jsonp_error_init (error, "<callback>");

	if (callback == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	if (lex_init (&lex, callback_get, flags, &stream_data))
		return NULL;

	result = parse_json (&lex, flags, error);

	lex_close (&lex);
	return result;

}

#pragma endregion

#pragma region packing

typedef struct token_t {

	int line;
	int column;
	size_t pos;
	char token;

} token_t;

typedef struct scanner_t {

	const char *start;
	const char *fmt;
	token_t prev_token;
	token_t token;
	token_t next_token;
	json_error_t *error;
	size_t flags;
	int line;
	int column;
	size_t pos;
	int has_error;

} scanner_t;

#define token(scanner) ((scanner)->token.token)

static const char *const type_names[] = {
	"object", "array", "string", "integer",
	"real",   "true",  "false",  "null"
};

#define type_name(x) type_names[json_typeof (x)]

static const char unpack_value_starters[] = "{[siIbfFOon";

static void scanner_init (
	scanner_t *s, json_error_t *error, size_t flags,
	const char *fmt
) {

	s->error = error;
	s->flags = flags;
	s->fmt = s->start = fmt;
	(void) memset (&s->prev_token, 0, sizeof (token_t));
	(void) memset (&s->token, 0, sizeof (token_t));
	(void) memset (&s->next_token, 0, sizeof (token_t));
	s->line = 1;
	s->column = 0;
	s->pos = 0;
	s->has_error = 0;

}

static void next_token (scanner_t *s) {

	const char *t;
	s->prev_token = s->token;

	if (s->next_token.line) {
		s->token = s->next_token;
		s->next_token.line = 0;
		return;
	}

	if (!token (s) && !*s->fmt)
		return;

	t = s->fmt;
	s->column++;
	s->pos++;

	/* skip space and ignored chars */
	while (*t == ' ' || *t == '\t' || *t == '\n' || *t == ',' || *t == ':') {
		if (*t == '\n') {
			s->line++;
			s->column = 1;
		} else
			s->column++;

		s->pos++;
		t++;
	}

	s->token.token = *t;
	s->token.line = s->line;
	s->token.column = s->column;
	s->token.pos = s->pos;

	if (*t)
		t++;
	s->fmt = t;

}

static void prev_token (scanner_t *s) {

	s->next_token = s->token;
	s->token = s->prev_token;

}

CERVER_PRINTF_FORMAT (4, 5) static void set_error (
	scanner_t *s, const char *source, enum json_error_code code,
	const char *fmt, ...
) {

	va_list ap;
	va_start (ap, fmt);

	jsonp_error_vset (
		s->error, s->token.line, s->token.column, s->token.pos,
		code, fmt, ap
	);

	jsonp_error_set_source (s->error, source);

	va_end (ap);
}

static json_t *pack (scanner_t *s, va_list *ap);

/* ours will be set to 1 if jsonp_free() must be called for the result
   afterwards */
static char *read_string (
	scanner_t *s, va_list *ap, const char *purpose, size_t *out_len,
	int *ours, int optional
) {

	char t;
	strbuffer_t strbuff;
	const char *str;
	size_t length;

	next_token (s);
	t = token (s);
	prev_token (s);

	*ours = 0;
	if (t != '#' && t != '%' && t != '+') {
		/* Optimize the simple case */
		str = va_arg (*ap, const char *);

		if (!str) {
			if (!optional) {
				set_error (s, "<args>", json_error_null_value, "NULL %s", purpose);
				s->has_error = 1;
			}
			return NULL;
		}

		length = strlen (str);

		if (!utf8_check_string (str, length)) {
			set_error (s, "<args>", json_error_invalid_utf8, "Invalid UTF-8 %s", purpose);
			s->has_error = 1;
			return NULL;
		}

		*out_len = length;
		return (char *)str;
	} else if (optional) {
		set_error (s, "<format>", json_error_invalid_format,
				  "Cannot use '%c' on optional strings", t);
		s->has_error = 1;

		return NULL;
	}

	if (strbuffer_init (&strbuff)) {
		set_error (s, "<internal>", json_error_out_of_memory, "Out of memory");
		s->has_error = 1;
	}

	while (1) {
		str = va_arg (*ap, const char *);
		if (!str) {
			set_error (s, "<args>", json_error_null_value, "NULL %s", purpose);
			s->has_error = 1;
		}

		next_token (s);

		if (token (s) == '#') {
			length = va_arg (*ap, int);
		} else if (token (s) == '%') {
			length = va_arg (*ap, size_t);
		} else {
			prev_token (s);
			length = s->has_error ? 0 : strlen (str);
		}

		if (!s->has_error && strbuffer_append_bytes (&strbuff, str, length) == -1) {
			set_error (s, "<internal>", json_error_out_of_memory, "Out of memory");
			s->has_error = 1;
		}

		next_token (s);
		if (token (s) != '+') {
			prev_token (s);
			break;
		}
	}

	if (s->has_error) {
		strbuffer_close (&strbuff);
		return NULL;
	}

	if (!utf8_check_string (strbuff.value, strbuff.length)) {
		set_error (s, "<args>", json_error_invalid_utf8, "Invalid UTF-8 %s", purpose);
		strbuffer_close (&strbuff);
		s->has_error = 1;
		return NULL;
	}

	*out_len = strbuff.length;
	*ours = 1;
	return strbuffer_steal_value (&strbuff);

}

// the format of an optional value, that can be NULL
#define PACK_OPTIONAL_VALUE				'*'

// the token that follows the value, without moving the scanner
static char pack_peek_next (scanner_t *s) {

	next_token (s);
	const char next = token (s);
	prev_token (s);

	return next;

}

// a format that ends before its object or array, it can not be recovered from
static bool pack_check_format_end (scanner_t *s) {

	if (!token (s)) {
		set_error (s, "<format>", json_error_invalid_format, "Unexpected end of format string");
		return false;
	}

	return true;

}

// a "key": value of the object, the errors of the arguments are kept in the scanner
// returns false on an error of the format, that ends the object
static bool pack_object_member (scanner_t *s, va_list *ap, json_t *object) {

	if (!pack_check_format_end (s)) return false;

	if (token (s) != 's') {
		set_error (s, "<format>", json_error_invalid_format, "Expected format 's', got '%c'", token (s));
		return false;
	}

	size_t length = 0;
	int ours = 0;
	char *key = read_string (s, ap, "object key", &length, &ours, 0);

	next_token (s);

	const char value_format = pack_peek_next (s);

	json_t *value = pack (s, ap);
	if (!value) {
		if (value_format != PACK_OPTIONAL_VALUE) {
			set_error (s, "<args>", json_error_null_value, "NULL object value");
			s->has_error = 1;
		}
	}

	else if (s->has_error) {
		json_decref (value);
	}

	else if (json_object_set_new_nocheck (object, key, value)) {
		set_error (s, "<internal>", json_error_out_of_memory, "Unable to add key \"%s\"", key);
		s->has_error = 1;
	}

	if (ours) jsonp_free (key);

	next_token (s);

	return true;

}

static json_t *pack_object (scanner_t *s, va_list *ap) {

	json_t *object = json_object ();
	next_token (s);

	bool packed = true;
	while (packed && (token (s) != '}')) {
		packed = pack_object_member (s, ap, object);
	}

	if (!packed || s->has_error) {
		json_decref (object);
		object = NULL;
	}

	return object;

}

// a value of the array, the errors of the arguments are kept in the scanner
// returns false on an error of the format, that ends the array
static bool pack_array_element (scanner_t *s, va_list *ap, json_t *array) {

	if (!pack_check_format_end (s)) return false;

	const char value_format = pack_peek_next (s);

	json_t *value = pack (s, ap);
	if (!value) {
		if (value_format != PACK_OPTIONAL_VALUE) s->has_error = 1;
	}

	else if (s->has_error) {
		json_decref (value);
	}

	else if (json_array_append_new (array, value)) {
		set_error (s, "<internal>", json_error_out_of_memory, "Unable to append to array");
		s->has_error = 1;
	}

	next_token (s);

	return true;

}

static json_t *pack_array (scanner_t *s, va_list *ap) {

	json_t *array = json_array ();
	next_token (s);

	bool packed = true;
	while (packed && (token (s) != ']')) {
		packed = pack_array_element (s, ap, array);
	}

	if (!packed || s->has_error) {
		json_decref (array);
		array = NULL;
	}

	return array;

}

static json_t *pack_string (scanner_t *s, va_list *ap) {

	char *str;
	char t;
	size_t len;
	int ours;
	int optional;

	next_token (s);
	t = token (s);
	optional = t == '?' || t == '*';
	if (!optional)
		prev_token (s);

	str = read_string (s, ap, "string", &len, &ours, optional);

	if (!str)
		return t == '?' && !s->has_error ? json_null () : NULL;

	if (s->has_error) {
		/* It's impossible to reach this point if ours != 0, do not free str. */
		return NULL;
	}

	if (ours)
		return jsonp_stringn_nocheck_own (str, len);

	return json_stringn_nocheck (str, len);

}

static json_t *pack_object_inter (
	scanner_t *s, va_list *ap, int need_incref
) {

	json_t *json;
	char ntoken;

	next_token (s);
	ntoken = token (s);

	if (ntoken != '?' && ntoken != '*')
		prev_token (s);

	json = va_arg (*ap, json_t *);

	if (json)
		return need_incref ? json_incref (json) : json;

	switch (ntoken) {
		case '?':
			return json_null ();
		case '*':
			return NULL;
		default:
			break;
	}

	set_error (s, "<args>", json_error_null_value, "NULL object");
	s->has_error = 1;
	return NULL;

}

static json_t *pack_integer (scanner_t *s, json_int_t value) {

	json_t *json = json_integer (value);

	if (!json) {
		set_error (s, "<internal>", json_error_out_of_memory, "Out of memory");
		s->has_error = 1;
	}

	return json;

}

static json_t *pack_real (scanner_t *s, double value) {

	/* Allocate without setting value so we can identify OOM error. */
	json_t *json = json_real (0.0);

	if (!json) {
		set_error (s, "<internal>", json_error_out_of_memory, "Out of memory");
		s->has_error = 1;

		return NULL;
	}

	if (json_real_set (json, value)) {
		json_decref (json);

		set_error (s, "<args>", json_error_numeric_overflow,
				  "Invalid floating point value");
		s->has_error = 1;

		return NULL;
	}

	return json;

}

static json_t *pack (scanner_t *s, va_list *ap) {

	switch (token (s)) {
		case '{':
			return pack_object (s, ap);

		case '[':
			return pack_array (s, ap);

		case 's': /* string */
			return pack_string (s, ap);

		case 'n': /* null */
			return json_null ();

		case 'b': /* boolean */
			return va_arg (*ap, int) ? json_true () : json_false ();

		case 'i': /* integer from int */
			return pack_integer (s, va_arg (*ap, int));

		case 'I': /* integer from json_int_t */
			return pack_integer (s, va_arg (*ap, json_int_t));

		case 'f': /* real */
			return pack_real (s, va_arg (*ap, double));

		case 'O': /* a json_t object; increments refcount */
			return pack_object_inter (s, ap, 1);

		case 'o': /* a json_t object; doesn't increment refcount */
			return pack_object_inter (s, ap, 0);

		default:
			set_error (s, "<format>", json_error_invalid_format,
					  "Unexpected format character '%c'", token (s));
			s->has_error = 1;
			return NULL;
	}

}

static int unpack (scanner_t *s, json_t *root, va_list *ap);

// how an object format ends: '!' every key must be unpacked, '*' some can be left
#define UNPACK_STRICT_NONE				0
#define UNPACK_STRICT_ALL				1
#define UNPACK_STRICT_SOME				-1

#define UNPACK_STRICT_ALL_FORMAT		'!'
#define UNPACK_STRICT_SOME_FORMAT		'*'
#define UNPACK_OPTIONAL_FORMAT			'?'

#define UNPACK_KEYS_SEPARATOR			", "

// what an object of the format keeps while its members are unpacked:
// a set (a hashtable) of the keys that were unpacked, because the same key can be unpacked many times
typedef struct UnpackObject {

	hashtable_t keys;
	int strict;
	bool got_optional;

} UnpackObject;

// the value of the key, NULL when the object is skipped or an optional key is missing
// returns 0 on success, -1 if a key that is not optional is missing
static int unpack_object_value (
	scanner_t *s, json_t *root, const char *key, const bool optional, json_t **value
) {

	*value = root ? json_object_get (root, key) : NULL;

	if (root && !*value && !optional) {
		set_error (s, "<validation>", json_error_item_not_found, "Object item not found: %s", key);
		return -1;
	}

	return 0;

}

// a "key": value of the format, the key is the next argument
// returns 0 on success, -1 on error
static int unpack_object_member (
	scanner_t *s, json_t *root, va_list *ap, UnpackObject *unpacked
) {

	if (token (s) != 's') {
		set_error (s, "<format>", json_error_invalid_format, "Expected format 's', got '%c'", token (s));
		return -1;
	}

	const char *key = va_arg (*ap, const char *);
	if (!key) {
		set_error (s, "<args>", json_error_null_value, "NULL object key");
		return -1;
	}

	next_token (s);

	bool optional = false;
	if (token (s) == UNPACK_OPTIONAL_FORMAT) {
		optional = true;
		unpacked->got_optional = true;
		next_token (s);
	}

	json_t *value = NULL;
	if (unpack_object_value (s, root, key, optional, &value)) return -1;

	if (unpack (s, value, ap)) return -1;

	(void) hashtable_set (&unpacked->keys, key, json_null ());

	next_token (s);

	return 0;

}

// the members of the format up to its '}'
// returns 0 on success, -1 on error
static int unpack_object_members (
	scanner_t *s, json_t *root, va_list *ap, UnpackObject *unpacked
) {

	next_token (s);

	while (token (s) != '}') {
		if (unpacked->strict != UNPACK_STRICT_NONE) {
			set_error (
				s, "<format>", json_error_invalid_format,
				"Expected '}' after '%c', got '%c'",
				(unpacked->strict == UNPACK_STRICT_ALL) ? UNPACK_STRICT_ALL_FORMAT : UNPACK_STRICT_SOME_FORMAT,
				token (s)
			);

			return -1;
		}

		if (!token (s)) {
			set_error (s, "<format>", json_error_invalid_format, "Unexpected end of format string");
			return -1;
		}

		if ((token (s) == UNPACK_STRICT_ALL_FORMAT) || (token (s) == UNPACK_STRICT_SOME_FORMAT)) {
			unpacked->strict = (token (s) == UNPACK_STRICT_ALL_FORMAT) ? UNPACK_STRICT_ALL : UNPACK_STRICT_SOME;
			next_token (s);
		}

		else if (unpack_object_member (s, root, ap, unpacked)) {
			return -1;
		}
	}

	return 0;

}

// adds a key that was left to the list of the error message,
// keys_result is 1 before the list is created, 0 while it is fine, -1 on error
static int unpack_object_left_key (strbuffer_t *left_keys, int keys_result, const char *key) {

	if (keys_result == 1) {
		keys_result = strbuffer_init (left_keys);
	}

	else if (!keys_result) {
		keys_result = strbuffer_append_bytes (left_keys, UNPACK_KEYS_SEPARATOR, strlen (UNPACK_KEYS_SEPARATOR));
	}

	if (!keys_result) keys_result = strbuffer_append_bytes (left_keys, key, strlen (key));

	return keys_result;

}

// with '!' or JSON_STRICT, every key of the object that is not optional must have been unpacked
// returns 0 on success, -1 if keys were left
static int unpack_object_check_left (scanner_t *s, json_t *root, UnpackObject *unpacked) {

	if (!unpacked->got_optional && (json_object_size (root) == unpacked->keys.size)) return 0;

	int keys_result = 1;
	strbuffer_t left_keys;
	long left = 0;

	const char *key = NULL;
	json_t *value = NULL;
	json_object_foreach (root, key, value) {
		if (!hashtable_get (&unpacked->keys, key)) {
			left += 1;
			keys_result = unpack_object_left_key (&left_keys, keys_result, key);
		}
	}

	if (!left) return 0;

	set_error (
		s, "<validation>", json_error_end_of_input_expected,
		"%li object item(s) left unpacked: %s", left,
		keys_result ? "<unknown>" : strbuffer_value (&left_keys)
	);

	strbuffer_close (&left_keys);

	return -1;

}

static int unpack_object (scanner_t *s, json_t *root, va_list *ap) {

	UnpackObject unpacked = CERVER_ZERO_INITIALIZER;
	unpacked.strict = UNPACK_STRICT_NONE;
	unpacked.got_optional = false;

	if (hashtable_init (&unpacked.keys)) {
		set_error (s, "<internal>", json_error_out_of_memory, "Out of memory");
		return -1;
	}

	int retval = -1;

	if (root && !json_is_object (root)) {
		set_error (s, "<validation>", json_error_wrong_type, "Expected object, got %s", type_name (root));
	}

	else if (!unpack_object_members (s, root, ap, &unpacked)) {
		if ((unpacked.strict == UNPACK_STRICT_NONE) && (s->flags & JSON_STRICT)) unpacked.strict = UNPACK_STRICT_ALL;

		retval = (root && (unpacked.strict == UNPACK_STRICT_ALL)) ?
			unpack_object_check_left (s, root, &unpacked) : 0;
	}

	hashtable_close (&unpacked.keys);

	return retval;

}

static int unpack_array (scanner_t *s, json_t *root, va_list *ap) {
	size_t i = 0;
	int strict = 0;

	if (root && !json_is_array (root)) {
		set_error (s, "<validation>", json_error_wrong_type, "Expected array, got %s",
				  type_name (root));
		return -1;
	}
	next_token (s);

	while (token (s) != ']') {
		json_t *value;

		if (strict != 0) {
			set_error (s, "<format>", json_error_invalid_format,
					  "Expected ']' after '%c', got '%c'", (strict == 1 ? '!' : '*'),
					  token (s));
			return -1;
		}

		if (!token (s)) {
			set_error (s, "<format>", json_error_invalid_format,
					  "Unexpected end of format string");
			return -1;
		}

		if (token (s) == '!' || token (s) == '*') {
			strict = (token (s) == '!' ? 1 : -1);
			next_token (s);
			continue;
		}

		if (!strchr (unpack_value_starters, token (s))) {
			set_error (s, "<format>", json_error_invalid_format,
					  "Unexpected format character '%c'", token (s));
			return -1;
		}

		if (!root) {
			/* skipping */
			value = NULL;
		} else {
			value = json_array_get (root, i);
			if (!value) {
				set_error (s, "<validation>", json_error_index_out_of_range,
						  "Array index %lu out of range", (unsigned long)i);
				return -1;
			}
		}

		if (unpack (s, value, ap))
			return -1;

		next_token (s);
		i++;
	}

	if (strict == 0 && (s->flags & JSON_STRICT))
		strict = 1;

	if (root && strict == 1 && i != json_array_size (root)) {
		long diff = (long)json_array_size (root) - (long)i;
		set_error (s, "<validation>", json_error_end_of_input_expected,
				  "%li array item(s) left unpacked", diff);
		return -1;
	}

	return 0;
}

static int unpack (scanner_t *s, json_t *root, va_list *ap) {

	switch (token (s)) {
		case '{':
			return unpack_object (s, root, ap);

		case '[':
			return unpack_array (s, root, ap);

		case 's':
			if (root && !json_is_string (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected string, got %s", type_name (root));
				return -1;
			}

			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				const char **str_target;
				size_t *len_target = NULL;

				str_target = va_arg (*ap, const char **);
				if (!str_target) {
					set_error (s, "<args>", json_error_null_value, "NULL string argument");
					return -1;
				}

				next_token (s);

				if (token (s) == '%') {
					len_target = va_arg (*ap, size_t *);
					if (!len_target) {
						set_error (s, "<args>", json_error_null_value,
								  "NULL string length argument");
						return -1;
					}
				} else
					prev_token (s);

				if (root) {
					*str_target = json_string_value (root);
					if (len_target)
						*len_target = json_string_length (root);
				}
			}
			return 0;

		case 'i':
			if (root && !json_is_integer (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected integer, got %s", type_name (root));
				return -1;
			}

			// an integer that does not fit in an int is an error, it was cut before
			if (root && ((json_integer_value (root) > INT_MAX) || (json_integer_value (root) < INT_MIN))) {
				set_error (s, "<validation>", json_error_numeric_overflow,
						  "Integer %" JSON_INTEGER_FORMAT " does not fit in an int", json_integer_value (root));
				return -1;
			}

			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				int *target = va_arg (*ap, int *);
				if (root)
					*target = (int)json_integer_value (root);
			}

			return 0;

		case 'I':
			if (root && !json_is_integer (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected integer, got %s", type_name (root));
				return -1;
			}

			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				json_int_t *target = va_arg (*ap, json_int_t *);
				if (root)
					*target = json_integer_value (root);
			}

			return 0;

		case 'b':
			if (root && !json_is_boolean (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected true or false, got %s", type_name (root));
				return -1;
			}

			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				int *target = va_arg (*ap, int *);
				if (root)
					*target = json_is_true (root);
			}

			return 0;

		case 'f':
			if (root && !json_is_real (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected real, got %s", type_name (root));
				return -1;
			}

			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				double *target = va_arg (*ap, double *);
				if (root)
					*target = json_real_value (root);
			}

			return 0;

		case 'F':
			if (root && !json_is_number (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected real or integer, got %s", type_name (root));
				return -1;
			}

			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				double *target = va_arg (*ap, double *);
				if (root)
					*target = json_number_value (root);
			}

			return 0;

		case 'O':
			if (root && !(s->flags & JSON_VALIDATE_ONLY))
				json_incref (root);
			/* Fall through */

		case 'o':
			if (!(s->flags & JSON_VALIDATE_ONLY)) {
				json_t **target = va_arg (*ap, json_t **);
				if (root)
					*target = root;
			}

			return 0;

		case 'n':
			/* Never assign, just validate */
			if (root && !json_is_null (root)) {
				set_error (s, "<validation>", json_error_wrong_type,
						  "Expected null, got %s", type_name (root));
				return -1;
			}
			return 0;

		default:
			set_error (s, "<format>", json_error_invalid_format,
					  "Unexpected format character '%c'", token (s));
			return -1;
	}
}

json_t *json_vpack_ex (
	json_error_t *error, size_t flags,
	const char *fmt, va_list ap
) {

	scanner_t s = CERVER_ZERO_INITIALIZER;
	va_list ap_copy;
	json_t *value = NULL;

	if (!fmt || !*fmt) {
		jsonp_error_init (error, "<format>");
		jsonp_error_set (error, -1, -1, 0, json_error_invalid_argument,
						"NULL or empty format string");
		return NULL;
	}
	jsonp_error_init (error, NULL);

	scanner_init (&s, error, flags, fmt);
	next_token (&s);

	va_copy (ap_copy, ap);
	value = pack (&s, &ap_copy);
	va_end (ap_copy);

	/* This will cover all situations where s.has_error is true */
	if (!value)
		return NULL;

	next_token (&s);
	if (token (&s)) {
		json_decref (value);
		set_error (&s, "<format>", json_error_invalid_format,
				  "Garbage after format string");
		return NULL;
	}

	return value;

}

json_t *json_pack_ex (
	json_error_t *error, size_t flags,
	const char *fmt, ...
) {

	va_list ap;

	va_start (ap, fmt);
	json_t * value = json_vpack_ex (error, flags, fmt, ap);
	va_end (ap);

	return value;

}

json_t *json_pack (const char *fmt, ...) {

	va_list ap;

	va_start (ap, fmt);
	json_t *value = json_vpack_ex (NULL, 0, fmt, ap);
	va_end (ap);

	return value;

}

int json_vunpack_ex (
	json_t *root, json_error_t *error, size_t flags,
	const char *fmt, va_list ap
) {

	scanner_t s = CERVER_ZERO_INITIALIZER;
	va_list ap_copy;

	if (!root) {
		jsonp_error_init (error, "<root>");
		jsonp_error_set (error, -1, -1, 0, json_error_null_value, "NULL root value");
		return -1;
	}

	if (!fmt || !*fmt) {
		jsonp_error_init (error, "<format>");
		jsonp_error_set (error, -1, -1, 0, json_error_invalid_argument,
						"NULL or empty format string");
		return -1;
	}
	jsonp_error_init (error, NULL);

	scanner_init (&s, error, flags, fmt);
	next_token (&s);

	va_copy (ap_copy, ap);
	if (unpack (&s, root, &ap_copy)) {
		va_end (ap_copy);
		return -1;
	}
	va_end (ap_copy);

	next_token (&s);
	if (token (&s)) {
		set_error (&s, "<format>", json_error_invalid_format,
				  "Garbage after format string");
		return -1;
	}

	return 0;

}

int json_unpack_ex (
	json_t *root, json_error_t *error, size_t flags,
	const char *fmt, ...
) {

	va_list ap;

	va_start (ap, fmt);
	int ret = json_vunpack_ex (root, error, flags, fmt, ap);
	va_end (ap);

	return ret;
}

int json_unpack (json_t *root, const char *fmt, ...) {

	va_list ap;

	va_start (ap, fmt);
	int ret = json_vunpack_ex (root, NULL, 0, fmt, ap);
	va_end (ap);

	return ret;
}

#pragma endregion

#pragma region print

static void print_json_aux (
	json_t *element, int indent, const char *key
);

static void print_json_indent (int indent) {

	for (int i = 0; i < indent; i++) {
		putchar (' ');
	}

}

// the values of an array and the root have no key
#define JSON_PRINT_NO_KEY			"(value)"

static const char *json_plural (size_t count) {

	return count == 1 ? "" : "s";

}

static const char *json_print_key (const char *key) {

	return key ? key : JSON_PRINT_NO_KEY;

}

static void print_json_object (
	json_t *element, int indent
) {

	print_json_indent (indent);

	const char *key = NULL;
	json_t *value = NULL;
	size_t size = json_object_size (element);
	(void) printf ("JSON Object of %zu pair%s:\n", size, json_plural (size));
	json_object_foreach (element, key, value) {
		print_json_indent (indent + 2);
		print_json_aux (value, indent + 2, key);
	}

}

static void print_json_array (
	json_t *element, int indent, const char *key
) {

	print_json_indent (indent);

	size_t size = json_array_size (element);
	(void) printf ("JSON Array of %zu element%s:\n", size, json_plural (size));
	for (size_t i = 0; i < size; i++) {
		print_json_aux (json_array_get (element, i), indent + 2, key);
	}

}

static void print_json_string (
	json_t *element, int indent, const char *key
) {

	print_json_indent (indent);
	(void) printf ("[String] %s: \"%s\"\n", json_print_key (key), json_string_value (element));

}

static void print_json_integer (
	json_t *element, int indent, const char *key
) {

	print_json_indent (indent);
	(void) printf (
		"[Integer] %s: \"%" JSON_INTEGER_FORMAT "\"\n",
		json_print_key (key), json_integer_value (element)
	);

}

static void print_json_real (
	json_t *element, int indent, const char *key
) {

	print_json_indent (indent);
	(void) printf ("[Real] %s: %f\n", json_print_key (key), json_real_value (element));

}

static void print_json_true (
	int indent, const char *key
) {

	print_json_indent (indent);
	(void) printf ("%s: True\n", json_print_key (key));

}

static void print_json_false (
	int indent, const char *key
) {

	print_json_indent (indent);
	(void) printf ("%s: False\n", json_print_key (key));

}

static void print_json_null (
	int indent, const char *key
) {

	print_json_indent (indent);
	(void) printf ("%s: NULL\n", json_print_key (key));

}

static void print_json_aux (
	json_t *element, int indent, const char *key
) {

	switch (json_typeof (element)) {
		case JSON_OBJECT: print_json_object (element, indent); break;
		case JSON_ARRAY: print_json_array (element, indent, key); break;
		case JSON_STRING: print_json_string (element, indent, key); break;
		case JSON_INTEGER: print_json_integer (element, indent, key); break;
		case JSON_REAL: print_json_real (element, indent, key); break;
		case JSON_TRUE: print_json_true (indent, key); break;
		case JSON_FALSE: print_json_false (indent, key); break;
		case JSON_NULL: print_json_null (indent, key); break;

		default: {
			(void) fprintf (
				stderr, "unrecognized JSON type %u\n", (unsigned int) json_typeof (element)
			);
		} break;
	}

}

void json_print (json_t *root) {

	if (root) print_json_aux (root, 0, NULL);

}

#pragma endregion

#pragma GCC diagnostic pop
