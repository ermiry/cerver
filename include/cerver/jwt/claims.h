/**
 * @file cerver/jwt/claims.h
 * @brief Validating the claims of a token.
 */

#ifndef _CERVER_JWT_CLAIMS_H_
#define _CERVER_JWT_CLAIMS_H_

#include "cerver/config.h"

#include "cerver/json/json.h"

#include "cerver/jwt/jwt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup jwt_claims Claims
 * @ingroup jwt
 * @brief The registered claims of a token (RFC 7519 section 4.1) against what a verifier expects.
 * @{
 */

/**
 * @brief Validates the claims of a token; cerver_jwt_verify () already does it with the claims that it reads.
 *
 * The claims are checked in this order, and the first one that is not valid is the error. The times are integers
 * of seconds since the epoch, and they are compared with the time of the validation and its leeway:
 * `exp` is valid while `now - leeway < exp`; `nbf` and `iat` are valid when `now + leeway >= ` their value.
 *
 * @param[in] claims the claims, an object of the JSON of cerver
 * @param[in] validation what the claims must have; NULL is the same as a struct of zeros
 * @retval CERVER_JWT_ERROR_NONE the claims are valid
 * @retval CERVER_JWT_ERROR_CLAIMS the claims are not an object, or `exp`, `nbf` or `iat` is there and it is not
 * an integer that fits in 64 bits with sign (a real, like `1.0` or `1e9`, and a string are not)
 * @retval CERVER_JWT_ERROR_EXPIRED the time reached `exp`, or there is no `exp` and the validation requires it
 * @retval CERVER_JWT_ERROR_NOT_BEFORE the time did not reach `nbf`
 * @retval CERVER_JWT_ERROR_ISSUED_AT the time did not reach `iat`: the token was issued in the future
 * @retval CERVER_JWT_ERROR_ISSUER the validation has an issuer and `iss` is not a string with its characters
 * @retval CERVER_JWT_ERROR_SUBJECT the validation has a subject and `sub` is not a string with its characters
 * @retval CERVER_JWT_ERROR_AUDIENCE the validation has an audience and `aud` is not a string with its characters
 * nor an array that has one
 * @note A claim that the validation does not ask for is not checked: without an issuer any `iss` is taken, also
 * one that is not a string.
 * @threadsafety Safe from any thread.
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtError cerver_jwt_claims_validate (
	const CerverJsonValue *claims, const CerverJwtValidation *validation
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
