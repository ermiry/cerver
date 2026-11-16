/**
 * @file cerver/jwt/jwt.h
 * @brief The JSON Web Tokens of cerver (RFC 7519) in their compact form (RFC 7515): the algorithms, the errors,
 * the limits and the types that its parts share.
 */

#ifndef _CERVER_JWT_H_
#define _CERVER_JWT_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup jwt JWT
 * @brief The JSON Web Tokens of cerver (RFC 7519): a signed token in its compact form (RFC 7515), `header.claims.
 * signature`, each part in base64url.
 *
 * A key signs and verifies with one algorithm, the one that it was created with: the `alg` of a token never
 * chooses how it is verified, it must be the one of the key. A token without a signature (`none`) is never
 * written nor taken. The claims are a text of the JSON of cerver (cerver/json), and every signature is computed
 * by the provider of the cryptography (cerver/crypto). Nothing here has a state: the key, the limits and the
 * validation are arguments of each call.
 *
 * @since 3.0b-8
 */

/**
 * @defgroup jwt_types Types
 * @ingroup jwt
 * @brief The algorithms, the errors, the limits, the key and the validation of the claims.
 * @{
 */

/**
 * @brief The X-macro table of the algorithms of a token (RFC 7518 section 3.1 and RFC 8037 section 3.1): the
 * number, the name and the string of each one, its `alg`.
 *
 * HS256, HS384 and HS512 are an HMAC with a secret; RS256, RS384 and RS512 are RSA with the padding of PKCS #1
 * v1.5; ES256, ES384 and ES512 are ECDSA with the curves P-256, P-384 and P-521; PS256, PS384 and PS512 are RSA
 * with PSS; EdDSA is Ed25519 or Ed448, the one of its key. NONE is not an algorithm: the `none` of RFC 7518 is
 * never used.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_ALGORITHM_MAP(XX)		\
	XX(0,	NONE,		None)				\
	XX(1,	HS256,		HS256)				\
	XX(2,	HS384,		HS384)				\
	XX(3,	HS512,		HS512)				\
	XX(4,	RS256,		RS256)				\
	XX(5,	RS384,		RS384)				\
	XX(6,	RS512,		RS512)				\
	XX(7,	ES256,		ES256)				\
	XX(8,	ES384,		ES384)				\
	XX(9,	ES512,		ES512)				\
	XX(10,	PS256,		PS256)				\
	XX(11,	PS384,		PS384)				\
	XX(12,	PS512,		PS512)				\
	XX(13,	EDDSA,		EdDSA)

/**
 * @brief The algorithm of a token, one `CERVER_JWT_ALGORITHM_<name>` for each row of #CERVER_JWT_ALGORITHM_MAP.
 *
 * @since 3.0b-8
 */
typedef enum CerverJwtAlgorithm {

	#define XX(num, name, string) CERVER_JWT_ALGORITHM_##name = num,
	CERVER_JWT_ALGORITHM_MAP (XX)
	#undef XX

} CerverJwtAlgorithm;

/**
 * @brief The number of rows of #CERVER_JWT_ALGORITHM_MAP, with CERVER_JWT_ALGORITHM_NONE.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_ALGORITHMS				14

/**
 * @brief Returns the string of an algorithm, the `alg` of its tokens.
 *
 * @param[in] algorithm the algorithm
 * @return the string of its row in #CERVER_JWT_ALGORITHM_MAP, or the one of CERVER_JWT_ALGORITHM_NONE for a value
 * out of the table; a static string that must not be freed
 * @since 3.0b-8
 */
CERVER_EXPORT const char *cerver_jwt_algorithm_to_string (
	const CerverJwtAlgorithm algorithm
);

/**
 * @brief Returns the algorithm of an `alg`.
 *
 * @param[in] string the characters of the `alg`, they do not need to end with a null byte
 * @param[in] length how many
 * @return the algorithm whose string is exactly that one (the case matters); CERVER_JWT_ALGORITHM_NONE for any
 * other text, also for `none`, for `None` and for a NULL string
 * @since 3.0b-8
 */
CERVER_EXPORT CerverJwtAlgorithm cerver_jwt_algorithm_from_string (
	const char *string, const size_t length
);

/**
 * @brief The X-macro table of the errors of a token: the number, the name and the description of each one.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_ERROR_MAP(XX)																			\
	XX(0,	NONE,			No error)																		\
	XX(1,	ARGUMENTS,		An argument is not valid)														\
	XX(2,	MEMORY,			The memory could not be allocated)												\
	XX(3,	KEY,			The key is not one that the algorithm takes or it can not sign)					\
	XX(4,	SIZE,			The token is bigger than its limit or than its buffer)							\
	XX(5,	FORMAT,			The token is not three parts of canonical base64url)							\
	XX(6,	HEADER,			The header is not an object that cerver takes)									\
	XX(7,	ALGORITHM,		The algorithm of the token is not the one of the key)							\
	XX(8,	SIGNATURE,		The signature is not the one of the token)										\
	XX(9,	CLAIMS,			The claims are not an object or one of them has another type)					\
	XX(10,	EXPIRED,		The token expired or it has no expiration and one is required)					\
	XX(11,	NOT_BEFORE,		The token is not valid yet)														\
	XX(12,	ISSUED_AT,		The token was issued in the future)												\
	XX(13,	ISSUER,			The issuer of the token is not the expected one)								\
	XX(14,	SUBJECT,		The subject of the token is not the expected one)								\
	XX(15,	AUDIENCE,		The audience of the token does not have the expected one)

/**
 * @brief The error of a token, one `CERVER_JWT_ERROR_<name>` for each row of #CERVER_JWT_ERROR_MAP.
 *
 * @since 3.0b-8
 */
typedef enum CerverJwtError {

	#define XX(num, name, description) CERVER_JWT_ERROR_##name = num,
	CERVER_JWT_ERROR_MAP (XX)
	#undef XX

} CerverJwtError;

/**
 * @brief Returns the description of an error.
 *
 * @param[in] error the error
 * @return the description of its row in #CERVER_JWT_ERROR_MAP, or the one of CERVER_JWT_ERROR_NONE for a value
 * out of the table; a static string that must not be freed
 * @since 3.0b-8
 */
CERVER_EXPORT const char *cerver_jwt_error_to_string (
	const CerverJwtError error
);

/**
 * @brief The most characters of the first part of a token that is verified, its header in base64url.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_HEADER_SIZE_MAX			(1 << 10)

/**
 * @brief The nested arrays and objects that the header of a token can have.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_HEADER_DEPTH				(1 << 2)

/**
 * @brief The nested arrays and objects that the claims of a token can have.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_CLAIMS_DEPTH				(1 << 5)

/**
 * @brief The most characters of a token that is verified when its validation has no limit: 2^20.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_SIZE_MAX					(1 << 20)

/**
 * @brief The bytes of the header that cerver writes with the string of an algorithm, in base64url and with its
 * null byte: `{"alg":"HS256","typ":"JWT"}` is 36 characters.
 *
 * @since 3.0b-8
 */
#define CERVER_JWT_HEADER_ENCODED_SIZE		40

/**
 * @brief A key of a token: a key of the provider of the cryptography (cerver/crypto/key.h) with the algorithm that
 * it signs and verifies with.
 *
 * It is created with the methods of cerver/jwt/key.h and it is not changed after: any thread can sign and
 * verify with it at the same time.
 *
 * @since 3.0b-8
 */
typedef struct CerverJwtKey CerverJwtKey;

/**
 * @brief What the claims of a token must have to be valid, given to each call; a struct of zeros checks the
 * times of the token against the clock, without leeway, and nothing else.
 *
 * @since 3.0b-8
 */
typedef struct CerverJwtValidation {

	/**
	 * @brief The time that the claims are checked against, in seconds since the epoch; 0 uses the clock
	 * (time ()).
	 */
	i64 now;

	/** @brief The seconds that the clocks of the issuer and of the verifier can differ: `exp`, `nbf` and `iat`
	 * are checked with them in favor of the token. */
	u32 leeway;

	/** @brief A token without `exp` is not valid; without it a token that has no `exp` never expires. */
	bool require_expiration;

	/** @brief The `iss` that the token must have, NULL takes any. */
	const char *issuer;

	/** @brief The `sub` that the token must have, NULL takes any. */
	const char *subject;

	/** @brief The `aud` that the token must have, as its string or as one of the strings of its array; NULL
	 * takes any. */
	const char *audience;

	/** @brief The most characters of the token; 0 uses #CERVER_JWT_SIZE_MAX, that is also the most. */
	size_t max_size;

} CerverJwtValidation;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
