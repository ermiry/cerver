/**
 * @file cerver/jwt/sign.h
 * @brief Signing the claims of a token.
 */

#ifndef _CERVER_JWT_SIGN_H_
#define _CERVER_JWT_SIGN_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/jwt/jwt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup jwt_sign Sign
 * @ingroup jwt
 * @brief Writing a token: the header of the key, the claims and their signature.
 *
 * The header of every token is `{"alg":"<algorithm>","typ":"JWT"}`, with the algorithm of the key. The token is
 * written in a buffer of the caller, without memory of its own.
 * @{
 */

/**
 * @brief Returns the bytes that the token of some claims needs.
 *
 * @param[in] key the key that signs
 * @param[in] claims_size the bytes of the claims
 * @return the bytes of the token with its null byte; 0 for a NULL key and for claims whose token would have more
 * than #CERVER_JWT_SIZE_MAX characters
 * @since 3.0b-8
 */
CERVER_EXPORT size_t cerver_jwt_sign_size (
	const CerverJwtKey *key, const size_t claims_size
);

/**
 * @brief Signs some claims: writes the token `header.claims.signature` of the key.
 *
 * @param[in] key the key: a secret or a private key
 * @param[in] claims the text of the claims: the JSON of an object, as a CerverJsonWriter leaves it
 * @param[in] claims_size its bytes
 * @param[out] token where the token is written, with its null byte
 * @param[in] capacity the bytes of the buffer, at least cerver_jwt_sign_size ()
 * @param[out] length the characters of the token without its null byte, 0 when it fails
 * @retval CERVER_JWT_ERROR_NONE the token was written
 * @retval CERVER_JWT_ERROR_ARGUMENTS a NULL key, claims, token or length, or claims without bytes
 * @retval CERVER_JWT_ERROR_KEY the key is a public one
 * @retval CERVER_JWT_ERROR_SIZE the buffer is smaller than the token, or the token would have more than
 * #CERVER_JWT_SIZE_MAX characters
 * @retval CERVER_JWT_ERROR_SIGNATURE the provider of the cryptography could not sign
 * @warning The claims are signed as they are, they are not read: a text that is not the JSON of an object makes a
 * token that cerver_jwt_verify () refuses. The keys of the claims are written in the order of the text.
 * @threadsafety Safe from any thread, also with the same key.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtError cerver_jwt_sign (
	const CerverJwtKey *key,
	const char *claims, const size_t claims_size,
	char *token, const size_t capacity, size_t *length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
