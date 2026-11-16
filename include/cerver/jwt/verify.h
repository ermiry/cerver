/**
 * @file cerver/jwt/verify.h
 * @brief Verifying a token and reading its claims.
 */

#ifndef _CERVER_JWT_VERIFY_H_
#define _CERVER_JWT_VERIFY_H_

#include <stddef.h>

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#include "cerver/json/json.h"

#include "cerver/jwt/jwt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup jwt_verify Verify
 * @ingroup jwt
 * @brief Verifying the signature of a token with a key and reading its claims.
 *
 * A token is taken in this order, and the first rule that it breaks is its error: its size; its three parts
 * (two separators, none of them empty); its header; its signature; its claims. The claims are only read after
 * the signature was verified.
 *
 * The header is the JSON of an object without a key more than once, in canonical base64url (RFC 4648 section 5:
 * without padding and without bits left). Its `alg` must be the string of the algorithm of the key: the token
 * never chooses its algorithm, and `none` is the one of no key. A header with `crit` is refused (RFC 7515 section
 * 4.1.11: no extension is understood), and the `iss`, `sub` and `aud` that a header repeats must be equal to the
 * ones of the claims (RFC 7519 section 5.3). The other members (`typ`, `kid`, `cty`, …) are not read.
 * @{
 */

/**
 * @brief Verifies a token with a key and validates its claims.
 *
 * @param[in] key the key: a secret, or a public or a private key
 * @param[in,out] arena where the claims are read: they live as long as it does
 * @param[in] token the characters of the token, they do not need to end with a null byte
 * @param[in] token_length how many
 * @param[in] validation what the claims must have (cerver_jwt_claims_validate ()) and the most characters of the
 * token; NULL is the same as a struct of zeros
 * @param[out] claims the claims of the token, an object of the JSON of cerver that lives in the arena; NULL when
 * the token is not valid; it can be NULL
 * @retval CERVER_JWT_ERROR_NONE the token is the one of the key and its claims are valid
 * @retval CERVER_JWT_ERROR_ARGUMENTS a NULL key, arena or token
 * @retval CERVER_JWT_ERROR_MEMORY the arena has no memory
 * @retval CERVER_JWT_ERROR_SIZE the token has more characters than its limit, or its header more than
 * #CERVER_JWT_HEADER_SIZE_MAX
 * @retval CERVER_JWT_ERROR_FORMAT the token is not three parts with characters, or one of them is not canonical
 * base64url
 * @retval CERVER_JWT_ERROR_HEADER the header is not the JSON of an object (with at most #CERVER_JWT_HEADER_DEPTH
 * levels, without a key more than once and without a null character), its `alg` is not a string, it has `crit`,
 * or it repeats a claim with another value
 * @retval CERVER_JWT_ERROR_ALGORITHM the `alg` of the header is not the string of the algorithm of the key
 * @retval CERVER_JWT_ERROR_SIGNATURE the signature is not the one of the first two parts with the key
 * @retval CERVER_JWT_ERROR_CLAIMS the claims are not the JSON of an object (with at most #CERVER_JWT_CLAIMS_DEPTH
 * levels, without a key more than once and without a null character), or a claim that is validated has
 * another type
 * @retval CERVER_JWT_ERROR_EXPIRED see cerver_jwt_claims_validate ()
 * @retval CERVER_JWT_ERROR_NOT_BEFORE see cerver_jwt_claims_validate ()
 * @retval CERVER_JWT_ERROR_ISSUED_AT see cerver_jwt_claims_validate ()
 * @retval CERVER_JWT_ERROR_ISSUER see cerver_jwt_claims_validate ()
 * @retval CERVER_JWT_ERROR_SUBJECT see cerver_jwt_claims_validate ()
 * @retval CERVER_JWT_ERROR_AUDIENCE see cerver_jwt_claims_validate ()
 * @note The memory that a token that is not valid took from the arena stays in it until the arena is reset.
 * @threadsafety Safe from any thread, also with the same key; an arena is used by one thread at a time.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtError cerver_jwt_verify (
	const CerverJwtKey *key, Arena *arena,
	const char *token, const size_t token_length,
	const CerverJwtValidation *validation,
	const CerverJsonValue **claims
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
