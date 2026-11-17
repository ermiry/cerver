/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void jsonp_free (void *pointer) {

	if (pointer) free (pointer);

}

char *jsonp_strdup (const char *string) {

	return jsonp_strndup (string, strlen (string));

}

char *jsonp_strndup (const char *string, size_t length) {

	char *new_string = (char *) jsonp_malloc (length + 1);
	if (!new_string)
		return NULL;

	(void) memcpy (new_string, string, length);
	new_string[length] = '\0';
	return new_string;

}

#pragma endregion

#pragma region error

// the position of an error is an int in json_error_t,
// a document of more than INT_MAX bytes keeps the largest one
static int json_error_position (const size_t position) {

	return (position > (size_t) INT_MAX) ? INT_MAX : (int) position;

}

CERVER_INLINE enum json_error_code json_error_code (
	const json_error_t *error
) {

	return (enum json_error_code) error->text[JSON_ERROR_TEXT_LENGTH - 1];

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
	const char *message, va_list arguments
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

	(void) vsnprintf (error->text, JSON_ERROR_TEXT_LENGTH - 1, message, arguments);
	error->text[JSON_ERROR_TEXT_LENGTH - 2] = '\0';
	error->text[JSON_ERROR_TEXT_LENGTH - 1] = code;

}

void jsonp_error_set (
	json_error_t *error, int line, int column, size_t position,
	enum json_error_code code,
	const char *message, ...
) {

	va_list arguments;

	va_start (arguments, message);
	jsonp_error_vset (error, line, column, position, code, message, arguments);
	va_end (arguments);

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

static int strbuffer_init (strbuffer_t *string_buffer) {

	string_buffer->size = STRBUFFER_MIN_SIZE;
	string_buffer->length = 0;

	string_buffer->value = (char *) jsonp_malloc (string_buffer->size);
	if (!string_buffer->value)
		return -1;

	/* initialize to empty */
	string_buffer->value[0] = '\0';
	return 0;

}

static void strbuffer_close (strbuffer_t *string_buffer) {

	if (string_buffer->value)
		jsonp_free (string_buffer->value);

	string_buffer->size = 0;
	string_buffer->length = 0;
	string_buffer->value = NULL;

}

static void strbuffer_clear (strbuffer_t *string_buffer) {

	string_buffer->length = 0;
	string_buffer->value[0] = '\0';

}

static const char *strbuffer_value (
	const strbuffer_t *string_buffer
) {

	return string_buffer->value;

}

static char *strbuffer_steal_value (strbuffer_t *string_buffer) {

	char *result = string_buffer->value;
	string_buffer->value = NULL;
	return result;

}

static int strbuffer_append_bytes (
	strbuffer_t *string_buffer, const char *data, size_t size
) {

	if (size >= string_buffer->size - string_buffer->length) {
		size_t new_size;
		char *new_value;

		/* avoid integer overflow */
		if (string_buffer->size > STRBUFFER_SIZE_MAX / STRBUFFER_FACTOR ||
			size > STRBUFFER_SIZE_MAX - 1 ||
			string_buffer->length > STRBUFFER_SIZE_MAX - 1 - size)
			return -1;

		new_size = MAX (string_buffer->size * STRBUFFER_FACTOR, string_buffer->length + size + 1);

		new_value = (char *) jsonp_malloc (new_size);
		if (!new_value)
			return -1;

		(void) memcpy (new_value, string_buffer->value, string_buffer->length);

		jsonp_free (string_buffer->value);
		string_buffer->value = new_value;
		string_buffer->size = new_size;
	}

	(void) memcpy (string_buffer->value + string_buffer->length, data, size);
	string_buffer->length += size;
	string_buffer->value[string_buffer->length] = '\0';

	return 0;

}

static int strbuffer_append_byte (
	strbuffer_t *string_buffer, char byte
) {

	return strbuffer_append_bytes (string_buffer, &byte, 1);

}

static char strbuffer_pop (strbuffer_t *string_buffer) {

	if (string_buffer->length > 0) {
		char character = string_buffer->value[--string_buffer->length];
		string_buffer->value[string_buffer->length] = '\0';
		return character;
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
static int jsonp_strtod (strbuffer_t *string_buffer, double *output) {

	const locale_t locale = json_numbers_locale_get ();
	if (!locale)
		return -1;

	char *end = NULL;

	errno = 0;
	const double value = strtod_l (string_buffer->value, &end, locale);

	if (end != (string_buffer->value + string_buffer->length))
		return -1;

	if ((errno == ERANGE) && isinf (value))
		return -1;

	*output = value;
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

	const int written = snprintf (buffer, size, "%.*g", precision, value);

	(void) uselocale (previous);

	return written;

}

int jsonp_dtostr (
	char *buffer, size_t size, double value, int precision
) {

	int written = 0;
	char *start = NULL, *end = NULL;
	size_t length = 0;

	if (precision == 0)
		precision = JSON_REAL_DEFAULT_PRECISION;

	written = jsonp_dtostr_format (buffer, size, value, precision);
	if (written < 0)
		return -1;

	length = (size_t)written;
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

#define FLAGS_TO_INDENT(flags)    ((flags)&0x1F)
#define FLAGS_TO_PRECISION(flags) (((flags) >> 11) & 0x1F)

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

	struct buffer *destination = (struct buffer *) data;

	if (destination->used + size <= destination->size)
		(void) memcpy (&destination->data[destination->used], buffer, size);

	destination->used += size;
	return 0;

}

static int dump_to_file (
	const char *buffer, size_t size, void *data
) {

	FILE *destination = (FILE *)data;
	if (fwrite (buffer, size, 1, destination) != 1)
		return -1;
	
	return 0;

}

static int dump_to_fd (
	const char *buffer, size_t size, void *data
) {

	// HAVE_UNISTD_H of upstream was never defined, so the dumps to a fd always failed
	int *destination = (int *)data;
	if (write (*destination, buffer, size) == (ssize_t)size)
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
		unsigned int indent = FLAGS_TO_INDENT (flags), n_spaces = depth * indent;

		if (dump ("\n", 1, data))
			return -1;

		while (n_spaces > 0) {
			int n_written =
				n_spaces < sizeof whitespace - 1 ? n_spaces : sizeof whitespace - 1;

			if (dump (whitespace, n_written, data))
				return -1;

			n_spaces -= n_written;
		}
	}
	
	else if (space && !(flags & JSON_COMPACT)) {
		return dump (" ", 1, data);
	}

	return 0;

}

static int dump_string (
	const char *string, size_t length, json_dump_callback_t dump, void *data,
	size_t flags
) {

	const char *position, *end, *limit;
	int32_t codepoint = 0;

	if (dump ("\"", 1, data))
		return -1;

	end = position = string;
	limit = string + length;
	while (1) {
		const char *text;
		char sequence[13];
		int text_length;

		while (end < limit) {
			end = utf8_iterate (position, limit - position, &codepoint);
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

			position = end;
		}

		if (position != string) {
			if (dump (string, position - string, data))
				return -1;
		}

		if (end == position)
			break;

		/* handle \, /, ", and control codes */
		text_length = 2;
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
					snprintf (sequence, sizeof (sequence), "\\u%04X", (unsigned int)codepoint);
					text_length = 6;
				}

				/* not in BMP -> construct a UTF-16 surrogate pair */
				else {
					int32_t first, last;

					codepoint -= 0x10000;
					first = 0xD800 | ((codepoint & 0xffc00) >> 10);
					last = 0xDC00 | (codepoint & 0x003ff);

					snprintf (sequence, sizeof (sequence), "\\u%04X\\u%04X", (unsigned int)first,
							 (unsigned int)last);
					text_length = 12;
				}

				text = sequence;
				break;
			}
		}

		if (dump (text, text_length, data))
			return -1;

		string = position = end;
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
			size_t n_items;
			size_t i;
			/* Space for "0x", double the sizeof a pointer for the hex and a
			 * terminator. */
			char key[2 + (sizeof (json) * 2) + 1];

			/* detect circular references */
			if (jsonp_loop_check (parents, json, key, sizeof (key)))
				return -1;

			n_items = json_array_size (json);

			if (!embed && dump ("[", 1, data))
				return -1;
			if (n_items == 0) {
				hashtable_del (parents, key);
				return embed ? 0 : dump ("]", 1, data);
			}
			if (dump_indent (flags, depth + 1, 0, dump, data))
				return -1;

			for (i = 0; i < n_items; ++i) {
				if (do_dump (
					json_array_get (json, i), flags, depth + 1, parents, dump,
					data
				))
					return -1;

				if (i < n_items - 1) {
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
			void *iterator;
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

			iterator = json_object_iter ((json_t *)json);

			if (!embed && dump ("{", 1, data))
				return -1;
			if (!iterator) {
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
				while (iterator) {
					keys[i] = json_object_iter_key (iterator);
					iterator = json_object_iter_next ((json_t *)json, iterator);
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

				while (iterator) {
					void *next = json_object_iter_next ((json_t *)json, iterator);
					const char *key = json_object_iter_key (iterator);

					dump_string (key, strlen (key), dump, data, flags);
					if (dump (separator, separator_length, data) ||
						do_dump (json_object_iter_value (iterator), flags, depth + 1, parents,
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

					iterator = next;
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

	strbuffer_t string_buffer;
	char *result;

	if (strbuffer_init (&string_buffer))
		return NULL;

	if (json_dump_callback (json, dump_to_strbuffer, (void *)&string_buffer, flags))
		result = NULL;
	else
		result = jsonp_strdup (strbuffer_value (&string_buffer));

	strbuffer_close (&string_buffer);
	return result;

}

size_t json_dumpb (
	const json_t *json, char *buffer, size_t size, size_t flags
) {

	struct buffer destination = {size, 0, buffer};

	if (json_dump_callback (json, dump_to_buffer, (void *)&destination, flags))
		return 0;

	return destination.used;

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

	int result;
	hashtable_t parents_set;

	if (!(flags & JSON_ENCODE_ANY)) {
		if (!json_is_array (json) && !json_is_object (json))
			return -1;
	}

	if (hashtable_init (&parents_set))
		return -1;
	result = do_dump (json, flags, 0, &parents_set, callback, data);
	hashtable_close (&parents_set);

	return result;

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
#define l_isupper(character) ('A' <= (character) && (character) <= 'Z')
#define l_islower(character) ('a' <= (character) && (character) <= 'z')
#define l_isalpha(character) (l_isupper (character) || l_islower (character))
#define l_isdigit(character) ('0' <= (character) && (character) <= '9')
#define l_isxdigit(character)                                                                    \
	(l_isdigit (character) || ('A' <= (character) && (character) <= 'F') || ('a' <= (character) && (character) <= 'f'))

/* Read one byte from stream, convert to unsigned char, then int, and
   return. return EOF on end of file. This corresponds to the
   behaviour of fgetc(). */
typedef int (*get_func)(void *data);

typedef struct {

	get_func get;
	void *data;
	char buffer[5];
	size_t buffer_position;
	// the bytes of the character in the buffer: a NUL byte of the text is a character, not the end of the buffer
	size_t buffer_length;
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
			char *value;
			size_t length;
		} string;
		json_int_t integer;
		double real;
	} value;

} lex_t;

#define stream_to_lex(stream) container_of (stream, lex_t, stream)

// the errors of a parse

CERVER_PRINTF_FORMAT (4, 5) static void error_set (
	json_error_t *error, const lex_t *lexer, enum json_error_code code,
	const char *message, ...
) {
	va_list arguments;
	char message_text[JSON_ERROR_TEXT_LENGTH];
	char message_with_context[JSON_ERROR_TEXT_LENGTH_MAX];

	int line = -1, column = -1;
	size_t position = 0;
	const char *result = message_text;

	if (!error)
		return;

	va_start (arguments, message);
	vsnprintf (message_text, JSON_ERROR_TEXT_LENGTH, message, arguments);
	message_text[JSON_ERROR_TEXT_LENGTH - 1] = '\0';
	va_end (arguments);

	if (lexer) {
		const char *saved_text = strbuffer_value (&lexer->saved_text);

		line = lexer->stream.line;
		column = lexer->stream.column;
		position = lexer->stream.position;

		if (saved_text && saved_text[0]) {
			if (lexer->saved_text.length <= 20) {
				(void) snprintf (
					message_with_context, JSON_ERROR_TEXT_LENGTH_MAX - 1,
					"%s near '%s'", message_text, saved_text
				);

				result = message_with_context;
			}
		}
		
		else {
			if (code == json_error_invalid_syntax) {
				/* More specific error code for premature end of file. */
				code = json_error_premature_end_of_input;
			}

			if (lexer->stream.state == STREAM_STATE_ERROR) {
				/* No context for UTF-8 decoding errors */
				result = message_text;
			}

			else {
				(void) snprintf (
					message_with_context, JSON_ERROR_TEXT_LENGTH_MAX - 1,
					"%s near end of file", message_text
				);

				result = message_with_context;
			}
		}
	}

	jsonp_error_set (error, line, column, position, code, "%s", result);
}

// the lexer

static void stream_init (
	stream_t *stream, get_func get, void *data
) {

	stream->get = get;
	stream->data = data;
	stream->buffer[0] = '\0';
	stream->buffer_position = 0;
	stream->buffer_length = 0;

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
	stream_t *stream, json_error_t *error, const int character
) {

	stream->state = STREAM_STATE_ERROR;

	error_set (
		error, stream_to_lex (stream), json_error_invalid_utf8,
		"unable to decode byte 0x%x", (unsigned int) character
	);

	return STREAM_STATE_ERROR;

}

// reads the rest of a character of many bytes into the buffer
// returns true if it is valid UTF-8
static bool stream_read_multi_byte (stream_t *stream, const int character) {

	const size_t count = utf8_check_first ((char) character);
	if (!count) return false;

	assert (count >= 2);

	for (size_t index = 1; index < count; index++) {
		stream->buffer[index] = (char) stream->get (stream->data);
	}

	if (!utf8_check_full (stream->buffer, count, NULL)) return false;

	stream->buffer[count] = '\0';
	stream->buffer_length = count;

	return true;

}

// the line and the column of the character that was read
static void stream_advance (stream_t *stream, const int character) {

	stream->position += 1;

	if (character == '\n') {
		stream->line += 1;
		stream->last_column = stream->column;
		stream->column = 0;
	}

	// the column of Unicode characters, only the first byte of each one counts
	else if (utf8_check_first ((char) character)) {
		stream->column += 1;
	}

}

static int stream_get (
	stream_t *stream, json_error_t *error
) {

	if (stream->state != STREAM_STATE_OK) return stream->state;

	// a NUL byte that was put back with stream_unget () is read again, it is not the end of the buffer
	if (stream->buffer_position >= stream->buffer_length) {
		const int character = stream->get (stream->data);
		if (character == EOF) {
			stream->state = STREAM_STATE_EOF;
			return STREAM_STATE_EOF;
		}

		json_parse_budget_add ();

		stream->buffer[0] = (char) character;
		stream->buffer_position = 0;

		if ((STREAM_MULTI_BYTE_FIRST <= character) && (character <= STREAM_MULTI_BYTE_LAST)) {
			if (!stream_read_multi_byte (stream, character)) return stream_decode_failed (stream, error, character);
		}

		else {
			stream->buffer[1] = '\0';
			stream->buffer_length = 1;
		}
	}

	const int character = stream->buffer[stream->buffer_position++];

	stream_advance (stream, character);

	return character;

}

static void stream_unget (stream_t *stream, int character) {
	if (character == STREAM_STATE_EOF || character == STREAM_STATE_ERROR)
		return;

	stream->position--;
	if (character == '\n') {
		stream->line--;
		stream->column = stream->last_column;
	} else if (utf8_check_first (character))
		stream->column--;

	assert (stream->buffer_position > 0);
	stream->buffer_position--;
	assert (stream->buffer[stream->buffer_position] == character);
}

static int lex_get (lex_t *lexer, json_error_t *error) {
	return stream_get (&lexer->stream, error);
}

static void lex_save (lex_t *lexer, int character) {

	(void) strbuffer_append_byte (&lexer->saved_text, character);

}

static int lex_get_save (lex_t *lexer, json_error_t *error) {

	int character = stream_get (&lexer->stream, error);
	if (character != STREAM_STATE_EOF && character != STREAM_STATE_ERROR)
		lex_save (lexer, character);

	return character;

}

static void lex_unget (lex_t *lexer, int character) {

	stream_unget (&lexer->stream, character);

}

static void lex_unget_unsave (lex_t *lexer, int character) {

	if (character != STREAM_STATE_EOF && character != STREAM_STATE_ERROR) {
/* Since we treat warnings as errors, when assertions are turned
 * off the "popped" variable would be set but never used. Which is
 * treated as an error by GCC.
 */
#ifndef NDEBUG
		char popped;
#endif
		stream_unget (&lexer->stream, character);
#ifndef NDEBUG
		popped =
#endif
			strbuffer_pop (&lexer->saved_text);
		assert (character == popped);
	}

}

static void lex_save_cached (lex_t *lexer) {

	while (lexer->stream.buffer_position < lexer->stream.buffer_length) {
		lex_save (lexer, lexer->stream.buffer[lexer->stream.buffer_position]);
		lexer->stream.buffer_position++;
		lexer->stream.position++;
	}

}

static void lex_free_string (lex_t *lexer) {

	jsonp_free (lexer->value.string.value);
	lexer->value.string.value = NULL;
	lexer->value.string.length = 0;

}

/* assumes that string points to 'u' plus at least 4 valid hex digits */
static int32_t decode_unicode_escape (const char *string) {

	int i;
	int32_t value = 0;

	assert (string[0] == 'u');

	for (i = 1; i <= 4; i++) {
		char character = string[i];
		value <<= 4;
		if (l_isdigit (character))
			value += character - '0';
		else if (l_islower (character))
			value += character - 'a' + 10;
		else if (l_isupper (character))
			value += character - 'A' + 10;
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
static bool lex_scan_string_control (lex_t *lexer, const int character, json_error_t *error) {

	lex_unget_unsave (lexer, character);

	if (character == '\n') {
		error_set (error, lexer, json_error_invalid_syntax, "unexpected newline");
	}

	else {
		error_set (error, lexer, json_error_invalid_syntax, "control character 0x%x", (unsigned int) character);
	}

	return false;

}

static bool lex_is_short_escape (const int character) {

	return (character == '"') || (character == '\\') || (character == '/') || (character == 'b')
		|| (character == 'f') || (character == 'n') || (character == 'r') || (character == 't');

}

// an escape after its '\\': \uXXXX or one of the short ones
// returns true if it is valid, with the character after it in character
static bool lex_scan_string_escape (lex_t *lexer, int *character, json_error_t *error) {

	*character = lex_get_save (lexer, error);

	if (*character == 'u') {
		*character = lex_get_save (lexer, error);
		for (unsigned int digit = 0; digit < LEX_UNICODE_ESCAPE_DIGITS; digit++) {
			if (!l_isxdigit (*character)) {
				error_set (error, lexer, json_error_invalid_syntax, "invalid escape");
				return false;
			}

			*character = lex_get_save (lexer, error);
		}
	}

	else if (lex_is_short_escape (*character)) {
		*character = lex_get_save (lexer, error);
	}

	else {
		error_set (error, lexer, json_error_invalid_syntax, "invalid escape");
		return false;
	}

	return true;

}

// reads the string up to its closing quote into the saved text
// returns true if it is a valid string
static bool lex_scan_string_read (lex_t *lexer, json_error_t *error) {

	int character = lex_get_save (lexer, error);

	while (character != '"') {
		if (character == STREAM_STATE_ERROR) return false;

		if (character == STREAM_STATE_EOF) {
			error_set (error, lexer, json_error_premature_end_of_input, "premature end of input");
			return false;
		}

		if ((0 <= character) && (character <= LEX_CONTROL_CHARACTER_LAST)) return lex_scan_string_control (lexer, character, error);

		if (character == '\\') {
			if (!lex_scan_string_escape (lexer, &character, error)) return false;
		}

		else {
			character = lex_get_save (lexer, error);
		}
	}

	return true;

}

// an escape whose 4 characters are not hex digits, from its '\\'
static bool lex_unicode_escape_invalid (lex_t *lexer, json_error_t *error, const char *escape) {

	error_set (error, lexer, json_error_invalid_syntax, "invalid Unicode escape '%.6s'", escape);

	return false;

}

// a surrogate without its other half
static bool lex_unicode_value_invalid (lex_t *lexer, json_error_t *error, const int32_t value) {

	error_set (error, lexer, json_error_invalid_syntax, "invalid Unicode '\\u%04X'", (unsigned int) value);

	return false;

}

// a high surrogate followed by a value that is not a low one
static bool lex_unicode_pair_invalid (
	lex_t *lexer, json_error_t *error, const int32_t first, const int32_t second
) {

	error_set (
		error, lexer, json_error_invalid_syntax,
		"invalid Unicode '\\u%04X\\u%04X'", (unsigned int) first, (unsigned int) second
	);

	return false;

}

// the code point of a \uXXXX escape that position points to, with the 'u',
// or of two that form a surrogate pair, position is moved after them
// returns true if the code point is valid
static bool lex_decode_unicode (
	lex_t *lexer, json_error_t *error, const char **position, int32_t *code_point
) {

	int32_t value = decode_unicode_escape (*position);
	if (value < 0) return lex_unicode_escape_invalid (lexer, error, *position - 1);

	*position += LEX_UNICODE_ESCAPE_LENGTH;

	if ((LEX_LOW_SURROGATE_FIRST <= value) && (value <= LEX_LOW_SURROGATE_LAST)) {
		return lex_unicode_value_invalid (lexer, error, value);
	}

	if ((LEX_HIGH_SURROGATE_FIRST <= value) && (value <= LEX_HIGH_SURROGATE_LAST)) {
		// a high surrogate must be followed by a low one
		if (((*position)[0] != '\\') || ((*position)[1] != 'u')) return lex_unicode_value_invalid (lexer, error, value);

		*position += 1;

		const int32_t second = decode_unicode_escape (*position);
		if (second < 0) return lex_unicode_escape_invalid (lexer, error, *position - 1);

		*position += LEX_UNICODE_ESCAPE_LENGTH;

		if ((second < LEX_LOW_SURROGATE_FIRST) || (second > LEX_LOW_SURROGATE_LAST)) {
			return lex_unicode_pair_invalid (lexer, error, value, second);
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
static bool lex_scan_string_decode (lex_t *lexer, json_error_t *error) {

	char *value = (char *) jsonp_malloc (lexer->saved_text.length + 1);
	if (!value) return false;

	lexer->value.string.value = value;

	// after the opening quote
	const char *position = strbuffer_value (&lexer->saved_text) + 1;

	while (*position != '"') {
		if (*position != '\\') {
			*(value++) = *(position++);
		}

		else if (*(++position) == 'u') {
			int32_t code_point = 0;
			if (!lex_decode_unicode (lexer, error, &position, &code_point)) return false;

			size_t length = 0;
			if (utf8_encode (code_point, value, &length)) assert (0);
			value += length;
		}

		else {
			*(value++) = lex_short_escape_value (*(position++));
		}
	}

	*value = '\0';
	lexer->value.string.length = (size_t) (value - lexer->value.string.value);

	return true;

}

static void lex_scan_string (lex_t *lexer, json_error_t *error) {

	lexer->value.string.value = NULL;
	lexer->token = TOKEN_INVALID;

	if (lex_scan_string_read (lexer, error) && lex_scan_string_decode (lexer, error)) {
		lexer->token = TOKEN_STRING;
	}

	else {
		lex_free_string (lexer);
	}

}

// the integers of the json are long long
#define json_strtoint strtoll

// the base of the integers of the json text
#define LEX_INTEGER_BASE				10

// a part of a number that is not valid, never a byte nor a state of the stream
#define LEX_NUMBER_INVALID				-3

// reads digits while they come, returns the first character that is not one
static int lex_scan_digits (lex_t *lexer, json_error_t *error) {

	int character = 0;

	do {
		character = lex_get_save (lexer, error);
	} while (l_isdigit (character));

	return character;

}

// an integer that fits in a json_int_t
// returns 0 on success, -1 on error
static int lex_scan_integer (lex_t *lexer, int character, json_error_t *error) {

	lex_unget_unsave (lexer, character);

	const char *saved_text = strbuffer_value (&lexer->saved_text);

	char *end = NULL;
	errno = 0;
	const json_int_t integer = json_strtoint (saved_text, &end, LEX_INTEGER_BASE);
	if (errno == ERANGE) {
		if (integer < 0) {
			error_set (error, lexer, json_error_numeric_overflow, "too big negative integer");
		}

		else {
			error_set (error, lexer, json_error_numeric_overflow, "too big integer");
		}

		return -1;
	}

	assert (end == saved_text + lexer->saved_text.length);

	lexer->token = TOKEN_INTEGER;
	lexer->value.integer = integer;

	return 0;

}

// the digits after the dot of a real
// returns the character after them, or LEX_NUMBER_INVALID if there are none
static int lex_scan_fraction (lex_t *lexer, json_error_t *error) {

	int character = lex_get (lexer, error);
	if (!l_isdigit (character)) {
		lex_unget (lexer, character);
		return LEX_NUMBER_INVALID;
	}

	lex_save (lexer, character);

	return lex_scan_digits (lexer, error);

}

// the exponent of a real, with its sign and its digits
// returns the character after them, or LEX_NUMBER_INVALID if there are no digits
static int lex_scan_exponent (lex_t *lexer, json_error_t *error) {

	int character = lex_get_save (lexer, error);
	if ((character == '+') || (character == '-')) character = lex_get_save (lexer, error);

	if (!l_isdigit (character)) {
		lex_unget_unsave (lexer, character);
		return LEX_NUMBER_INVALID;
	}

	return lex_scan_digits (lexer, error);

}

// the integer part of a number: a single 0 or digits that do not start with 0
// returns the character after it, or LEX_NUMBER_INVALID if it is not valid
static int lex_scan_integer_part (lex_t *lexer, int character, json_error_t *error) {

	if (character == '0') {
		character = lex_get_save (lexer, error);
		if (l_isdigit (character)) {
			lex_unget_unsave (lexer, character);
			return LEX_NUMBER_INVALID;
		}
	}

	else if (l_isdigit (character)) {
		character = lex_scan_digits (lexer, error);
	}

	else {
		lex_unget_unsave (lexer, character);
		return LEX_NUMBER_INVALID;
	}

	return character;

}

static int lex_scan_number (
	lex_t *lexer, int character, json_error_t *error
) {

	lexer->token = TOKEN_INVALID;

	if (character == '-') character = lex_get_save (lexer, error);

	character = lex_scan_integer_part (lexer, character, error);
	if (character == LEX_NUMBER_INVALID) return -1;

	if (!(lexer->flags & JSON_DECODE_INT_AS_REAL) && (character != '.') && (character != 'E') && (character != 'e')) {
		return lex_scan_integer (lexer, character, error);
	}

	if (character == '.') {
		character = lex_scan_fraction (lexer, error);
		if (character == LEX_NUMBER_INVALID) return -1;
	}

	if ((character == 'E') || (character == 'e')) {
		character = lex_scan_exponent (lexer, error);
		if (character == LEX_NUMBER_INVALID) return -1;
	}

	lex_unget_unsave (lexer, character);

	double real = 0;
	if (jsonp_strtod (&lexer->saved_text, &real)) {
		error_set (error, lexer, json_error_numeric_overflow, "real number overflow");
		return -1;
	}

	lexer->token = TOKEN_REAL;
	lexer->value.real = real;

	return 0;

}

#define LEX_WORD_TRUE					"true"
#define LEX_WORD_FALSE					"false"
#define LEX_WORD_NULL					"null"

// the whole word is read, so its error is clearer
static void lex_scan_word (lex_t *lexer, json_error_t *error) {

	int character = 0;
	do {
		character = lex_get_save (lexer, error);
	} while (l_isalpha (character));

	lex_unget_unsave (lexer, character);

	const char *saved_text = strbuffer_value (&lexer->saved_text);

	if (!strcmp (saved_text, LEX_WORD_TRUE)) lexer->token = TOKEN_TRUE;
	else if (!strcmp (saved_text, LEX_WORD_FALSE)) lexer->token = TOKEN_FALSE;
	else if (!strcmp (saved_text, LEX_WORD_NULL)) lexer->token = TOKEN_NULL;
	else lexer->token = TOKEN_INVALID;

}

static int lex_scan (
	lex_t *lexer, json_error_t *error
) {

	int character = 0;

	strbuffer_clear (&lexer->saved_text);

	if (lexer->token == TOKEN_STRING)
		lex_free_string (lexer);

	do
		character = lex_get (lexer, error);
	while (character == ' ' || character == '\t' || character == '\n' || character == '\r');

	if (character == STREAM_STATE_EOF) {
		lexer->token = TOKEN_EOF;
		return lexer->token;
	}

	if (character == STREAM_STATE_ERROR) {
		lexer->token = TOKEN_INVALID;
		return lexer->token;
	}

	lex_save (lexer, character);

	if (character == '{' || character == '}' || character == '[' || character == ']' || character == ':' || character == ',')
		lexer->token = character;

	else if (character == '"')
		lex_scan_string (lexer, error);

	// a number that fails keeps its invalid token
	else if (l_isdigit (character) || (character == '-')) {
		(void) lex_scan_number (lexer, character, error);
	}

	else if (l_isalpha (character)) {
		lex_scan_word (lexer, error);
	}

	// the rest of the UTF-8 sequence is saved, so its error is valid UTF-8
	else {
		lex_save_cached (lexer);
		lexer->token = TOKEN_INVALID;
	}

	return lexer->token;

}

static char *lex_steal_string (
	lex_t *lexer, size_t *output_length
) {

	char *result = NULL;
	if (lexer->token == TOKEN_STRING) {
		result = lexer->value.string.value;
		*output_length = lexer->value.string.length;
		lexer->value.string.value = NULL;
		lexer->value.string.length = 0;
	}

	return result;

}

static int lex_init (
	lex_t *lexer, get_func get, size_t flags, void *data
) {

	stream_init (&lexer->stream, get, data);
	if (strbuffer_init (&lexer->saved_text))
		return -1;

	lexer->flags = flags;
	lexer->token = TOKEN_INVALID;

	return 0;

}

static void lex_close (lex_t *lexer) {

	if (lexer->token == TOKEN_STRING)
		lex_free_string (lexer);
	strbuffer_close (&lexer->saved_text);

}

// the parser

static json_t *parse_value (
	lex_t *lexer, size_t flags, json_error_t *error
);

// the key of a member: a string without NUL bytes that is not repeated if they are rejected
// returns the key, released with jsonp_free (), or NULL on error
static char *parse_object_key (
	lex_t *lexer, size_t flags, json_error_t *error, const json_t *object
) {

	if (lexer->token != TOKEN_STRING) {
		error_set (error, lexer, json_error_invalid_syntax, "string or '}' expected");
		return NULL;
	}

	size_t length = 0;
	char *key = lex_steal_string (lexer, &length);
	if (!key) return NULL;

	if (memchr (key, '\0', length)) {
		error_set (error, lexer, json_error_null_byte_in_key, "NUL byte in object key not supported");
	}

	else if ((flags & JSON_REJECT_DUPLICATES) && json_object_get (object, key)) {
		error_set (error, lexer, json_error_duplicate_key, "duplicate object key");
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
	lex_t *lexer, size_t flags, json_error_t *error, json_t *object
) {

	char *key = parse_object_key (lexer, flags, error, object);
	if (!key) return false;

	bool parsed = false;

	lex_scan (lexer, error);
	if (lexer->token != ':') {
		error_set (error, lexer, json_error_invalid_syntax, "':' expected");
	}

	else {
		lex_scan (lexer, error);

		json_t *value = parse_value (lexer, flags, error);
		parsed = value && !json_object_set_new_nocheck (object, key, value);
	}

	jsonp_free (key);

	return parsed;

}

static json_t *parse_object (
	lex_t *lexer, size_t flags, json_error_t *error
) {

	json_t *object = json_object ();
	if (!object) return NULL;

	lex_scan (lexer, error);
	if (lexer->token == '}') return object;

	bool parsed = true;
	while (parsed) {
		parsed = parse_object_member (lexer, flags, error, object);
		if (!parsed) break;

		lex_scan (lexer, error);
		if (lexer->token != ',') break;

		lex_scan (lexer, error);
	}

	if (parsed && (lexer->token != '}')) {
		error_set (error, lexer, json_error_invalid_syntax, "'}' expected");
		parsed = false;
	}

	if (!parsed) {
		json_decref (object);
		object = NULL;
	}

	return object;

}

static json_t *parse_array (
	lex_t *lexer, size_t flags, json_error_t *error
) {

	json_t *array = json_array ();
	if (!array) return NULL;

	lex_scan (lexer, error);
	if (lexer->token == ']') return array;

	bool parsed = true;
	while (lexer->token) {
		json_t *element = parse_value (lexer, flags, error);

		// the array releases the element when it fails to append it
		parsed = element && !json_array_append_new (array, element);
		if (!parsed) break;

		lex_scan (lexer, error);
		if (lexer->token != ',') break;

		lex_scan (lexer, error);
	}

	if (parsed && (lexer->token != ']')) {
		error_set (error, lexer, json_error_invalid_syntax, "']' expected");
		parsed = false;
	}

	if (!parsed) {
		json_decref (array);
		array = NULL;
	}

	return array;

}

static json_t *parse_value (
	lex_t *lexer, size_t flags, json_error_t *error
) {

	json_t *json;

	lexer->depth++;
	if (lexer->depth > JSON_PARSER_MAX_DEPTH) {
		error_set (error, lexer, json_error_stack_overflow, "maximum parsing depth reached");
		return NULL;
	}

	switch (lexer->token) {
		case TOKEN_STRING: {
			const char *value = lexer->value.string.value;
			size_t length = lexer->value.string.length;

			if (!(flags & JSON_ALLOW_NUL)) {
				if (memchr (value, '\0', length)) {
					error_set (error, lexer, json_error_null_character,
							  "\\u0000 is not allowed without JSON_ALLOW_NUL");
					return NULL;
				}
			}

			json = jsonp_stringn_nocheck_own (value, length);
			lexer->value.string.value = NULL;
			lexer->value.string.length = 0;
			break;
		}

		case TOKEN_INTEGER: {
			json = json_integer (lexer->value.integer);
			break;
		}

		case TOKEN_REAL: {
			json = json_real (lexer->value.real);
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
			json = parse_object (lexer, flags, error);
			break;

		case '[':
			json = parse_array (lexer, flags, error);
			break;

		case TOKEN_INVALID:
			error_set (error, lexer, json_error_invalid_syntax, "invalid token");
			return NULL;

		default:
			error_set (error, lexer, json_error_invalid_syntax, "unexpected token");
			return NULL;
	}

	if (!json)
		return NULL;

	lexer->depth--;
	return json;

}

// a parse that ran out of its memory says why, whatever error its parser saw after that
static void parse_json_exhausted (lex_t *lexer, json_error_t *error) {

	if (json_parse_exhausted) {
		if (error) error->text[0] = '\0';

		error_set (
			error, lexer, json_error_out_of_memory,
			"the json needs more memory than its size allows"
		);
	}

}

static json_t *parse_json_value (
	lex_t *lexer, size_t flags, json_error_t *error
) {

	json_t *result;

	lexer->depth = 0;

	lex_scan (lexer, error);
	if (!(flags & JSON_DECODE_ANY)) {
		if (lexer->token != '[' && lexer->token != '{') {
			error_set (error, lexer, json_error_invalid_syntax, "'[' or '{' expected");
			return NULL;
		}
	}

	result = parse_value (lexer, flags, error);
	if (!result)
		return NULL;

	if (!(flags & JSON_DISABLE_EOF_CHECK)) {
		lex_scan (lexer, error);
		if (lexer->token != TOKEN_EOF) {
			error_set (error, lexer, json_error_end_of_input_expected,
					  "end of file expected");
			json_decref (result);
			return NULL;
		}
	}

	if (error) {
		/* Save the position even though there was no error */
		error->position = json_error_position (lexer->stream.position);
	}

	return result;

}

// the memory of the values is limited by the size of the document
static json_t *parse_json (
	lex_t *lexer, size_t flags, json_error_t *error
) {

	json_parse_budget_start ();

	json_t *result = parse_json_value (lexer, flags, error);
	if (!result) parse_json_exhausted (lexer, error);

	json_parse_budget_end ();

	return result;

}

typedef struct {

	const char *data;
	size_t position;

} string_data_t;

static int string_get (void *data) {

	char character;
	string_data_t *stream = (string_data_t *)data;
	character = stream->data[stream->position];
	if (character == '\0')
		return EOF;
	else {
		stream->position++;
		return (unsigned char)character;
	}

}

json_t *json_loads (
	const char *string, size_t flags, json_error_t *error
) {

	lex_t lexer;
	json_t *result;
	string_data_t stream_data;

	jsonp_error_init (error, "<string>");

	if (string == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	stream_data.data = string;
	stream_data.position = 0;

	if (lex_init (&lexer, string_get, flags, (void *)&stream_data))
		return NULL;

	result = parse_json (&lexer, flags, error);

	lex_close (&lexer);
	return result;

}

typedef struct {

	const char *data;
	size_t length;
	size_t position;

} buffer_data_t;

static int buffer_get (void *data) {

	char character;
	buffer_data_t *stream = (buffer_data_t *) data;
	if (stream->position >= stream->length)
		return EOF;

	character = stream->data[stream->position];
	stream->position++;
	return (unsigned char)character;

}

json_t *json_loadb (
	const char *buffer, size_t buffer_length, size_t flags, json_error_t *error
) {

	lex_t lexer;
	json_t *result;
	buffer_data_t stream_data;

	jsonp_error_init (error, "<buffer>");

	if (buffer == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	stream_data.data = buffer;
	stream_data.position = 0;
	stream_data.length = buffer_length;

	if (lex_init (&lexer, buffer_get, flags, (void *)&stream_data))
		return NULL;

	result = parse_json (&lexer, flags, error);

	lex_close (&lexer);
	return result;

}

static int file_get_func (void *data) {

	return fgetc ((FILE *) data);

}

json_t *json_loadf (
	FILE *input, size_t flags, json_error_t *error
) {

	lex_t lexer;
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

	if (lex_init (&lexer, file_get_func, flags, input))
		return NULL;

	result = parse_json (&lexer, flags, error);

	lex_close (&lexer);
	return result;

}

static int fd_get_func (void *data) {

	uint8_t character = 0;
	if (read (*(int *) data, &character, 1) == 1)
		return (int) character;

	return EOF;

}

json_t *json_loadfd (
	int input, size_t flags, json_error_t *error
) {

	lex_t lexer = CERVER_ZERO_INITIALIZER;
	const char *source;
	json_t *result;

	source = (input == STDIN_FILENO) ? "<stdin>" : "<stream>";

	jsonp_error_init (error, source);

	if (input < 0) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	if (lex_init (&lexer, fd_get_func, flags, &input))
		return NULL;

	result = parse_json (&lexer, flags, error);

	lex_close (&lexer);
	return result;

}

json_t *json_load_file (
	const char *path, size_t flags, json_error_t *error
) {

	json_t *result;
	FILE *file;

	jsonp_error_init (error, path);

	if (path == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	file = fopen (path, "rb");
	if (!file) {
		error_set (error, NULL, json_error_cannot_open_file, "unable to open %s: %s", path,
				  strerror (errno));
		return NULL;
	}

	result = json_loadf (file, flags, error);

	fclose (file);
	return result;

}

#define CALLBACK_BUFFER_SIZE 1024

typedef struct {

	char data[CALLBACK_BUFFER_SIZE];
	size_t length;
	size_t position;
	json_load_callback_t callback;
	void *argument;

} callback_data_t;

static int callback_get (void *data) {

	char character;
	callback_data_t *stream = (callback_data_t *) data;

	if (stream->position >= stream->length) {
		stream->position = 0;
		stream->length = stream->callback (stream->data, CALLBACK_BUFFER_SIZE, stream->argument);
		if (stream->length == 0 || stream->length == (size_t)-1)
			return EOF;
	}

	character = stream->data[stream->position];
	stream->position++;
	return (unsigned char)character;

}

json_t *json_load_callback (
	json_load_callback_t callback, void *argument, size_t flags,
	json_error_t *error
) {

	lex_t lexer = CERVER_ZERO_INITIALIZER;
	json_t *result = NULL;

	callback_data_t stream_data;

	memset (&stream_data, 0, sizeof (stream_data));
	stream_data.callback = callback;
	stream_data.argument = argument;

	jsonp_error_init (error, "<callback>");

	if (callback == NULL) {
		error_set (error, NULL, json_error_invalid_argument, "wrong arguments");
		return NULL;
	}

	if (lex_init (&lexer, callback_get, flags, &stream_data))
		return NULL;

	result = parse_json (&lexer, flags, error);

	lex_close (&lexer);
	return result;

}

#pragma endregion

#pragma region packing

typedef struct token_t {

	int line;
	int column;
	size_t position;
	char token;

} token_t;

typedef struct scanner_t {

	const char *start;
	const char *format;
	token_t prev_token;
	token_t token;
	token_t next_token;
	json_error_t *error;
	size_t flags;
	int line;
	int column;
	size_t position;
	int has_error;

} scanner_t;

#define token(scanner) ((scanner)->token.token)

static const char *const type_names[] = {
	"object", "array", "string", "integer",
	"real",   "true",  "false",  "null"
};

#define type_name(json) type_names[json_typeof (json)]

static const char unpack_value_starters[] = "{[siIbfFOon";

static void scanner_init (
	scanner_t *scanner, json_error_t *error, size_t flags,
	const char *format
) {

	scanner->error = error;
	scanner->flags = flags;
	scanner->format = scanner->start = format;
	(void) memset (&scanner->prev_token, 0, sizeof (token_t));
	(void) memset (&scanner->token, 0, sizeof (token_t));
	(void) memset (&scanner->next_token, 0, sizeof (token_t));
	scanner->line = 1;
	scanner->column = 0;
	scanner->position = 0;
	scanner->has_error = 0;

}

static void next_token (scanner_t *scanner) {

	const char *cursor;
	scanner->prev_token = scanner->token;

	if (scanner->next_token.line) {
		scanner->token = scanner->next_token;
		scanner->next_token.line = 0;
		return;
	}

	if (!token (scanner) && !*scanner->format)
		return;

	cursor = scanner->format;
	scanner->column++;
	scanner->position++;

	/* skip space and ignored chars */
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == ',' || *cursor == ':') {
		if (*cursor == '\n') {
			scanner->line++;
			scanner->column = 1;
		} else
			scanner->column++;

		scanner->position++;
		cursor++;
	}

	scanner->token.token = *cursor;
	scanner->token.line = scanner->line;
	scanner->token.column = scanner->column;
	scanner->token.position = scanner->position;

	if (*cursor)
		cursor++;
	scanner->format = cursor;

}

static void prev_token (scanner_t *scanner) {

	scanner->next_token = scanner->token;
	scanner->token = scanner->prev_token;

}

CERVER_PRINTF_FORMAT (4, 5) static void set_error (
	scanner_t *scanner, const char *source, enum json_error_code code,
	const char *format, ...
) {

	va_list arguments;
	va_start (arguments, format);

	jsonp_error_vset (
		scanner->error, scanner->token.line, scanner->token.column, scanner->token.position,
		code, format, arguments
	);

	jsonp_error_set_source (scanner->error, source);

	va_end (arguments);
}

static json_t *pack (scanner_t *scanner, va_list *arguments);

/* ours will be set to 1 if jsonp_free() must be called for the result
   afterwards */
static char *read_string (
	scanner_t *scanner, va_list *arguments, const char *purpose, size_t *output_length,
	int *ours, int optional
) {

	char following;
	strbuffer_t string_buffer;
	const char *string;
	size_t length;

	next_token (scanner);
	following = token (scanner);
	prev_token (scanner);

	*ours = 0;
	if (following != '#' && following != '%' && following != '+') {
		/* Optimize the simple case */
		string = va_arg (*arguments, const char *);

		if (!string) {
			if (!optional) {
				set_error (scanner, "<args>", json_error_null_value, "NULL %s", purpose);
				scanner->has_error = 1;
			}
			return NULL;
		}

		length = strlen (string);

		if (!utf8_check_string (string, length)) {
			set_error (scanner, "<args>", json_error_invalid_utf8, "Invalid UTF-8 %s", purpose);
			scanner->has_error = 1;
			return NULL;
		}

		*output_length = length;
		return (char *)string;
	} else if (optional) {
		set_error (scanner, "<format>", json_error_invalid_format,
				  "Cannot use '%c' on optional strings", following);
		scanner->has_error = 1;

		return NULL;
	}

	if (strbuffer_init (&string_buffer)) {
		set_error (scanner, "<internal>", json_error_out_of_memory, "Out of memory");
		scanner->has_error = 1;
	}

	while (1) {
		string = va_arg (*arguments, const char *);
		if (!string) {
			set_error (scanner, "<args>", json_error_null_value, "NULL %s", purpose);
			scanner->has_error = 1;
		}

		next_token (scanner);

		if (token (scanner) == '#') {
			length = va_arg (*arguments, int);
		} else if (token (scanner) == '%') {
			length = va_arg (*arguments, size_t);
		} else {
			prev_token (scanner);
			length = scanner->has_error ? 0 : strlen (string);
		}

		if (!scanner->has_error && strbuffer_append_bytes (&string_buffer, string, length) == -1) {
			set_error (scanner, "<internal>", json_error_out_of_memory, "Out of memory");
			scanner->has_error = 1;
		}

		next_token (scanner);
		if (token (scanner) != '+') {
			prev_token (scanner);
			break;
		}
	}

	if (scanner->has_error) {
		strbuffer_close (&string_buffer);
		return NULL;
	}

	if (!utf8_check_string (string_buffer.value, string_buffer.length)) {
		set_error (scanner, "<args>", json_error_invalid_utf8, "Invalid UTF-8 %s", purpose);
		strbuffer_close (&string_buffer);
		scanner->has_error = 1;
		return NULL;
	}

	*output_length = string_buffer.length;
	*ours = 1;
	return strbuffer_steal_value (&string_buffer);

}

// the format of an optional value, that can be NULL
#define PACK_OPTIONAL_VALUE				'*'

// the token that follows the value, without moving the scanner
static char pack_peek_next (scanner_t *scanner) {

	next_token (scanner);
	const char next = token (scanner);
	prev_token (scanner);

	return next;

}

// a format that ends before its object or array, it can not be recovered from
static bool pack_check_format_end (scanner_t *scanner) {

	if (!token (scanner)) {
		set_error (scanner, "<format>", json_error_invalid_format, "Unexpected end of format string");
		return false;
	}

	return true;

}

// a "key": value of the object, the errors of the arguments are kept in the scanner
// returns false on an error of the format, that ends the object
static bool pack_object_member (scanner_t *scanner, va_list *arguments, json_t *object) {

	if (!pack_check_format_end (scanner)) return false;

	if (token (scanner) != 's') {
		set_error (scanner, "<format>", json_error_invalid_format, "Expected format 's', got '%c'", token (scanner));
		return false;
	}

	size_t length = 0;
	int ours = 0;
	char *key = read_string (scanner, arguments, "object key", &length, &ours, 0);

	next_token (scanner);

	const char value_format = pack_peek_next (scanner);

	json_t *value = pack (scanner, arguments);
	if (!value) {
		if (value_format != PACK_OPTIONAL_VALUE) {
			set_error (scanner, "<args>", json_error_null_value, "NULL object value");
			scanner->has_error = 1;
		}
	}

	else if (scanner->has_error) {
		json_decref (value);
	}

	else if (json_object_set_new_nocheck (object, key, value)) {
		set_error (scanner, "<internal>", json_error_out_of_memory, "Unable to add key \"%s\"", key);
		scanner->has_error = 1;
	}

	if (ours) jsonp_free (key);

	next_token (scanner);

	return true;

}

static json_t *pack_object (scanner_t *scanner, va_list *arguments) {

	json_t *object = json_object ();
	next_token (scanner);

	bool packed = true;
	while (packed && (token (scanner) != '}')) {
		packed = pack_object_member (scanner, arguments, object);
	}

	if (!packed || scanner->has_error) {
		json_decref (object);
		object = NULL;
	}

	return object;

}

// a value of the array, the errors of the arguments are kept in the scanner
// returns false on an error of the format, that ends the array
static bool pack_array_element (scanner_t *scanner, va_list *arguments, json_t *array) {

	if (!pack_check_format_end (scanner)) return false;

	const char value_format = pack_peek_next (scanner);

	json_t *value = pack (scanner, arguments);
	if (!value) {
		if (value_format != PACK_OPTIONAL_VALUE) scanner->has_error = 1;
	}

	else if (scanner->has_error) {
		json_decref (value);
	}

	else if (json_array_append_new (array, value)) {
		set_error (scanner, "<internal>", json_error_out_of_memory, "Unable to append to array");
		scanner->has_error = 1;
	}

	next_token (scanner);

	return true;

}

static json_t *pack_array (scanner_t *scanner, va_list *arguments) {

	json_t *array = json_array ();
	next_token (scanner);

	bool packed = true;
	while (packed && (token (scanner) != ']')) {
		packed = pack_array_element (scanner, arguments, array);
	}

	if (!packed || scanner->has_error) {
		json_decref (array);
		array = NULL;
	}

	return array;

}

static json_t *pack_string (scanner_t *scanner, va_list *arguments) {

	char *string;
	char modifier;
	size_t length;
	int ours;
	int optional;

	next_token (scanner);
	modifier = token (scanner);
	optional = modifier == '?' || modifier == '*';
	if (!optional)
		prev_token (scanner);

	string = read_string (scanner, arguments, "string", &length, &ours, optional);

	if (!string)
		return modifier == '?' && !scanner->has_error ? json_null () : NULL;

	if (scanner->has_error) {
		/* It's impossible to reach this point if ours != 0, do not free string. */
		return NULL;
	}

	if (ours)
		return jsonp_stringn_nocheck_own (string, length);

	return json_stringn_nocheck (string, length);

}

static json_t *pack_object_inter (
	scanner_t *scanner, va_list *arguments, int need_incref
) {

	json_t *json;
	char next;

	next_token (scanner);
	next = token (scanner);

	if (next != '?' && next != '*')
		prev_token (scanner);

	json = va_arg (*arguments, json_t *);

	if (json)
		return need_incref ? json_incref (json) : json;

	switch (next) {
		case '?':
			return json_null ();
		case '*':
			return NULL;
		default:
			break;
	}

	set_error (scanner, "<args>", json_error_null_value, "NULL object");
	scanner->has_error = 1;
	return NULL;

}

static json_t *pack_integer (scanner_t *scanner, json_int_t value) {

	json_t *json = json_integer (value);

	if (!json) {
		set_error (scanner, "<internal>", json_error_out_of_memory, "Out of memory");
		scanner->has_error = 1;
	}

	return json;

}

static json_t *pack_real (scanner_t *scanner, double value) {

	/* Allocate without setting value so we can identify OOM error. */
	json_t *json = json_real (0.0);

	if (!json) {
		set_error (scanner, "<internal>", json_error_out_of_memory, "Out of memory");
		scanner->has_error = 1;

		return NULL;
	}

	if (json_real_set (json, value)) {
		json_decref (json);

		set_error (scanner, "<args>", json_error_numeric_overflow,
				  "Invalid floating point value");
		scanner->has_error = 1;

		return NULL;
	}

	return json;

}

static json_t *pack (scanner_t *scanner, va_list *arguments) {

	switch (token (scanner)) {
		case '{':
			return pack_object (scanner, arguments);

		case '[':
			return pack_array (scanner, arguments);

		case 's': /* string */
			return pack_string (scanner, arguments);

		case 'n': /* null */
			return json_null ();

		case 'b': /* boolean */
			return va_arg (*arguments, int) ? json_true () : json_false ();

		case 'i': /* integer from int */
			return pack_integer (scanner, va_arg (*arguments, int));

		case 'I': /* integer from json_int_t */
			return pack_integer (scanner, va_arg (*arguments, json_int_t));

		case 'f': /* real */
			return pack_real (scanner, va_arg (*arguments, double));

		case 'O': /* a json_t object; increments refcount */
			return pack_object_inter (scanner, arguments, 1);

		case 'o': /* a json_t object; doesn't increment refcount */
			return pack_object_inter (scanner, arguments, 0);

		default:
			set_error (scanner, "<format>", json_error_invalid_format,
					  "Unexpected format character '%c'", token (scanner));
			scanner->has_error = 1;
			return NULL;
	}

}

static int unpack (scanner_t *scanner, json_t *root, va_list *arguments);

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
	scanner_t *scanner, json_t *root, const char *key, const bool optional, json_t **value
) {

	*value = root ? json_object_get (root, key) : NULL;

	if (root && !*value && !optional) {
		set_error (scanner, "<validation>", json_error_item_not_found, "Object item not found: %s", key);
		return -1;
	}

	return 0;

}

// a "key": value of the format, the key is the next argument
// returns 0 on success, -1 on error
static int unpack_object_member (
	scanner_t *scanner, json_t *root, va_list *arguments, UnpackObject *unpacked
) {

	if (token (scanner) != 's') {
		set_error (scanner, "<format>", json_error_invalid_format, "Expected format 's', got '%c'", token (scanner));
		return -1;
	}

	const char *key = va_arg (*arguments, const char *);
	if (!key) {
		set_error (scanner, "<args>", json_error_null_value, "NULL object key");
		return -1;
	}

	next_token (scanner);

	bool optional = false;
	if (token (scanner) == UNPACK_OPTIONAL_FORMAT) {
		optional = true;
		unpacked->got_optional = true;
		next_token (scanner);
	}

	json_t *value = NULL;
	if (unpack_object_value (scanner, root, key, optional, &value)) return -1;

	if (unpack (scanner, value, arguments)) return -1;

	(void) hashtable_set (&unpacked->keys, key, json_null ());

	next_token (scanner);

	return 0;

}

// the members of the format up to its '}'
// returns 0 on success, -1 on error
static int unpack_object_members (
	scanner_t *scanner, json_t *root, va_list *arguments, UnpackObject *unpacked
) {

	next_token (scanner);

	while (token (scanner) != '}') {
		if (unpacked->strict != UNPACK_STRICT_NONE) {
			set_error (
				scanner, "<format>", json_error_invalid_format,
				"Expected '}' after '%c', got '%c'",
				(unpacked->strict == UNPACK_STRICT_ALL) ? UNPACK_STRICT_ALL_FORMAT : UNPACK_STRICT_SOME_FORMAT,
				token (scanner)
			);

			return -1;
		}

		if (!token (scanner)) {
			set_error (scanner, "<format>", json_error_invalid_format, "Unexpected end of format string");
			return -1;
		}

		if ((token (scanner) == UNPACK_STRICT_ALL_FORMAT) || (token (scanner) == UNPACK_STRICT_SOME_FORMAT)) {
			unpacked->strict = (token (scanner) == UNPACK_STRICT_ALL_FORMAT) ? UNPACK_STRICT_ALL : UNPACK_STRICT_SOME;
			next_token (scanner);
		}

		else if (unpack_object_member (scanner, root, arguments, unpacked)) {
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
static int unpack_object_check_left (scanner_t *scanner, json_t *root, UnpackObject *unpacked) {

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
		scanner, "<validation>", json_error_end_of_input_expected,
		"%li object item(s) left unpacked: %s", left,
		keys_result ? "<unknown>" : strbuffer_value (&left_keys)
	);

	strbuffer_close (&left_keys);

	return -1;

}

static int unpack_object (scanner_t *scanner, json_t *root, va_list *arguments) {

	UnpackObject unpacked = CERVER_ZERO_INITIALIZER;
	unpacked.strict = UNPACK_STRICT_NONE;
	unpacked.got_optional = false;

	if (hashtable_init (&unpacked.keys)) {
		set_error (scanner, "<internal>", json_error_out_of_memory, "Out of memory");
		return -1;
	}

	int retval = -1;

	if (root && !json_is_object (root)) {
		set_error (scanner, "<validation>", json_error_wrong_type, "Expected object, got %s", type_name (root));
	}

	else if (!unpack_object_members (scanner, root, arguments, &unpacked)) {
		if ((unpacked.strict == UNPACK_STRICT_NONE) && (scanner->flags & JSON_STRICT)) unpacked.strict = UNPACK_STRICT_ALL;

		retval = (root && (unpacked.strict == UNPACK_STRICT_ALL)) ?
			unpack_object_check_left (scanner, root, &unpacked) : 0;
	}

	hashtable_close (&unpacked.keys);

	return retval;

}

static int unpack_array (scanner_t *scanner, json_t *root, va_list *arguments) {
	size_t i = 0;
	int strict = 0;

	if (root && !json_is_array (root)) {
		set_error (scanner, "<validation>", json_error_wrong_type, "Expected array, got %s",
				  type_name (root));
		return -1;
	}
	next_token (scanner);

	while (token (scanner) != ']') {
		json_t *value;

		if (strict != 0) {
			set_error (scanner, "<format>", json_error_invalid_format,
					  "Expected ']' after '%c', got '%c'", (strict == 1 ? '!' : '*'),
					  token (scanner));
			return -1;
		}

		if (!token (scanner)) {
			set_error (scanner, "<format>", json_error_invalid_format,
					  "Unexpected end of format string");
			return -1;
		}

		if (token (scanner) == '!' || token (scanner) == '*') {
			strict = (token (scanner) == '!' ? 1 : -1);
			next_token (scanner);
			continue;
		}

		if (!strchr (unpack_value_starters, token (scanner))) {
			set_error (scanner, "<format>", json_error_invalid_format,
					  "Unexpected format character '%c'", token (scanner));
			return -1;
		}

		if (!root) {
			/* skipping */
			value = NULL;
		} else {
			value = json_array_get (root, i);
			if (!value) {
				set_error (scanner, "<validation>", json_error_index_out_of_range,
						  "Array index %lu out of range", (unsigned long)i);
				return -1;
			}
		}

		if (unpack (scanner, value, arguments))
			return -1;

		next_token (scanner);
		i++;
	}

	if (strict == 0 && (scanner->flags & JSON_STRICT))
		strict = 1;

	if (root && strict == 1 && i != json_array_size (root)) {
		long difference = (long)json_array_size (root) - (long)i;
		set_error (scanner, "<validation>", json_error_end_of_input_expected,
				  "%li array item(s) left unpacked", difference);
		return -1;
	}

	return 0;
}

static int unpack (scanner_t *scanner, json_t *root, va_list *arguments) {

	switch (token (scanner)) {
		case '{':
			return unpack_object (scanner, root, arguments);

		case '[':
			return unpack_array (scanner, root, arguments);

		case 's':
			if (root && !json_is_string (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected string, got %s", type_name (root));
				return -1;
			}

			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				const char **string_target;
				size_t *length_target = NULL;

				string_target = va_arg (*arguments, const char **);
				if (!string_target) {
					set_error (scanner, "<args>", json_error_null_value, "NULL string argument");
					return -1;
				}

				next_token (scanner);

				if (token (scanner) == '%') {
					length_target = va_arg (*arguments, size_t *);
					if (!length_target) {
						set_error (scanner, "<args>", json_error_null_value,
								  "NULL string length argument");
						return -1;
					}
				} else
					prev_token (scanner);

				if (root) {
					*string_target = json_string_value (root);
					if (length_target)
						*length_target = json_string_length (root);
				}
			}
			return 0;

		case 'i':
			if (root && !json_is_integer (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected integer, got %s", type_name (root));
				return -1;
			}

			// an integer that does not fit in an int is an error, it was cut before
			if (root && ((json_integer_value (root) > INT_MAX) || (json_integer_value (root) < INT_MIN))) {
				set_error (scanner, "<validation>", json_error_numeric_overflow,
						  "Integer %" JSON_INTEGER_FORMAT " does not fit in an int", json_integer_value (root));
				return -1;
			}

			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				int *target = va_arg (*arguments, int *);
				if (root)
					*target = (int)json_integer_value (root);
			}

			return 0;

		case 'I':
			if (root && !json_is_integer (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected integer, got %s", type_name (root));
				return -1;
			}

			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				json_int_t *target = va_arg (*arguments, json_int_t *);
				if (root)
					*target = json_integer_value (root);
			}

			return 0;

		case 'b':
			if (root && !json_is_boolean (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected true or false, got %s", type_name (root));
				return -1;
			}

			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				int *target = va_arg (*arguments, int *);
				if (root)
					*target = json_is_true (root);
			}

			return 0;

		case 'f':
			if (root && !json_is_real (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected real, got %s", type_name (root));
				return -1;
			}

			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				double *target = va_arg (*arguments, double *);
				if (root)
					*target = json_real_value (root);
			}

			return 0;

		case 'F':
			if (root && !json_is_number (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected real or integer, got %s", type_name (root));
				return -1;
			}

			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				double *target = va_arg (*arguments, double *);
				if (root)
					*target = json_number_value (root);
			}

			return 0;

		case 'O':
			if (root && !(scanner->flags & JSON_VALIDATE_ONLY))
				json_incref (root);
			/* Fall through */

		case 'o':
			if (!(scanner->flags & JSON_VALIDATE_ONLY)) {
				json_t **target = va_arg (*arguments, json_t **);
				if (root)
					*target = root;
			}

			return 0;

		case 'n':
			/* Never assign, just validate */
			if (root && !json_is_null (root)) {
				set_error (scanner, "<validation>", json_error_wrong_type,
						  "Expected null, got %s", type_name (root));
				return -1;
			}
			return 0;

		default:
			set_error (scanner, "<format>", json_error_invalid_format,
					  "Unexpected format character '%c'", token (scanner));
			return -1;
	}
}

json_t *json_vpack_ex (
	json_error_t *error, size_t flags,
	const char *format, va_list arguments
) {

	scanner_t scanner = CERVER_ZERO_INITIALIZER;
	va_list arguments_copy;
	json_t *value = NULL;

	if (!format || !*format) {
		jsonp_error_init (error, "<format>");
		jsonp_error_set (error, -1, -1, 0, json_error_invalid_argument,
						"NULL or empty format string");
		return NULL;
	}
	jsonp_error_init (error, NULL);

	scanner_init (&scanner, error, flags, format);
	next_token (&scanner);

	va_copy (arguments_copy, arguments);
	value = pack (&scanner, &arguments_copy);
	va_end (arguments_copy);

	/* This will cover all situations where scanner.has_error is true */
	if (!value)
		return NULL;

	next_token (&scanner);
	if (token (&scanner)) {
		json_decref (value);
		set_error (&scanner, "<format>", json_error_invalid_format,
				  "Garbage after format string");
		return NULL;
	}

	return value;

}

json_t *json_pack_ex (
	json_error_t *error, size_t flags,
	const char *format, ...
) {

	va_list arguments;

	va_start (arguments, format);
	json_t * value = json_vpack_ex (error, flags, format, arguments);
	va_end (arguments);

	return value;

}

json_t *json_pack (const char *format, ...) {

	va_list arguments;

	va_start (arguments, format);
	json_t *value = json_vpack_ex (NULL, 0, format, arguments);
	va_end (arguments);

	return value;

}

int json_vunpack_ex (
	json_t *root, json_error_t *error, size_t flags,
	const char *format, va_list arguments
) {

	scanner_t scanner = CERVER_ZERO_INITIALIZER;
	va_list arguments_copy;

	if (!root) {
		jsonp_error_init (error, "<root>");
		jsonp_error_set (error, -1, -1, 0, json_error_null_value, "NULL root value");
		return -1;
	}

	if (!format || !*format) {
		jsonp_error_init (error, "<format>");
		jsonp_error_set (error, -1, -1, 0, json_error_invalid_argument,
						"NULL or empty format string");
		return -1;
	}
	jsonp_error_init (error, NULL);

	scanner_init (&scanner, error, flags, format);
	next_token (&scanner);

	va_copy (arguments_copy, arguments);
	if (unpack (&scanner, root, &arguments_copy)) {
		va_end (arguments_copy);
		return -1;
	}
	va_end (arguments_copy);

	next_token (&scanner);
	if (token (&scanner)) {
		set_error (&scanner, "<format>", json_error_invalid_format,
				  "Garbage after format string");
		return -1;
	}

	return 0;

}

int json_unpack_ex (
	json_t *root, json_error_t *error, size_t flags,
	const char *format, ...
) {

	va_list arguments;

	va_start (arguments, format);
	int result = json_vunpack_ex (root, error, flags, format, arguments);
	va_end (arguments);

	return result;
}

int json_unpack (json_t *root, const char *format, ...) {

	va_list arguments;

	va_start (arguments, format);
	int result = json_vunpack_ex (root, NULL, 0, format, arguments);
	va_end (arguments);

	return result;
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
