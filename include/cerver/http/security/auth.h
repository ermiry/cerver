/**
 * @file cerver/http/security/auth.h
 * @brief The JWT of an HttpCerver: its keys and its settings, how a token is generated and how a bearer is
 * validated.
 */

#ifndef _CERVER_HTTP_AUTH_H_
#define _CERVER_HTTP_AUTH_H_

#include <stddef.h>
#include <stdint.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#include "cerver/jwt/jwt.h"

/**
 * @defgroup http_auth Authentication
 * @ingroup http_security
 * @brief The tokens that an HttpCerver signs and verifies.
 * @{
 */

/** @brief The initial size of the pool of the HttpJwt that every HTTP cerver of the process shares. */
#define HTTP_JWT_POOL_INIT			32

/** @brief The size of the key of a value of a token, with its terminator. */
#define HTTP_JWT_VALUE_KEY_SIZE		128
/** @brief The size of a text value of a token, with its terminator. */
#define HTTP_JWT_VALUE_VALUE_SIZE	128

/** @brief The most values of a token. */
#define HTTP_JWT_VALUES_SIZE		16

/** @brief The size of "Bearer " and a token of up to #HTTP_CERVER_DEFAULT_JWT_MAX_SIZE. */
#define HTTP_JWT_BEARER_SIZE		(1 << 13)

/** @brief The size of the json with the bearer and an extra value. */
#define HTTP_JWT_TOKEN_SIZE			(1 << 14)

/**
 * @brief The most levels of nested arrays and objects of the claims of a token; one with more is not valid.
 *
 * @since 3.0b-7
 */
#define HTTP_JWT_CLAIMS_DEPTH		CERVER_JWT_CLAIMS_DEPTH

/**
 * @brief The bytes of the buffer where the claims of an HttpJwt are written: its #HTTP_JWT_VALUES_SIZE values with
 * every character of their keys and of their texts as its longest escape.
 *
 * @since 3.0b-8
 */
#define HTTP_JWT_CLAIMS_SIZE		(1 << 15)

/** @brief The start of the value of an Authorization header with a token. */
#define HTTP_JWT_BEARER_PREFIX			"Bearer "
/** @brief The length of #HTTP_JWT_BEARER_PREFIX. */
#define HTTP_JWT_BEARER_PREFIX_LENGTH	(sizeof (HTTP_JWT_BEARER_PREFIX) - 1)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A value of a token: a text, a bool or an integer with its key.
 */
typedef struct HttpJwtValue {

	/** @brief The key of the value. */
	char key[HTTP_JWT_VALUE_KEY_SIZE];

	/** @brief CERVER_TYPE_UTF8, CERVER_TYPE_BOOL or CERVER_TYPE_INT64. */
	cerver_type_t type;

	/** @brief The value, by its type. */
	union {
		/** @brief A bool. */
		bool value_bool;
		/** @brief An integer of 64 bits. */
		int64_t value_int;
		/** @brief A text. */
		char value_str[HTTP_JWT_VALUE_VALUE_SIZE];
	};

} HttpJwtValue;

/**
 * @brief A token that is being generated: its values, its bearer and its json.
 */
typedef struct HttpJwt {

	/** @brief The values of #values. */
	u8 n_values;
	/** @brief The values of the token. */
	HttpJwtValue values[HTTP_JWT_VALUES_SIZE];

	/** @brief The length of #bearer. */
	size_t bearer_len;
	/** @brief The generated bearer: "Bearer <token>". */
	char bearer[HTTP_JWT_BEARER_SIZE];
	/** @brief The length of #json. */
	size_t json_len;
	/** @brief The generated json with the bearer. */
	char json[HTTP_JWT_TOKEN_SIZE];

} HttpJwt;

/**
 * @brief Allocates an empty HttpJwt.
 *
 * @return the new HttpJwt, NULL if it could not be allocated
 * @note For the internal methods of cerver: the application takes one with http_cerver_auth_jwt_new ().
 */
CERVER_PRIVATE void *http_jwt_new (void);

/**
 * @brief Releases an HttpJwt.
 *
 * @param[in] http_jwt_ptr the HttpJwt, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_jwt_delete (void *http_jwt_ptr);

/**
 * @brief Returns the bearer of an HttpJwt.
 *
 * @param[in] http_jwt the HttpJwt, it must not be NULL
 * @return the bearer, empty until it is generated
 */
CERVER_EXPORT const char *http_jwt_get_bearer (
	const HttpJwt *http_jwt
);

/**
 * @brief Returns the length of the bearer of an HttpJwt.
 *
 * @param[in] http_jwt the HttpJwt, it must not be NULL
 * @return the length
 */
CERVER_EXPORT const size_t http_jwt_get_bearer_len (
	const HttpJwt *http_jwt
);

/**
 * @brief Returns the json of an HttpJwt.
 *
 * @param[in] http_jwt the HttpJwt, it must not be NULL
 * @return the json, empty until it is generated
 */
CERVER_EXPORT const char *http_jwt_get_json (
	const HttpJwt *http_jwt
);

/**
 * @brief Returns the length of the json of an HttpJwt.
 *
 * @param[in] http_jwt the HttpJwt, it must not be NULL
 * @return the length
 */
CERVER_EXPORT const size_t http_jwt_get_json_len (
	const HttpJwt *http_jwt
);

/**
 * @brief Loads a key from a file.
 *
 * @param[in] filename the file
 * @param[out] keylen the length of the key
 * @return a new string with the key, released with free (); NULL for a NULL argument or if the file could not be
 * read
 */
CERVER_PUBLIC char *http_cerver_auth_load_key (
	const char *filename, size_t *keylen
);

/**
 * @brief Sets the algorithm that signs and verifies the tokens.
 *
 * The keys are loaded for it when the cerver starts (cerver/jwt/key.h): a secret of at least the size of the
 * digest for HS256 / HS384 / HS512, a key of RSA of at least 2048 bits for RS256 / RS384 / RS512 and PS256 / PS384 /
 * PS512, a key of P-256 / P-384 / P-521 for ES256 / ES384 / ES512, and a key of Ed25519 or of Ed448 for EdDSA.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] algorithm the algorithm, the default is CERVER_JWT_ALGORITHM_HS256
 * @retval 0 the algorithm was set
 * @retval 1 a NULL HttpCerver, CERVER_JWT_ALGORITHM_NONE or a value out of the table
 * @since 3.0b-8 it takes a CerverJwtAlgorithm, it took the jwt_alg_t of libjwt (the same numbers).
 */
CERVER_EXPORT u8 http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, const CerverJwtAlgorithm algorithm
);

/**
 * @brief Sets the file of the private key, that signs the tokens; it is loaded when the cerver starts.
 *
 * The file is a JWK when its first character that is not a space is `{` (RFC 7517); if not, it is the bytes of
 * the secret for HS256 / HS384 / HS512, as they are, and a PEM without a passphrase for the other algorithms.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] filename the file, copied; NULL removes it
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
);

/**
 * @brief Sets the file of the public key, that verifies the tokens; it is loaded when the cerver starts.
 *
 * The file is read as the one of the private key (http_cerver_auth_set_jwt_priv_key_filename ()): for HS256 /
 * HS384 / HS512 it is the same secret, and for the other algorithms the PEM or the JWK of the public key.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] filename the file, copied; NULL removes it
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
);

/**
 * @brief Sets the longest bearer token that is decoded; a longer one is rejected without decoding it.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] max_size the bytes, 0 is the limit of every token (#CERVER_JWT_SIZE_MAX), that is also the most; the
 * default is #HTTP_CERVER_DEFAULT_JWT_MAX_SIZE
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_max_size (
	HttpCerver *http_cerver, const size_t max_size
);

/**
 * @brief Sets whether a token without "exp" is rejected.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] require true to reject it, the default is #HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_require_expiration (
	HttpCerver *http_cerver, const bool require
);

/**
 * @brief Sets the seconds of clock skew that are accepted in "exp", "nbf" and "iat".
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] leeway_seconds the seconds, the default is #HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_leeway_seconds (
	HttpCerver *http_cerver, const unsigned int leeway_seconds
);

/**
 * @brief Sets the "iss" that every token must have.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] issuer the issuer, copied; NULL accepts any
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_issuer (
	HttpCerver *http_cerver, const char *issuer
);

/**
 * @brief Sets the "sub" that every token must have.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] subject the subject, copied; NULL accepts any
 * @since 3.0b-8
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_subject (
	HttpCerver *http_cerver, const char *subject
);

/**
 * @brief Sets the "aud" that every token must have, as its value or in its array.
 *
 * @param[in,out] http_cerver the HttpCerver, NULL is ignored
 * @param[in] audience the audience, copied; NULL accepts any
 */
CERVER_EXPORT void http_cerver_auth_set_jwt_audience (
	HttpCerver *http_cerver, const char *audience
);

/**
 * @brief Creates the pool of the HttpJwt, with #HTTP_JWT_POOL_INIT of them.
 *
 * @retval 0 the pool is ready
 * @retval 1 it could not be created
 * @note For the internal methods of cerver: the first HTTP cerver that starts creates it.
 */
CERVER_PRIVATE unsigned int http_jwt_init_pool (void);

/**
 * @brief Takes an empty HttpJwt from the pool, to generate a token.
 *
 * @return the HttpJwt, given back with http_cerver_auth_jwt_delete (); NULL before the first HTTP cerver starts
 */
CERVER_EXPORT HttpJwt *http_cerver_auth_jwt_new (void);

/**
 * @brief Gives an HttpJwt back to the pool, empty.
 *
 * @param[in] http_jwt the HttpJwt, it can be NULL
 * @note After the last HTTP cerver ended, or when the pool can not take it, the HttpJwt is released.
 */
CERVER_EXPORT void http_cerver_auth_jwt_delete (HttpJwt *http_jwt);

/**
 * @brief Adds a text value to a token.
 *
 * @param[in,out] http_jwt the HttpJwt
 * @param[in] key the key, cut at HTTP_JWT_VALUE_KEY_SIZE - 1 bytes like the value
 * @param[in] value the text, up to HTTP_JWT_VALUE_VALUE_SIZE - 1 bytes: a longer one is cut at a complete character
 * of UTF-8, with a warning in the log
 * @note A NULL argument, or a token that already has #HTTP_JWT_VALUES_SIZE values, is ignored.
 */
CERVER_EXPORT void http_cerver_auth_jwt_add_value (
	HttpJwt *http_jwt,
	const char *key, const char *value
);

/**
 * @brief Adds a bool value to a token.
 *
 * @param[in,out] http_jwt the HttpJwt
 * @param[in] key the key
 * @param[in] value the value
 * @note A NULL argument, or a token that already has #HTTP_JWT_VALUES_SIZE values, is ignored.
 */
CERVER_EXPORT void http_cerver_auth_jwt_add_value_bool (
	HttpJwt *http_jwt,
	const char *key, const bool value
);

/**
 * @brief Adds an integer value to a token, of 64 bits so a time after 2038 fits.
 *
 * @param[in,out] http_jwt the HttpJwt
 * @param[in] key the key
 * @param[in] value the value
 * @note A NULL argument, or a token that already has #HTTP_JWT_VALUES_SIZE values, is ignored.
 */
CERVER_EXPORT void http_cerver_auth_jwt_add_value_int (
	HttpJwt *http_jwt,
	const char *key, const int64_t value
);

/**
 * @brief Generates and signs a token with a key.
 *
 * The claims of the token are its values in the compact JSON of an object, with their keys sorted. A value
 * without a key, a key more than once, and a key or a text that is not UTF-8 are not claims: the token is not
 * generated.
 *
 * @param[in,out] http_jwt the HttpJwt, with its values
 * @param[in] key the key, a secret or a private one
 * @return the token, released with free (); NULL without an HttpJwt or a key that signs, with values that are not
 * claims, or on error
 * @note For the internal methods of cerver.
 * @since 3.0b-8 the key is a CerverJwtKey and the token is released with free ().
 */
CERVER_PRIVATE char *http_cerver_auth_generate_jwt_actual (
	HttpJwt *http_jwt, const CerverJwtKey *key
);

/**
 * @brief Generates and signs a token with the private key of the cerver.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in,out] http_jwt the HttpJwt, with its values
 * @return the token, released with free (); NULL for a NULL argument, a cerver without its private key or on
 * error (see http_cerver_auth_generate_jwt_actual ())
 * @since 3.0b-8 the token is released with free (), it was released with jwt_free_str ().
 */
CERVER_EXPORT char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
);

/**
 * @brief Generates and signs a token with a key, and keeps it as the bearer of the HttpJwt.
 *
 * @param[in,out] http_jwt the HttpJwt, with its values
 * @param[in] key the key
 * @retval 0 the bearer was generated
 * @retval 1 the token could not be generated or does not fit in #HTTP_JWT_BEARER_SIZE; the bearer is empty
 * @note For the internal methods of cerver.
 * @since 3.0b-8 the key is a CerverJwtKey.
 */
CERVER_PRIVATE u8 http_cerver_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt, const CerverJwtKey *key
);

/**
 * @brief Generates and signs a bearer with the private key of the cerver.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in,out] http_jwt the HttpJwt, with its values; it gets the bearer
 * @retval 0 the bearer was generated
 * @retval 1 a NULL argument, the token could not be generated or it does not fit in #HTTP_JWT_BEARER_SIZE
 */
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt (
	HttpCerver *http_cerver, HttpJwt *http_jwt
);

/**
 * @brief Generates and signs a bearer, and places it inside a json.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in,out] http_jwt the HttpJwt, with its values; it gets the bearer and the json
 * @retval 0 the json was generated
 * @retval 1 the bearer or the json could not be generated
 */
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json (
	HttpCerver *http_cerver, HttpJwt *http_jwt
);

/**
 * @brief Works like http_cerver_auth_generate_bearer_jwt_json () with an extra text value in the json; the key and
 * the value are escaped.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in,out] http_jwt the HttpJwt, with its values; it gets the bearer and the json
 * @param[in] key the key of the extra value
 * @param[in] value the extra value
 * @retval 0 the json was generated
 * @retval 1 a NULL key or value, or the bearer or the json could not be generated
 */
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json_with_value (
	HttpCerver *http_cerver, HttpJwt *http_jwt,
	const char *key, const char *value
);

/**
 * @brief Verifies a token with the public key of the cerver (cerver_jwt_verify ()) and validates its claims with
 * its settings: "exp", "nbf" and "iat" with its leeway, and the "iss", the "sub" and the "aud" that it asks for.
 *
 * The claims are read with the JSON of cerver in the arena, only after the signature was verified: an object
 * without a key more than once and without a NUL in its strings, of at most #HTTP_JWT_CLAIMS_DEPTH levels.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in,out] arena the arena where the claims are read
 * @param[in] token the token, without "Bearer "
 * @param[in] token_length its length, within the max size of the cerver
 * @return the claims of the token, an object that lives as long as the arena; NULL if the token is not valid
 * @note For the internal methods of cerver.
 * @since 3.0b-7 it takes the arena and returns the claims, it returned the token of the JWT.
 */
CERVER_PRIVATE const CerverJsonValue *http_cerver_auth_jwt_verify (
	const HttpCerver *http_cerver, struct Arena *arena,
	const char *token, const size_t token_length
);

/**
 * @brief Returns the token of a bearer: "Bearer <token>".
 *
 * @param[in] bearer the value of the Authorization header
 * @param[in] bearer_length its length
 * @return the start of the token, inside the bearer; NULL if the value is not a bearer
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE const char *http_cerver_auth_bearer_token (
	const char *bearer, const size_t bearer_length
);

/**
 * @brief Validates a bearer with the public key and the settings of the cerver.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in] bearer_token the bearer: "Bearer <token>"
 * @param[in] decode_data creates the decoded data from the claims of the token, NULL to skip it
 * @param[out] decoded_data the decoded data, set only when the token is valid and there is a decode method
 * @return true when the token was decoded and its claims are valid, false otherwise
 * @since 3.0b-7 decode_data is a HttpDecodeData: it takes the claims as a value of the JSON of cerver.
 */
CERVER_EXPORT bool http_cerver_auth_validate_jwt (
	HttpCerver *http_cerver, const char *bearer_token,
	HttpDecodeData decode_data, void **decoded_data
);

/**
 * @brief Writes the claims of a token as a json string, with the members in the order of the token.
 *
 * @param[in] claims the claims of the token
 * @return the json, released with free (); NULL for claims that can not be written or without memory
 * @note For the internal methods of cerver: see http_cerver_admin_routes_auth_decode_to_json ().
 * @since 3.0b-7 it takes the claims as a value of the JSON of cerver.
 */
CERVER_PRIVATE void *http_decode_data_into_json (const CerverJsonValue *claims);

/**
 * @brief Releases the pool of the HttpJwt.
 *
 * @note For the internal methods of cerver: the last HTTP cerver that ends releases it.
 */
CERVER_PRIVATE void http_jwt_end_pool (void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
