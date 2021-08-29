#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <ctype.h>
#include <math.h>

#include "cerver/utils/utils.h"

/*** misc ***/

bool system_is_little_endian (void) {

	unsigned int x = 0x76543210;
	char *c = (char *) &x;
	if (*c == 0x10) return true;
	else return false;

}

/*** math ***/

int clamp_int (
	const int val, const int min, const int max
) {

	const int t = val < min ? min : val;
	return t > max ? max : t;

}

int abs_int (const int value) {
	
	return value > 0 ? value : (value * -1);
	
}

float lerp (
	const float first, const float second, const float by
) {
	
	return first * (1 - by) + second * by;
	
}

bool float_compare (const float f1, const float f2) {

	return fabs (f1 - f2) < 0.00001;

}

/*** random ***/

// init psuedo random generator based on our seed
void random_set_seed (const unsigned int seed) {

	srand (seed);

}

int random_int_in_range (const int min, const int max) {

	int low = 0, high = 0;

	if (min < max) {
		low = min;
		high = max + 1;
	}

	else {
		low = max + 1;
		high = min;
	}

	return (rand () % (high - low)) + low;

}

float random_float (const float abs) {

	return ((float) rand () / (float)(RAND_MAX)) * abs;

}

/*** converters ***/

// convert a string representing a hex to a string
int xtoi (char *hex_string) {

	int i = 0;

	if ((*hex_string == '0') && (*(hex_string + 1) == 'x')) hex_string += 2;

	while (*hex_string) {
		char c = toupper (*hex_string++);
		if ((c < '0') || (c > 'F') || ((c > '9') && (c < 'A'))) break;
		c -= '0';
		if (c > 9) c-= 7;
		i = (i << 4) + c;
	}

	return i;

}

char *itoa (int i, char *b) {

	char const digit[] = "0123456789";
	char *p = b;

	if (i < 0) {
		*p++ = '-';
		i *= -1;
	}

	int shifter = i;
	do { //Move to where representation ends
		++p;
		shifter = shifter / 10;
	} while (shifter);

	*p = '\0';
	do { //Move back, inserting digits as u go
		*--p = digit [i % 10];
		i = i / 10;
	} while (i);

	return b;

}

bool bool_value_from_string (const char *string_value) {

	if (!strcasecmp ("true", string_value)) return true;
	return false;

}

/*** c strings ***/

// copies a c string into another one previuosly allocated
void c_string_copy (char *to, const char *from) {

	if (to && from) {
		while (*from) *to++ = *from++;

		*to = '\0';
	}

}

// copies n bytes from a c string into another one previuosly allocated
void c_string_n_copy (char *to, const char *from, size_t n) {

	if (to && from) {
		while (*from && n) {
			*to++ = *from++;
			n--;
		}

		if (!n) *(--to) = '\0';
		else *to = '\0';
	}

}

static inline void c_string_concat_actual (
	char *s1, char *s2, char *des
) {

	while (*s1) *des++ = *s1++;
	while (*s2) *des++ = *s2++;

	*des = '\0';

}

// concats two c strings into a newly allocated buffer of len s1 + s2
// returns a newly allocated buffer on success, NULL on any error
char *c_string_concat (
	const char *s1, const char *s2, size_t *des_size
) {

	char *retval = NULL;

	if (s1 && s2) {
		size_t len = strlen (s1) + strlen (s2);
		retval = (char *) calloc (len + 1, sizeof (char));
		if (retval) {
			c_string_concat_actual (
				(char *) s1, (char *) s2, retval
			);

			*des_size = len;
		}
	}

	return retval;

}

// concats two strings into the same buffer
// wont perform operation if result would overflow buffer
// returns the len of the final string
size_t c_string_concat_safe (
	const char *s1, const char *s2,
	const char *des, size_t des_size
) {

	size_t retval = 0;

	if (s1 && s2 && des) {
		if ((strlen (s1) + strlen (s2)) < des_size) {
			c_string_concat_actual (
				(char *) s1, (char *) s2, (char *) des
			);

			retval = strlen (des);
		}
	}

	return retval;

}

// creates a new c string with the desired format, as in printf
char *c_string_create (const char *format, ...) {

	char *result = NULL;

	if (format) {
		va_list args;
		va_start (args, format);

		vasprintf (&result, format, args);

		va_end (args);
	}

	return result;

}

// removes all the spaces in the c string
void c_string_remove_spaces (char *s) {

	const char *d = s;
	do {
		while (*d == ' ') {
			++d;
		}
	} while ((*s++ = *d++));

}

// removes any CRLF characters in a string
void c_string_remove_line_breaks (char *s) {

	const char *d = s;
	do {
		while (*d == '\r' || *d == '\n') {
			++d;
		}
	} while ((*s++ = *d++));

}

// removes all spaces and CRLF in the c string
void c_string_remove_spaces_and_line_breaks (char *s) {

	const char *d = s;
	do {
		while (*d == ' ' || *d == '\r' || *d == '\n') {
			++d;
		}
	} while ((*s++ = *d++));

}

// get how many tokens will be extracted by counting the number of apperances of the delim
// the original string won't be affected
size_t c_string_count_tokens (
	const char *original, const char delim
) {

	size_t count = 0;

	if (original) {
		char *temp = (char *) original;
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
		if (original[0] == delim && count) count--;

		if (last) count += (last < temp);
	}

	return count;

}

// splits a c string into tokens based on a delimiter
// the original string won't be affected
// this method is thread safe as it uses __strtok_r () instead of the regular strtok ()
char **c_string_split (
	const char *original, const char delim, size_t *n_tokens
) {

	char **result = NULL;

	if (original) {
		if (strlen (original) > 1) {
			char *string = strdup (original);
			if (string) {
				size_t count = c_string_count_tokens (original, delim);
				if (count) {
					result = (char **) calloc (count, sizeof (char *));
					if (result) {
						if (n_tokens) *n_tokens = count;

						size_t idx = 0;

						char dlm[2];
						dlm[0] = delim;
						dlm[1] = '\0';

						char *token = NULL;
						char *rest = string;
						while ((token = __strtok_r (rest, dlm, &rest))) {
							result[idx] = strdup (token);
							idx++;
						}
					}
				}

				free (string);
			}
		}
	}

	return result;

}

// revers a c string
// returns a newly allocated c string
char *c_string_reverse (const char *str) {

	char *reverse = NULL;

	if (str) {
		size_t len = strlen (str);
		reverse = (char *) calloc (len + 1, sizeof (char));
		if (reverse) {
			size_t end = len - 1;
			size_t begin = 0;
			for (; begin < len; begin++) {
				reverse[begin] = str[end];
				end--;
			}

			reverse[begin] = '\0';
		}
	}

	return reverse;

}

// removes all ocurrances of a char from a string
void c_string_remove_char (char *string, char garbage) {

	char *src, *dst;
	for (src = dst = string; *src != '\0'; src++) {
		*dst = *src;
		if (*dst != garbage) dst++;
	}

	*dst = '\0';

}

// removes the exact sub string from the main one
// returns a newly allocated copy of the original str but withput the sub
char *c_string_remove_sub (char *str, const char *sub) {

	char *retval = NULL;

	if (str && sub) {
		char *start_sub = strstr (str, sub);
		if (start_sub) {
			ptrdiff_t len_str = strlen (str);
			size_t len_sub = strlen (sub);

			ptrdiff_t diff = start_sub - str;
			size_t new_len = len_str - len_sub;
			retval = (char *) calloc (new_len + 1, sizeof (char));
			if (retval) {
				char *ptr = retval;
				ptrdiff_t idx = 0;

				// copy the first part of the string
				while (idx < diff) {
					*ptr = str[idx];
					ptr++;
					idx++;
				}

				idx += len_sub;
				// char *end_sub = str + len_sub;
				while (idx < len_str) {
					*ptr = str[idx];
					ptr++;
					idx++;
				}

				*ptr = '\0';
			}
		}
	}

	return retval;

}

// removes any white space from the string
char *c_string_trim (char *str) {

	while (isspace (*str)) str++;

	if (*str == 0) return str;

	char *end = str + strlen (str) - 1;
	while (end > str && isspace (*end)) end--;

	*(end + 1) = 0;

	return str;

}

static inline bool is_quote (char c) { return (c == '"' || c == '\''); }

// removes quotes from string
char *c_string_strip_quotes (char *str) {

	while (is_quote (*str)) str++;

	if (*str == 0) return str;

	char *end = str + strlen(str) - 1;
	while (end > str && is_quote (*end)) end--;

	*(end + 1) = 0;

	return str;

}

// returns true if the string starts with the selected sub string
bool c_string_starts_with (const char *str, const char *substr) {

	return (str && substr) ?
		strncmp (str, substr, strlen (substr)) == 0 : false;

}

// find a substring in another string with a max length
char *c_string_find_sub_in_len (
	const char *haystack, const char *needle, size_t len
) {

	int i = 0;
	size_t needle_len = 0;

	if (0 == (needle_len = strnlen (needle, len)))
			return (char *) haystack;

	for (i = 0; i <= (int) (len - needle_len); i++) {
		if (
			(haystack[0] == needle[0]) &&
			(0 == strncmp(haystack, needle, needle_len))
		)
			return (char *) haystack;

		haystack++;
	}

	return NULL;

}

// creates a newly allocated string using the data between the two pointers of the SAME string
// returns a new string, NULL on error
char *c_string_create_with_ptrs (char *first, char *last) {

	char *retval = NULL;

	if (first && last) {
		ptrdiff_t diff = last - first;
		diff += 1;
		retval = (char *) calloc (diff, sizeof (char));

		if (retval) {
			char *ptr = first;
			ptrdiff_t count = 0;
			while (count < diff) {
				retval[count] = *ptr;
				ptr++;
				count++;
			}

			retval[diff] = '\0';
		}
	}

	return retval;

}

// removes a substring from a c string that is defined after a token
// returns a newly allocated string without the sub,
// and option to retrieve the actual substring
char *c_string_remove_sub_after_token (
	char *str, const char token, char **sub
) {

	char *retval = NULL;

	if (str) {
		char *ptr = str;
		while (*ptr) {
			if (token == *ptr) {
				break;
			}

			ptr++;
		}

		size_t str_len = strlen (str);
		size_t sub_len = strlen (ptr);
		size_t diff_len = str_len - sub_len;

		if (sub) {
			*sub = (char *) calloc (sub_len + 1, sizeof (char));
			(void) memcpy (*sub, ptr, sub_len);
			// *sub[sub_len] = '\0';
		}

		retval = (char *) calloc (diff_len + 1, sizeof (char));
		(void) memcpy (retval, str, diff_len);
		// retval[diff_len] = '\0';
	}

	return retval;

}

// removes a substring from a c string after the idex of the token
// returns a newly allocated string without the sub,
// and option to retrieve the actual substring
// idx set to -1 for the last token match
// example: /home/ermiry/Documents, token: '/', idx: -1, returns: Documents
char *c_string_remove_sub_after_token_with_idx (
	char *str, const char token, char **sub, int idx
) {

	char *retval = NULL;

	if (str) {
		// int count = 0;
		char *ptr = str;
		char *last_ptr = NULL;
		int last_token_count = 0;
		while (*ptr) {
			if (token == *ptr) {
				last_token_count++;

				if (idx < 0) last_ptr = ptr;
				else if (last_token_count == idx) last_ptr = ptr;
			}

			ptr++;
		}

		last_ptr++;

		size_t str_len = strlen (str);
		size_t sub_len = strlen (last_ptr);
		size_t diff_len = str_len - sub_len;

		if (sub) {
			*sub = (char *) calloc (sub_len + 1, sizeof (char));
			(void) memcpy (*sub, last_ptr, sub_len);
			// *sub[sub_len] = '\0';
		}

		retval = (char *) calloc (diff_len + 1, sizeof (char));
		(void) memcpy (retval, str, diff_len);
		// retval[diff_len] = '\0';
	}

	return retval;

}

// removes a substring from a c string delimited by two equal tokens
// takes the first and last appearance of the token
// example: test_20191118142101759__TEST__.png - token: '_'
// result: test.png
// returns a newly allocated string, and a option to get the substring
char *c_string_remove_sub_simetric_token (
	char *str, const char token, char **sub
) {

	char *retval = NULL;

	if (str) {
		char *ptr = str;
		char *first = NULL;
		char *last = NULL;
		while (*ptr) {
			if (token == *ptr) {
				if (!first) first = ptr;
				last = ptr;
			}

			ptr++;
		}

		bool out = true;
		char *sub_ptr = NULL;
		if (sub) {
			*sub = c_string_create_with_ptrs (first, last);
			sub_ptr = *sub;
		}

		else {
			sub_ptr = c_string_create_with_ptrs (first, last);
			out = false;
		}

		// get the substring between the two tokens
		retval = c_string_remove_sub (str, sub_ptr);

		if (!out) free (sub_ptr);
	}

	return retval;

}

// removes a substring from a c string delimitied by two equal tokens
// and you can select the idx of the token; use -1 for last token
// example: test_20191118142101759__TEST__.png - token: '_' - idx (first: 1,  last: 3)
// result: testTEST__.png
// returns a newly allocated string, and a option to get the substring
char *c_string_remove_sub_range_token (
	char *str,
	const char token, int first, int last,
	char **sub
) {

	char *retval = NULL;

	if (str) {
		if (first != last) {
			int first_token_count = 0;
			int last_token_count = 0;
			char *ptr = str;
			char *first_ptr = NULL;
			char *last_ptr = NULL;
			while (*ptr) {
				if (token == *ptr) {
					first_token_count++;
					last_token_count++;

					if (first_token_count == first) first_ptr = ptr;

					if (last < 0) last_ptr = ptr;
					else if (last_token_count == last) last_ptr = ptr;
				}

				ptr++;
			}

			bool out = true;
			char *sub_ptr = NULL;
			if (sub) {
				*sub = c_string_create_with_ptrs (first_ptr, last_ptr);
				sub_ptr = *sub;
			}

			else {
				sub_ptr = c_string_create_with_ptrs (first_ptr, last_ptr);
				out = false;
			}

			// get the substring between the two tokens
			retval = c_string_remove_sub (str, sub_ptr);

			if (!out) free (sub_ptr);
		}
	}

	return retval;

}
