#ifndef _CERVER_TESTS_JWT_H_
#define _CERVER_TESTS_JWT_H_

#include <stdbool.h>
#include <stddef.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/jwt/jwt.h>

// the secret of the keys of the HMAC of the tests: the bytes of the digest of SHA-512
#define JWT_TEST_SECRET						"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

// the most characters of a token of the tests, with its null byte
#define JWT_TEST_TOKEN_SIZE					4096

// the header that cerver writes for an algorithm
#define JWT_TEST_HEADER(algorithm)			"{\"alg\":\"" algorithm "\",\"typ\":\"JWT\"}"

// the whole text of a file, that the caller frees
extern char *jwt_test_file_read (const char *filename, size_t *size);

// the key of the tokens of an algorithm from the PEM of one of the keys of the tests (test/crypto/keys);
// it can fail with its error
extern CerverJwtKey *jwt_test_key_load (
	const CerverJwtAlgorithm algorithm, const char *name, const bool is_private, CerverJwtError *error
);

// the same key, that must be there
extern CerverJwtKey *jwt_test_key_pem (
	const CerverJwtAlgorithm algorithm, const char *name, const bool is_private
);

// the key of the tokens of an algorithm from a JWK, that must be one
extern CerverJwtKey *jwt_test_key_jwk (const CerverJwtAlgorithm algorithm, const char *jwk);

// the key of an HMAC with the secret of the tests
extern CerverJwtKey *jwt_test_key_secret (const CerverJwtAlgorithm algorithm);

// a key of the provider from the PEM of one of the keys of the tests, that must be there
extern CerverCryptoKey *jwt_test_crypto_pem (const char *name, const bool is_private);

// the token of any header and any payload with the signature that a key of the provider gives them:
// what cerver_jwt_sign () never writes; the caller frees it
extern char *jwt_test_forge (
	const CerverCryptoKey *key, const CerverCryptoSignature signature,
	const char *header, const void *payload, const size_t payload_size
);

// the same token with the secret of the tests as the key of an HMAC with SHA-256
extern char *jwt_test_forge_secret (const char *header, const char *payload);

// the token of a header and a payload with the bytes of any signature; the caller frees it
extern char *jwt_test_join (
	const char *header, const void *payload, const size_t payload_size,
	const u8 *signature, const size_t signature_size
);

extern void jwt_tests_algorithms (void);

extern void jwt_tests_keys (void);

extern void jwt_tests_sign (void);

extern void jwt_tests_verify (void);

extern void jwt_tests_claims (void);

#endif
