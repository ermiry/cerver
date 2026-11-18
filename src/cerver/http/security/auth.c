#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/files/files.h"

#include "cerver/http/core/http.h"
#include "cerver/http/core/types.h"
#include "cerver/http/security/auth.h"

#include "cerver/json/json.h"
#include "cerver/json/value.h"
#include "cerver/json/writer.h"

#include "cerver/jwt/jwt.h"
#include "cerver/jwt/key.h"
#include "cerver/jwt/sign.h"
#include "cerver/jwt/verify.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the key of the token in the json of a generated jwt
#define HTTP_JWT_JSON_TOKEN_KEY				"token"

static Pool *cerver_http_jwt_pool = NULL;

void *cerver_http_jwt_new (void) {

	HttpJwt *http_jwt = (HttpJwt *) malloc (sizeof (HttpJwt));
	if (http_jwt) (void) memset (http_jwt, 0, sizeof (HttpJwt));

	return http_jwt;

}

void cerver_http_jwt_delete (void *http_jwt_ptr) {

	free (http_jwt_ptr);

}

const char *cerver_http_jwt_get_bearer (const HttpJwt *http_jwt) {

	return http_jwt->bearer;

}

const size_t cerver_http_jwt_get_bearer_len (const HttpJwt *http_jwt) {

	return http_jwt->bearer_len;

}

const char *cerver_http_jwt_get_json (const HttpJwt *http_jwt) {

	return http_jwt->json;

}

const size_t cerver_http_jwt_get_json_len (const HttpJwt *http_jwt) {

	return http_jwt->json_len;

}

unsigned int cerver_http_jwt_init_pool (void) {

	unsigned int retval = 1;

	cerver_http_jwt_pool = pool_create (cerver_http_jwt_delete);
	if (cerver_http_jwt_pool) {
		pool_set_create (cerver_http_jwt_pool, cerver_http_jwt_new);
		pool_set_produce_if_empty (cerver_http_jwt_pool, true);
		if (!pool_init (cerver_http_jwt_pool, cerver_http_jwt_new, HTTP_JWT_POOL_INIT)) {
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

HttpJwt *cerver_http_auth_jwt_new (void) {

	return (HttpJwt *) pool_pop (cerver_http_jwt_pool);

}

void cerver_http_auth_jwt_delete (HttpJwt *http_jwt) {

	if (http_jwt) {
		(void) memset (http_jwt, 0, sizeof (HttpJwt));

		// an HttpJwt that the pool does not take is released (it was lost)
		if (pool_push (cerver_http_jwt_pool, http_jwt)) cerver_http_jwt_delete (http_jwt);
	}

}

#pragma region values

// the bytes that continue a character of UTF-8 are 10xxxxxx
#define HTTP_JWT_UTF8_CONTINUATION_MASK		0xC0
#define HTTP_JWT_UTF8_CONTINUATION			0x80

static bool cerver_http_auth_jwt_is_continuation (const char byte) {

	return (((unsigned char) byte) & HTTP_JWT_UTF8_CONTINUATION_MASK) == HTTP_JWT_UTF8_CONTINUATION;

}

// a text that does not fit is cut at a complete character of UTF-8:
// cut in the middle of one, the json of the token rejected it
// and the jwt was generated without that value
static void cerver_http_auth_jwt_value_copy (
	char *destination, const size_t size, const char *text, const char *key
) {

	size_t length = strlen (text);
	if (length >= size) {
		length = size - 1;
		while (length && cerver_http_auth_jwt_is_continuation (text[length])) length -= 1;

		cerver_log_warning (
			"The JWT value of %s has %zu bytes, it was cut to %zu",
			key, strlen (text), length
		);
	}

	(void) memcpy (destination, text, length);
	destination[length] = '\0';

}

static inline u8 cerver_http_auth_jwt_add_value_internal (
	HttpJwt *http_jwt, const char *key
) {

	u8 retval = 1;

	if (http_jwt && key) {
		if (http_jwt->n_values < HTTP_JWT_VALUES_SIZE) {
			cerver_http_auth_jwt_value_copy (
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

void cerver_http_auth_jwt_add_value (
	HttpJwt *http_jwt,
	const char *key, const char *value
) {

	if (value && !cerver_http_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_UTF8;
		cerver_http_auth_jwt_value_copy (
			http_jwt->values[http_jwt->n_values - 1].value_str,
			HTTP_JWT_VALUE_VALUE_SIZE,
			value, key
		);
	}

}

void cerver_http_auth_jwt_add_value_bool (
	HttpJwt *http_jwt,
	const char *key, const bool value
) {

	if (!cerver_http_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_BOOL;
		http_jwt->values[http_jwt->n_values - 1].value_bool = value;
	}

}

void cerver_http_auth_jwt_add_value_int (
	HttpJwt *http_jwt,
	const char *key, const int64_t value
) {

	if (!cerver_http_auth_jwt_add_value_internal (
		http_jwt, key
	)) {
		http_jwt->values[http_jwt->n_values - 1].type = CERVER_TYPE_INT64;
		http_jwt->values[http_jwt->n_values - 1].value_int = value;
	}

}

#pragma endregion

#pragma region generate

// the values of a token in the order of their keys, as the bytes of each one compare:
// the claims of a token are always written with their keys sorted
static void cerver_http_auth_jwt_values_sort (
	const HttpJwt *http_jwt, u8 order[HTTP_JWT_VALUES_SIZE]
) {

	for (u8 i = 0; i < http_jwt->n_values; i++) {
		u8 position = i;
		while (position && (strcmp (http_jwt->values[order[position - 1]].key, http_jwt->values[i].key) > 0)) {
			order[position] = order[position - 1];
			position -= 1;
		}

		order[position] = i;
	}

}

// a value without a key and a key more than once are not claims: a token is never generated with them
static bool cerver_http_auth_jwt_values_are_valid (
	const HttpJwt *http_jwt, const u8 order[HTTP_JWT_VALUES_SIZE]
) {

	bool valid = true;

	for (u8 i = 0; (i < http_jwt->n_values) && valid; i++) {
		const char *key = http_jwt->values[order[i]].key;

		valid = key[0] && (!i || strcmp (http_jwt->values[order[i - 1]].key, key));
	}

	return valid;

}

static void cerver_http_auth_jwt_claims_write_value (
	CerverJsonWriter *writer, const HttpJwtValue *value
) {

	cerver_json_writer_key (writer, value->key);

	switch (value->type) {
		case CERVER_TYPE_UTF8:
			cerver_json_writer_string (writer, value->value_str, strlen (value->value_str));
			break;

		case CERVER_TYPE_BOOL: cerver_json_writer_boolean (writer, value->value_bool); break;

		case CERVER_TYPE_INT64: cerver_json_writer_integer (writer, (i64) value->value_int); break;

		// a value of another type has nothing to write: the writer fails with a key without its value
		default: break;
	}

}

// the claims of a token: the compact json of an object with its values, their keys sorted;
// a key or a text that is not UTF-8 fails the writer, and the token is not generated
// returns the bytes of the claims, 0 when they could not be written
static size_t cerver_http_auth_jwt_claims_write (
	const HttpJwt *http_jwt, char *claims, const size_t capacity
) {

	u8 order[HTTP_JWT_VALUES_SIZE] = { 0 };
	cerver_http_auth_jwt_values_sort (http_jwt, order);

	if (!cerver_http_auth_jwt_values_are_valid (http_jwt, order)) return 0;

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, claims, capacity, CERVER_JSON_WRITE_COMPACT);

	cerver_json_writer_object_begin (&writer);

	for (u8 i = 0; i < http_jwt->n_values; i++) {
		cerver_http_auth_jwt_claims_write_value (&writer, &http_jwt->values[order[i]]);
	}

	cerver_json_writer_object_end (&writer);

	return (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) ?
		cerver_json_writer_length (&writer) : 0;

}

// signs the claims of the values of a token into a buffer
// returns the characters of the token, 0 when it was not generated
static size_t cerver_http_auth_jwt_sign (
	const HttpJwt *http_jwt, const CerverJwtKey *key,
	char *token, const size_t capacity
) {

	size_t length = 0;

	char claims[HTTP_JWT_CLAIMS_SIZE];
	const size_t claims_size = cerver_http_auth_jwt_claims_write (http_jwt, claims, HTTP_JWT_CLAIMS_SIZE);

	if (
		claims_size
		&& (cerver_jwt_sign (key, claims, claims_size, token, capacity, &length) != CERVER_JWT_ERROR_NONE)
	) {
		length = 0;
	}

	return length;

}

char *cerver_http_auth_generate_jwt_actual (
	HttpJwt *http_jwt, const CerverJwtKey *key
) {

	if (!http_jwt || !key) return NULL;

	char claims[HTTP_JWT_CLAIMS_SIZE];
	const size_t claims_size = cerver_http_auth_jwt_claims_write (http_jwt, claims, HTTP_JWT_CLAIMS_SIZE);

	// the token in memory of its size
	const size_t capacity = claims_size ? cerver_jwt_sign_size (key, claims_size) : 0;
	char *token = capacity ? (char *) malloc (capacity) : NULL;

	size_t length = 0;
	if (token && (cerver_jwt_sign (key, claims, claims_size, token, capacity, &length) != CERVER_JWT_ERROR_NONE)) {
		free (token);
		token = NULL;
	}

	return token;

}

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
char *cerver_http_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	return (http_cerver && http_jwt) ?
		cerver_http_auth_generate_jwt_actual (http_jwt, http_cerver->jwt_private_key) :
		NULL;

}

static void cerver_http_auth_jwt_bearer_clear (HttpJwt *http_jwt) {

	http_jwt->bearer_len = 0;
	http_jwt->bearer[0] = '\0';

}

// the token is signed in the bearer itself, after its prefix, without memory of its own:
// one that does not fit is an error, it is never truncated
u8 cerver_http_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt, const CerverJwtKey *key
) {

	u8 retval = 1;

	(void) memcpy (http_jwt->bearer, HTTP_JWT_BEARER_PREFIX, HTTP_JWT_BEARER_PREFIX_LENGTH);

	const size_t length = cerver_http_auth_jwt_sign (
		http_jwt, key,
		http_jwt->bearer + HTTP_JWT_BEARER_PREFIX_LENGTH, HTTP_JWT_BEARER_SIZE - HTTP_JWT_BEARER_PREFIX_LENGTH
	);

	if (length) {
		http_jwt->bearer_len = HTTP_JWT_BEARER_PREFIX_LENGTH + length;
		retval = 0;
	}

	else {
		cerver_http_auth_jwt_bearer_clear (http_jwt);
	}

	return retval;

}

// generates and signs a bearer jwt that is ready to be used
// returns 0 on success, 1 on error
u8 cerver_http_auth_generate_bearer_jwt (
	HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	return (http_cerver && http_jwt) ?
		cerver_http_auth_generate_bearer_jwt_actual (
			http_jwt, http_cerver->jwt_private_key
		) : 1;

}

static void cerver_http_auth_jwt_json_clear (HttpJwt *http_jwt) {

	http_jwt->json_len = 0;
	http_jwt->json[0] = '\0';

}

// the extra value of the json of a jwt; a key that is the one of the token replaces the bearer,
// as an object with one member for each key always did
static void cerver_http_auth_jwt_json_write_value (
	CerverJsonWriter *writer, const HttpJwt *http_jwt,
	const char *key, const char *value
) {

	const bool replaces = !strcmp (key, HTTP_JWT_JSON_TOKEN_KEY);

	cerver_json_writer_key (writer, HTTP_JWT_JSON_TOKEN_KEY);

	if (replaces) {
		cerver_json_writer_string (writer, value, strlen (value));
	}

	else {
		cerver_json_writer_string (writer, http_jwt->bearer, http_jwt->bearer_len);
		cerver_json_writer_key (writer, key);
		cerver_json_writer_string (writer, value, strlen (value));
	}

}

// {"token": "Bearer <jwt>"} and an extra value if the key is set,
// written in the buffer of the jwt: the values are escaped by the json writer,
// and a json that does not fit or a value that is not UTF-8 is an error
static u8 cerver_http_auth_jwt_json_set (
	HttpJwt *http_jwt, const char *key, const char *value
) {

	u8 retval = 1;

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, http_jwt->json, HTTP_JWT_TOKEN_SIZE, CERVER_JSON_FLAGS_NONE);

	cerver_json_writer_object_begin (&writer);

	if (key) {
		cerver_http_auth_jwt_json_write_value (&writer, http_jwt, key, value);
	}

	else {
		cerver_json_writer_key (&writer, HTTP_JWT_JSON_TOKEN_KEY);
		cerver_json_writer_string (&writer, http_jwt->bearer, http_jwt->bearer_len);
	}

	cerver_json_writer_object_end (&writer);

	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
		http_jwt->json_len = cerver_json_writer_length (&writer);
		retval = 0;
	}

	else {
		cerver_http_auth_jwt_json_clear (http_jwt);
	}

	return retval;

}

// generates and signs a bearer jwt
// and places it inside a json packet
// returns 0 on success, 1 on error
u8 cerver_http_auth_generate_bearer_jwt_json (
	HttpCerver *http_cerver, HttpJwt *http_jwt
) {

	u8 retval = 1;

	if (!cerver_http_auth_generate_bearer_jwt (
		http_cerver, http_jwt
	)) {
		retval = cerver_http_auth_jwt_json_set (http_jwt, NULL, NULL);
	}

	return retval;

}

// works as cerver_http_auth_generate_bearer_jwt_json ()
// but with the ability to add an extra string value to
// the generated json
// returns 0 on success, 1 on error
u8 cerver_http_auth_generate_bearer_jwt_json_with_value (
	HttpCerver *http_cerver, HttpJwt *http_jwt,
	const char *key, const char *value
) {

	u8 retval = 1;

	if (key && value) {
		if (!cerver_http_auth_generate_bearer_jwt (
			http_cerver, http_jwt
		)) {
			retval = cerver_http_auth_jwt_json_set (http_jwt, key, value);
		}
	}

	return retval;

}

#pragma endregion

#pragma region verify

static const char *cerver_http_auth_string_get (const String *string) {

	return string ? string->str : NULL;

}

// what the cerver asks of the claims of its tokens, from its settings
static void cerver_http_auth_jwt_validation_set (
	const HttpCerver *http_cerver, CerverJwtValidation *validation
) {

	validation->leeway = http_cerver->jwt_leeway_seconds;
	validation->require_expiration = http_cerver->jwt_require_expiration;

	validation->issuer = cerver_http_auth_string_get (http_cerver->jwt_issuer);
	validation->subject = cerver_http_auth_string_get (http_cerver->jwt_subject);
	validation->audience = cerver_http_auth_string_get (http_cerver->jwt_audience);

	validation->max_size = http_cerver->jwt_max_size;

}

// verifies the token with the public key of the cerver and validates its claims with its settings:
// "exp", "nbf" and "iat" with its leeway, and the "iss", the "sub" and the "aud" that it asks for
// returns the claims, that live in the arena, or NULL if the token is not valid
const CerverJsonValue *cerver_http_auth_jwt_verify (
	const HttpCerver *http_cerver, Arena *arena,
	const char *token, const size_t token_length
) {

	const CerverJsonValue *claims = NULL;

	if (http_cerver && arena && token && http_cerver->jwt_public_key) {
		CerverJwtValidation validation = CERVER_ZERO_INITIALIZER;
		cerver_http_auth_jwt_validation_set (http_cerver, &validation);

		const CerverJwtError error = cerver_jwt_verify (
			http_cerver->jwt_public_key, arena, token, token_length, &validation, &claims
		);

		#ifdef HTTP_AUTH_DEBUG
		if (error != CERVER_JWT_ERROR_NONE) {
			cerver_log_error ("Invalid JWT: %s", cerver_jwt_error_to_string (error));
		}
		#else
		(void) error;
		#endif
	}

	return claims;

}

// the token of a bearer header: "Bearer <token>"
// returns the start of the token, or NULL if the value is not a bearer
const char *cerver_http_auth_bearer_token (
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
bool cerver_http_auth_validate_jwt (
	HttpCerver *http_cerver, const char *bearer_token,
	CerverHttpDecodeData decode_data, void **decoded_data
) {

	bool retval = false;

	if (!bearer_token) return retval;

	const size_t bearer_length = strlen (bearer_token);
	const char *token = cerver_http_auth_bearer_token (bearer_token, bearer_length);

	// the claims live in an arena of the call, the decoded data is what is left of them
	Arena *arena = token ? arena_create (ARENA_DEFAULT_CHUNK_SIZE) : NULL;
	if (arena) {
		const CerverJsonValue *claims = cerver_http_auth_jwt_verify (
			http_cerver, arena, token, bearer_length - HTTP_JWT_BEARER_PREFIX_LENGTH
		);

		if (claims) {
			if (decode_data && decoded_data) {
				*decoded_data = decode_data (claims);
			}

			retval = true;
		}

		arena_delete (arena);
	}

	return retval;

}

#pragma endregion

// the claims are written once to count their bytes and once more into memory of that size
void *http_decode_data_into_json (const CerverJsonValue *claims) {

	char *json = NULL;

	const size_t length = cerver_json_write (claims, NULL, 0, CERVER_JSON_FLAGS_NONE);
	if (length) {
		json = (char *) malloc (length + 1);
		if (json) (void) cerver_json_write (claims, json, length + 1, CERVER_JSON_FLAGS_NONE);
	}

	return json;

}

void cerver_http_jwt_end_pool (void) {

	pool_delete (cerver_http_jwt_pool);
	cerver_http_jwt_pool = NULL;

}

#pragma region auth

// loads a key from a filename that can be used for jwt
// returns a newly allocated c string on success, NULL on error
char *cerver_http_auth_load_key (
	const char *filename, size_t *keylen
) {

	char *key = NULL;

	if (filename && keylen) {
		key = file_read (filename, keylen);
	}

	return key;

}

// the algorithms of the tokens of a cerver always sign them
static bool cerver_http_auth_jwt_algorithm_is_valid (const CerverJwtAlgorithm algorithm) {

	return (algorithm > CERVER_JWT_ALGORITHM_NONE) && ((unsigned int) algorithm < CERVER_JWT_ALGORITHMS);

}

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is CERVER_JWT_ALGORITHM_HS256, CERVER_JWT_ALGORITHM_NONE is not accepted
// returns 0 on success, 1 on error
u8 cerver_http_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, const CerverJwtAlgorithm algorithm
) {

	u8 retval = 1;

	if (http_cerver && cerver_http_auth_jwt_algorithm_is_valid (algorithm)) {
		http_cerver->jwt_alg = algorithm;

		retval = 0;
	}

	return retval;

}

// a setter called again replaces the value it set before
static void cerver_http_auth_set_string (
	String **destination, const char *value
) {

	str_delete (*destination);

	*destination = value ? str_new (value) : NULL;

}

// sets the filename from where the jwt private key will be loaded
void cerver_http_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		cerver_http_auth_set_string (&http_cerver->jwt_opt_key_name, filename);
	}

}

// sets the filename from where the jwt public key will be loaded
void cerver_http_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		cerver_http_auth_set_string (&http_cerver->jwt_opt_pub_key_name, filename);
	}

}

// sets the longest bearer token that is decoded,
// a longer one is rejected without decoding it
// the default value is CERVER_HTTP_DEFAULT_JWT_MAX_SIZE, 0 is the limit of every token
void cerver_http_auth_set_jwt_max_size (
	HttpCerver *http_cerver, const size_t max_size
) {

	if (http_cerver) http_cerver->jwt_max_size = max_size;

}

// sets if a token without "exp" is rejected
// the default value is CERVER_HTTP_DEFAULT_JWT_REQUIRE_EXPIRATION
void cerver_http_auth_set_jwt_require_expiration (
	HttpCerver *http_cerver, const bool require
) {

	if (http_cerver) http_cerver->jwt_require_expiration = require;

}

// sets the seconds of clock skew that are accepted in "exp", "nbf" and "iat"
// the default value is CERVER_HTTP_DEFAULT_JWT_LEEWAY_SECONDS
void cerver_http_auth_set_jwt_leeway_seconds (
	HttpCerver *http_cerver, const unsigned int leeway_seconds
) {

	if (http_cerver) http_cerver->jwt_leeway_seconds = leeway_seconds;

}

// sets the "iss" that every token must have, NULL accepts any
void cerver_http_auth_set_jwt_issuer (
	HttpCerver *http_cerver, const char *issuer
) {

	if (http_cerver) {
		cerver_http_auth_set_string (&http_cerver->jwt_issuer, issuer);
	}

}

// sets the "sub" that every token must have, NULL accepts any
void cerver_http_auth_set_jwt_subject (
	HttpCerver *http_cerver, const char *subject
) {

	if (http_cerver) {
		cerver_http_auth_set_string (&http_cerver->jwt_subject, subject);
	}

}

// sets the "aud" that every token must have, as its value or in its array,
// NULL accepts any
void cerver_http_auth_set_jwt_audience (
	HttpCerver *http_cerver, const char *audience
) {

	if (http_cerver) {
		cerver_http_auth_set_string (&http_cerver->jwt_audience, audience);
	}

}

#pragma endregion
