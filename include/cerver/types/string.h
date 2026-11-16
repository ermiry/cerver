/**
 * @file cerver/types/string.h
 * @brief A String: a heap allocated C string with its length.
 */

#ifndef _CERVER_STRING_H_
#define _CERVER_STRING_H_

#include <stdarg.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

/**
 * @defgroup types_string String
 * @ingroup types
 * @brief A C string that knows its length and grows geometrically when it is appended to.
 *
 * Every String keeps its str terminated with '\0'. Its max_len is the bytes allocated for str, set by
 * str_allocate (), str_reserve () and the appends; the other methods leave it at 0, which means that str has
 * exactly len + 1 bytes.
 * @{
 */

/** @brief The smallest capacity in bytes of a String that grows. */
#define STRING_MIN_CAPACITY				(1 << 4)

/** @brief A String that is appended beyond its capacity grows by this factor. */
#define STRING_GROWTH_FACTOR			2

/**
 * @brief A heap allocated C string with its length.
 */
typedef struct String {

	/** @brief The bytes allocated for str, including the '\0'; 0 if they are exactly len + 1. */
	size_t max_len;
	/** @brief The length of str, without the '\0'. */
	size_t len;
	/** @brief The characters, terminated with '\0'; NULL for an empty String created with str_new (NULL). */
	char *str;

} String;

/**
 * @brief Creates a String with a copy of a C string.
 *
 * @param[in] str the C string, NULL for an empty String without characters (str is NULL)
 * @return the new String, released with str_delete (); NULL if it could not be allocated
 * @warning If the characters can not be allocated, the String has its len but its str is NULL.
 */
CERVER_PUBLIC String *str_new (const char *str);

/**
 * @brief Releases a String and its characters.
 *
 * @param[in] str_ptr the String, it can be NULL
 */
CERVER_PUBLIC void str_delete (void *str_ptr);

/**
 * @brief Creates a String from a printf () format.
 *
 * @param[in] format the format
 * @param[in] ... the arguments of the format
 * @return the new String, released with str_delete (); NULL for a NULL format or if it could not be created
 */
CERVER_PUBLIC String *str_create (
	const char *format, ...
) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Creates an empty String with room for some bytes, to be filled with str_set ().
 *
 * @param[in] max_len the bytes allocated for the characters, including the '\0'
 * @return the new String, released with str_delete (); NULL if it could not be allocated
 * @warning If the characters can not be allocated, the String has its max_len but its str is NULL.
 */
CERVER_PUBLIC String *str_allocate (
	const unsigned int max_len
);

/**
 * @brief Writes a printf () format into a String.
 *
 * @param[in,out] str the String
 * @param[in] format the format
 * @param[in] ... the arguments of the format
 * @note A String with its max_len (str_allocate (), str_reserve ()) is written in its room, truncated to
 * max_len - 2 characters; the others (max_len is 0: str_new (), str_create ()) get a new buffer with the size of
 * the value, and keep the value they had if it could not be allocated.
 */
CERVER_PUBLIC void str_set (
	String *str, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Works as str_set () with a va_list.
 *
 * @param[in,out] str the String
 * @param[in] format the format
 * @param[in] args the arguments of the format
 */
CERVER_PUBLIC void str_set_with_args (
	String *str, const char *format, va_list args
) CERVER_PRINTF_FORMAT (2, 0);

/**
 * @brief Compares two Strings with strcmp ().
 *
 * @param[in] s1 the first String
 * @param[in] s2 the second String
 * @return like strcmp (); a NULL String, or a String with NULL characters, goes after the other one, and two NULL
 * Strings are equal
 */
CERVER_PUBLIC int str_compare (
	const String *s1, const String *s2
);

/**
 * @brief Works as str_compare (), as a #Comparator of two Strings.
 *
 * @param[in] a the first String
 * @param[in] b the second String
 * @return like str_compare ()
 */
CERVER_PUBLIC int str_comparator (
	const void *a, const void *b
);

/**
 * @brief Replaces the characters of a String with a copy of the ones of another.
 *
 * @param[in,out] to the String that is replaced, its max_len goes back to 0
 * @param[in] from the String that is copied, NULL (or with NULL characters) keeps to as it is
 * @note If the copy can not be allocated, to keeps its characters.
 */
CERVER_PUBLIC void str_copy (
	String *to, const String *from
);

/**
 * @brief Replaces the characters of a String with a copy of the first n of another.
 *
 * @param[in,out] to the String that is replaced, its max_len goes back to 0
 * @param[in] from the String that is copied, NULL (or with NULL characters) keeps to as it is
 * @param[in] n the characters to copy; when from is shorter, the len of to is the one of what was copied
 * @note If the copy can not be allocated, to keeps its characters.
 */
CERVER_PUBLIC void str_n_copy (
	String *to, const String *from, const size_t n
);

/**
 * @brief Creates a copy of a String.
 *
 * @param[in] original the String
 * @return the new String, released with str_delete (); NULL for a NULL String or if it could not be allocated
 */
CERVER_PUBLIC String *str_clone (
	const String *original
);

/**
 * @brief Replaces the characters of a String with a printf () format.
 *
 * @param[in,out] str the String, its max_len goes back to 0
 * @param[in] format the format
 * @param[in] ... the arguments of the format
 * @note If the format can not be printed, the String is left empty (str is NULL and len is 0).
 */
CERVER_PUBLIC void str_replace (
	String *str, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Replaces the characters of a String with a copy of a C string.
 *
 * @param[in,out] str the String, its max_len goes back to 0
 * @param[in] c_str the C string, NULL keeps the String as it is
 * @note If the copy can not be allocated, the String keeps its characters.
 */
CERVER_PUBLIC void str_replace_with (
	String *str, const char *c_str
);

/**
 * @brief Replaces the characters of a String with a copy of the first n characters of a C string.
 *
 * @param[in,out] str the String, its max_len goes back to 0
 * @param[in] c_str the C string, NULL keeps the String as it is
 * @param[in] n the characters to copy; when c_str is shorter, the len of the String is the one of what was copied
 * @note If the copy can not be allocated, the String keeps its characters.
 */
CERVER_PUBLIC void str_n_replace_with (
	String *str, const char *c_str, const size_t n
);

/**
 * @brief Concatenates two Strings into a new one.
 *
 * @param[in] s1 the first String
 * @param[in] s2 the second String
 * @return the new String, released with str_delete (); NULL if a String is NULL or it could not be created
 */
CERVER_PUBLIC String *str_concat (
	const String *s1, const String *s2
);

/**
 * @brief Appends a character to the end of a String, which grows when it is full.
 *
 * @param[in,out] str the String, NULL is ignored
 * @param[in] c the character
 * @note If the String can not grow, it is kept as it was.
 */
CERVER_PUBLIC void str_append_char (
	String *str, const char c
);

/**
 * @brief Appends a C string to the end of a String, which grows when it is full.
 *
 * @param[in,out] str the String, NULL is ignored
 * @param[in] c_str the C string, NULL is ignored
 * @note If the String can not grow, it is kept as it was.
 */
CERVER_PUBLIC void str_append_c_string (
	String *str, const char *c_str
);

/**
 * @brief Appends exactly n bytes of a C string to the end of a String, which grows when it is full.
 *
 * @param[in,out] str the String, NULL is ignored
 * @param[in] c_str the bytes, NULL is ignored; they must have at least n bytes, they are copied as they are
 * @param[in] n the bytes to append
 * @note If the String can not grow, it is kept as it was.
 */
CERVER_PUBLIC void str_append_n_from_c_string (
	String *str, const char *c_str, const size_t n
);

/**
 * @brief Creates a String with exactly n bytes copied from a buffer.
 *
 * @param[in] bytes the bytes, they may contain '\0'; NULL only when n is 0
 * @param[in] n the bytes to copy
 * @return the new String, terminated with '\0' and released with str_delete (); NULL if it could not be allocated
 * @note If the characters can not be allocated, the String is empty (str is NULL and len is 0).
 */
CERVER_PUBLIC String *str_create_from_bytes (
	const char *bytes, const size_t n
);

/**
 * @brief Works as str_create_from_bytes (), but the String and its bytes are a single allocation of an arena.
 *
 * @param[in,out] arena the arena
 * @param[in] bytes the bytes, they may contain '\0'; NULL only when n is 0
 * @param[in] n the bytes to copy
 * @return the new String, released with the arena; NULL if the arena could not allocate it
 * @warning The String must NOT be passed to str_delete () nor grown with any of the append methods.
 */
CERVER_PUBLIC String *str_create_from_bytes_arena (
	struct Arena *arena, const char *bytes, const size_t n
);

/**
 * @brief Makes sure that a String has at least some bytes allocated, so the next appends that fit do not
 * reallocate it; it never shrinks the String.
 *
 * @param[in,out] str the String
 * @param[in] capacity the bytes, including the '\0'
 * @retval 0 the String has the room
 * @retval 1 a NULL String or it could not grow; the String is kept as it was
 */
CERVER_PUBLIC int str_reserve (
	String *str, const size_t capacity
);

/**
 * @brief Appends exactly n bytes at the end of a String, which grows geometrically, so many appends are not
 * quadratic.
 *
 * @param[in,out] str the String
 * @param[in] bytes the bytes, they may contain '\0'; NULL only when n is 0
 * @param[in] n the bytes to append
 * @retval 0 the bytes were appended, and the String is terminated with '\0'
 * @retval 1 a NULL String, NULL bytes with n bigger than 0, a length that does not fit or it could not grow; the
 * String is kept as it was
 */
CERVER_PUBLIC int str_append_bytes (
	String *str, const char *bytes, const size_t n
);

/**
 * @brief Converts the characters of a String to upper case with toupper ().
 *
 * @param[in,out] string the String, NULL is ignored
 */
CERVER_PUBLIC void str_to_upper (String *string);

/**
 * @brief Converts the characters of a String to lower case with tolower ().
 *
 * @param[in,out] string the String, NULL is ignored
 */
CERVER_PUBLIC void str_to_lower (String *string);

/**
 * @brief Splits a String in the tokens between a delimiter; the empty tokens are skipped.
 *
 * @param[in] string the String, it is not changed
 * @param[in] delim the delimiter
 * @param[out] n_tokens the number of tokens, it can be NULL
 * @return a new array of n_tokens C strings, each one and the array released with free (); a String without the
 * delimiter is its only token; NULL for a NULL String, one of less than 2 characters, of only delimiters, or if it
 * could not be allocated
 */
CERVER_PUBLIC char **str_split (
	String *string, const char delim, int *n_tokens
);

/**
 * @brief Removes every occurrence of a character from a String.
 *
 * @param[in,out] string the String, NULL (or with NULL characters) is ignored
 * @param[in] garbage the character to remove
 */
CERVER_PUBLIC void str_remove_char (
	String *string, const char garbage
);

/**
 * @brief Removes the last character of a String and shrinks it.
 *
 * @param[in,out] string the String, NULL or an empty one is ignored; its max_len goes back to 0
 * @note If the shrinking reallocation fails, the String is not changed.
 */
CERVER_PUBLIC void str_remove_last_char (
	String *string
);

/**
 * @brief Checks if a C string is inside a String.
 *
 * @param[in] str the String
 * @param[in] to_find the C string to find
 * @retval 0 to_find is inside the String
 * @retval 1 the String is at least as long as to_find but it does not contain it (an empty to_find is never found)
 * @retval -1 the String is shorter than to_find, or an argument is NULL
 */
CERVER_PUBLIC int str_contains (
	const String *str, const char *to_find
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
