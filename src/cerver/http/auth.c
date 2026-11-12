#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/pool.h"

#include "cerver/http/auth.h"
#include "cerver/http/http.h"

#include "cerver/http/json/json.h"

#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the registered claims that the cerver checks (RFC 7519 §4.1)
#define HTTP_JWT_CLAIM_EXPIRATION			"exp"
#define HTTP_JWT_CLAIM_NOT_BEFORE			"nbf"
#define HTTP_JWT_CLAIM_ISSUER				"iss"
#define HTTP_JWT_CLAIM_AUDIENCE				"aud"

// the key of the token in the json of a generated jwt
#define HTTP_JWT_JSON_TOKEN_KEY				"token"

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

#pragma region values

// the bytes that continue a character of UTF-8 are 10xxxxxx
#define HTTP_JWT_UTF8_CONTINUATION_MASK		0xC0
#define HTTP_JWT_UTF8_CONTINUATION			0x80

static bool http_cerver_auth_jwt_is_continuation (const char byte) {

	return (((unsigned char) byte) & HTTP_JWT_UTF8_CONTINUATION_MASK) == HTTP_JWT_UTF8_CONTINUATION;

}

// a text that does not fit is cut at a complete character of UTF-8:
// cut in the middle of one, the json of the token rejected it
// and the jwt was generated without that value
static void http_cerver_auth_jwt_value_copy (
	char *destination, const size_t size, const char *text, const char *key
) {

	size_t length = strlen (text);
	if (length >= size) {
		length = size - 1;
		while (length && http_cerver_auth_jwt_is_continuation (text[length])) length -= 1;

		cerver_log_warning (
			"The JWT value of %s has %zu bytes, it was cut to %zu",
			key, strlen (text), length
		);
	}

	(void) memcpy (destination, text, length);
	destination[length] = '\0';

}

static inline u8 http_cerver_auth_jwt_add_value_internal (
	HttpJwt *http_jwt, const char *key
) {

	u8 retval = 1;

	if (http_jwt && key) {
		if (http_jwt->n_values < HTTP_JWT_VALUES_SIZE) {
			http_cerver_auth_jwt_value_copy (
				http_jwt->values[http_jwt->n_values].key,
				HTTP_JWT_VALUE_KEY_SIZE,
				key, key
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

	if (value && !http_cerver_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_UTF8;
		http_cerver_auth_jwt_value_copy (
			http_jwt->values[http_jwt->n_values - 1].value_str,
			HTTP_JWT_VALUE_VALUE_SIZE,
			value, key
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
	const char *key, const int64_t value
) {

	if (!http_cerver_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_INT64;
		http_jwt->values[http_jwt->n_values - 1].value_int = value;
	}

}

#pragma endregion

#pragma region generate

static int http_cerver_auth_jwt_add_grant (
	jwt_t *jwt, const HttpJwtValue *value
) {

	int retval = 1;

	switch (value->type) {
		case CERVER_TYPE_UTF8:
			retval = jwt_add_grant (jwt, value->key, value->value_str);
			break;

		case CERVER_TYPE_BOOL:
			retval = jwt_add_grant_bool (jwt, value->key, value->value_bool);
			break;

		case CERVER_TYPE_INT64:
			retval = jwt_add_grant_int (jwt, value->key, (long) value->value_int);
			break;

		default: break;
	}

	return retval;

}

// returns 0 if every value is a grant of the jwt
static unsigned int http_cerver_auth_jwt_add_grants (HttpJwt *http_jwt) {

	unsigned int errors = 0;

	for (u8 i = 0; i < http_jwt->n_values; i++) {
		if (http_cerver_auth_jwt_add_grant (http_jwt->jwt, &http_jwt->values[i])) {
			errors |= 1;
		}
	}

	return errors;

}

char *http_cerver_auth_generate_jwt_actual (
	HttpJwt *http_jwt, const JwtKey *key
) {

	char *token = NULL;

	// a jwt that was generated before is replaced
	if (http_jwt->jwt) {
		jwt_free (http_jwt->jwt);
		http_jwt->jwt = NULL;
	}

	if (key && !jwt_new (&http_jwt->jwt)) {
		if (!http_cerver_auth_jwt_add_grants (http_jwt)) {
			token = jwt_encode_with_key (http_jwt->jwt, key);
		}
	}

	return token;

}

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	return (http_cerver && http_jwt) ?
		http_cerver_auth_generate_jwt_actual (http_jwt, http_cerver->jwt_private_key) :
		NULL;

}

static void http_cerver_auth_jwt_bearer_clear (HttpJwt *http_jwt) {

	http_jwt->bearer_len = 0;
	http_jwt->bearer[0] = '\0';

}

// a bearer that does not fit is an error, it is never truncated
static u8 http_cerver_auth_jwt_bearer_set (
	HttpJwt *http_jwt, const char *token
) {

	u8 retval = 1;

	const int length = snprintf (
		http_jwt->bearer, HTTP_JWT_BEARER_SIZE,
		"%s%s", HTTP_JWT_BEARER_PREFIX, token
	);

	if ((length > 0) && ((size_t) length < HTTP_JWT_BEARER_SIZE)) {
		http_jwt->bearer_len = (size_t) length;
		retval = 0;
	}

	else {
		http_cerver_auth_jwt_bearer_clear (http_jwt);
	}

	return retval;

}

u8 http_cerver_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt, const JwtKey *key
) {

	u8 retval = 1;

	char *token = http_cerver_auth_generate_jwt_actual (http_jwt, key);
	if (token) {
		retval = http_cerver_auth_jwt_bearer_set (http_jwt, token);

		jwt_free_str (token);
	}

	else {
		http_cerver_auth_jwt_bearer_clear (http_jwt);
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
			http_jwt, http_cerver->jwt_private_key
		) : 1;

}

static void http_cerver_auth_jwt_json_clear (HttpJwt *http_jwt) {

	http_jwt->json_len = 0;
	http_jwt->json[0] = '\0';

}

// writes the json in the buffer of the jwt, a json that does not fit is an error
static u8 http_cerver_auth_jwt_json_dump (
	HttpJwt *http_jwt, const json_t *json
) {

	u8 retval = 1;

	// one byte is left for the null
	const size_t length = json_dumpb (json, http_jwt->json, HTTP_JWT_TOKEN_SIZE - 1, 0);
	if (length && (length < HTTP_JWT_TOKEN_SIZE)) {
		http_jwt->json[length] = '\0';
		http_jwt->json_len = length;
		retval = 0;
	}

	return retval;

}

// {"token": "Bearer <jwt>"} and an extra value if the key is set,
// the values are escaped by the json library
static u8 http_cerver_auth_jwt_json_set (
	HttpJwt *http_jwt, const char *key, const char *value
) {

	u8 retval = 1;

	json_t *json = json_object ();
	if (json) {
		if (
			!json_object_set_new (json, HTTP_JWT_JSON_TOKEN_KEY, json_string (http_jwt->bearer))
			&& (!key || !json_object_set_new (json, key, json_string (value)))
		) {
			retval = http_cerver_auth_jwt_json_dump (http_jwt, json);
		}

		json_decref (json);
	}

	if (retval) http_cerver_auth_jwt_json_clear (http_jwt);

	return retval;

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
		retval = http_cerver_auth_jwt_json_set (http_jwt, NULL, NULL);
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
			retval = http_cerver_auth_jwt_json_set (http_jwt, key, value);
		}
	}

	return retval;

}

#pragma endregion

#pragma region verify

typedef enum HttpJwtClaimState {

	HTTP_JWT_CLAIM_STATE_MISSING	= 0,
	HTTP_JWT_CLAIM_STATE_FOUND		= 1,
	HTTP_JWT_CLAIM_STATE_INVALID	= 2

} HttpJwtClaimState;

// a time claim that is in the token must be an integer
static HttpJwtClaimState http_cerver_auth_jwt_time_claim (
	const json_t *grants, const char *name, json_int_t *value
) {

	HttpJwtClaimState state = HTTP_JWT_CLAIM_STATE_MISSING;

	const json_t *claim = json_object_get (grants, name);
	if (claim) {
		if (json_is_integer (claim)) {
			*value = json_integer_value (claim);
			state = HTTP_JWT_CLAIM_STATE_FOUND;
		}

		else {
			state = HTTP_JWT_CLAIM_STATE_INVALID;
		}
	}

	return state;

}

// the token is valid while now is before "exp" plus the leeway
static bool http_cerver_auth_jwt_check_expiration (
	const HttpCerver *http_cerver, const json_t *grants, const json_int_t now
) {

	bool valid = false;

	json_int_t expiration = 0;
	switch (http_cerver_auth_jwt_time_claim (grants, HTTP_JWT_CLAIM_EXPIRATION, &expiration)) {
		case HTTP_JWT_CLAIM_STATE_MISSING:
			valid = !http_cerver->jwt_require_expiration;
			break;

		// the leeway is taken from now, so a large "exp" never overflows
		case HTTP_JWT_CLAIM_STATE_FOUND:
			valid = (now - (json_int_t) http_cerver->jwt_leeway_seconds) < expiration;
			break;

		default: break;
	}

	return valid;

}

// the token is valid from "nbf" minus the leeway
static bool http_cerver_auth_jwt_check_not_before (
	const HttpCerver *http_cerver, const json_t *grants, const json_int_t now
) {

	bool valid = false;

	json_int_t not_before = 0;
	switch (http_cerver_auth_jwt_time_claim (grants, HTTP_JWT_CLAIM_NOT_BEFORE, &not_before)) {
		case HTTP_JWT_CLAIM_STATE_MISSING: valid = true; break;

		case HTTP_JWT_CLAIM_STATE_FOUND:
			valid = (now + (json_int_t) http_cerver->jwt_leeway_seconds) >= not_before;
			break;

		default: break;
	}

	return valid;

}

static bool http_cerver_auth_jwt_string_equals (
	const json_t *value, const String *expected
) {

	return json_is_string (value)
		&& (json_string_length (value) == expected->len)
		&& !memcmp (json_string_value (value), expected->str, expected->len);

}

static bool http_cerver_auth_jwt_check_issuer (
	const HttpCerver *http_cerver, const json_t *grants
) {

	return !http_cerver->jwt_issuer || http_cerver_auth_jwt_string_equals (
		json_object_get (grants, HTTP_JWT_CLAIM_ISSUER), http_cerver->jwt_issuer
	);

}

// "aud" is a string or an array of strings (RFC 7519 §4.1.3)
static bool http_cerver_auth_jwt_check_audience (
	const HttpCerver *http_cerver, const json_t *grants
) {

	if (!http_cerver->jwt_audience) return true;

	bool valid = false;

	const json_t *audience = json_object_get (grants, HTTP_JWT_CLAIM_AUDIENCE);
	if (json_is_array (audience)) {
		for (size_t i = 0; (i < json_array_size (audience)) && !valid; i++) {
			valid = http_cerver_auth_jwt_string_equals (
				json_array_get (audience, i), http_cerver->jwt_audience
			);
		}
	}

	else {
		valid = http_cerver_auth_jwt_string_equals (audience, http_cerver->jwt_audience);
	}

	return valid;

}

// the claims that the header repeats must be the same ones of the payload
static bool http_cerver_auth_jwt_check_headers (
	const HttpCerver *http_cerver, jwt_t *jwt
) {

	bool valid = false;

	jwt_valid_t *jwt_valid = NULL;
	if (!jwt_valid_new (&jwt_valid, http_cerver->jwt_alg)) {
		jwt_valid->headers = 1;

		valid = (jwt_validate (jwt, jwt_valid) == JWT_VALIDATION_SUCCESS);

		jwt_valid_free (jwt_valid);
	}

	return valid;

}

static bool http_cerver_auth_jwt_check_claims (
	const HttpCerver *http_cerver, jwt_t *jwt
) {

	const json_int_t now = (json_int_t) time (NULL);

	return http_cerver_auth_jwt_check_expiration (http_cerver, jwt->grants, now)
		&& http_cerver_auth_jwt_check_not_before (http_cerver, jwt->grants, now)
		&& http_cerver_auth_jwt_check_issuer (http_cerver, jwt->grants)
		&& http_cerver_auth_jwt_check_audience (http_cerver, jwt->grants)
		&& http_cerver_auth_jwt_check_headers (http_cerver, jwt);

}

static bool http_cerver_auth_jwt_check_size (
	const HttpCerver *http_cerver, const size_t token_length
) {

	return !http_cerver->jwt_max_size || (token_length <= http_cerver->jwt_max_size);

}

// decodes the token with the public key of the cerver and checks its claims:
// the algorithm of the cerver, "exp", "nbf", "iss" and "aud" with its settings
// returns the token, deleted with jwt_free (), or NULL if it is not valid
jwt_t *http_cerver_auth_jwt_verify (
	const HttpCerver *http_cerver,
	const char *token, const size_t token_length
) {

	jwt_t *jwt = NULL;

	if (
		http_cerver && token && http_cerver->jwt_public_key
		&& http_cerver_auth_jwt_check_size (http_cerver, token_length)
	) {
		if (!jwt_decode_with_key (&jwt, token, token_length, http_cerver->jwt_public_key)) {
			if (!http_cerver_auth_jwt_check_claims (http_cerver, jwt)) {
				#ifdef HTTP_AUTH_DEBUG
				cerver_log_error ("The claims of the JWT are not valid!");
				#endif

				jwt_free (jwt);
				jwt = NULL;
			}
		}

		#ifdef HTTP_AUTH_DEBUG
		else {
			cerver_log_error ("Invalid JWT!");
		}
		#endif
	}

	return jwt;

}

// the token of a bearer header: "Bearer <token>"
// returns the start of the token, or NULL if the value is not a bearer
const char *http_cerver_auth_bearer_token (
	const char *bearer, const size_t bearer_length
) {

	return (
		bearer
		&& (bearer_length > HTTP_JWT_BEARER_PREFIX_LENGTH)
		&& !strncmp (bearer, HTTP_JWT_BEARER_PREFIX, HTTP_JWT_BEARER_PREFIX_LENGTH)
	) ? bearer + HTTP_JWT_BEARER_PREFIX_LENGTH : NULL;

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

	if (!bearer_token) return retval;

	const size_t bearer_length = strlen (bearer_token);
	const char *token = http_cerver_auth_bearer_token (bearer_token, bearer_length);
	if (token) {
		jwt_t *jwt = http_cerver_auth_jwt_verify (
			http_cerver, token, bearer_length - HTTP_JWT_BEARER_PREFIX_LENGTH
		);

		if (jwt) {
			if (decode_data && decoded_data) {
				*decoded_data = decode_data (jwt->grants);
			}

			jwt_free (jwt);

			retval = true;
		}
	}

	return retval;

}

#pragma endregion

void *http_decode_data_into_json (void *json_ptr) {

	return json_dumps ((json_t *) json_ptr, 0);

}

void http_jwt_end_pool (void) {

	pool_delete (http_jwt_pool);
	http_jwt_pool = NULL;

}
