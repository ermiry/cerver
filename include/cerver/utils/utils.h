/**
 * @file cerver/utils/utils.h
 * @brief The helpers of cerver: the endianness, the math, the random values, the converters and the c strings.
 */

#ifndef _CERVER_UTILS_H_
#define _CERVER_UTILS_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup utils Utils
 * @brief The helpers that the parts of cerver share: the log, the histograms, base64, SHA-256, the math and the c
 * strings.
 */

/**
 * @defgroup utils_misc Misc
 * @ingroup utils
 * @brief The system.
 * @{
 */

/**
 * @brief Returns whether the system stores the integers in little endian.
 *
 * @return true in little endian
 */
CERVER_PUBLIC bool system_is_little_endian (void);

/** @} */

/**
 * @defgroup utils_math Math
 * @ingroup utils
 * @brief The math helpers.
 * @{
 */

/**
 * @brief Clamps a value between a min and a max.
 *
 * @param[in] val the value
 * @param[in] min the min
 * @param[in] max the max
 * @return min if val is smaller, max if it is bigger, or val
 */
CERVER_PUBLIC int clamp_int (
	const int val, const int min, const int max
);

/**
 * @brief Returns the absolute value of an int.
 *
 * @param[in] value the value
 * @return the absolute value; INT_MAX for INT_MIN, which has no positive int
 */
CERVER_PUBLIC int abs_int (const int value);

/**
 * @brief The linear interpolation between two values.
 *
 * @param[in] first the value when by is 0
 * @param[in] second the value when by is 1
 * @param[in] by the position between them
 * @return first * (1 - by) + second * by
 */
CERVER_PUBLIC float lerp (
	const float first, const float second, const float by
);

/**
 * @brief Returns whether two floats are equal with a tolerance of 0.00001.
 *
 * @param[in] f1 the first float
 * @param[in] f2 the second float
 * @return true when their difference is smaller than the tolerance
 */
CERVER_PUBLIC bool float_compare (
	const float f1, const float f2
);

/** @} */

/**
 * @defgroup utils_random Random
 * @ingroup utils
 * @brief The pseudo random values of rand (), not for secrets (cerver/crypto/random.h is).
 * @{
 */

/**
 * @brief Seeds the pseudo random generator, with srand ().
 *
 * @param[in] seed the seed
 */
CERVER_PUBLIC void random_set_seed (
	const unsigned int seed
);

/**
 * @brief Returns a pseudo random int between two limits, both included, in any order.
 *
 * @param[in] min one limit
 * @param[in] max the other limit
 * @return the value, with rand ()
 * @note Any two limits work, INT_MIN to INT_MAX too; a range bigger than RAND_MAX values only gets its first
 * RAND_MAX + 1 values.
 */
CERVER_PUBLIC int random_int_in_range (
	const int min, int max
);

/**
 * @brief Returns a pseudo random float between 0 and abs, both included.
 *
 * @param[in] abs the biggest value
 * @return the value, with rand ()
 */
CERVER_PUBLIC float random_float (
	const float abs
);

/** @} */

/**
 * @defgroup utils_converters Converters
 * @ingroup utils
 * @brief Strings to numbers and back.
 * @{
 */

/**
 * @brief Converts the hex digits of a string to an int.
 *
 * An optional `0x` prefix is skipped, and the conversion stops at the first character that is not a hex digit.
 *
 * @param[in] hex_string the string, NULL returns 0
 * @return the value, 0 without digits
 * @warning More than 7 digits overflow the int.
 */
CERVER_PUBLIC int xtoi (char *hex_string);

/**
 * @brief Writes an int as its decimal characters, with an ending null byte.
 *
 * @param[in] i the value
 * @param[out] b the string, with room for the characters, the sign and the null byte (12 bytes)
 * @return b; INT_MIN has its digits too
 */
CERVER_PUBLIC char *itoa (int i, char *b);

/**
 * @brief Returns whether a string is "true", in any case.
 *
 * @param[in] string_value the string, NULL returns false
 * @return true only for "true"
 */
CERVER_PUBLIC bool bool_value_from_string (
	const char *string_value
);

/** @} */

/**
 * @defgroup utils_c_strings C strings
 * @ingroup utils
 * @brief Copies, concats, splits and edits of null terminated strings.
 *
 * The functions that return a new string allocate it with malloc (), and it is released with free ().
 * @{
 */

/**
 * @brief Copies a string into another one.
 *
 * @param[out] to the destination, it can be NULL
 * @param[in] from the string, it can be NULL
 * @warning It does not check the size of the destination, that needs room for the whole string and its null byte.
 */
CERVER_PUBLIC void c_string_copy (
	char *to, const char *from
);

/**
 * @brief Copies up to n - 1 characters of a string into a buffer of n bytes, always with an ending null byte.
 *
 * @param[out] to the destination, of n bytes; it can be NULL
 * @param[in] from the string, it can be NULL
 * @param[in] n the size of the destination, 0 copies nothing
 */
CERVER_PUBLIC void c_string_n_copy (
	char *to, const char *from, size_t n
);

/**
 * @brief Copies a string into a buffer of size bytes, cut to size - 1 characters and always with an ending null
 * byte, so the buffer does not need to start with zeros.
 *
 * @param[out] to the destination, of size bytes; NULL copies nothing
 * @param[in] from the string, NULL leaves an empty destination
 * @param[in] size the size of the destination, 0 copies nothing
 * @retval true the string did not fit and was cut
 * @retval false it was copied whole, or there was nothing to copy
 * @since 1.9b-2
 */
CERVER_PUBLIC bool c_string_copy_bounded (
	char *to, const char *from, size_t size
);

/**
 * @brief Concats two strings into a new one.
 *
 * @param[in] s1 the first string
 * @param[in] s2 the second string
 * @param[out] des_size the length of the result, it can be NULL
 * @return the new string, NULL for a NULL string or if it could not be allocated
 */
CERVER_PUBLIC char *c_string_concat (
	const char *s1, const char *s2, size_t *des_size
);

/**
 * @brief Concats two strings into a buffer, only when the result and its null byte fit.
 *
 * @param[in] s1 the first string
 * @param[in] s2 the second string
 * @param[out] des the destination
 * @param[in] des_size the size of the destination
 * @return the length of the result, 0 for a NULL argument or when it did not fit (des is not changed)
 */
CERVER_PUBLIC size_t c_string_concat_safe (
	const char *s1, const char *s2,
	char *des, size_t des_size
);

/**
 * @brief Creates a new string with a format, as in printf (), with vasprintf ().
 *
 * @param[in] format the format, NULL returns NULL
 * @param[in] ... the arguments of the format
 * @return the new string, NULL on error
 */
CERVER_PUBLIC char *c_string_create (const char *format, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Removes every space of a string, in place.
 *
 * @param[in,out] s the string, it must not be NULL
 */
CERVER_PUBLIC void c_string_remove_spaces (char *s);

/**
 * @brief Removes every CR and LF of a string, in place.
 *
 * @param[in,out] s the string, it must not be NULL
 */
CERVER_PUBLIC void c_string_remove_line_breaks (char *s);

/**
 * @brief Removes every space, CR and LF of a string, in place.
 *
 * @param[in,out] s the string, it must not be NULL
 */
CERVER_PUBLIC void c_string_remove_spaces_and_line_breaks (char *s);

/**
 * @brief Counts the tokens between the appearances of a delimiter, the size of the array of c_string_split ().
 *
 * @param[in] original the string, it is not changed; NULL returns 0
 * @param[in] delim the delimiter
 * @return the number of tokens
 */
CERVER_PUBLIC size_t c_string_count_tokens (
	const char *original, const char delim
);

/**
 * @brief Splits a string into the tokens between the appearances of a delimiter.
 *
 * It uses __strtok_r (), so it is safe from many threads; consecutive delimiters do not make empty tokens.
 *
 * @param[in] original the string, it is not changed
 * @param[in] delim the delimiter
 * @param[out] n_tokens the number of tokens, it can be NULL
 * @return an array of n_tokens new strings, released with free () each and the array; NULL for a NULL string, a
 * string of one character or less, or without tokens (a string of only delimiters, like ",,", sets n_tokens to 0)
 */
CERVER_PUBLIC char **c_string_split (
	const char *original, const char delim, size_t *n_tokens
);

/**
 * @brief Creates a new string with the characters of a string in the opposite order.
 *
 * @param[in] str the string
 * @return the new string, NULL for a NULL string or if it could not be allocated
 */
CERVER_PUBLIC char *c_string_reverse (const char *str);

/**
 * @brief Removes every appearance of a character from a string, in place.
 *
 * @param[in,out] string the string, it must not be NULL
 * @param[in] garbage the character to remove
 */
CERVER_PUBLIC void c_string_remove_char (
	char *string, char garbage
);

/**
 * @brief Creates a copy of a string without the first appearance of a sub string.
 *
 * @param[in] str the string
 * @param[in] sub the sub string
 * @return the new string, NULL for a NULL argument, when sub is not in str or if it could not be allocated
 */
CERVER_PUBLIC char *c_string_remove_sub (
	char *str, const char *sub
);

/**
 * @brief Removes the white space at the start and at the end of a string.
 *
 * @param[in,out] str the string, it must not be NULL; its end is cut with a null byte
 * @return a pointer inside str to its first character that is not a white space
 */
CERVER_PUBLIC char *c_string_trim (char *str);

/**
 * @brief Removes the quotes (`"` and `'`) at the start and at the end of a string.
 *
 * @param[in,out] str the string, it must not be NULL; its end is cut with a null byte
 * @return a pointer inside str to its first character that is not a quote
 */
CERVER_PUBLIC char *c_string_strip_quotes (char *str);

/**
 * @brief Returns whether a string starts with a sub string.
 *
 * @param[in] str the string
 * @param[in] substr the sub string
 * @return true when it starts with it, false for a NULL argument
 */
CERVER_PUBLIC bool c_string_starts_with (
	const char *str, const char *substr
);

/**
 * @brief Finds a sub string in the first len bytes of a string.
 *
 * @param[in] haystack the string, with at least len bytes
 * @param[in] needle the sub string, it must not be NULL
 * @param[in] len the bytes of haystack to search
 * @return a pointer inside haystack to the match, haystack for an empty needle, or NULL
 */
CERVER_PUBLIC char *c_string_find_sub_in_len (
	const char *haystack, const char *needle, size_t len
);

/**
 * @brief Creates a new string with the characters between two pointers of the same string, both included.
 *
 * @param[in] first the first character
 * @param[in] last the last character, not before first
 * @return the new string, NULL for a NULL pointer or if it could not be allocated
 */
CERVER_PUBLIC char *c_string_create_with_ptrs (
	char *first, char *last
);

/**
 * @brief Splits a string at the first appearance of a token.
 *
 * @param[in] str the string
 * @param[in] token the token
 * @param[out] sub a new string from the token to the end (empty without the token), it can be NULL
 * @return a new string with the characters before the token (all of them without it), NULL for a NULL string
 * @warning The allocations are not checked: without memory the copies write to NULL.
 */
CERVER_PUBLIC char *c_string_remove_sub_after_token (
	char *str, const char token, char **sub
);

/**
 * @brief Splits a string after an appearance of a token.
 *
 * For example, `/home/ermiry/Documents` with the token `/` and the index -1 returns `/home/ermiry/` and its sub
 * is `Documents`.
 *
 * @param[in] str the string
 * @param[in] token the token
 * @param[out] sub a new string after the token, it can be NULL
 * @param[in] idx the appearance of the token, from 1; -1 for the last one
 * @return a new string up to the token, with it; NULL for a NULL string or when the token (or its appearance) is not
 * in the string
 */
CERVER_PUBLIC char *c_string_remove_sub_after_token_with_idx (
	char *str, const char token, char **sub, int idx
);

/**
 * @brief Removes the characters from the first to the last appearance of a token, both included.
 *
 * For example, `test_20191118142101759__TEST__.png` with the token `_` returns `test.png`.
 *
 * @param[in] str the string
 * @param[in] token the token
 * @param[out] sub a new string with the characters that were removed, it can be NULL
 * @return the new string, NULL for a NULL string or without the token
 */
CERVER_PUBLIC char *c_string_remove_sub_simetric_token (
	char *str, const char token, char **sub
);

/**
 * @brief Removes the characters between two appearances of a token, both included.
 *
 * For example, `test_20191118142101759__TEST__.png` with the token `_`, first 1 and last 3 returns
 * `testTEST__.png`.
 *
 * @param[in] str the string
 * @param[in] token the token
 * @param[in] first the first appearance, from 1
 * @param[in] last the last appearance, -1 for the last one
 * @param[out] sub a new string with the characters that were removed, it can be NULL
 * @return the new string, NULL for a NULL string, when first equals last, or when an appearance is not in the
 * string
 */
CERVER_PUBLIC char *c_string_remove_sub_range_token (
	char *str,
	const char token, int first, int last,
	char **sub
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
