#ifndef _CERVER_HTTP_AUTH_H_
#define _CERVER_HTTP_AUTH_H_

#include <stddef.h>
#include <stdint.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/http.h"

#define HTTP_JWT_POOL_INIT			32

#define HTTP_JWT_VALUE_KEY_SIZE		128
#define HTTP_JWT_VALUE_VALUE_SIZE	128

#define HTTP_JWT_VALUES_SIZE		16

// "Bearer " and a token of up to HTTP_CERVER_DEFAULT_JWT_MAX_SIZE
#define HTTP_JWT_BEARER_SIZE		(1 << 13)

// the json with the bearer and an extra value
#define HTTP_JWT_TOKEN_SIZE			(1 << 14)

// the value of an authorization header with a token
#define HTTP_JWT_BEARER_PREFIX			"Bearer "
#define HTTP_JWT_BEARER_PREFIX_LENGTH	(sizeof (HTTP_JWT_BEARER_PREFIX) - 1)

#ifdef __cplusplus
extern "C" {
#endif

struct jwt;
struct JwtKey;

typedef struct HttpJwtValue {

	char key[HTTP_JWT_VALUE_KEY_SIZE];

	cerver_type_t type;

	union {
		bool value_bool;
		int64_t value_int;
		char value_str[HTTP_JWT_VALUE_VALUE_SIZE];
	};

} HttpJwtValue;

typedef struct HttpJwt {

	struct jwt *jwt;

	u8 n_values;
	HttpJwtValue values[HTTP_JWT_VALUES_SIZE];

	size_t bearer_len;
	char bearer[HTTP_JWT_BEARER_SIZE];
	size_t json_len;
	char json[HTTP_JWT_TOKEN_SIZE];

} HttpJwt;

CERVER_PRIVATE void *http_jwt_new (void);

CERVER_PRIVATE void http_jwt_delete (void *http_jwt_ptr);

CERVER_EXPORT const char *http_jwt_get_bearer (
	const HttpJwt *http_jwt
);

CERVER_EXPORT const size_t http_jwt_get_bearer_len (
	const HttpJwt *http_jwt
);

CERVER_EXPORT const char *http_jwt_get_json (
	const HttpJwt *http_jwt
);

CERVER_EXPORT const size_t http_jwt_get_json_len (
	const HttpJwt *http_jwt
);

// loads a key from a filename that can be used for jwt
// returns a newly allocated c string on success, NULL on error
CERVER_PUBLIC char *http_cerver_auth_load_key (
	const char *filename, size_t *keylen
);

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256, JWT_ALG_NONE is not accepted
// the keys are loaded for this algorithm when the cerver starts:
// a secret of at least the size of the hash for HS256 / HS384 / HS512,
// an RSA key of at least 2048 bits for RS256 / RS384 / RS512,
// and a key of P-256 / P-384 / P-521 for ES256 / ES384 / ES512
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, const jwt_alg_t jwt_alg
);

// sets the filename from where the jwt private key will be loaded
CERVER_EXPORT void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
);

// sets the filename from where the jwt public key will be loaded
CERVER_EXPORT void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
);

// sets the longest bearer token that is decoded,
// a longer one is rejected without decoding it
// the default value is HTTP_CERVER_DEFAULT_JWT_MAX_SIZE, 0 has no limit
CERVER_EXPORT void http_cerver_auth_set_jwt_max_size (
	HttpCerver *http_cerver, const size_t max_size
);

// sets if a token without "exp" is rejected
// the default value is HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION
CERVER_EXPORT void http_cerver_auth_set_jwt_require_expiration (
	HttpCerver *http_cerver, const bool require
);

// sets the seconds of clock skew that are accepted in "exp" and "nbf"
// the default value is HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS
CERVER_EXPORT void http_cerver_auth_set_jwt_leeway_seconds (
	HttpCerver *http_cerver, const unsigned int leeway_seconds
);

// sets the "iss" that every token must have, NULL accepts any
CERVER_EXPORT void http_cerver_auth_set_jwt_issuer (
	HttpCerver *http_cerver, const char *issuer
);

// sets the "aud" that every token must have, as its value or in its array,
// NULL accepts any
CERVER_EXPORT void http_cerver_auth_set_jwt_audience (
	HttpCerver *http_cerver, const char *audience
);

CERVER_PRIVATE unsigned int http_jwt_init_pool (void);

CERVER_EXPORT HttpJwt *http_cerver_auth_jwt_new (void);

CERVER_EXPORT void http_cerver_auth_jwt_delete (HttpJwt *http_jwt);

// a text value of up to HTTP_JWT_VALUE_VALUE_SIZE - 1 bytes,
// a longer one is cut at a complete character of UTF-8 (with a warning in the log)
CERVER_EXPORT void http_cerver_auth_jwt_add_value (
	HttpJwt *http_jwt,
	const char *key, const char *value
);

CERVER_EXPORT void http_cerver_auth_jwt_add_value_bool (
	HttpJwt *http_jwt,
	const char *key, const bool value
);

// an integer value, 64 bits so a time after 2038 fits
CERVER_EXPORT void http_cerver_auth_jwt_add_value_int (
	HttpJwt *http_jwt,
	const char *key, const int64_t value
);

CERVER_PRIVATE char *http_cerver_auth_generate_jwt_actual (
	HttpJwt *http_jwt, const struct JwtKey *key
);

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
CERVER_EXPORT char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
);

CERVER_PRIVATE u8 http_cerver_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt, const struct JwtKey *key
);

// generates and signs a bearer jwt that is ready to be used
// a bearer that does not fit in HTTP_JWT_BEARER_SIZE is an error
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt (
	HttpCerver *http_cerver, HttpJwt *http_jwt
);

// generates and signs a bearer jwt
// and places it inside a json packet
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json (
	HttpCerver *http_cerver, HttpJwt *http_jwt
);

// works as http_cerver_auth_generate_bearer_jwt_json ()
// but with the ability to add an extra string value to
// the generated json, the key and the value are escaped
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json_with_value (
	HttpCerver *http_cerver, HttpJwt *http_jwt,
	const char *key, const char *value
);

// decodes the token with the public key of the cerver and checks its claims:
// the algorithm of the cerver, "exp", "nbf", "iss" and "aud" with its settings
// returns the token, deleted with jwt_free (), or NULL if it is not valid
CERVER_PRIVATE struct jwt *http_cerver_auth_jwt_verify (
	const HttpCerver *http_cerver,
	const char *token, const size_t token_length
);

// the token of a bearer header: "Bearer <token>"
// returns the start of the token, or NULL if the value is not a bearer
CERVER_PRIVATE const char *http_cerver_auth_bearer_token (
	const char *bearer, const size_t bearer_length
);

// returns TRUE if the jwt has been decoded and validate successfully
// returns FALSE if token is NOT valid or if an error has occurred
// option to pass the method to be used to create a decoded data
// that will be placed in the decoded data argument
CERVER_EXPORT bool http_cerver_auth_validate_jwt (
	HttpCerver *http_cerver, const char *bearer_token,
	void *(*decode_data)(void *), void **decoded_data
);

CERVER_PRIVATE void *http_decode_data_into_json (void *json_ptr);

CERVER_PRIVATE void http_jwt_end_pool (void);

#ifdef __cplusplus
}
#endif

#endif
