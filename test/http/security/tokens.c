#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/collections/arena.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/http/core/http.h>
#include "cerver/http/core/types.h"
#include <cerver/http/security/auth.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/sign.h>

#include <cerver/utils/base64.h>

#include "../../test.h"
#include "../http.h"

#define TOKENS_CERVER_NAME				"tokens-cerver"
#define TOKENS_CERVER_PORT				8080
#define TOKENS_CERVER_CONNECTION_QUEUE	2

// an HS256 secret of the size of its hash, and one that is shorter
#define TOKENS_SECRET					"0123456789abcdef0123456789abcdef"
#define TOKENS_SHORT_SECRET				"0123456789abcdef"

// the same secret as a JWK, and the JWK of a secret that is shorter
#define TOKENS_SECRET_JWK				"{\"kty\":\"oct\",\"k\":\"MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY\"}"
#define TOKENS_SHORT_SECRET_JWK			"{\"kty\":\"oct\",\"k\":\"MDEyMzQ1Njc4OWFiY2RlZg\"}"

#define TOKENS_SECRET_FILE				"/tmp/cerver-tokens-secret"
#define TOKENS_SHORT_SECRET_FILE		"/tmp/cerver-tokens-short-secret"
#define TOKENS_EMPTY_FILE				"/tmp/cerver-tokens-empty"
#define TOKENS_SECRET_JWK_FILE			"/tmp/cerver-tokens-secret-jwk"
#define TOKENS_SHORT_SECRET_JWK_FILE	"/tmp/cerver-tokens-short-secret-jwk"

#define TOKENS_RSA_PRIVATE_FILE			"./test/http/keys/key.key"
#define TOKENS_RSA_PUBLIC_FILE			"./test/http/keys/key.pub"
#define TOKENS_RSA_WEAK_FILE			"./test/crypto/keys/weak.key"
#define TOKENS_RSA_WEAK_PUBLIC_FILE		"./test/crypto/keys/weak.pub"
#define TOKENS_P256_PRIVATE_FILE		"./test/crypto/keys/p256.key"
#define TOKENS_P256_PUBLIC_FILE			"./test/crypto/keys/p256.pub"
#define TOKENS_P384_PUBLIC_FILE			"./test/crypto/keys/p384.pub"
#define TOKENS_ED25519_PRIVATE_FILE		"./test/crypto/keys/ed25519.key"
#define TOKENS_ED25519_PUBLIC_FILE		"./test/crypto/keys/ed25519.pub"
#define TOKENS_ENCRYPTED_FILE			"./test/crypto/keys/encrypted.key"

#define TOKENS_FILE_SIZE				(1 << 14)
#define TOKENS_TOKEN_SIZE				(1 << 14)

// "exp" in 2100, after the 32 bits of time_t
#define TOKENS_FAR_EXPIRATION			4102444800LL

#define TOKENS_HOUR_SECONDS				3600
#define TOKENS_LEEWAY_SECONDS			(TOKENS_HOUR_SECONDS * 2)

#define TOKENS_ISSUER					"https://issuer.cerver"
#define TOKENS_SUBJECT					"user-7"
#define TOKENS_AUDIENCE					"cerver-tests"

// the tokens of a key with a random part that are signed and verified
#define TOKENS_RANDOM_SIGNATURES		(1 << 5)

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

// a token with the claims of the json as they are, signed with the key
static char *tokens_sign (const CerverJwtKey *key, const char *claims) {

	char *token = (char *) calloc (TOKENS_TOKEN_SIZE, 1);
	test_check_ptr (token);

	size_t length = 0;
	test_check_int_eq (
		cerver_jwt_sign (key, claims, strlen (claims), token, TOKENS_TOKEN_SIZE, &length), CERVER_JWT_ERROR_NONE, claims
	);

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

	CerverCryptoKey *key = cerver_crypto_key_create_secret (secret, strlen (secret), NULL);
	test_check_ptr (key);

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	size_t signature_length = 0;
	test_check_true (
		cerver_crypto_sign (
			key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, token, position,
			signature, sizeof (signature), &signature_length
		)
	);

	token[position++] = '.';
	(void) base64url_encode (token + position, signature, signature_length);

	cerver_crypto_key_delete (key);

	return token;

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
	Cerver *cerver, const CerverJwtAlgorithm algorithm,
	const char *private_file, const char *public_file
) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	test_check_unsigned_eq (cerver_http_auth_set_jwt_algorithm (http_cerver, algorithm), 0, NULL);
	if (private_file) cerver_http_auth_set_jwt_priv_key_filename (http_cerver, private_file);
	if (public_file) cerver_http_auth_set_jwt_pub_key_filename (http_cerver, public_file);

	return cerver_http_init (http_cerver);

}

// a cerver with those keys starts, or it does not
static void tokens_cerver_starts (
	const CerverJwtAlgorithm algorithm,
	const char *private_file, const char *public_file, const bool expected
) {

	Cerver *cerver = tokens_cerver_create ();

	const u8 result = tokens_cerver_init (cerver, algorithm, private_file, public_file);
	test_check_bool_eq (result == 0, expected, private_file ? private_file : public_file);

	(void) cerver_teardown (cerver);

}

static bool tokens_cerver_verifies (const HttpCerver *http_cerver, const char *token) {

	Arena *arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
	test_check_ptr (arena);

	const bool verified = cerver_http_auth_jwt_verify (http_cerver, arena, token, strlen (token)) != NULL;

	arena_delete (arena);

	return verified;

}

// the claims of the bearer of a jwt, read in the arena
static const CerverJsonValue *tokens_cerver_verified_claims (
	const HttpCerver *http_cerver, Arena *arena, const HttpJwt *http_jwt
) {

	const CerverJsonValue *claims = cerver_http_auth_jwt_verify (
		http_cerver, arena,
		http_jwt->bearer + HTTP_JWT_BEARER_PREFIX_LENGTH,
		http_jwt->bearer_len - HTTP_JWT_BEARER_PREFIX_LENGTH
	);

	test_check_ptr (claims);

	return claims;

}

// a cerver of HS256 with the secret of the tests
static Cerver *tokens_secret_cerver_create (void) {

	Cerver *cerver = tokens_cerver_create ();

	test_check_unsigned_eq (
		tokens_cerver_init (cerver, CERVER_JWT_ALGORITHM_HS256, TOKENS_SECRET_FILE, TOKENS_SECRET_FILE), 0, NULL
	);

	return cerver;

}

#pragma endregion

#pragma region tokens

// a token without values has the header of its algorithm and the claims of an empty object
static void tokens_encode_empty (void) {

	Cerver *cerver = tokens_secret_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpJwt *http_jwt = (HttpJwt *) cerver_http_jwt_new ();

	char *token = cerver_http_auth_generate_jwt (http_cerver, http_jwt);
	test_check_ptr (token);
	test_check (!strncmp (token, "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30.", strlen ("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30.")), token);

	test_check (tokens_cerver_verifies (http_cerver, token), NULL);

	free (token);
	cerver_http_jwt_delete (http_jwt);

	(void) cerver_teardown (cerver);

}

// a signature that is changed, cut or longer is rejected
static void tokens_hmac_signature (void) {

	Cerver *cerver = tokens_secret_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	char *token = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);

	const size_t length = strlen (token);

	char *changed = strdup (token);
	changed[length - 2] = (changed[length - 2] == 'A') ? 'B' : 'A';
	test_check (!tokens_cerver_verifies (http_cerver, changed), NULL);
	free (changed);

	char *cut = strndup (token, length - 4);
	test_check (!tokens_cerver_verifies (http_cerver, cut), NULL);
	free (cut);

	char *longer = (char *) calloc (length + 8, 1);
	(void) snprintf (longer, length + 8, "%sAAAA", token);
	test_check (!tokens_cerver_verifies (http_cerver, longer), NULL);
	free (longer);

	// without a signature
	char *unsigned_token = strndup (token, (size_t) (strrchr (token, '.') - token) + 1);
	test_check (!tokens_cerver_verifies (http_cerver, unsigned_token), NULL);
	free (unsigned_token);

	// the token of another secret
	char *other = tokens_forge (
		"fedcba9876543210fedcba9876543210", "{\"alg\":\"HS256\",\"typ\":\"JWT\"}", "{\"id\":\"tokens\"}"
	);

	test_check (!tokens_cerver_verifies (http_cerver, other), NULL);
	free (other);

	free (token);

	(void) cerver_teardown (cerver);

}

// a token never chooses how it is verified
static void tokens_algorithm (void) {

	Cerver *cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (
		tokens_cerver_init (cerver, CERVER_JWT_ALGORITHM_RS256, TOKENS_RSA_PRIVATE_FILE, TOKENS_RSA_PUBLIC_FILE), 0, NULL
	);

	// HS256 signed with the public PEM as its secret, the classic confusion
	unsigned char pem[TOKENS_FILE_SIZE] = { 0 };
	(void) tokens_file_read (TOKENS_RSA_PUBLIC_FILE, pem, sizeof (pem) - 1);
	char *confused = tokens_forge (
		(const char *) pem,
		"{\"alg\":\"HS256\",\"typ\":\"JWT\"}", "{\"id\":\"tokens\"}"
	);

	test_check (!tokens_cerver_verifies (http_cerver, confused), "the public key as the secret of an HMAC");
	free (confused);

	// the same HMAC under the header of the cerver
	confused = tokens_forge (
		(const char *) pem,
		"{\"alg\":\"RS256\",\"typ\":\"JWT\"}", "{\"id\":\"tokens\"}"
	);

	test_check (!tokens_cerver_verifies (http_cerver, confused), "an HMAC as the signature of RS256");
	free (confused);

	// its own tokens
	char *valid = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check (tokens_cerver_verifies (http_cerver, valid), NULL);
	free (valid);

	(void) cerver_teardown (cerver);

	// "none" and the names in other cases are not accepted
	cerver = tokens_secret_cerver_create ();
	http_cerver = (HttpCerver *) cerver->cerver_data;

	char *none = tokens_forge (TOKENS_SECRET, "{\"alg\":\"none\"}", "{\"id\":\"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, none), NULL);
	free (none);

	char *lower = tokens_forge (TOKENS_SECRET, "{\"alg\":\"hs256\"}", "{\"id\":\"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, lower), NULL);
	free (lower);

	char *other = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS384\"}", "{\"id\":\"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, other), NULL);
	free (other);

	valid = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "{\"id\":\"tokens\"}");
	test_check (tokens_cerver_verifies (http_cerver, valid), NULL);
	free (valid);

	// a token of the cerver of RS256 is not one of this cerver
	test_check (!tokens_cerver_verifies (http_cerver, base_token), NULL);

	(void) cerver_teardown (cerver);

}

// the parts are canonical base64url and json objects without repeated keys
static void tokens_parts (void) {

	Cerver *cerver = tokens_secret_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	char *duplicated = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\",\"alg\":\"none\"}", "{\"id\":\"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, duplicated), NULL);
	free (duplicated);

	char *array = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "[\"tokens\"]");
	test_check (!tokens_cerver_verifies (http_cerver, array), NULL);
	free (array);

	char *repeated = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "{\"id\":\"one\",\"id\":\"two\"}");
	test_check (!tokens_cerver_verifies (http_cerver, repeated), NULL);
	free (repeated);

	char *critical = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\",\"crit\":[\"exp\"]}", "{\"id\":\"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, critical), NULL);
	free (critical);

	char *token = tokens_forge (TOKENS_SECRET, "{\"alg\":\"HS256\"}", "{\"id\":\"tokens\"}");
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);

	const size_t length = strlen (token);

	// a fourth part
	char *four = (char *) calloc (length + 5, 1);
	(void) snprintf (four, length + 5, "%s.e30", token);
	test_check (!tokens_cerver_verifies (http_cerver, four), NULL);
	free (four);

	// the padding of base64
	const char *first_separator = strchr (token, '.');
	char *padded = (char *) calloc (length + 4, 1);
	(void) snprintf (padded, length + 4, "%.*s==%s", (int) (first_separator - token), token, first_separator);
	test_check (!tokens_cerver_verifies (http_cerver, padded), NULL);
	free (padded);

	free (token);

	(void) cerver_teardown (cerver);

}

// a cerver with a key that has a random part signs and verifies its own tokens: ES256 and PS256
static void tokens_random_signatures (
	const CerverJwtAlgorithm algorithm, const char *private_file, const char *public_file
) {

	Cerver *cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (tokens_cerver_init (cerver, algorithm, private_file, public_file), 0, private_file);

	for (int i = 0; i < TOKENS_RANDOM_SIGNATURES; i++) {
		char *token = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
		test_check (tokens_cerver_verifies (http_cerver, token), private_file);
		free (token);
	}

	(void) cerver_teardown (cerver);

}

// a cerver verifies with its public key and only with it: the one that has just the public key of a pair takes
// the tokens of the one that signs with its private key, that has nothing to verify its own with
static void tokens_public_key (void) {

	Cerver *signer = tokens_cerver_create ();
	HttpCerver *signer_http = (HttpCerver *) signer->cerver_data;
	test_check_unsigned_eq (
		tokens_cerver_init (signer, CERVER_JWT_ALGORITHM_ES256, TOKENS_P256_PRIVATE_FILE, NULL), 0, NULL
	);

	Cerver *verifier = tokens_cerver_create ();
	HttpCerver *verifier_http = (HttpCerver *) verifier->cerver_data;
	test_check_unsigned_eq (
		tokens_cerver_init (verifier, CERVER_JWT_ALGORITHM_ES256, NULL, TOKENS_P256_PUBLIC_FILE), 0, NULL
	);

	test_check_null_ptr (verifier_http->jwt_private_key);
	test_check_null_ptr (signer_http->jwt_public_key);

	char *token = tokens_sign (signer_http->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check_bool_eq (tokens_cerver_verifies (verifier_http, token), true, NULL);
	test_check_bool_eq (tokens_cerver_verifies (signer_http, token), false, NULL);
	free (token);

	(void) cerver_teardown (verifier);
	(void) cerver_teardown (signer);

}

static void tokens_signatures (void) {

	tokens_random_signatures (CERVER_JWT_ALGORITHM_ES256, TOKENS_P256_PRIVATE_FILE, TOKENS_P256_PUBLIC_FILE);
	tokens_random_signatures (CERVER_JWT_ALGORITHM_PS256, TOKENS_RSA_PRIVATE_FILE, TOKENS_RSA_PUBLIC_FILE);
	tokens_random_signatures (CERVER_JWT_ALGORITHM_EDDSA, TOKENS_ED25519_PRIVATE_FILE, TOKENS_ED25519_PUBLIC_FILE);

	tokens_public_key ();

}

#pragma endregion

#pragma region cerver

// a key that does not match the algorithm, or that is weak, does not start the cerver
static void tokens_cerver_keys (void) {

	tokens_file_write (TOKENS_SECRET_FILE, TOKENS_SECRET);
	tokens_file_write (TOKENS_SHORT_SECRET_FILE, TOKENS_SHORT_SECRET);
	tokens_file_write (TOKENS_EMPTY_FILE, "");
	tokens_file_write (TOKENS_SECRET_JWK_FILE, " " TOKENS_SECRET_JWK "\n");
	tokens_file_write (TOKENS_SHORT_SECRET_JWK_FILE, TOKENS_SHORT_SECRET_JWK);

	// the key of another type, of another curve, and one of RSA with less bits
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_ES256, NULL, TOKENS_RSA_PUBLIC_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_ES256, NULL, TOKENS_P384_PUBLIC_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_RS256, TOKENS_RSA_WEAK_FILE, TOKENS_RSA_WEAK_PUBLIC_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_PS256, NULL, TOKENS_RSA_WEAK_PUBLIC_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_EDDSA, NULL, TOKENS_P256_PUBLIC_FILE, false);

	// a secret that is shorter than its digest, an empty file and a file that is not there
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, TOKENS_SHORT_SECRET_FILE, TOKENS_SHORT_SECRET_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, NULL, TOKENS_EMPTY_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, NULL, TOKENS_EMPTY_FILE "-missing", false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS384, TOKENS_SECRET_FILE, TOKENS_SECRET_FILE, false);

	// the public PEM as the secret of an HS256 cerver
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, NULL, TOKENS_RSA_PUBLIC_FILE, false);

	// a public key can not be the private key of a cerver, nor a private PEM its public key;
	// a PEM with a passphrase is never decrypted
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_RS256, TOKENS_RSA_PUBLIC_FILE, NULL, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_RS256, NULL, TOKENS_RSA_PRIVATE_FILE, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_RS256, TOKENS_ENCRYPTED_FILE, NULL, false);

	// the keys of each kind of algorithm
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, TOKENS_SECRET_FILE, TOKENS_SECRET_FILE, true);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_RS512, TOKENS_RSA_PRIVATE_FILE, TOKENS_RSA_PUBLIC_FILE, true);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_PS384, TOKENS_RSA_PRIVATE_FILE, TOKENS_RSA_PUBLIC_FILE, true);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_ES256, TOKENS_P256_PRIVATE_FILE, TOKENS_P256_PUBLIC_FILE, true);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_EDDSA, TOKENS_ED25519_PRIVATE_FILE, TOKENS_ED25519_PUBLIC_FILE, true);

	// a file that starts an object is a JWK, also for a secret
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, TOKENS_SECRET_JWK_FILE, TOKENS_SECRET_JWK_FILE, true);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_HS256, TOKENS_SHORT_SECRET_JWK_FILE, NULL, false);
	tokens_cerver_starts (CERVER_JWT_ALGORITHM_RS256, TOKENS_SECRET_JWK_FILE, NULL, false);

	Cerver *cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	test_check_unsigned_eq (cerver_http_auth_set_jwt_algorithm (http_cerver, CERVER_JWT_ALGORITHM_NONE), 1, NULL);
	test_check_unsigned_eq (
		cerver_http_auth_set_jwt_algorithm (http_cerver, (CerverJwtAlgorithm) CERVER_JWT_ALGORITHMS), 1, NULL
	);
	test_check_unsigned_eq (cerver_http_auth_set_jwt_algorithm (NULL, CERVER_JWT_ALGORITHM_HS256), 1, NULL);
	test_check_int_eq (http_cerver->jwt_alg, CERVER_HTTP_DEFAULT_JWT_ALGORITHM, NULL);
	test_check_unsigned_eq (
		tokens_cerver_init (cerver, CERVER_JWT_ALGORITHM_RS256, TOKENS_RSA_PRIVATE_FILE, TOKENS_RSA_PUBLIC_FILE), 0, NULL
	);
	(void) cerver_teardown (cerver);

}

// the secret of a JWK is the secret of its bytes: the tokens of one cerver are the ones of the other
static void tokens_cerver_jwk (void) {

	Cerver *secret_cerver = tokens_secret_cerver_create ();
	HttpCerver *secret_http = (HttpCerver *) secret_cerver->cerver_data;

	char *token = tokens_sign (secret_http->jwt_private_key, "{\"id\": \"tokens\"}");

	(void) cerver_teardown (secret_cerver);

	Cerver *jwk_cerver = tokens_cerver_create ();
	HttpCerver *jwk_http = (HttpCerver *) jwk_cerver->cerver_data;
	test_check_unsigned_eq (
		tokens_cerver_init (jwk_cerver, CERVER_JWT_ALGORITHM_HS256, TOKENS_SECRET_JWK_FILE, TOKENS_SECRET_JWK_FILE),
		0, NULL
	);

	test_check (tokens_cerver_verifies (jwk_http, token), NULL);

	char *again = tokens_sign (jwk_http->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check_str_eq (again, token, NULL);

	free (again);
	free (token);

	(void) cerver_teardown (jwk_cerver);

}

// "exp", "nbf", "iat", "iss", "sub", "aud" and the size with the settings of the cerver
static void tokens_cerver_claims (void) {

	Cerver *cerver = tokens_secret_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

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
	cerver_http_auth_set_jwt_leeway_seconds (http_cerver, TOKENS_LEEWAY_SECONDS);
	test_check (tokens_cerver_verifies (http_cerver, expired), NULL);
	cerver_http_auth_set_jwt_leeway_seconds (http_cerver, 0);
	free (expired);

	(void) snprintf (grants, sizeof (grants), "{\"nbf\": %ld}", now + TOKENS_HOUR_SECONDS);
	char *early = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, early), NULL);

	// and one that is ahead
	cerver_http_auth_set_jwt_leeway_seconds (http_cerver, TOKENS_LEEWAY_SECONDS);
	test_check (tokens_cerver_verifies (http_cerver, early), NULL);
	cerver_http_auth_set_jwt_leeway_seconds (http_cerver, 0);
	free (early);

	// a token that was issued in the future is not valid, one that was issued before is
	(void) snprintf (grants, sizeof (grants), "{\"iat\": %ld}", now + TOKENS_HOUR_SECONDS);
	char *issued = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, issued), "a token that was issued in the future");

	cerver_http_auth_set_jwt_leeway_seconds (http_cerver, TOKENS_LEEWAY_SECONDS);
	test_check (tokens_cerver_verifies (http_cerver, issued), NULL);
	cerver_http_auth_set_jwt_leeway_seconds (http_cerver, 0);
	free (issued);

	(void) snprintf (grants, sizeof (grants), "{\"iat\": %ld}", now - TOKENS_HOUR_SECONDS);
	issued = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, issued), NULL);
	free (issued);

	// time claims that are not integers fail closed
	char *text_nbf = tokens_sign (http_cerver->jwt_private_key, "{\"nbf\": \"0\"}");
	test_check (!tokens_cerver_verifies (http_cerver, text_nbf), NULL);
	free (text_nbf);

	char *text_exp = tokens_sign (http_cerver->jwt_private_key, "{\"exp\": \"99999999999\"}");
	test_check (!tokens_cerver_verifies (http_cerver, text_exp), NULL);
	free (text_exp);

	// a real is not an integer, whatever its value
	char *real_exp = tokens_sign (http_cerver->jwt_private_key, "{\"exp\": 4102444800.0}");
	test_check (!tokens_cerver_verifies (http_cerver, real_exp), "a real \"exp\"");
	free (real_exp);

	char *real_nbf = tokens_sign (http_cerver->jwt_private_key, "{\"nbf\": 0.5}");
	test_check (!tokens_cerver_verifies (http_cerver, real_nbf), "a real \"nbf\"");
	free (real_nbf);

	char *exponent_nbf = tokens_sign (http_cerver->jwt_private_key, "{\"nbf\": 1e0}");
	test_check (!tokens_cerver_verifies (http_cerver, exponent_nbf), "a \"nbf\" with an exponent");
	free (exponent_nbf);

	char *real_iat = tokens_sign (http_cerver->jwt_private_key, "{\"iat\": 0.5}");
	test_check (!tokens_cerver_verifies (http_cerver, real_iat), "a real \"iat\"");
	free (real_iat);

	// a claim that the cerver does not check can be of any type
	token = tokens_sign (http_cerver->jwt_private_key, "{\"iss\": 0.5, \"sub\": 7, \"aud\": null, \"values\": [1.5, null, true]}");
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	// the claims are read in an arena: without one there are none
	token = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check_null_ptr (cerver_http_auth_jwt_verify (http_cerver, NULL, token, strlen (token)));
	test_check_null_ptr (cerver_http_auth_jwt_verify (NULL, NULL, token, strlen (token)));
	free (token);

	test_check_null_ptr (http_decode_data_into_json (NULL));

	// "exp" required
	cerver_http_auth_set_jwt_require_expiration (http_cerver, true);
	token = tokens_sign (http_cerver->jwt_private_key, "{\"id\": \"tokens\"}");
	test_check (!tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld}", TOKENS_FAR_EXPIRATION);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	free (token);

	// the subject
	cerver_http_auth_set_jwt_subject (http_cerver, TOKENS_SUBJECT);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"sub\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_SUBJECT);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, token), "the subject of the cerver");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"sub\": \"%s-other\"}", TOKENS_FAR_EXPIRATION, TOKENS_SUBJECT);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "another subject");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"sub\": 7}", TOKENS_FAR_EXPIRATION);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "a subject that is a number");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld}", TOKENS_FAR_EXPIRATION);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "a token without its subject");

	// without a subject any token is taken again
	cerver_http_auth_set_jwt_subject (http_cerver, NULL);
	test_check (tokens_cerver_verifies (http_cerver, token), NULL);
	cerver_http_auth_set_jwt_subject (NULL, TOKENS_SUBJECT);
	free (token);

	// the issuer and the audience
	cerver_http_auth_set_jwt_issuer (http_cerver, TOKENS_ISSUER);
	cerver_http_auth_set_jwt_audience (http_cerver, TOKENS_AUDIENCE);

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

	// an issuer and an audience that only start as the ones of the cerver, or that are only their start
	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s/other\", \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "an issuer that is longer");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"https://issuer\", \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "an issuer that is shorter");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": \"%s-other\"}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "an audience that is longer");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": [\"%s-other\", \"cerver\"]}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "audiences that only look like the one of the cerver");
	free (token);

	// an issuer and an audience that are not strings, and the ones that are missing
	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": 7, \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "an issuer that is a number");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": [7, null, [\"%s\"]]}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "audiences that are not strings");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "a token without its issuer");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "a token without its audience");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": [\"other\"]}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), NULL);

	free (token);

	// a token longer than the limit is not decoded; one that is valid, with the limit at its length and under it
	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"%s\", \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_ISSUER, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	cerver_http_auth_set_jwt_max_size (http_cerver, strlen (token));
	test_check (tokens_cerver_verifies (http_cerver, token), "a token with the size of the limit");
	cerver_http_auth_set_jwt_max_size (http_cerver, strlen (token) - 1);
	test_check (!tokens_cerver_verifies (http_cerver, token), "a token over the limit");

	// without a limit of the cerver the one of every token is the limit
	cerver_http_auth_set_jwt_max_size (http_cerver, 0);
	test_check (tokens_cerver_verifies (http_cerver, token), "a token without a limit of the cerver");
	cerver_http_auth_set_jwt_max_size (http_cerver, CERVER_HTTP_DEFAULT_JWT_MAX_SIZE);
	free (token);

	// the claims that the header of a token repeats are the ones of its payload (RFC 7519 section 5.3)
	token = tokens_forge (
		TOKENS_SECRET,
		"{\"alg\":\"HS256\",\"typ\":\"JWT\",\"iss\":\"" TOKENS_ISSUER "\",\"sub\":\"one\",\"aud\":\"" TOKENS_AUDIENCE "\"}",
		"{\"exp\":4102444800,\"iss\":\"" TOKENS_ISSUER "\",\"sub\":\"one\",\"aud\":\"" TOKENS_AUDIENCE "\"}"
	);

	test_check (tokens_cerver_verifies (http_cerver, token), "a header with the claims of its payload");
	free (token);

	token = tokens_forge (
		TOKENS_SECRET,
		"{\"alg\":\"HS256\",\"typ\":\"JWT\",\"iss\":\"other\"}",
		"{\"exp\":4102444800,\"iss\":\"" TOKENS_ISSUER "\",\"aud\":\"" TOKENS_AUDIENCE "\"}"
	);

	test_check (!tokens_cerver_verifies (http_cerver, token), "a header with another issuer");
	free (token);

	token = tokens_forge (
		TOKENS_SECRET,
		"{\"alg\":\"HS256\",\"typ\":\"JWT\",\"sub\":\"one\"}",
		"{\"exp\":4102444800,\"iss\":\"" TOKENS_ISSUER "\",\"sub\":\"other\",\"aud\":\"" TOKENS_AUDIENCE "\"}"
	);

	test_check (!tokens_cerver_verifies (http_cerver, token), "a header with another subject");
	free (token);

	token = tokens_forge (
		TOKENS_SECRET,
		"{\"alg\":\"HS256\",\"typ\":\"JWT\",\"aud\":[\"" TOKENS_AUDIENCE "\"]}",
		"{\"exp\":4102444800,\"iss\":\"" TOKENS_ISSUER "\",\"aud\":\"" TOKENS_AUDIENCE "\"}"
	);

	test_check (!tokens_cerver_verifies (http_cerver, token), "a header with another audience");
	free (token);

	// an empty issuer of the cerver is only the one of a token with an empty issuer
	cerver_http_auth_set_jwt_issuer (http_cerver, "");

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": \"\", \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (tokens_cerver_verifies (http_cerver, token), "an empty issuer");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": 7, \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "an issuer that is a number against an empty one");
	free (token);

	(void) snprintf (grants, sizeof (grants), "{\"exp\": %lld, \"iss\": [], \"aud\": \"%s\"}", TOKENS_FAR_EXPIRATION, TOKENS_AUDIENCE);
	token = tokens_sign (http_cerver->jwt_private_key, grants);
	test_check (!tokens_cerver_verifies (http_cerver, token), "an issuer that is an array against an empty one");
	free (token);

	(void) cerver_teardown (cerver);

}

// the claims {"values": [[...]]} with some arrays inside each other
static void tokens_nested_claims (char *claims, const size_t arrays) {

	size_t length = (size_t) snprintf (claims, TOKENS_FILE_SIZE, "{\"values\": ");

	(void) memset (claims + length, '[', arrays);
	length += arrays;

	(void) memset (claims + length, ']', arrays);
	length += arrays;

	claims[length] = '}';
	claims[length + 1] = '\0';

}

// the claims have at most HTTP_JWT_CLAIMS_DEPTH levels, their object is the first one
static void tokens_cerver_depth (void) {

	Cerver *cerver = tokens_secret_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	char claims[TOKENS_FILE_SIZE] = { 0 };

	tokens_nested_claims (claims, HTTP_JWT_CLAIMS_DEPTH - 1);
	char *token = tokens_sign (http_cerver->jwt_private_key, claims);
	test_check (tokens_cerver_verifies (http_cerver, token), "claims with as many levels as the limit");
	free (token);

	tokens_nested_claims (claims, HTTP_JWT_CLAIMS_DEPTH);
	token = tokens_sign (http_cerver->jwt_private_key, claims);
	test_check (!tokens_cerver_verifies (http_cerver, token), "claims with a level over the limit");
	free (token);

	(void) cerver_teardown (cerver);

}

// the bearer API of auth.c: the prefix, a missing key, the json and the 64 bits values
static void tokens_cerver_bearer (void) {

	Cerver *cerver = tokens_cerver_create ();
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	// without keys nothing is validated nor generated
	test_check (!cerver_http_auth_validate_jwt (http_cerver, "Bearer abc", NULL, NULL), NULL);

	HttpJwt *http_jwt = (HttpJwt *) cerver_http_jwt_new ();
	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt (http_cerver, http_jwt), 1, NULL);
	test_check_null_ptr (cerver_http_auth_generate_jwt (http_cerver, http_jwt));
	cerver_http_jwt_delete (http_jwt);

	(void) cerver_teardown (cerver);

	cerver = tokens_secret_cerver_create ();
	http_cerver = (HttpCerver *) cerver->cerver_data;

	// values that are not a bearer, before they were read past their end
	test_check (!cerver_http_auth_validate_jwt (http_cerver, "", NULL, NULL), NULL);
	test_check (!cerver_http_auth_validate_jwt (http_cerver, "abc", NULL, NULL), NULL);
	test_check (!cerver_http_auth_validate_jwt (http_cerver, "Bearer", NULL, NULL), NULL);
	test_check (!cerver_http_auth_validate_jwt (http_cerver, "Bearer ", NULL, NULL), NULL);
	test_check (!cerver_http_auth_validate_jwt (http_cerver, NULL, NULL, NULL), NULL);

	http_jwt = (HttpJwt *) cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value (http_jwt, "id", "tokens");
	cerver_http_auth_jwt_add_value_int (http_jwt, "exp", TOKENS_FAR_EXPIRATION);

	// the extra value of the json is escaped
	test_check_unsigned_eq (
		cerver_http_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, "message", "a \"quoted\" \\ value"),
		0, NULL
	);

	test_check_unsigned_eq (http_jwt->bearer_len, strlen (http_jwt->bearer), NULL);
	test_check_unsigned_eq (http_jwt->json_len, strlen (http_jwt->json), NULL);

	// the bearer of these values, byte by byte: the keys sorted ("exp" before "id") and the HMAC of the
	// openssl tool over its first two parts
	test_check_str_eq (
		http_jwt->bearer,
		"Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjQxMDI0NDQ4MDAsImlkIjoidG9rZW5zIn0."
		"plge7FblP7anBnUIBlG_Ab4Y_mHaOvd8t8Z0mtl6cKw",
		NULL
	);

	char expected[TOKENS_TOKEN_SIZE] = { 0 };
	(void) snprintf (
		expected, TOKENS_TOKEN_SIZE,
		"{\"token\": \"%s\", \"message\": \"a \\\"quoted\\\" \\\\ value\"}", http_jwt->bearer
	);

	test_check_str_eq (http_jwt->json, expected, NULL);

	// the json without an extra value
	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt_json (http_cerver, http_jwt), 0, NULL);
	(void) snprintf (expected, TOKENS_TOKEN_SIZE, "{\"token\": \"%s\"}", http_jwt->bearer);
	test_check_str_eq (http_jwt->json, expected, NULL);
	test_check_unsigned_eq (http_jwt->json_len, strlen (expected), NULL);

	// an extra value with the key of the token takes its place
	test_check_unsigned_eq (
		cerver_http_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, "token", "replaced"),
		0, NULL
	);

	test_check_str_eq (http_jwt->json, "{\"token\": \"replaced\"}", NULL);

	// a value that is not UTF-8 and a json that does not fit leave no json
	test_check_unsigned_eq (
		cerver_http_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, "message", "\xff"),
		1, "a json with a value that is not UTF-8"
	);

	test_check_unsigned_eq (http_jwt->json_len, 0, NULL);
	test_check_str_eq (http_jwt->json, "", NULL);

	char *long_value = (char *) malloc (HTTP_JWT_TOKEN_SIZE);
	test_check_ptr (long_value);
	(void) memset (long_value, 'v', HTTP_JWT_TOKEN_SIZE - 1);
	long_value[HTTP_JWT_TOKEN_SIZE - 1] = '\0';

	test_check_unsigned_eq (
		cerver_http_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, "message", long_value),
		1, "a json that does not fit"
	);

	test_check_unsigned_eq (http_jwt->json_len, 0, NULL);
	test_check_str_eq (http_jwt->json, "", NULL);

	free (long_value);

	test_check_unsigned_eq (
		cerver_http_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, NULL, "value"), 1, NULL
	);

	test_check_unsigned_eq (
		cerver_http_auth_generate_bearer_jwt_json_with_value (http_cerver, http_jwt, "message", NULL), 1, NULL
	);

	// the bearer is validated and its 64 bits "exp" is kept
	test_check (cerver_http_auth_validate_jwt (http_cerver, http_jwt->bearer, NULL, NULL), NULL);

	Arena *arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
	test_check_ptr (arena);

	const CerverJsonValue *claims = tokens_cerver_verified_claims (http_cerver, arena, http_jwt);
	test_check_int_eq (
		cerver_json_integer_value (cerver_json_object_get (claims, "exp")), TOKENS_FAR_EXPIRATION, NULL
	);

	// the claims as json have the members in the order of the token, that has its keys sorted
	char *decoded = NULL;
	test_check (
		cerver_http_auth_validate_jwt (http_cerver, http_jwt->bearer, http_decode_data_into_json, (void **) &decoded),
		NULL
	);

	test_check_ptr (decoded);
	test_check_str_eq (decoded, "{\"exp\": 4102444800, \"id\": \"tokens\"}", NULL);
	free (decoded);

	// a token that is not valid leaves the decoded data as it was
	decoded = NULL;
	test_check (
		!cerver_http_auth_validate_jwt (http_cerver, "Bearer a.b.c", http_decode_data_into_json, (void **) &decoded),
		NULL
	);

	test_check_null_ptr (decoded);

	cerver_http_jwt_delete (http_jwt);

	// a long username with characters of two bytes was cut in the middle of one,
	// the json rejected it and the token was generated without it (users-service)
	char username[TOKENS_LONG_USERNAME_SIZE] = { 0 };
	for (size_t i = 0; (i + TOKENS_TWO_BYTES_LENGTH) < sizeof (username); i += TOKENS_TWO_BYTES_LENGTH) {
		(void) memcpy (username + i, TOKENS_TWO_BYTES, TOKENS_TWO_BYTES_LENGTH);
	}

	http_jwt = (HttpJwt *) cerver_http_jwt_new ();
	cerver_http_auth_jwt_add_value (http_jwt, "id", "tokens");
	cerver_http_auth_jwt_add_value (http_jwt, "username", username);
	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt (http_cerver, http_jwt), 0, NULL);

	claims = tokens_cerver_verified_claims (http_cerver, arena, http_jwt);

	const char *claim = cerver_json_string_value (cerver_json_object_get (claims, "username"));
	test_check_ptr (claim);
	test_check (strlen (claim) < HTTP_JWT_VALUE_VALUE_SIZE, NULL);
	test_check_unsigned_eq (strlen (claim) % TOKENS_TWO_BYTES_LENGTH, 0, "a character was cut");
	test_check (!strncmp (claim, username, strlen (claim)), NULL);

	arena_delete (arena);

	cerver_http_jwt_delete (http_jwt);

	// the values of a token with every character as its longest escape fit in its claims, not in its bearer
	http_jwt = (HttpJwt *) cerver_http_jwt_new ();

	char control[HTTP_JWT_VALUE_VALUE_SIZE] = { 0 };
	(void) memset (control, '\x01', HTTP_JWT_VALUE_VALUE_SIZE - 1);

	for (unsigned int i = 0; i < HTTP_JWT_VALUES_SIZE; i++) {
		char key[HTTP_JWT_VALUE_KEY_SIZE] = { 0 };
		(void) memset (key, '\x02', HTTP_JWT_VALUE_KEY_SIZE - 1);
		key[0] = (char) ('a' + i);

		cerver_http_auth_jwt_add_value (http_jwt, key, control);
	}

	test_check_unsigned_eq (http_jwt->n_values, HTTP_JWT_VALUES_SIZE, NULL);

	char *large = cerver_http_auth_generate_jwt (http_cerver, http_jwt);
	test_check_ptr (large);
	test_check (strlen (large) > HTTP_JWT_BEARER_SIZE, "the token of the largest values fits in a bearer");
	free (large);

	test_check_unsigned_eq (cerver_http_auth_generate_bearer_jwt (http_cerver, http_jwt), 1, "a bearer that does not fit");
	test_check_unsigned_eq (http_jwt->bearer_len, 0, NULL);
	test_check_str_eq (http_jwt->bearer, "", NULL);

	cerver_http_jwt_delete (http_jwt);

	(void) cerver_teardown (cerver);

	(void) remove (TOKENS_SECRET_FILE);
	(void) remove (TOKENS_SHORT_SECRET_FILE);
	(void) remove (TOKENS_EMPTY_FILE);
	(void) remove (TOKENS_SECRET_JWK_FILE);
	(void) remove (TOKENS_SHORT_SECRET_JWK_FILE);

}

#pragma endregion

void http_tests_tokens (void) {

	(void) printf ("Testing HTTP tokens...\n");

	// the files of the secrets of every test
	tokens_file_write (TOKENS_SECRET_FILE, TOKENS_SECRET);

	test_run (tokens_cerver_keys);
	test_run (tokens_cerver_jwk);
	test_run (tokens_encode_empty);
	test_run (tokens_hmac_signature);
	test_run (tokens_algorithm);
	test_run (tokens_parts);
	test_run (tokens_signatures);
	test_run (tokens_cerver_claims);
	test_run (tokens_cerver_depth);
	test_run (tokens_cerver_bearer);

	(void) printf ("Done!\n");

}
