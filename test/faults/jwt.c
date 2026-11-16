#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/arena.h>

#include <cerver/crypto/key.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include "cerver/jwt/internal.h"
#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/sign.h>
#include <cerver/jwt/verify.h>

#include "../test.h"
#include "faults.h"
#include "jwt.h"

// the allocations of the tokens that fail, with a fault injected in one of its calls

#define FAULTS_JWT_SECRET				"0123456789abcdef0123456789abcdef"

// the keys of the tests of cerver/crypto
#define FAULTS_JWT_KEYS_PATH			"./test/crypto/keys/"

#define FAULTS_JWT_TOKEN_SIZE			4096

// an arena whose chunks are smaller than the claims of a token: their text and their values get blocks of
// their own, so each of them is an allocation that can fail
#define FAULTS_JWT_SMALL_CHUNK			64

// the allocations of the arena of a token: at least the text of its claims and their values
#define FAULTS_JWT_ARENA_ALLOCATIONS	2

#define FAULTS_JWT_CLAIMS				"{\"id\":\"faults\",\"values\":[1,2,3,4,5,6,7,8]}"

// the key of the tokens of a secret is its own allocation, after the ones of its key of the provider:
// without it there is no key
static void faults_jwt_key (void) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	CerverJwtError error = CERVER_JWT_ERROR_NONE;

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (CerverJwtKey));
	CerverJwtKey *key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, FAULTS_JWT_SECRET, strlen (FAULTS_JWT_SECRET), &error
	);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the key was created");

	// its key of the provider
	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	key = cerver_jwt_key_create_secret (
		CERVER_JWT_ALGORITHM_HS256, FAULTS_JWT_SECRET, strlen (FAULTS_JWT_SECRET), &error
	);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 2, "the key was created");

	// a key of the provider that is given is released with the key that could not take it
	CerverCryptoKey *crypto_key = cerver_crypto_key_create_secret (FAULTS_JWT_SECRET, strlen (FAULTS_JWT_SECRET), NULL);
	test_check_ptr (crypto_key);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE);
	key = cerver_jwt_key_create (CERVER_JWT_ALGORITHM_HS256, crypto_key, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (key);
	test_check_int_eq (error, CERVER_JWT_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 3, "the key was created");

}

// a token of the key of the tests with some claims
static size_t faults_jwt_sign (const CerverJwtKey *key, const char *claims, char *token) {

	size_t length = 0;
	test_check_int_eq (
		cerver_jwt_sign (key, claims, strlen (claims), token, FAULTS_JWT_TOKEN_SIZE, &length), CERVER_JWT_ERROR_NONE, NULL
	);

	return length;

}

// a key that signs and verifies tokens with each allocation of its path failing
typedef struct FaultsJwtTokens {

	const CerverJwtKey *key;

	// the arena of the claims, with chunks so small that every part of a token needs memory
	Arena *arena;

	// a token of the key, made without faults, and the same one with a character of its claims changed
	char valid[FAULTS_JWT_TOKEN_SIZE];
	char changed[FAULTS_JWT_TOKEN_SIZE];
	size_t length;

	unsigned int done;
	unsigned int memory;

} FaultsJwtTokens;

// a run of a signature: a whole token that the key verifies, or its error and nothing of it in the buffer
static void faults_jwt_sign_work (void *data) {

	FaultsJwtTokens *run = (FaultsJwtTokens *) data;

	char token[FAULTS_JWT_TOKEN_SIZE];
	(void) memset (token, 'x', sizeof (token));

	size_t length = sizeof (token);
	const CerverJwtError error = cerver_jwt_sign (
		run->key, FAULTS_JWT_CLAIMS, strlen (FAULTS_JWT_CLAIMS), token, sizeof (token), &length
	);

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	if (error == CERVER_JWT_ERROR_NONE) {
		test_check_unsigned_eq (length, strlen (token), NULL);

		arena_reset (run->arena);
		test_check_int_eq (
			cerver_jwt_verify (run->key, run->arena, token, length, NULL, NULL), CERVER_JWT_ERROR_NONE, NULL
		);

		run->done += 1;
	}

	else {
		test_check_int_eq (error, CERVER_JWT_ERROR_SIGNATURE, NULL);
		test_check_unsigned_eq (length, 0, NULL);
		test_check_unsigned_eq ((unsigned char) token[0], 0, "a token without its signature was left");
	}

}

// a run of a verification: a failed allocation can refuse the token of the key, with the error of its memory
// or of its signature and without claims; it never takes another token
static void faults_jwt_verify_work (void *data) {

	FaultsJwtTokens *run = (FaultsJwtTokens *) data;

	arena_reset (run->arena);

	const CerverJsonValue *changed_claims = (const CerverJsonValue *) run;
	const CerverJwtError changed_error = cerver_jwt_verify (
		run->key, run->arena, run->changed, run->length, NULL, &changed_claims
	);

	const CerverJsonValue *claims = (const CerverJsonValue *) run;
	const CerverJwtError error = cerver_jwt_verify (run->key, run->arena, run->valid, run->length, NULL, &claims);

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_int_ne (changed_error, CERVER_JWT_ERROR_NONE);
	test_check (changed_claims == NULL, "the claims of a token that was not signed");

	test_check_bool_eq (claims != NULL, error == CERVER_JWT_ERROR_NONE, "claims with an error, or none without one");

	if (error == CERVER_JWT_ERROR_NONE) {
		test_check_str_eq (cerver_json_string_value (cerver_json_object_get (claims, "id")), "faults", NULL);
		run->done += 1;
	}

	else {
		test_check ((error == CERVER_JWT_ERROR_MEMORY) || (error == CERVER_JWT_ERROR_SIGNATURE), "another error");
		if (error == CERVER_JWT_ERROR_MEMORY) run->memory += 1;
	}

}

// the tokens of a key, taken as the claims of another signer: its header is not the one that cerver writes
static void faults_jwt_tokens_sweep (CerverJwtKey *key, const bool with_memory) {

	FaultsJwtTokens run;
	(void) memset (&run, 0, sizeof (run));
	run.key = key;

	run.arena = arena_create (FAULTS_JWT_SMALL_CHUNK);
	test_check_ptr (run.arena);

	run.length = faults_jwt_sign (key, FAULTS_JWT_CLAIMS, run.valid);

	(void) memcpy (run.changed, run.valid, run.length + 1);
	char *claims = strchr (run.changed, JWT_SEPARATOR) + 2;
	*claims = (*claims == 'A') ? 'B' : 'A';

	(void) faults_sweep (faults_jwt_sign_work, &run);
	test_check (run.done > 0, "the key did not sign once nothing failed");

	run.done = 0;
	const unsigned int allocations = faults_sweep (faults_jwt_verify_work, &run);
	test_check (run.done > 0, "the key did not verify once nothing failed");

	// the claims of a token always need memory of the arena, for their text and for their values:
	// each one fails as the memory of the token, never as claims that are not valid
	if (with_memory) {
		test_check (allocations > 0, "the verification of a token has no allocations");
		test_check (run.memory >= FAULTS_JWT_ARENA_ALLOCATIONS, "the memory of the arena never failed");
	}

	arena_delete (run.arena);

	cerver_jwt_key_delete (key);

}

static CerverJwtKey *faults_jwt_key_load (const CerverJwtAlgorithm algorithm, const char *filename) {

	char pem[FAULTS_JWT_TOKEN_SIZE] = { 0 };

	FILE *file = fopen (filename, "rb");
	test_check (file != NULL, filename);
	const size_t size = fread (pem, sizeof (char), sizeof (pem), file);
	(void) fclose (file);

	CerverJwtKey *key = cerver_jwt_key_create_pem (algorithm, pem, size, true, NULL);
	test_check (key != NULL, filename);

	return key;

}

static void faults_jwt_tokens (void) {

	faults_jwt_tokens_sweep (
		cerver_jwt_key_create_secret (CERVER_JWT_ALGORITHM_HS256, FAULTS_JWT_SECRET, strlen (FAULTS_JWT_SECRET), NULL),
		true
	);

	faults_jwt_tokens_sweep (faults_jwt_key_load (CERVER_JWT_ALGORITHM_RS256, FAULTS_JWT_KEYS_PATH "rsa.key"), true);
	faults_jwt_tokens_sweep (faults_jwt_key_load (CERVER_JWT_ALGORITHM_PS256, FAULTS_JWT_KEYS_PATH "rsa.key"), true);
	faults_jwt_tokens_sweep (faults_jwt_key_load (CERVER_JWT_ALGORITHM_ES256, FAULTS_JWT_KEYS_PATH "p256.key"), true);
	faults_jwt_tokens_sweep (faults_jwt_key_load (CERVER_JWT_ALGORITHM_EDDSA, FAULTS_JWT_KEYS_PATH "ed25519.key"), true);

}

void faults_tests_jwt (void) {

	(void) printf ("Testing FAULTS jwt...\n");

	faults_jwt_key ();
	faults_jwt_tokens ();

	(void) printf ("Done!\n");

}
