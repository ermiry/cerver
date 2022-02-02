#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <openssl/sha.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/pool.h"

#include "cerver/http/auth.h"
#include "cerver/http/http.h"

#include "cerver/http/json/json.h"

#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static Pool *http_jwt_pool = NULL;

void *http_jwt_new (void) {

	HttpJwt *http_jwt = (HttpJwt *) malloc (sizeof (HttpJwt));
	if (http_jwt) {
		(void) memset (http_jwt, 0, sizeof (HttpJwt));

		http_jwt->jwt = NULL;
	}

	return http_jwt;

}

void http_jwt_delete (void *http_jwt_ptr) {

	if (http_jwt_ptr) {
		HttpJwt *http_jwt = (HttpJwt *) http_jwt_ptr;

		if (http_jwt->jwt) jwt_free (http_jwt->jwt);

		free (http_jwt_ptr);
	}

}

const char *http_jwt_get_bearer (const HttpJwt *http_jwt) {

	return http_jwt->bearer;

}

const size_t http_jwt_get_bearer_len (const HttpJwt *http_jwt) {

	return http_jwt->bearer_len;

}

const char *http_jwt_get_json (const HttpJwt *http_jwt) {

	return http_jwt->json;

}

const size_t http_jwt_get_json_len (const HttpJwt *http_jwt) {

	return http_jwt->json_len;

}

unsigned int http_jwt_init_pool (void) {

	unsigned int retval = 1;

	http_jwt_pool = pool_create (http_jwt_delete);
	if (http_jwt_pool) {
		pool_set_create (http_jwt_pool, http_jwt_new);
		pool_set_produce_if_empty (http_jwt_pool, true);
		if (!pool_init (http_jwt_pool, http_jwt_new, HTTP_JWT_POOL_INIT)) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to init HTTP JWT pool!");
		}
	}

	else {
		cerver_log_error ("Failed to create HTTP JWT pool!");
	}

	return retval;

}

HttpJwt *http_cerver_auth_jwt_new (void) {

	return (HttpJwt *) pool_pop (http_jwt_pool);

}

void http_cerver_auth_jwt_delete (HttpJwt *http_jwt) {

	if (http_jwt) {
		if (http_jwt->jwt) jwt_free (http_jwt->jwt);

		(void) memset (http_jwt, 0, sizeof (HttpJwt));

		http_jwt->jwt = NULL;

		(void) pool_push (http_jwt_pool, http_jwt);
	}

}

static inline u8 http_cerver_auth_jwt_add_value_internal (
	HttpJwt *http_jwt, const char *key
) {

	u8 retval = 1;

	if (http_jwt) {
		if (http_jwt->n_values < HTTP_JWT_VALUES_SIZE) {
			(void) strncpy (
				http_jwt->values[http_jwt->n_values].key,
				key,
				HTTP_JWT_VALUE_KEY_SIZE - 1
			);

			http_jwt->n_values += 1;

			retval = 0;
		}
	}

	return retval;

}

void http_cerver_auth_jwt_add_value (
	HttpJwt *http_jwt,
	const char *key, const char *value
) {

	if (!http_cerver_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_UTF8;
		(void) strncpy (
			http_jwt->values[http_jwt->n_values - 1].value_str,
			value,
			HTTP_JWT_VALUE_VALUE_SIZE - 1
		);
	}

}

void http_cerver_auth_jwt_add_value_bool (
	HttpJwt *http_jwt,
	const char *key, const bool value
) {

	if (!http_cerver_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_BOOL;
		http_jwt->values[http_jwt->n_values - 1].value_bool = value;
	}

}

void http_cerver_auth_jwt_add_value_int (
	HttpJwt *http_jwt,
	const char *key, const int value
) {

	if (!http_cerver_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_INT32;
		http_jwt->values[http_jwt->n_values - 1].value_int = value;
	}

}

char *http_cerver_auth_generate_jwt_actual (
	HttpJwt *http_jwt, const jwt_alg_t alg,
	const unsigned char *key, const int keylen
) {

	char *token = NULL;

	if (!jwt_new (&http_jwt->jwt)) {
		// add grants
		for (u8 i = 0; i < http_jwt->n_values; i++) {
			switch (http_jwt->values[i].type) {
				case CERVER_TYPE_UTF8: {
					(void) jwt_add_grant (
						http_jwt->jwt,
						http_jwt->values[i].key,
						http_jwt->values[i].value_str
					);
				} break;

				case CERVER_TYPE_BOOL: {
					(void) jwt_add_grant_bool (
						http_jwt->jwt,
						http_jwt->values[i].key,
						http_jwt->values[i].value_bool
					);
				} break;

				case CERVER_TYPE_INT32: {
					(void) jwt_add_grant_int (
						http_jwt->jwt,
						http_jwt->values[i].key,
						http_jwt->values[i].value_int
					);
				} break;

				default: break;
			}
		}

		// sign
		if (!jwt_set_alg (
			http_jwt->jwt,
			alg, key, keylen
		)) {
			token = jwt_encode_str (http_jwt->jwt);
		}
	}

	return token;

}

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	char *token = NULL;

	if (http_cerver && http_jwt) {
		token = http_cerver_auth_generate_jwt_actual (
			http_jwt,
			http_cerver->jwt_alg,
			(const unsigned char *) http_cerver->jwt_private_key->str,
			http_cerver->jwt_private_key->len
		);
	}

	return token;

}

u8 http_cerver_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt, const jwt_alg_t alg,
	const unsigned char *key, const int keylen
) {

	u8 retval = 1;

	char *token = http_cerver_auth_generate_jwt_actual (
		http_jwt,
		alg, key, keylen
	);

	if (token) {
		http_jwt->bearer_len = snprintf (
			http_jwt->bearer,
			HTTP_JWT_BEARER_SIZE -1,
			"Bearer %s",
			token
		);

		free (token);

		retval = 0;
	}

	return retval;

}

// generates and signs a bearer jwt that is ready to be used
// returns 0 on success, 1 on error
u8 http_cerver_auth_generate_bearer_jwt (
	HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	return (http_cerver && http_jwt) ?
		http_cerver_auth_generate_bearer_jwt_actual (
			http_jwt,
			http_cerver->jwt_alg,
			(const unsigned char *) http_cerver->jwt_private_key->str,
			http_cerver->jwt_private_key->len
		) : 1;

}

// generates and signs a bearer jwt
// and places it inside a json packet
// returns 0 on success, 1 on error
u8 http_cerver_auth_generate_bearer_jwt_json (
	HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	u8 retval = 1;

	if (!http_cerver_auth_generate_bearer_jwt (
		http_cerver, http_jwt
	)) {
		http_jwt->json_len = snprintf (
			http_jwt->json,
			HTTP_JWT_TOKEN_SIZE -1,
			"{\"token\": \"%s\"}",
			http_jwt->bearer
		);

		retval = 0;
	}

	return retval;

}

// works as http_cerver_auth_generate_bearer_jwt_json ()
// but with the ability to add an extra string value to
// the generated json
// returns 0 on success, 1 on error
u8 http_cerver_auth_generate_bearer_jwt_json_with_value (
	HttpCerver *http_cerver, HttpJwt *http_jwt,
	const char *key, const char *value
) {

	u8 retval = 1;

	if (key && value) {
		if (!http_cerver_auth_generate_bearer_jwt (
			http_cerver, http_jwt
		)) {
			(void) snprintf (
				http_jwt->json,
				HTTP_JWT_TOKEN_SIZE -1,
				"{\"token\": \"%s\", \"%s\": \"%s\"}",
				http_jwt->bearer,
				key, value
			);

			retval = 0;
		}
	}

	return retval;

}

// returns TRUE if the jwt has been decoded and validate successfully
// returns FALSE if token is NOT valid or if an error has occurred
// option to pass the method to be used to create a decoded data
// that will be placed in the decoded data argument
bool http_cerver_auth_validate_jwt (
	HttpCerver *http_cerver, const char *bearer_token,
	void *(*decode_data)(void *), void **decoded_data
) {

	bool retval = false;

	if (http_cerver && bearer_token) {
		char *token = (char *) bearer_token;
		token += sizeof ("Bearer");

		jwt_t *jwt = NULL;
		jwt_valid_t *jwt_valid = NULL;
		if (!jwt_valid_new (&jwt_valid, http_cerver->jwt_alg)) {
			jwt_valid->hdr = 1;
			jwt_valid->now = time (NULL);

			if (!jwt_decode (
				&jwt, token,
				(unsigned char *) http_cerver->jwt_public_key->str,
				http_cerver->jwt_public_key->len
			)) {
				#ifdef HTTP_AUTH_DEBUG
				cerver_log_debug ("JWT decoded successfully!");
				#endif

				if (!jwt_validate (jwt, jwt_valid)) {
					#ifdef HTTP_AUTH_DEBUG
					cerver_log_success ("JWT is authentic!");
					#endif

					if (decode_data) {
						*decoded_data = decode_data (jwt->grants);
					}

					retval = true;
				}

				#ifdef HTTP_AUTH_DEBUG
				else {
					cerver_log_error (
						"Failed to validate JWT: %08x",
						jwt_valid_get_status(jwt_valid)
					);
				}
				#endif

				jwt_free (jwt);
			}

			#ifdef HTTP_AUTH_DEBUG
			else {
				cerver_log_error ("Invalid JWT!");
			}
			#endif
		}

		jwt_valid_free (jwt_valid);
	}

	return retval;

}

void *http_decode_data_into_json (void *json_ptr) {

	return json_dumps ((json_t *) json_ptr, 0);

}

void http_jwt_end_pool (void) {

	pool_delete (http_jwt_pool);
	http_jwt_pool = NULL;

}
