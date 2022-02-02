#ifndef _CERVER_HTTP_AUTH_H_
#define _CERVER_HTTP_AUTH_H_

#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/http.h"

#define HTTP_JWT_POOL_INIT			32

#define HTTP_JWT_VALUE_KEY_SIZE		128
#define HTTP_JWT_VALUE_VALUE_SIZE	128

#define HTTP_JWT_VALUES_SIZE		16
#define HTTP_JWT_BEARER_SIZE		2048
#define HTTP_JWT_TOKEN_SIZE			4096

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HttpJwtValue {

	char key[HTTP_JWT_VALUE_KEY_SIZE];

	cerver_type_t type;

	union {
		bool value_bool;
		int value_int;
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
// the default value is JWT_ALG_HS256
CERVER_EXPORT void http_cerver_auth_set_jwt_algorithm (
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

CERVER_PRIVATE unsigned int http_jwt_init_pool (void);

CERVER_EXPORT HttpJwt *http_cerver_auth_jwt_new (void);

CERVER_EXPORT void http_cerver_auth_jwt_delete (HttpJwt *http_jwt);

CERVER_EXPORT void http_cerver_auth_jwt_add_value (
	HttpJwt *http_jwt,
	const char *key, const char *value
);

CERVER_EXPORT void http_cerver_auth_jwt_add_value_bool (
	HttpJwt *http_jwt,
	const char *key, const bool value
);

CERVER_EXPORT void http_cerver_auth_jwt_add_value_int (
	HttpJwt *http_jwt,
	const char *key, const int value
);

CERVER_PRIVATE char *http_cerver_auth_generate_jwt_actual (
	HttpJwt *http_jwt, const jwt_alg_t alg,
	const unsigned char *key, const int keylen
);

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
CERVER_EXPORT char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
);

CERVER_PRIVATE u8 http_cerver_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt, const jwt_alg_t alg,
	const unsigned char *key, const int keylen
);

// generates and signs a bearer jwt that is ready to be used
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
// the generated json
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json_with_value (
	HttpCerver *http_cerver, HttpJwt *http_jwt,
	const char *key, const char *value
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