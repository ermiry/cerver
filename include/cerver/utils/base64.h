/**
 * @file cerver/utils/base64.h
 * @brief The standard base64 with padding (fastbase64, thirdparty.md) and the canonical base64url of cerver.
 */

#ifndef _CERVER_UTILS_BASE64_H_
#define _CERVER_UTILS_BASE64_H_

#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup utils_base64 Base64
 * @ingroup utils
 * @brief Encoding and decoding base64 and base64url.
 * @{
 */

/**
 * @brief The bytes that the encoded string of an input of A bytes needs, with its ending null byte:
 * `ceil (A / 3) * 4 + 1`.
 *
 * @param A the bytes of the input
 */
#define base64_encode_len(A) (((A) + 2) / 3 * 4 + 1)

/**
 * @brief The bytes that the output of decoding a string of A characters needs: `floor (A * 3 / 4) + 2`, a bit more
 * than the bytes that are written.
 *
 * @param A the characters of the encoded string
 */
#define base64_decode_len(A) ((A) / 4 * 3 + 2)

/**
 * @brief The length of the encoded string of an input of A bytes, without the ending null byte.
 *
 * @param A the bytes of the input
 */
#define base64_encode_strlen(A) (((A) + 2) / 3 * 4)

/**
 * @brief Encodes some bytes as base64 with padding, with the scalar code.
 *
 * @param[out] dest the encoded string, with room for base64_encode_len (len) bytes; it ends with a null byte
 * @param[in] str the bytes
 * @param[in] len the number of bytes
 * @return the length of the encoded string, without the null byte
 * @note For the internal methods of cerver: an application uses base64_encode ().
 */
CERVER_PRIVATE size_t chromium_base64_encode (
	char *dest, const char *str, size_t len
);

/**
 * @brief Decodes a base64 string with padding, with the scalar code.
 *
 * @param[out] dest the bytes, with room for base64_decode_len (len) bytes
 * @param[in] src exactly len characters of base64, without any other character (like a white space)
 * @param[in] len the length of the string: 0, or a multiple of 4
 * @return the number of bytes written, or (size_t) -1 for a length that is not a multiple of 4 or a character that
 * is not base64
 * @note For the internal methods of cerver: an application uses base64_decode ().
 */
CERVER_PRIVATE size_t chromium_base64_decode (
	char *dest, const char *src, size_t len
);

#if defined (__AVX__) || defined (__AVX2__)

/**
 * @brief Encodes some bytes as base64 with the AVX2 code, like chromium_base64_encode ().
 *
 * @param[out] dest the encoded string, with room for base64_encode_len (len) bytes
 * @param[in] str the bytes
 * @param[in] len the number of bytes
 * @return the length of the encoded string, without the null byte
 * @note Only defined when cerver is compiled with `__AVX2__`.
 */
CERVER_PRIVATE size_t fast_avx2_base64_encode (
	char *dest, const char *str, size_t len
);

/**
 * @brief Decodes a base64 string with the AVX2 code, like chromium_base64_decode ().
 *
 * @param[out] out the bytes, with room for base64_decode_len (srclen) bytes
 * @param[in] src the string
 * @param[in] srclen the length of the string
 * @return the number of bytes written, or (size_t) -1 for a string that is not valid base64
 * @note Only defined when cerver is compiled with `__AVX2__`.
 */
CERVER_PRIVATE size_t fast_avx2_base64_decode (
	char *out, const char *src, size_t srclen
);

#endif

/**
 * @brief Encodes some bytes as base64 with padding (`+`, `/` and `=`), with the AVX2 code when cerver is compiled
 * with it.
 *
 * @param[out] encoded the encoded string, with room for base64_encode_len (inlen) bytes; it ends with a null byte
 * @param[in] input the bytes
 * @param[in] inlen the number of bytes
 * @return the length of the encoded string, without the null byte
 */
CERVER_PUBLIC size_t base64_encode (
	char *encoded, const char *input, size_t inlen
);

/**
 * @brief Decodes a base64 string with padding.
 *
 * @param[out] output the bytes, with room for base64_decode_len (codedlen) bytes
 * @param[in] bufcoded exactly codedlen characters of base64
 * @param[in] codedlen the length of the string: 0, or a multiple of 4
 * @return the number of bytes written, or (size_t) -1 for a string that is not valid base64
 */
CERVER_PUBLIC size_t base64_decode (
	char *output, const char *bufcoded, size_t codedlen
);

/**
 * @brief The characters of the base64url (RFC 4648 §5) string of an input of length bytes, without padding and
 * without the ending null byte.
 *
 * @param length the bytes of the input
 */
#define base64url_encode_length(length) ((((length) / 3) * 4) + ((((length) % 3) * 4 + 2) / 3))

/**
 * @brief The bytes that a base64url string of length characters can produce.
 *
 * @param length the characters of the string
 */
#define base64url_decode_length(length) ((((length) / 4) * 3) + ((((length) % 4) * 3) / 4))

/**
 * @brief Encodes some bytes as base64url (RFC 4648 §5): `-` and `_`, without padding.
 *
 * @param[out] encoded the encoded string, with room for base64url_encode_length (input_length) + 1 bytes; it ends
 * with a null byte
 * @param[in] input the bytes
 * @param[in] input_length the number of bytes
 * @return the length of the encoded string, without the null byte
 */
CERVER_PUBLIC size_t base64url_encode (
	char *encoded, const unsigned char *input, size_t input_length
);

/**
 * @brief Decodes a canonical base64url string.
 *
 * Only the characters of the alphabet are taken, without padding and with the unused bits of the last character
 * set to zero, so every token has a single valid form.
 *
 * @param[out] output the bytes, with room for base64url_decode_length (encoded_length) bytes
 * @param[out] output_length the number of bytes written, 0 on error
 * @param[in] encoded the string, NULL only when encoded_length is 0
 * @param[in] encoded_length the length of the string
 * @retval 0 the string was decoded
 * @retval 1 a NULL argument, a length that leaves a single character in its last group, a character out of the
 * alphabet, or unused bits that are not zero
 */
CERVER_PUBLIC unsigned int base64url_decode (
	unsigned char *output, size_t *output_length,
	const char *encoded, size_t encoded_length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
