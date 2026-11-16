#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/files/files.h"

#include "cerver/http/core/http.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/security/auth.h"

#include "cerver/json/document.h"
#include "cerver/json/json.h"
#include "cerver/json/value.h"
#include "cerver/json/writer.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the registered claims that the cerver checks (RFC 7519 §4.1)
#define HTTP_JWT_CLAIM_EXPIRATION			"exp"
#define HTTP_JWT_CLAIM_NOT_BEFORE			"nbf"
#define HTTP_JWT_CLAIM_ISSUER				"iss"
#define HTTP_JWT_CLAIM_AUDIENCE				"aud"

// what is between the three parts of a token
#define HTTP_JWT_PARTS_SEPARATOR			'.'

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

		// an HttpJwt that the pool does not take is released (it was lost)
		if (pool_push (http_jwt_pool, http_jwt)) http_jwt_delete (http_jwt);
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

// the extra value of the json of a jwt; a key that is the one of the token replaces the bearer,
// as an object with one member for each key always did
static void http_cerver_auth_jwt_json_write_value (
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
static u8 http_cerver_auth_jwt_json_set (
	HttpJwt *http_jwt, const char *key, const char *value
) {

	u8 retval = 1;

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, http_jwt->json, HTTP_JWT_TOKEN_SIZE, CERVER_JSON_FLAGS_NONE);

	cerver_json_writer_object_begin (&writer);

	if (key) {
		http_cerver_auth_jwt_json_write_value (&writer, http_jwt, key, value);
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
		http_cerver_auth_jwt_json_clear (http_jwt);
	}

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

// a time claim that is in the token must be an integer:
// a real, a string and an integer that only fits without a sign are not
static HttpJwtClaimState http_cerver_auth_jwt_time_claim (
	const CerverJsonValue *claims, const char *name, i64 *value
) {

	HttpJwtClaimState state = HTTP_JWT_CLAIM_STATE_MISSING;

	const CerverJsonValue *claim = cerver_json_object_get (claims, name);
	if (claim) {
		if (cerver_json_type (claim) == CERVER_JSON_TYPE_INTEGER) {
			*value = cerver_json_integer_value (claim);
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
	const HttpCerver *http_cerver, const CerverJsonValue *claims, const i64 now
) {

	bool valid = false;

	i64 expiration = 0;
	switch (http_cerver_auth_jwt_time_claim (claims, HTTP_JWT_CLAIM_EXPIRATION, &expiration)) {
		case HTTP_JWT_CLAIM_STATE_MISSING:
			valid = !http_cerver->jwt_require_expiration;
			break;

		// the leeway is taken from now, so a large "exp" never overflows
		case HTTP_JWT_CLAIM_STATE_FOUND:
			valid = (now - (i64) http_cerver->jwt_leeway_seconds) < expiration;
			break;

		default: break;
	}

	return valid;

}

// the token is valid from "nbf" minus the leeway
static bool http_cerver_auth_jwt_check_not_before (
	const HttpCerver *http_cerver, const CerverJsonValue *claims, const i64 now
) {

	bool valid = false;

	i64 not_before = 0;
	switch (http_cerver_auth_jwt_time_claim (claims, HTTP_JWT_CLAIM_NOT_BEFORE, &not_before)) {
		case HTTP_JWT_CLAIM_STATE_MISSING: valid = true; break;

		case HTTP_JWT_CLAIM_STATE_FOUND:
			valid = (now + (i64) http_cerver->jwt_leeway_seconds) >= not_before;
			break;

		default: break;
	}

	return valid;

}

static bool http_cerver_auth_jwt_string_equals (
	const CerverJsonValue *value, const String *expected
) {

	return cerver_json_is_string (value)
		&& (cerver_json_string_length (value) == expected->len)
		&& !memcmp (cerver_json_string_value (value), expected->str, expected->len);

}

static bool http_cerver_auth_jwt_check_issuer (
	const HttpCerver *http_cerver, const CerverJsonValue *claims
) {

	return !http_cerver->jwt_issuer || http_cerver_auth_jwt_string_equals (
		cerver_json_object_get (claims, HTTP_JWT_CLAIM_ISSUER), http_cerver->jwt_issuer
	);

}

// one of the strings of an "aud" that is an array is the audience of the cerver
static bool http_cerver_auth_jwt_audience_has (
	const CerverJsonValue *audiences, const String *expected
) {

	bool found = false;

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (audiences);
	const CerverJsonValue *audience = cerver_json_array_iterator_next (&iterator);
	while (audience && !found) {
		found = http_cerver_auth_jwt_string_equals (audience, expected);
		audience = cerver_json_array_iterator_next (&iterator);
	}

	return found;

}

// "aud" is a string or an array of strings (RFC 7519 §4.1.3)
static bool http_cerver_auth_jwt_check_audience (
	const HttpCerver *http_cerver, const CerverJsonValue *claims
) {

	if (!http_cerver->jwt_audience) return true;

	const CerverJsonValue *audience = cerver_json_object_get (claims, HTTP_JWT_CLAIM_AUDIENCE);

	return cerver_json_is_array (audience) ?
		http_cerver_auth_jwt_audience_has (audience, http_cerver->jwt_audience) :
		http_cerver_auth_jwt_string_equals (audience, http_cerver->jwt_audience);

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
	const HttpCerver *http_cerver, const CerverJsonValue *claims
) {

	const i64 now = (i64) time (NULL);

	return http_cerver_auth_jwt_check_expiration (http_cerver, claims, now)
		&& http_cerver_auth_jwt_check_not_before (http_cerver, claims, now)
		&& http_cerver_auth_jwt_check_issuer (http_cerver, claims)
		&& http_cerver_auth_jwt_check_audience (http_cerver, claims);

}

static bool http_cerver_auth_jwt_check_size (
	const HttpCerver *http_cerver, const size_t token_length
) {

	return !http_cerver->jwt_max_size || (token_length <= http_cerver->jwt_max_size);

}

// the claims of a token as an object of the json of cerver, read in the arena inside their own text:
// a key more than once and a NUL in a string are rejected, and so are the claims with too many levels
static const CerverJsonValue *http_cerver_auth_jwt_claims_parse (
	Arena *arena, char *text, const size_t length
) {

	CerverJsonLimits limits = CERVER_ZERO_INITIALIZER;
	limits.depth = HTTP_JWT_CLAIMS_DEPTH;

	const CerverJsonValue *claims = cerver_json_document_root (
		cerver_json_parse_in_situ (
			arena, text, length,
			CERVER_JSON_PARSE_REJECT_DUPLICATES | CERVER_JSON_PARSE_REJECT_NUL,
			&limits, NULL
		)
	);

	return cerver_json_is_object (claims) ? claims : NULL;

}

// the second part of a token, decoded into the arena with the bytes that the reader of the json writes after it
static const CerverJsonValue *http_cerver_auth_jwt_claims_decode (
	Arena *arena, const char *payload, const size_t payload_length
) {

	const CerverJsonValue *claims = NULL;

	size_t length = 0;
	unsigned char *text = (unsigned char *) arena_alloc (
		arena, base64url_decode_length (payload_length) + CERVER_JSON_PADDING
	);

	if (text && !base64url_decode (text, &length, payload, payload_length)) {
		claims = http_cerver_auth_jwt_claims_parse (arena, (char *) text, length);
	}

	return claims;

}

// the claims of a token are its second part, between its two separators
static const CerverJsonValue *http_cerver_auth_jwt_claims_read (
	Arena *arena, const char *token, const size_t token_length
) {

	const CerverJsonValue *claims = NULL;

	const char *end = token + token_length;
	const char *payload = (const char *) memchr (token, HTTP_JWT_PARTS_SEPARATOR, token_length);
	const char *signature = payload ?
		(const char *) memchr (payload + 1, HTTP_JWT_PARTS_SEPARATOR, (size_t) (end - (payload + 1))) : NULL;

	if (signature) {
		claims = http_cerver_auth_jwt_claims_decode (
			arena, payload + 1, (size_t) (signature - (payload + 1))
		);
	}

	return claims;

}

// the claims of a token whose signature was verified, when the cerver takes them
static const CerverJsonValue *http_cerver_auth_jwt_verified_claims (
	const HttpCerver *http_cerver, Arena *arena, jwt_t *jwt,
	const char *token, const size_t token_length
) {

	const CerverJsonValue *claims = http_cerver_auth_jwt_check_headers (http_cerver, jwt) ?
		http_cerver_auth_jwt_claims_read (arena, token, token_length) : NULL;

	return (claims && http_cerver_auth_jwt_check_claims (http_cerver, claims)) ? claims : NULL;

}

// verifies the token with the public key of the cerver and checks its claims:
// the algorithm of the cerver, "exp", "nbf", "iss" and "aud" with its settings
// returns the claims, that live in the arena, or NULL if the token is not valid
const CerverJsonValue *http_cerver_auth_jwt_verify (
	const HttpCerver *http_cerver, Arena *arena,
	const char *token, const size_t token_length
) {

	const CerverJsonValue *claims = NULL;

	if (
		http_cerver && arena && token && http_cerver->jwt_public_key
		&& http_cerver_auth_jwt_check_size (http_cerver, token_length)
	) {
		jwt_t *jwt = NULL;
		if (!jwt_decode_with_key (&jwt, token, token_length, http_cerver->jwt_public_key)) {
			claims = http_cerver_auth_jwt_verified_claims (http_cerver, arena, jwt, token, token_length);

			#ifdef HTTP_AUTH_DEBUG
			if (!claims) cerver_log_error ("The claims of the JWT are not valid!");
			#endif

			jwt_free (jwt);
		}

		#ifdef HTTP_AUTH_DEBUG
		else {
			cerver_log_error ("Invalid JWT!");
		}
		#endif
	}

	return claims;

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
	HttpDecodeData decode_data, void **decoded_data
) {

	bool retval = false;

	if (!bearer_token) return retval;

	const size_t bearer_length = strlen (bearer_token);
	const char *token = http_cerver_auth_bearer_token (bearer_token, bearer_length);

	// the claims live in an arena of the call, the decoded data is what is left of them
	Arena *arena = token ? arena_create (ARENA_DEFAULT_CHUNK_SIZE) : NULL;
	if (arena) {
		const CerverJsonValue *claims = http_cerver_auth_jwt_verify (
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

void http_jwt_end_pool (void) {

	pool_delete (http_jwt_pool);
	http_jwt_pool = NULL;

}

#pragma region auth

// loads a key from a filename that can be used for jwt
// returns a newly allocated c string on success, NULL on error
char *http_cerver_auth_load_key (
	const char *filename, size_t *keylen
) {

	char *key = NULL;

	if (filename && keylen) {
		key = file_read (filename, keylen);
	}

	return key;

}

// the algorithms of the tokens of a cerver always sign them
static bool http_cerver_auth_jwt_algorithm_is_valid (const jwt_alg_t jwt_alg) {

	return (jwt_alg > JWT_ALG_NONE) && (jwt_alg < JWT_ALG_INVAL);

}

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256, JWT_ALG_NONE is not accepted
// returns 0 on success, 1 on error
u8 http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, const jwt_alg_t jwt_alg
) {

	u8 retval = 1;

	if (http_cerver && http_cerver_auth_jwt_algorithm_is_valid (jwt_alg)) {
		http_cerver->jwt_alg = jwt_alg;

		retval = 0;
	}

	return retval;

}

// a setter called again replaces the value it set before
static void http_cerver_auth_set_string (
	String **destination, const char *value
) {

	str_delete (*destination);

	*destination = value ? str_new (value) : NULL;

}

// sets the filename from where the jwt private key will be loaded
void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_opt_key_name, filename);
	}

}

// sets the filename from where the jwt public key will be loaded
void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_opt_pub_key_name, filename);
	}

}

// sets the longest bearer token that is decoded,
// a longer one is rejected without decoding it
// the default value is HTTP_CERVER_DEFAULT_JWT_MAX_SIZE, 0 has no limit
void http_cerver_auth_set_jwt_max_size (
	HttpCerver *http_cerver, const size_t max_size
) {

	if (http_cerver) http_cerver->jwt_max_size = max_size;

}

// sets if a token without "exp" is rejected
// the default value is HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION
void http_cerver_auth_set_jwt_require_expiration (
	HttpCerver *http_cerver, const bool require
) {

	if (http_cerver) http_cerver->jwt_require_expiration = require;

}

// sets the seconds of clock skew that are accepted in "exp" and "nbf"
// the default value is HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS
void http_cerver_auth_set_jwt_leeway_seconds (
	HttpCerver *http_cerver, const unsigned int leeway_seconds
) {

	if (http_cerver) http_cerver->jwt_leeway_seconds = leeway_seconds;

}

// sets the "iss" that every token must have, NULL accepts any
void http_cerver_auth_set_jwt_issuer (
	HttpCerver *http_cerver, const char *issuer
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_issuer, issuer);
	}

}

// sets the "aud" that every token must have, as its value or in its array,
// NULL accepts any
void http_cerver_auth_set_jwt_audience (
	HttpCerver *http_cerver, const char *audience
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_audience, audience);
	}

}

#pragma endregion
