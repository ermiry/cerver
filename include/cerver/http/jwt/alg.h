#ifndef _CERVER_HTTP_JWT_ALG_H_
#define _CERVER_HTTP_JWT_ALG_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JWT_ALG_MAP(XX)					\
	XX(0,	NONE, 		None)			\
	XX(1,	HS256, 		HS256)			\
	XX(2,	HS384, 		HS384)			\
	XX(3,	HS512, 		HS512)			\
	XX(4,	RS256, 		RS256)			\
	XX(5,	RS384, 		RS384)			\
	XX(6,	RS512, 		RS512)			\
	XX(7,	ES256, 		ES256)			\
	XX(8,	ES384, 		ES384)			\
	XX(9,	ES512, 		ES512)			\
	XX(10,	TERM, 		Invalid)		\

typedef enum jwt_alg_t {

	#define XX(num, name, string) JWT_ALG_##name = num,
	JWT_ALG_MAP (XX)
	#undef XX

} jwt_alg_t;

#define JWT_ALG_INVAL 		JWT_ALG_TERM

#define JWT_DEFAULT_ALG		JWT_ALG_HS256

/**
 * Convert alg type to it's string representation.
 *
 * Returns a string that matches the alg type provided.
 *
 * @param alg A valid jwt_alg_t specifier.
 * @returns Returns a string (e.g. "RS256") matching the alg or NULL for
 *     invalid alg.
 */
CERVER_PUBLIC const char *jwt_alg_str (const jwt_alg_t alg);

/**
 * Convert alg string to type.
 *
 * Returns an alg type based on the string representation.
 *
 * @param alg A valid string algorithm type (e.g. "RS256").
 * @returns Returns an alg type matching the string or JWT_ALG_INVAL if no
 *     matches were found.
 *
 * Note, this only works for algorithms that LibJWT supports or knows about.
 */
CERVER_PUBLIC jwt_alg_t jwt_str_alg (const char *alg);

#ifdef __cplusplus
}
#endif

#endif