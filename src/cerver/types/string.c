#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/string.h"

#include "cerver/collections/arena.h"

static inline void char_copy (char *to, char *from) {

	while (*from) *to++ = *from++;
	*to = '\0';

}

// a String whose characters could not be allocated is released: every method uses them without a check
static String *str_without_characters (String *s) {

	free (s);

	return NULL;

}

String *str_new (const char *str) {

	String *s = (String *) malloc (sizeof (String));
	if (s) {
		s->max_len = 0;

		if (str) {
			s->len = strlen (str);
			s->str = (char *) calloc (s->len + 1, sizeof (char));
			if (s->str) char_copy (s->str, (char *) str);
			else s = str_without_characters (s);
		}

		else {
			s->len = 0;
			s->str = NULL;
		}
	}

	return s;

}

void str_delete (void *str_ptr) {

	if (str_ptr) {
		String *str = (String *) str_ptr;

		if (str->str) free (str->str);
		free (str);
	}

}

String *str_create (const char *format, ...) {

	String *str = NULL;

	if (format) {
		va_list args;
		va_start (args, format);

		str = (String *) malloc (sizeof (String));
		if (str) {
			// the string is undefined when vasprintf () fails
			if (vasprintf (&str->str, format, args) < 0) {
				free (str);
				str = NULL;
			}

			else {
				str->len = strlen (str->str);
				str->max_len = 0;
			}
		}

		va_end (args);
	}

	return str;

}

String *str_allocate (const unsigned int max_len) {

	String *s = (String *) malloc (sizeof (String));
	if (s) {
		s->max_len = max_len;
		s->len = 0;
		s->str = (char *) calloc (max_len, sizeof (char));

		// calloc () can return NULL for a size of 0
		if (!s->str && max_len) s = str_without_characters (s);
	}

	return s;

}

static inline void str_set_in_room (
	String *str, const char *format, va_list args
) CERVER_PRINTF_FORMAT (2, 0);

static inline void str_set_growing (
	String *str, const char *format, va_list args
) CERVER_PRINTF_FORMAT (2, 0);

// a string with its max len (str_allocate (), str_reserve ()) is written in its room, cut to fit
static inline void str_set_in_room (String *str, const char *format, va_list args) {

	(void) vsnprintf (
		str->str, str->max_len - 1,
		format, args
	);

	str->len = strlen (str->str);

}

// the others (str_new (), str_create ()) have exactly len + 1 bytes and a max len of 0,
// so they get a new buffer with the size of the value; without memory they keep the value they had
static inline void str_set_growing (String *str, const char *format, va_list args) {

	char *value = NULL;
	if (vasprintf (&value, format, args) >= 0) {
		free (str->str);
		str->str = value;
		str->len = strlen (value);
	}

}

void str_set_with_args (
	String *str, const char *format, va_list args
) {

	if (str && format) {
		if (str->str && str->max_len) str_set_in_room (str, format, args);
		else str_set_growing (str, format, args);
	}

}

void str_set (String *str, const char *format, ...) {

	if (str && format) {
		va_list args;
		va_start (args, format);

		str_set_with_args (str, format, args);

		va_end (args);
	}

}

// a NULL goes after a value, like a NULL String
static int str_compare_values (const char *one, const char *two) {

	int result = 0;
	if (one && two) result = strcmp (one, two);
	else if (one) result = -1;
	else if (two) result = 1;

	return result;

}

int str_compare (const String *s1, const String *s2) {

	if (s1 && s2) return str_compare_values (s1->str, s2->str);
	else if (s1 && !s2) return -1;
	else if (!s1 && s2) return 1;
	return 0;

}

int str_comparator (const void *a, const void *b) {

	return str_compare ((const String *) a, (const String *) b);

}

// the string takes a copy of the first n characters of the value (fewer when it is shorter),
// without memory it keeps the value that it had
static void str_take_copy (String *to, const char *value, const size_t n) {

	char *copy = (char *) calloc (n + 1, sizeof (char));
	if (copy) {
		(void) strncpy (copy, value, n);

		free (to->str);
		to->str = copy;
		to->len = strlen (copy);
		to->max_len = 0;
	}

}

void str_copy (String *to, const String *from) {

	if (to && from && from->str) {
		str_take_copy (to, from->str, from->len);
	}

}

void str_n_copy (String *to, const String *from, const size_t n) {

	if (to && from && from->str) {
		str_take_copy (to, from->str, n);
	}

}

String *str_clone (const String *original) {

	return original ? str_new (original->str) : NULL;

}

void str_replace (String *str, const char *format, ...) {

	if (str && format) {
		va_list args;
		va_start (args, format);

		if (str->str) free (str->str);
		str->max_len = 0;

		// the string is undefined when vasprintf () fails
		if (vasprintf (&str->str, format, args) < 0) {
			str->str = NULL;
			str->len = 0;
		}

		else {
			str->len = strlen (str->str);
		}

		va_end (args);
	}

}

void str_replace_with (String *str, const char *c_str) {

	if (str && c_str) {
		str_take_copy (str, c_str, strlen (c_str));
	}

}

void str_n_replace_with (
	String *str, const char *c_str, const size_t n
) {

	if (str && c_str) {
		str_take_copy (str, c_str, n);
	}

}

String *str_concat (const String *s1, const String *s2) {

	return (s1 && s2) ? str_create ("%s%s", s1->str, s2->str) : NULL;

}

// appends a char to the end of the string
// reallocates the same string
void str_append_char (String *str, const char c) {

	if (str) {
		str_append_bytes (str, &c, 1);
	}

}

// appends a c string at the end of the string
// reallocates the same string
void str_append_c_string (String *str, const char *c_str) {

	if (str && c_str) {
		str_append_bytes (str, c_str, strlen (c_str));
	}

}

// appends the first n chars of c_str at the end of the string
// reallocates the same string
void str_append_n_from_c_string (
	String *str, const char *c_str, const size_t n
) {

	if (str && c_str) {
		str_append_bytes (str, c_str, n);
	}

}

// creates a new string with exactly n bytes copied from bytes,
// bytes may contain '\0' and the result is always terminated with '\0'
String *str_create_from_bytes (const char *bytes, const size_t n) {

	String *str = str_new (NULL);
	if (str) {
		str->str = (char *) malloc (n + 1);
		if (str->str) {
			if (n) (void) memcpy (str->str, bytes, n);
			str->str[n] = '\0';
			str->len = n;
		}

		else {
			str = str_without_characters (str);
		}
	}

	return str;

}

// works as str_create_from_bytes () but everything is allocated in the arena
String *str_create_from_bytes_arena (
	struct Arena *arena, const char *bytes, const size_t n
) {

	String *str = (String *) arena_alloc (arena, sizeof (String) + n + 1);
	if (str) {
		str->max_len = 0;
		str->len = n;
		str->str = ((char *) str) + sizeof (String);

		if (n) (void) memcpy (str->str, bytes, n);
		str->str[n] = '\0';
	}

	return str;

}

// the bytes allocated for the string, including the '\0'
static size_t str_capacity (const String *str) {

	if (!str->str) return 0;

	return (str->max_len > str->len) ? str->max_len : str->len + 1;

}

// the next capacity grows geometrically and always fits needed bytes
static size_t str_next_capacity (const size_t capacity, const size_t needed) {

	size_t next = (capacity > (SIZE_MAX / STRING_GROWTH_FACTOR)) ?
		needed : capacity * STRING_GROWTH_FACTOR;

	if (next < STRING_MIN_CAPACITY) next = STRING_MIN_CAPACITY;

	return (next < needed) ? needed : next;

}

// reallocates the string to exactly capacity bytes, bigger than its len
// returns 0 on success, 1 on error (the string is kept unchanged)
static int str_resize (String *str, const size_t capacity) {

	char *new_str = (char *) realloc (str->str, capacity);
	if (!new_str) return 1;

	new_str[str->len] = '\0';

	str->str = new_str;
	str->max_len = capacity;

	return 0;

}

// makes sure that the string has at least capacity bytes allocated
// (including the '\0'), so the next appends that fit do not reallocate it
// it never shrinks the string
// returns 0 on success, 1 on error (the string is kept unchanged)
int str_reserve (String *str, const size_t capacity) {

	if (!str) return 1;

	if (capacity <= str_capacity (str)) return 0;

	return str_resize (str, capacity);

}

// appends exactly n bytes at the end of the string,
// bytes may contain '\0' and the result is always terminated with '\0'
// the string grows geometrically, so many appends are not quadratic
// returns 0 on success, 1 on error (the string is kept unchanged)
int str_append_bytes (String *str, const char *bytes, const size_t n) {

	if (!str || (!bytes && n)) return 1;

	if (n > (SIZE_MAX - str->len - 1)) return 1;

	const size_t needed = str->len + n + 1;
	const size_t capacity = str_capacity (str);

	if (
		(needed > capacity)
		&& str_resize (str, str_next_capacity (capacity, needed))
	) {
		return 1;
	}

	if (n) (void) memcpy (str->str + str->len, bytes, n);
	str->len += n;
	str->str[str->len] = '\0';

	return 0;

}

void str_to_upper (String *str) {

	if (str) {
		for (size_t i = 0; i < str->len; i++) {
			str->str[i] = (char) toupper ((unsigned char) str->str[i]);
		}
	}

}

void str_to_lower (String *str) {

	if (str) {
		for (size_t i = 0; i < str->len; i++) {
			str->str[i] = (char) tolower ((unsigned char) str->str[i]);
		}
	}

}

static char **str_split_internal (
	String *str, const char delim, int *n_tokens, char *string
) {

	char **result = NULL;

	size_t count = 0;

	char *temp = (char *) str->str;
	char prev = '\0';
	char *last = NULL;

	while (*temp) {
		if (delim == *temp) {
			if (prev != delim) count++;
			last = temp;
		}

		prev = *temp;
		temp++;
	}

	// don't count if the delim is the last char of the string
	if (prev == delim) count--;

	// check if we have info between delims
	if (str->str[0] == delim && count) count--;

	// without the delimiter the whole string is its only token
	if (last) count += (last < temp);
	else count = 1;

	if (count) {
		result = (char **) calloc (count, sizeof (char *));
		if (result) {
			size_t idx = 0;

			char dlm[2];
			dlm[0] = delim;
			dlm[1] = '\0';

			char *token = NULL;
			char *rest = string;
			while ((idx < count) && (token = __strtok_r (rest, dlm, &rest))) {
				result[idx] = strdup (token);
				idx++;
			}

			// a string of only delimiters is counted as one token but has none
			if (n_tokens) *n_tokens = (int) idx;

			if (!idx) {
				free (result);
				result = NULL;
			}
		}
	}

	return result;

}

char **str_split (
	String *str, const char delim, int *n_tokens
) {

	char **result = NULL;

	if (str) {
		if (str->len > 1) {
			char *string = strdup (str->str);
			if (string) {
				result = str_split_internal (
					str, delim, n_tokens, string
				);

				free (string);
			}
		}
	}

	return result;

}

void str_remove_char (String *str, const char garbage) {

	if (str && str->str) {
		char *src, *dst;
		for (src = dst = str->str; *src != '\0'; src++) {
			*dst = *src;
			if (*dst != garbage) dst++;
		}

		*dst = '\0';

		str->len = strlen (str->str);
	}

}

// removes the last char from a string
void str_remove_last_char (String *s) {

	if (s) {
		if (s->len > 0) {
			const size_t new_len = s->len;

			// a realloc () that fails keeps the buffer that it got, so the string is not lost
			char *shorter = (char *) realloc (s->str, s->len);
			if (shorter) {
				s->str = shorter;
				s->str[s->len - 1] = '\0';
				s->len = new_len - 1;
				s->max_len = 0;
			}
		}
	}

}

int str_contains (const String *str, const char *to_find) {

	if (!str || !to_find) return -1;

	const size_t slen = str->len;
	const size_t to_find_len = strlen (to_find);
	size_t found = 0;

	if (slen >= to_find_len) {
		for (size_t s = 0, t = 0; s < slen; s++) {
			do {
				if (str->str[s] == to_find[t] ) {
					if (++found == to_find_len) return 0;
					s++;
					t++;
				}

				else { s -= found; found = 0; t = 0; }
			} while (found);
		}

		return 1;
	}

	return -1;

}
