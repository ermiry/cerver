#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include "cerver/crypto/internal.h"

#include <cerver/json/json.h>

#include "../test.h"
#include "../crypto/crypto.h"
#include "../crypto/vectors.h"
#include "crypto.h"
#include "faults.h"

// the allocations of the keys and of the signatures that fail, with a fault injected in one of their calls

// a secret whose bytes are the only malloc () of their size
#define FAULTS_CRYPTO_SECRET			"the secret of the keys of the faults, with a size of its own !"

// the JWK of a secret: its text is copied with the bytes that its reader and its members need
#define FAULTS_CRYPTO_JWK				"{\"kty\":\"oct\",\"k\":\"dGhlIHNlY3JldCBvZiB0aGUgSldLIG9mIHRoZSBmYXVsdHM\"}"
#define FAULTS_CRYPTO_JWK_MEMORY_SIZE	((2 * (sizeof (FAULTS_CRYPTO_JWK) - 1)) + CERVER_JSON_PADDING)

#define FAULTS_CRYPTO_MESSAGE			"the message that the keys of the faults sign"

#define FAULTS_CRYPTO_FILE_SIZE			4096

#pragma region sizes

// a key that is not created without memory, with its error
static void faults_crypto_secret_fails (const size_t size) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;

	faults_arm (FAULTS_FUNCTION_MALLOC, true, size);
	CerverCryptoKey *key = cerver_crypto_key_create_secret (
		FAULTS_CRYPTO_SECRET, sizeof (FAULTS_CRYPTO_SECRET) - 1, &error
	);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the key was created");

}

// a secret is its key and the copy of its bytes: without one of them there is no key
static void faults_crypto_secret (void) {

	faults_crypto_secret_fails (sizeof (CerverCryptoKey));
	faults_crypto_secret_fails (sizeof (FAULTS_CRYPTO_SECRET) - 1);

	// the same call with memory
	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = cerver_crypto_key_create_secret (
		FAULTS_CRYPTO_SECRET, sizeof (FAULTS_CRYPTO_SECRET) - 1, &error
	);

	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);

	cerver_crypto_key_delete (key);

}

// the copy of the text of a JWK, the arena of its reader and the key of its secret
static void faults_crypto_jwk_fails (const size_t size) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;

	faults_arm (FAULTS_FUNCTION_MALLOC, true, size);
	CerverCryptoKey *key = cerver_crypto_key_create_jwk (FAULTS_CRYPTO_JWK, sizeof (FAULTS_CRYPTO_JWK) - 1, &error);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the key was created");

}

static void faults_crypto_jwk (void) {

	faults_crypto_jwk_fails (FAULTS_CRYPTO_JWK_MEMORY_SIZE);
	faults_crypto_jwk_fails (sizeof (Arena));
	faults_crypto_jwk_fails (sizeof (CerverCryptoKey));

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;
	CerverCryptoKey *key = cerver_crypto_key_create_jwk (FAULTS_CRYPTO_JWK, sizeof (FAULTS_CRYPTO_JWK) - 1, &error);
	test_check_ptr (key);
	test_check_int_eq (error, CERVER_CRYPTO_KEY_ERROR_NONE, NULL);
	cerver_crypto_key_delete (key);

}

#pragma endregion

#pragma region keys

// a key that is created with each allocation of its path failing: the text of its PEM or of its JWK,
// and what its runs left
typedef struct FaultsCryptoKey {

	const char *text;
	size_t size;

	bool is_pem;
	bool is_private;

	unsigned int created;
	unsigned int memory;
	unsigned int refused;

} FaultsCryptoKey;

// a run of the creation of a key: a key without an error, or its error without a key
static void faults_crypto_key_work (void *data) {

	FaultsCryptoKey *run = (FaultsCryptoKey *) data;

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;
	CerverCryptoKey *key = run->is_pem ?
		cerver_crypto_key_create_pem (run->text, run->size, run->is_private, &error) :
		cerver_crypto_key_create_jwk (run->text, run->size, &error);

	// the checks of the test are not part of the run
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_bool_eq (key != NULL, error == CERVER_CRYPTO_KEY_ERROR_NONE, "a key with an error, or none without one");

	if (key) {
		test_check_bool_eq (cerver_crypto_key_is_private (key), run->is_private, NULL);
		run->created += 1;
	}

	else if (error == CERVER_CRYPTO_KEY_ERROR_MEMORY) {
		run->memory += 1;
	}

	else {
		run->refused += 1;
	}

	cerver_crypto_key_delete (key);

}

// every allocation of the creation of a key fails once: there is never a key that is not whole,
// and the allocations of cerver give its memory error
static void faults_crypto_key_sweep (
	const char *text, const size_t size, const bool is_pem, const bool is_private
) {

	FaultsCryptoKey run = { text, size, is_pem, is_private, 0, 0, 0 };

	// the first key, without faults: what the provider keeps for every key is there
	faults_crypto_key_work (&run);
	test_check_unsigned_eq (run.created, 1, "the key of the sweep is not a key");

	const unsigned int allocations = faults_sweep (faults_crypto_key_work, &run);

	test_check (allocations > 0, "the creation of a key has no allocations");
	test_check (run.memory > 0, "no allocation of cerver failed");

	// the last run of the sweep had no allocation left to fail: its key was created
	test_check (run.created >= 2, "the key was not created once nothing failed");

}

static void faults_crypto_pem_sweep (const char *name, const bool is_private) {

	char filename[FAULTS_CRYPTO_FILE_SIZE] = { 0 };
	(void) snprintf (
		filename, sizeof (filename), "%s%s",
		name, is_private ? CRYPTO_TEST_KEY_PRIVATE_EXTENSION : CRYPTO_TEST_KEY_PUBLIC_EXTENSION
	);

	char pem[FAULTS_CRYPTO_FILE_SIZE] = { 0 };

	FILE *file = fopen (filename, "rb");
	test_check (file != NULL, name);
	const size_t size = fread (pem, sizeof (char), sizeof (pem), file);
	(void) fclose (file);

	faults_crypto_key_sweep (pem, size, true, is_private);

}

static void faults_crypto_keys (void) {

	faults_crypto_key_sweep (CRYPTO_VECTOR_SECRET_JWK, strlen (CRYPTO_VECTOR_SECRET_JWK), false, true);
	faults_crypto_key_sweep (CRYPTO_VECTOR_RSA_PUBLIC_JWK, strlen (CRYPTO_VECTOR_RSA_PUBLIC_JWK), false, false);
	faults_crypto_key_sweep (CRYPTO_VECTOR_RSA_PRIVATE_JWK, strlen (CRYPTO_VECTOR_RSA_PRIVATE_JWK), false, true);
	faults_crypto_key_sweep (CRYPTO_VECTOR_P256_PUBLIC_JWK, strlen (CRYPTO_VECTOR_P256_PUBLIC_JWK), false, false);
	faults_crypto_key_sweep (CRYPTO_VECTOR_P521_PRIVATE_JWK, strlen (CRYPTO_VECTOR_P521_PRIVATE_JWK), false, true);
	faults_crypto_key_sweep (CRYPTO_VECTOR_ED25519_PRIVATE_JWK, strlen (CRYPTO_VECTOR_ED25519_PRIVATE_JWK), false, true);
	faults_crypto_key_sweep (CRYPTO_VECTOR_ED25519_PUBLIC_JWK, strlen (CRYPTO_VECTOR_ED25519_PUBLIC_JWK), false, false);

	faults_crypto_pem_sweep (CRYPTO_TEST_KEY_RSA, true);
	faults_crypto_pem_sweep (CRYPTO_TEST_KEY_RSA, false);
	faults_crypto_pem_sweep (CRYPTO_TEST_KEY_P384, true);
	faults_crypto_pem_sweep (CRYPTO_TEST_KEY_P384, false);
	faults_crypto_pem_sweep (CRYPTO_TEST_KEY_ED448, true);
	faults_crypto_pem_sweep (CRYPTO_TEST_KEY_ED448, false);

}

#pragma endregion

#pragma region signatures

// a key that signs and verifies with each allocation of its path failing
typedef struct FaultsCryptoSignature {

	const CerverCryptoKey *key;
	CerverCryptoSignature algorithm;

	// a signature of the key, made without faults, and the same one with a bit changed
	u8 valid[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	u8 changed[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t size;

	unsigned int done;

} FaultsCryptoSignature;

// a run of a signature: one that the key verifies, or none and no size
static void faults_crypto_sign_work (void *data) {

	FaultsCryptoSignature *run = (FaultsCryptoSignature *) data;

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX] = { 0 };
	size_t size = sizeof (signature);

	const bool is_signed = cerver_crypto_sign (
		run->key, run->algorithm, FAULTS_CRYPTO_MESSAGE, strlen (FAULTS_CRYPTO_MESSAGE),
		signature, sizeof (signature), &size
	);

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	if (is_signed) {
		test_check_unsigned_eq (size, run->size, "a signature with another size");
		test_check (
			cerver_crypto_verify (
				run->key, run->algorithm, FAULTS_CRYPTO_MESSAGE, strlen (FAULTS_CRYPTO_MESSAGE), signature, size
			),
			cerver_crypto_signature_to_string (run->algorithm)
		);

		run->done += 1;
	}

	else {
		test_check_unsigned_eq (size, 0, "a signature that failed left its size");
	}

}

// a run of a verification: a failed allocation can refuse the signature of the key, never take another one
static void faults_crypto_verify_work (void *data) {

	FaultsCryptoSignature *run = (FaultsCryptoSignature *) data;

	const bool taken = cerver_crypto_verify (
		run->key, run->algorithm, FAULTS_CRYPTO_MESSAGE, strlen (FAULTS_CRYPTO_MESSAGE), run->changed, run->size
	);

	const bool verified = cerver_crypto_verify (
		run->key, run->algorithm, FAULTS_CRYPTO_MESSAGE, strlen (FAULTS_CRYPTO_MESSAGE), run->valid, run->size
	);

	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_false (taken);

	if (verified) run->done += 1;

}

static void faults_crypto_signature_sweep (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm
) {

	FaultsCryptoSignature run;
	(void) memset (&run, 0, sizeof (run));
	run.key = key;
	run.algorithm = algorithm;

	test_check_true (
		cerver_crypto_sign (
			key, algorithm, FAULTS_CRYPTO_MESSAGE, strlen (FAULTS_CRYPTO_MESSAGE),
			run.valid, sizeof (run.valid), &run.size
		)
	);

	(void) memcpy (run.changed, run.valid, run.size);
	run.changed[0] ^= 1;

	(void) faults_sweep (faults_crypto_sign_work, &run);
	test_check (run.done > 0, "the key did not sign once nothing failed");

	run.done = 0;
	(void) faults_sweep (faults_crypto_verify_work, &run);
	test_check (run.done > 0, "the key did not verify once nothing failed");

}

static CerverCryptoKey *faults_crypto_jwk_key (const char *jwk) {

	CerverCryptoKey *key = cerver_crypto_key_create_jwk (jwk, strlen (jwk), NULL);
	test_check_ptr (key);

	return key;

}

static void faults_crypto_signatures (void) {

	CerverCryptoKey *key = faults_crypto_jwk_key (CRYPTO_VECTOR_SECRET_JWK);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_HMAC_SHA512);
	cerver_crypto_key_delete (key);

	key = faults_crypto_jwk_key (CRYPTO_VECTOR_RSA_PRIVATE_JWK);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA384);
	cerver_crypto_key_delete (key);

	key = faults_crypto_jwk_key (CRYPTO_VECTOR_P256_PRIVATE_JWK);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256);
	cerver_crypto_key_delete (key);

	key = faults_crypto_jwk_key (CRYPTO_VECTOR_P521_PRIVATE_JWK);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_ECDSA_P521_SHA512);
	cerver_crypto_key_delete (key);

	key = faults_crypto_jwk_key (CRYPTO_VECTOR_ED25519_PRIVATE_JWK);
	faults_crypto_signature_sweep (key, CERVER_CRYPTO_SIGNATURE_ED25519);
	cerver_crypto_key_delete (key);

}

#pragma endregion

void faults_tests_crypto (void) {

	(void) printf ("Testing FAULTS crypto...\n");

	faults_crypto_secret ();
	faults_crypto_jwk ();
	faults_crypto_keys ();
	faults_crypto_signatures ();

	(void) printf ("Done!\n");

}
