#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/cerver.h>

#include <cerver/http/auth.h>
#include <cerver/http/http.h>

#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>

#include <cerver/http/jwt/alg.h>
#include <cerver/http/jwt/jwt.h>
#include <cerver/http/jwt/key.h>

#include <cerver/utils/base64.h>

#include "http.h"

#include "../test.h"

#define TOKENS_CERVER_NAME				"tokens-cerver"
#define TOKENS_CERVER_PORT				8080
#define TOKENS_CERVER_CONNECTION_QUEUE	2

// an HS256 secret of the size of its hash, and one that is shorter
#define TOKENS_SECRET					"0123456789abcdef0123456789abcdef"
#define TOKENS_SHORT_SECRET				"0123456789abcdef"

#define TOKENS_SECRET_FILE				"/tmp/cerver-tokens-secret"
#define TOKENS_SHORT_SECRET_FILE		"/tmp/cerver-tokens-short-secret"
#define TOKENS_EMPTY_FILE				"/tmp/cerver-tokens-empty"

#define TOKENS_RSA_PRIVATE_FILE			"./test/http/keys/key.key"
#define TOKENS_RSA_PUBLIC_FILE			"./test/http/keys/key.pub"
#define TOKENS_RSA_WEAK_FILE			"./test/jwt/keys/rsa_key_1024.pem"
#define TOKENS_P256_PRIVATE_FILE		"./test/jwt/keys/ec_key_prime256v1.pem"
#define TOKENS_P256_PUBLIC_FILE			"./test/jwt/keys/ec_key_prime256v1-pub.pem"
#define TOKENS_P384_PUBLIC_FILE			"./test/jwt/keys/ec_key_secp384r1-pub.pem"

#define TOKENS_FILE_SIZE				(1 << 14)
#define TOKENS_TOKEN_SIZE				(1 << 14)

// "exp" in 2100, after the 32 bits of time_t
#define TOKENS_FAR_EXPIRATION			4102444800LL

#define TOKENS_HOUR_SECONDS				3600
#define TOKENS_LEEWAY_SECONDS			(TOKENS_HOUR_SECONDS * 2)

#define TOKENS_ISSUER					"https://issuer.cerver"
#define TOKENS_AUDIENCE					"cerver-tests"

// a username of "ñ" longer than a value of a jwt
#define TOKENS_TWO_BYTES				"\xc3\xb1"
#define TOKENS_TWO_BYTES_LENGTH			2
#define TOKENS_LONG_USERNAME_SIZE		(HTTP_JWT_VALUE_VALUE_SIZE * 2)

#pragma region helpers

static void tokens_file_write (const char *filename, const char *content) {

	FILE *file = fopen (filename, "w");
	test_check_ptr (file);

	if (strlen (content)) {
		test_check_unsigned_eq (fwrite (content, 1, strlen (content), file), strlen (content), NULL);
	}

	(void) fclose (file);

}

static size_t tokens_file_read (
	const char *filename, unsigned char *buffer, const size_t size
) {

	FILE *file = fopen (filename, "r");
	test_check_ptr (file);

	const size_t length = fread (buffer, 1, size, file);
	(void) fclose (file);

	return length;

}

static JwtKey *tokens_key_create (
	const jwt_alg_t alg, const JwtKeyType type, const char *filename, JwtKeyError *error
) {

	unsigned char material[TOKENS_FILE_SIZE] = { 0 };
	const size_t length = tokens_file_read (filename, material, sizeof (material));

	return jwt_key_create (alg, type, material, length, error);

}

static JwtKey *tokens_secret_key (const char *secret) {

	JwtKeyError error = JWT_KEY_ERROR_NONE;
	JwtKey *key = jwt_key_create (
		JWT_ALG_HS256, JWT_KEY_TYPE_PRIVATE,
		(const unsigned char *) secret, strlen (secret), &error
	);

	test_check_ptr (key);
	test_check_int_eq (error, JWT_KEY_ERROR_NONE, NULL);

	return key;

}

// a token with the grants of the json, signed with the key
static char *tokens_sign (const JwtKey *key, const char *grants) {

	jwt_t *jwt = NULL;
	test_check_int_eq (jwt_new (&jwt), 0, NULL);

	if (grants) test_check_int_eq (jwt_add_grants_json (jwt, grants), 0, NULL);

	char *token = jwt_encode_with_key (jwt, key);
	test_check_ptr (token);

	jwt_free (jwt);

	return token;

}

// a token with any header and payload, signed with HMAC over what they are
static char *tokens_forge (
	const char *secret, const char *header, const char *payload
) {

	char *token = (char *) calloc (TOKENS_TOKEN_SIZE, 1);
	test_check_ptr (token);

	size_t position = base64url_encode (token, (const unsigned char *) header, strlen (header));
	token[position++] = '.';
	position += base64url_encode (token + position, (const unsigned char *) payload, strlen (payload));

	JwtKey *key = tokens_secret_key (secret);

	unsigned char *signature = NULL;
	size_t signature_length = 0;
	test_check_int_eq (jwt_key_sign (key, token, position, &signature, &signature_length), 0, NULL);

	token[position++] = '.';
	(void) base64url_encode (token + position, signature, signature_length);

	free (signature);
	jwt_key_delete (key);

	return token;

}

static int tokens_decode (const char *token, const JwtKey *key) {

	jwt_t *jwt = NULL;
	const int result = jwt_decode_with_key (&jwt, token, strlen (token), key);

	if (result) test_check_null_ptr (jwt);
	jwt_free (jwt);

	return result;

}

static Cerver *tokens_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_WEB, TOKENS_CERVER_NAME, TOKENS_CERVER_PORT,
		PROTOCOL_TCP, false, TOKENS_CERVER_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_ptr (cerver->cerver_data);

	return cerver;

}

// a cerver with the keys of the algorithm, returns the result of its init
static u8 tokens_cerver_init (
	Cerver *cerver, const jwt_alg_t alg,
	const char *private_file, const char *public_file
) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	test_check_unsigned_eq (http_cerver_auth_set_jwt_algorithm (http_cerver, alg), 0, NULL);
	if (private_file) http_cerver_auth_set_jwt_priv_key_filename (http_cerver, private_file);
	if (public_file) http_cerver_auth_set_jwt_pub_key_filename (http_cerver, public_file);

	return http_cerver_init (http_cerver);

}

static bool tokens_cerver_verifies (const HttpCerver *http_cerver, const char *token) {

	jwt_t *jwt = http_cerver_auth_jwt_verify (http_cerver, token, strlen (token));
	jwt_free (jwt);

	return jwt != NULL;

}

#pragma endregion

#pragma region jwt

// a token without grants was encoded into a buffer one byte shorter than its base64
static void tokens_encode_empty (void) {

	jwt_t *jwt = NULL;
	test_check_int_eq (jwt_new (&jwt), 0, NULL);
	test_check_int_eq (
		jwt_set_alg (jwt, JWT_ALG_HS256, (const unsigned char *) TOKENS_SECRET, strlen (TOKENS_SECRET)),
		0, NULL
	);

	char *token = jwt_encode_str (jwt);
	test_check_ptr (token);
	test_check (!strncmp (token, "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30.", strlen ("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30.")), token);

	JwtKey *key = tokens_secret_key (TOKENS_SECRET);
	test_check_int_eq (tokens_decode (token, key), 0, NULL);

	jwt_key_delete (key);
	jwt_free_str (token);
	jwt_free (jwt);

}

// a signature that is changed, cut or longer is rejected
static void tokens_hmac_signature (void) {

	JwtKey *key = tokens_secret_key (TOKENS_SECRET);
	char *token = tokens_sign (key, "{\"id\": \"tokens\"}");
	test_check_int_eq (tokens_decode (token, key), 0, NULL);

	const size_t length = strlen (token);

	char *changed = strdup (token);
	changed[length - 2] = (changed[length - 2] == 'A') ? 'B' : 'A';
	test_check_int_ne (tokens_decode (changed, key), 0);
	free (changed);

	char *cut = strndup (token, length - 4);
	test_check_int_ne (tokens_decode (cut, key), 0);
	free (cut);

	char *longer = (char *) calloc (length + 8, 1);
	(void) snprintf (longer, length + 8, "%sAAAA", token);
	test_check_int_ne (tokens_decode (longer, key), 0);
	free (longer);

	// without a signature
	char *unsigned_token = strndup (token, (size_t) (strrchr (token, '.') - token) + 1);
	test_check_int_ne (tokens_decode (unsigned_token, key), 0);
	free (unsigned_token);

	free (token);
	jwt_key_delete (key);

}

// a token never chooses how it is verified
static void tokens_algorithm (void) {

	JwtKeyError error = JWT_KEY_ERROR_NONE;
	JwtKey *public_key = tokens_key_create (JWT_ALG_RS256, JWT_KEY_TYPE_PUBLIC, TOKENS_RSA_PUBLIC_FILE, &error);
	test_check_ptr (public_key);

	// HS256 signed with the public PEM as its secret, the classic confusion
	unsigned char pem[TOKENS_FILE_SIZE] = { 0 };
	(void) tokens_file_read (TOKENS_RSA_PUBLIC_FILE, pem, sizeof (pem) - 1);
	char *confused = tokens_forge (
		(const char *) pem,
		"{\"alg\":\"HS256\",\"typ\":\"JWT\"}", "{\"id\":\"tokens\"}"
	);

	test_check_int_ne (tokens_decode (confused, public_key), 0);

	jwt_t *jwt = NULL;
	test_check_int_ne (jwt_decode (&jwt, confused, pem, strlen ((const char *) pem)), 0);
	test_check_null_ptr (jwt);

	free (confused);

	// "none" and the names in other cases are not accepted
	char *none = tokens_forge (TOKENS_SECRET, "{\"alg\":\"none\"}", "{\"id\":\"tokens\"}");
	JwtKey *secret = tokens_secret_key (TOKENS_SECRET);
	test_check_int_ne (tokens_decode (none, secret), 0);
	free (none);

	char *lower = tokens_forge (TOKENS_SECRET, "{\"alg\":\"hs256\"}", "{\"id\":\"tokens\"}");
	test_check_int_ne (tokens_decode (lower, secret), 0);
	test_check_int_eq (jwt_str_alg ("hs256"), JWT_ALG_INVAL, NULL);
	test_check_int_eq (jwt_str_alg ("None"), JWT_ALG_INVAL, NULL);
	test_check_int_eq (jwt_str_alg ("none"), JWT_ALG_NONE, NULL);
	free (lower);

	char *valid = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "{\"id\":\"tokens\"}");
	test_check_int_eq (tokens_decode (valid, secret), 0, NULL);
	free (valid);

	jwt_key_delete (secret);
	jwt_key_delete (public_key);

}

// the parts are canonical base64url and json objects without repeated keys
static void tokens_parts (void) {

	JwtKey *secret = tokens_secret_key (TOKENS_SECRET);

	char *duplicated = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\",\"alg\":\"none\"}", "{\"id\":\"tokens\"}");
	test_check_int_ne (tokens_decode (duplicated, secret), 0);
	free (duplicated);

	char *array = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "[\"tokens\"]");
	test_check_int_ne (tokens_decode (array, secret), 0);
	free (array);

	char *repeated = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "{\"id\":\"one\",\"id\":\"two\"}");
	test_check_int_ne (tokens_decode (repeated, secret), 0);
	free (repeated);

	char *token = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "{\"id\":\"tokens\"}");
	const size_t length = strlen (token);

	// a fourth part
	char *four = (char *) calloc (length + 4, 1);
	(void) snprintf (four, length + 4, "%s.e30", token);
	test_check_int_ne (tokens_decode (four, secret), 0);
	free (four);

	// the padding of base64
	const char *first_separator = strchr (token, '.');
	char *padded = (char *) calloc (length + 4, 1);
	(void) snprintf (padded, length + 4, "%.*s==%s", (int) (first_separator - token), token, first_separator);
	test_check_int_ne (tokens_decode (padded, secret), 0);
	free (padded);

	free (token);
	jwt_key_delete (secret);

}

// each key is checked against its algorithm
static void tokens_keys (void) {

	JwtKeyError error = JWT_KEY_ERROR_NONE;

	test_check_null_ptr (jwt_key_create (JWT_ALG_NONE, JWT_KEY_TYPE_PRIVATE, (const unsigned char *) TOKENS_SECRET, strlen (TOKENS_SECRET), &error));
	test_check_int_eq (error, JWT_KEY_ERROR_ALGORITHM, NULL);

	test_check_null_ptr (jwt_key_create (JWT_ALG_HS256, JWT_KEY_TYPE_PRIVATE, NULL, 0, &error));
	test_check_int_eq (error, JWT_KEY_ERROR_EMPTY, NULL);

	test_check_null_ptr (jwt_key_create (JWT_ALG_RS256, JWT_KEY_TYPE_PUBLIC, (const unsigned char *) TOKENS_SECRET, strlen (TOKENS_SECRET), &error));
	test_check_int_eq (error, JWT_KEY_ERROR_PEM, NULL);

	test_check_null_ptr (tokens_key_create (JWT_ALG_ES256, JWT_KEY_TYPE_PUBLIC, TOKENS_RSA_PUBLIC_FILE, &error));
	test_check_int_eq (error, JWT_KEY_ERROR_TYPE, NULL);

	test_check_null_ptr (tokens_key_create (JWT_ALG_ES256, JWT_KEY_TYPE_PUBLIC, TOKENS_P384_PUBLIC_FILE, &error));
	test_check_int_eq (error, JWT_KEY_ERROR_CURVE, NULL);

	JwtKey *es256 = tokens_key_create (JWT_ALG_ES256, JWT_KEY_TYPE_PUBLIC, TOKENS_P256_PUBLIC_FILE, &error);
	test_check_ptr (es256);
	test_check_int_eq (jwt_key_check_strength (es256), JWT_KEY_ERROR_NONE, NULL);
	jwt_key_delete (es256);

	JwtKey *weak = tokens_key_create (JWT_ALG_RS256, JWT_KEY_TYPE_PRIVATE, TOKENS_RSA_WEAK_FILE, &error);
	test_check_ptr (weak);
	test_check_int_eq (jwt_key_check_strength (weak), JWT_KEY_ERROR_STRENGTH, NULL);
	jwt_key_delete (weak);

	JwtKey *short_secret = tokens_secret_key (TOKENS_SHORT_SECRET);
	test_check_int_eq (jwt_key_check_strength (short_secret), JWT_KEY_ERROR_STRENGTH, NULL);
	jwt_key_delete (short_secret);

	unsigned char pem[TOKENS_FILE_SIZE] = { 0 };
	(void) tokens_file_read (TOKENS_RSA_PUBLIC_FILE, pem, sizeof (pem) - 1);
	JwtKey *pem_secret = tokens_secret_key ((const char *) pem);
	test_check_int_eq (jwt_key_check_strength (pem_secret), JWT_KEY_ERROR_SECRET, NULL);
	jwt_key_delete (pem_secret);

	test_check_str_eq (jwt_key_error_to_string (JWT_KEY_ERROR_CURVE), "The curve of the key does not match the algorithm", NULL);

}

// ES256 signs with its curve and with a low S, and verifies its own signatures
static void tokens_ecdsa (void) {

	JwtKeyError error = JWT_KEY_ERROR_NONE;
	JwtKey *private_key = tokens_key_create (JWT_ALG_ES256, JWT_KEY_TYPE_PRIVATE, TOKENS_P256_PRIVATE_FILE, &error);
	JwtKey *public_key = tokens_key_create (JWT_ALG_ES256, JWT_KEY_TYPE_PUBLIC, TOKENS_P256_PUBLIC_FILE, &error);
	test_check_ptr (private_key);
	test_check_ptr (public_key);

	// the signatures are random, many of them have a high S before it is lowered
	for (int i = 0; i < (1 << 5); i++) {
		char *token = tokens_sign (private_key, "{\"id\": \"tokens\"}");
		test_check_int_eq (tokens_decode (token, public_key), 0, NULL);
		free (token);
	}

	jwt_key_delete (private_key);
	jwt_key_delete (public_key);

}

#pragma endregion

#pragma region cerver

// a key that does not match the algorithm, or that is weak, does not start the cerver
static void tokens_cerver_keys (void) {

	tokens_file_write (TOKENS_SECRET_FILE, TOKENS_SECRET);
	tokens_file_write (TOKENS_SHORT_SECRET_FILE, TOKENS_SHORT_SECRET);
	tokens_file_write (TOKENS_EMPTY_FILE, "");

	Cerver *cerver = tokens_cerver_create ();
	test_check_unsigned_ne (tokens_cerver_init (cerver, JWT_ALG_ES256, NULL, TOKENS_RSA_PUBLIC_FILE), 0);
	(void) cerver_teardown (cerver);

	cerver = tokens_cerver_create ();
	test_check_unsigned_ne (tokens_cerver_init (cerver, JWT_ALG_HS256, TOKENS_SHORT_SECRET_FILE, TOKENS_SHORT_SECRET_FILE), 0);
	(void) cerver_teardown (cerver);

	cerver = tokens_cerver_create ();
	test_check_unsigned_ne (tokens_cerver_init (cerver, JWT_ALG_HS256, NULL, TOKENS_EMPTY_FILE), 0);
	(void) cerver_teardown (cerver);

	// the public PEM as the secret of an HS256 cerver
	cerver = tokens_cerver_create ();
	test_check_unsigned_ne (tokens_cerver_init (cerver, JWT_ALG_HS256, NULL, TOKENS_RSA_PUBLIC_FILE), 0);
	(void) cerver_teardown (cerver);

	cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_NONE), 1, NULL);
	test_check_unsigned_eq (http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_INVAL), 1, NULL);
	test_check_int_eq (http_cerver->jwt_alg, JWT_DEFAULT_ALG, NULL);
	test_check_unsigned_eq (tokens_cerver_init (cerver, JWT_ALG_RS256, TOKENS_RSA_PRIVATE_FILE, TOKENS_RSA_PUBLIC_FILE), 0, NULL);
	(void) cerver_teardown (cerver);

}

// "exp", "nbf", "iss", "aud" and the size with the settings of the cerver
static void tokens_cerver_claims (void) {

	Cerver *cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (tokens_cerver_init (cerver, JWT_ALG_HS256, TOKENS_SECRET_FILE, TOKENS_SECRET_FILE), 0, NULL);

	const long now = (long) time (NULL);
	char grants[TOKENS_FILE_SIZE] = { 0 };

	// the default settings: without "exp" it is accepted, an expired one is not
	char *token = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %ld}", now - TOKENS_HOUR_SECONDS);
	char *expired = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, expired), NULL);

	// the leeway accepts a clock that is behind
	http_cerver_auth_set_jwt_leeway_seconds (http_cerver, TOKENS_LEEWAY_SECONDS);
	test_check (tokens_cerver_verifies (http_cerver, expired), NULL);
	http_cerver_auth_set_jwt_leeway_seconds (http_cerver, 0);
	free (expired);

	(void) snprintf (grants, sizeof (grants), "{\"nbf\": %ld}", now + TOKENS_HOUR_SECONDS);
	char *early = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, early), NULL);
	free (early);

	// time claims that are not integers fail closed
	char *text_nbf = tokens_sign (http_cerver->jwt_private_key, "{\"nbf\": \"0\"}");
	test_check (!tokens_cerver_verifies (http_cerver, text_nbf), NULL);
	free (text_nbf);

	char *text_exp = tokens_sign (http_cerver->jwt_private_key, "{\"exp\": \"99999999999\"}");
	test_check (!tokens_cerver_verifies (http_cerver, text_exp), NULL);
	free (text_exp);

	// "exp" required
	http_cerver_auth_set_jwt_require_expiration (http_cerver, true);
	token = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld}", TOKENS_FAR_EXPIRATION);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	// the issuer and the audience
	http_cerver_auth_set_jwt_issuer (http_cerver, TOKENS_ISSUER);
	http_cerver_auth_set_jwt_audience (http_cerver, TOKENS_AUDIENCE);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": [\"other\", \"%s\"]}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"other\", \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": [\"other\"]}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), NULL);

	// a token longer than the limit is not decoded
	http_cerver_auth_set_jwt_max_size (http_cerver, strlen (token) - 1);
	test_check (!tokens_cerver_verifies (http_cerver, token), NULL);
	http_cerver_auth_set_jwt_max_size (http_cerver, HTTP_CERVER_DEFAULT_JWT_MAX_SIZE);
	free (token);

	(void) cerver_teardown (cerver);

}

// the bearer API of auth.c: the prefix, a missing key, the json and the 64 bits values
static void tokens_cerver_bearer (void) {

	Cerver *cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	// without keys nothing is validated nor generated
	test_check (!http_cerver_auth_validate_jwt (http_cerver, "Bearer abc", NULL, NULL), NULL);

	HttpJwt *http_jwt = (HttpJwt *) http_jwt_new ();
	test_check_unsigned_eq (http_cerver_auth_generate_bearer_jwt (http_cerver, http_jwt), 1, NULL);
	test_check_null_ptr (http_cerver_auth_generate_jwt (http_cerver, http_jwt));
	http_jwt_delete (http_jwt);

	(void) cerver_teardown (cerver);

	cerver = tokens_cerver_create ();
	http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (tokens_cerver_init (cerver, JWT_ALG_HS256, TOKENS_SECRET_FILE, TOKENS_SECRET_FILE), 0, NULL);

	// values that are not a bearer, before they were read past their end
	test_check (!http_cerver_auth_validate_jwt (http_cerver, "", NULL, NULL), NULL);
	test_check (!http_cerver_auth_validate_jwt (http_cerver, "abc", NULL, NULL), NULL);
	test_check (!http_cerver_auth_validate_jwt (http_cerver, "Bearer", NULL, NULL), NULL);
	test_check (!http_cerver_auth_validate_jwt (http_cerver, "Bearer ", NULL, NULL), NULL);
	test_check (!http_cerver_auth_validate_jwt (http_cerver, NULL, NULL, NULL), NULL);

	http_jwt = (HttpJwt *) http_jwt_new ();
	http_cerver_auth_jwt_add_value (http_jwt, "id", "tokens");
	http_cerver_auth_jwt_add_value_int (http_jwt, "exp", TOKENS_FAR_EXPIRATION);

	// the extra value of the json is escaped
	test_check_unsigned_eq (
		http_cerver_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, "message", "a \"quoted\" \\ value"),
		0, NULL
	);

	test_check_unsigned_eq (http_jwt->bearer_len, strlen (http_jwt->bearer), NULL);
	test_check_unsigned_eq (http_jwt->json_len, strlen (http_jwt->json), NULL);

	json_error_t error;
	json_t *json = json_loads (http_jwt->json, 0, &error);
	test_check_ptr (json);
	test_check_str_eq (json_string_value (json_object_get (json, "message")), "a \"quoted\" \\ value", NULL);
	test_check_str_eq (json_string_value (json_object_get (json, "token")), http_jwt->bearer, NULL);
	json_decref (json);

	// the bearer is validated and its 64 bits "exp" is kept
	test_check (http_cerver_auth_validate_jwt (http_cerver, http_jwt->bearer, NULL, NULL), NULL);

	jwt_t *jwt = http_cerver_auth_jwt_verify (
		http_cerver, http_jwt->bearer + HTTP_JWT_BEARER_PREFIX_LENGTH,
		http_jwt->bearer_len - HTTP_JWT_BEARER_PREFIX_LENGTH
	);

	test_check_ptr (jwt);
	test_check_int_eq (json_integer_value (json_object_get (jwt->grants, "exp")), TOKENS_FAR_EXPIRATION, NULL);
	jwt_free (jwt);

	http_jwt_delete (http_jwt);

	// a long username with characters of two bytes was cut in the middle of one,
	// the json rejected it and the token was generated without it (users-service)
	char username[TOKENS_LONG_USERNAME_SIZE] = { 0 };
	for (size_t i = 0; (i + TOKENS_TWO_BYTES_LENGTH) < sizeof (username); i += TOKENS_TWO_BYTES_LENGTH) {
		(void) memcpy (username + i, TOKENS_TWO_BYTES, TOKENS_TWO_BYTES_LENGTH);
	}

	http_jwt = (HttpJwt *) http_jwt_new ();
	http_cerver_auth_jwt_add_value (http_jwt, "id", "tokens");
	http_cerver_auth_jwt_add_value (http_jwt, "username", username);
	test_check_unsigned_eq (http_cerver_auth_generate_bearer_jwt (http_cerver, http_jwt), 0, NULL);

	jwt = http_cerver_auth_jwt_verify (
		http_cerver, http_jwt->bearer + HTTP_JWT_BEARER_PREFIX_LENGTH,
		http_jwt->bearer_len - HTTP_JWT_BEARER_PREFIX_LENGTH
	);

	test_check_ptr (jwt);
	const char *claim = json_string_value (json_object_get (jwt->grants, "username"));
	test_check_ptr (claim);
	test_check (strlen (claim) < HTTP_JWT_VALUE_VALUE_SIZE, NULL);
	test_check_unsigned_eq (strlen (claim) % TOKENS_TWO_BYTES_LENGTH, 0, "a character was cut");
	test_check (!strncmp (claim, username, strlen (claim)), NULL);
	jwt_free (jwt);

	http_jwt_delete (http_jwt);

	(void) cerver_teardown (cerver);

	(void) remove (TOKENS_SECRET_FILE);
	(void) remove (TOKENS_SHORT_SECRET_FILE);
	(void) remove (TOKENS_EMPTY_FILE);

}

#pragma endregion

void http_tests_tokens (void) {

	(void) printf ("Testing HTTP tokens...\n");

	test_run (tokens_encode_empty);
	test_run (tokens_hmac_signature);
	test_run (tokens_algorithm);
	test_run (tokens_parts);
	test_run (tokens_keys);
	test_run (tokens_ecdsa);
	test_run (tokens_cerver_keys);
	test_run (tokens_cerver_claims);
	test_run (tokens_cerver_bearer);

	(void) printf ("Done!\n");

}
