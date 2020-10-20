#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "cerver/types/string.h"

static inline void char_copy (char *to, char *from) {

	while (*from) *to++ = *from++;
	*to = '\0';

}

String *str_new (const char *str) {

	String *s = (String *) malloc (sizeof (String));
	if (s) {
		if (str) {
			s->len = strlen (str);
			s->str = (char *) calloc (s->len + 1, sizeof (char));
			if (s->str) char_copy (s->str, (char *) str);
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

	String *s = NULL;

	if (format) {
		char *fmt = strdup (format);

		va_list argp;
		va_start (argp, format);
		char oneChar[1];
		int len = vsnprintf (oneChar, 1, fmt, argp);
		va_end (argp);

		char *str = (char *) calloc (len + 1, sizeof (char));

		va_start (argp, format);
		vsnprintf (str, len + 1, fmt, argp);
		va_end (argp);

		s = str_new (str);

		free (str);
		free (fmt);
	}

	return s;

}

int str_compare (const String *s1, const String *s2) {

	if (s1 && s2) return strcmp (s1->str, s2->str);
	else if (s1 && !s2) return -1;
	else if (!s1 && s2) return 1;
	return 0;

}

int str_comparator (const void *a, const void *b) {

	if (a && b) return strcmp (((String *) a)->str, ((String *) b)->str);
	else if (a && !b) return -1;
	else if (!a && b) return 1;
	return 0;

}

void str_copy (String *to, String *from) {

	if (to && from) {
		while (*from->str)
			*to->str++ = *from->str++;

		*to->str = '\0';
		to->len = from->len;
	}

}

void str_replace (String *old, const char *str) {

	if (old && str) {
		if (old->str) free (old->str);
		old->len = strlen (str);
		old->str = (char *) calloc (old->len + 1, sizeof (char));
		if (old->str) char_copy (old->str, (char *) str);
	}

}

String *str_concat (String *s1, String *s2) {

	String *des = NULL;

	if (s1 && s2) {
		des = str_new (NULL);
		des->str = (char *) calloc (s1->len + s2->len + 1, sizeof (char));

		while (*s1->str) *des->str++ = *s1->str++;
		while (*s2->str) *des->str++ = *s2->str++;

		*des->str = '\0';

		des->len = s1->len + s2->len;
	}

	return des;

}

// appends a char to the end of the string
// reallocates the same string
void str_append_char (String *s, const char c) {

	if (s) {
		unsigned int new_len = s->len + 1;

		s->str = (char *) realloc (s->str, new_len);
		if (s->str) {
			char *des = s->str + (s->len);
			*des = c;
			s->len = new_len;
		}
	}

}

// appends a c string at the end of the string
// reallocates the same string
void str_append_c_string (String *s, const char *c_str) {

	if (s && c_str) {
		unsigned int new_len = s->len + strlen (c_str);

		s->str = (char *) realloc (s->str, new_len);
		if (s->str) {
			char *des = s->str + (s->len);
			char_copy (des, (char *) c_str);
			s->len = new_len;
		}
	}

}

void str_to_upper (String *str) {

	if (str) for (unsigned int i = 0; i < str->len; i++) str->str[i] = toupper (str->str[i]);

}

void str_to_lower (String *str) {

	if (str) for (unsigned int i = 0; i < str->len; i++) str->str[i] = tolower (str->str[i]);

}

char **str_split (String *str, const char delim, int *n_tokens) {

	char **result = NULL;

	if (str) {
		if (str->len > 1) {
			char *string = strdup (str->str);
			if (string) {
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

				if (last) count += (last < temp);

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

void str_remove_char (String *str, char garbage) {

	char *src, *dst;
	for (src = dst = str->str; *src != '\0'; src++) {
		*dst = *src;
		if (*dst != garbage) dst++;
	}
	*dst = '\0';

}

// removes the last char from a string
void str_remove_last_char (String *s) {

	if (s) {
		if (s->len > 0) {
			unsigned int new_len = s->len - 1;

			s->str = (char *) realloc (s->str, s->len);
			if (s->str) {
				s->str[s->len - 1] = '\0';
				s->len = new_len;
			}
		}
	}

}

int str_contains (String *str, char *to_find) {

	unsigned int slen = str->len;
	unsigned int tFlen = strlen (to_find);
	unsigned int found = 0;

	if (slen >= tFlen) {
		for (unsigned int s = 0, t = 0; s < slen; s++) {
			do {
				if (str->str[s] == to_find[t] ) {
					if (++found == tFlen) return 0;
					s++;
					t++;
				}

				else { s -= found; found = 0; t = 0; }
			} while(found);
		}

		return 1;
	}

	else return -1;

}

/*** serialization ***/

// returns a ptr to a serialized str
void *str_selialize (String *str, SStringSize size) {

	void *retval = NULL;

	if (str) {
		switch (size) {
			case SS_SMALL: {
				SStringS *s_small = (SStringS *) malloc (sizeof (SStringS));
				if (s_small) {
					memset (s_small, 0, sizeof (SStringS));
					strncpy (s_small->str, str->str, 64);
					s_small->len = str->len > 64 ? 64 : str->len;
					retval = s_small;
				}
			} break;
			case SS_MEDIUM: {
				SStringM *s_medium = (SStringM *) malloc (sizeof (SStringM));
				if (s_medium) {
					memset (s_medium, 0, sizeof (SStringM));
					strncpy (s_medium->str, str->str, 128);
					s_medium->len = str->len > 128 ? 128 : str->len;
					retval = s_medium;
				}
			} break;
			case SS_LARGE: {
				SStringL *s_large = (SStringL *) malloc (sizeof (SStringL));
				if (s_large) {
					memset (s_large, 0, sizeof (SStringL));
					strncpy (s_large->str, str->str, 256);
					s_large->len = str->len > 256 ? 256 : str->len;
					retval = s_large;
				}
			} break;
			case SS_EXTRA_LARGE: {
				SStringXL *s_xlarge = (SStringXL *) malloc (sizeof (SStringXL));
				if (s_xlarge) {
					memset (s_xlarge, 0, sizeof (SStringXL));
					strncpy (s_xlarge->str, str->str, 512);
					s_xlarge->len = str->len > 512 ? 512 : str->len;
					retval = s_xlarge;
				}
			} break;

			default: break;
		}
	}

	return retval;

}