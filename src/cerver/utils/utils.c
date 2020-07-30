#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <stdarg.h>

#include "cerver/utils/utils.h"

/*** misc ***/

bool system_is_little_endian (void) {

	unsigned int x = 0x76543210;
	char *c = (char *) &x;
	if (*c == 0x10) return true;
	else return false;

}

/*** math ***/

int clamp_int (int val, int min, int max) {

	const int t = val < min ? min : val;
	return t > max ? max : t;

}

int abs_int (int value) { return value > 0 ? value : (value * -1); }

float lerp (float first, float second, float by) { return first * (1 - by) + second * by; }

/*** random ***/

// init psuedo random generator based on our seed
void random_set_seed (unsigned int seed) { srand (seed); }

int random_int_in_range (int min, int max) {

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

float random_float (float abs) {

	return ((float) rand () / (float)(RAND_MAX)) * abs;

}

/*** converters ***/

// convert a string representing a hex to a string
int xtoi (char *hexString) {

	int i = 0;

	if ((*hexString == '0') && (*(hexString + 1) == 'x')) hexString += 2;

	while (*hexString) {
		char c = toupper (*hexString++);
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

/*** c strings ***/

// copies a c string into another one previuosly allocated
void c_string_copy (char *to, const char *from) {

	if (to && from) {
		while (*from) *to++ = *from++;
		
		*to = '\0';
	}

}

// creates a new c string with the desired format, as in printf
char *c_string_create (const char *format, ...) {

	char *fmt;

	if (format != NULL) fmt = strdup (format);
	else fmt = strdup ("");

	va_list argp;
	va_start (argp, format);
	char oneChar[1];
	int len = vsnprintf (oneChar, 1, fmt, argp);
	if (len < 1) return NULL;
	va_end (argp);

	char *str = (char *) calloc (len + 1, sizeof (char));
	if (!str) return NULL;

	va_start (argp, format);
	vsnprintf (str, len + 1, fmt, argp);
	va_end (argp);

	free (fmt);

	return str;

}

// get how many tokens will be extracted by counting the number of apperances of the delim
unsigned int c_string_count_tokens (char *original, const char delim) {

	unsigned int count = 0;

	if (original) {
		char *temp = original;
		char *last = NULL;

		// count how many elements will be extracted
		while (*temp) {
			if (delim == *temp) {
				count++;
				last = temp;
			}

			temp++;
		}

		count += last < (original + strlen (original) - 1);

		// count++;
	}

	return count;

}

// splits a c string into tokens based on a delimiter
char **c_string_split (char *original, const char delim, unsigned int *n_tokens) {

	char **result = NULL;

	if (original) {
		char *string = (char *) calloc (strlen (original) + 1, sizeof (char));
		if (string) {
			c_string_copy (string, original);

			size_t count = 0;
			char *temp = string;
			char *last = NULL;
			char dlm[2];
			dlm[0] = delim;
			dlm[1] = 0;

			// count how many elements will be extracted
			while (*temp) {
				if (delim == *temp) {
					count++;
					last = temp;
				}

				temp++;
			}

			count += last < (string + strlen (string) - 1);

			count++;

			result = (char **) calloc (count, sizeof (char *));
			if (n_tokens) *n_tokens = count;

			if (result) {
				size_t idx = 0;
				char *token = strtok (string, dlm);

				while (token) {
					// assert (idx < count);
					*(result + idx++) = strdup (token);
					token = strtok (0, dlm);
				}

				// assert (idx == count - 1);
				*(result + idx) = 0;
			}

			free (string);
		}
	}

	return result;

}

// revers a c string
// returns a newly allocated c string
char *c_string_reverse (char *str) {

	char *reverse = NULL;

	if (str) {
		size_t len = strlen (str);
		reverse = (char *) calloc (len, sizeof (char));

		size_t end = len - 1;
		size_t begin = 0;
		for ( ; begin < len; begin++) {
			reverse[begin] = str[end];
			end--;
		}

		reverse[begin] = '\0';
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
char *c_string_remove_sub_after_token (char *str, const char token, char **sub) {

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
			memcpy (*sub, ptr, sub_len);
			// *sub[sub_len] = '\0';
		} 

		retval = (char *) calloc (diff_len + 1, sizeof (char));
		memcpy (retval, str, diff_len);
		// retval[diff_len] = '\0';
	}

	return retval;

}

// removes a substring from a c string after the idex of the token
// returns a newly allocated string without the sub,
// and option to retrieve the actual substring
// idx set to -1 for the last token match
// example: /home/ermiry/Documents, token: '/', idx: -1, returns: Documents
char *c_string_remove_sub_after_token_with_idx (char *str, const char token, char **sub, int idx) {

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
			memcpy (*sub, last_ptr, sub_len);
			// *sub[sub_len] = '\0';
		} 

		retval = (char *) calloc (diff_len + 1, sizeof (char));
		memcpy (retval, str, diff_len);
		// retval[diff_len] = '\0';
	}

	return retval;

}

// removes a substring from a c string delimited by two equal tokens
// takes the first and last appearance of the token
// example: test_20191118142101759__TEST__.png - token: '_'
// result: test.png
// returns a newly allocated string, and a option to get the substring
char *c_string_remove_sub_simetric_token (char *str, const char token, char **sub) {

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
char *c_string_remove_sub_range_token (char *str, const char token, unsigned int first, unsigned int last,
	char **sub) {

	char *retval = NULL;

	if (str) {
		if (first != last) {
			unsigned int first_token_count = 0;
			unsigned int last_token_count = 0;
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

// removes a substring from a c string delimited by two different tokens
// takes the first appearance of the first token, and the last appearance of the second one
// example: test_20191118142101759__TEST__.png - first token: '_' - last token: 'T'
// result: test__.png
char *c_string_remove_sub_different_token (char *str, const char token_one, const char token_two) {

	// TODO:

	return NULL;

}