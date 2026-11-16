#ifndef _CERVER_TESTS_CRYPTO_H_
#define _CERVER_TESTS_CRYPTO_H_

#include <stdbool.h>
#include <stddef.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>

// the keys of the tests, made with the openssl tool: a private one (.key) and its public one (.pub)
#define CRYPTO_TEST_KEYS_PATH				"./test/crypto/keys/"

#define CRYPTO_TEST_KEY_RSA					CRYPTO_TEST_KEYS_PATH "rsa"
#define CRYPTO_TEST_KEY_WEAK				CRYPTO_TEST_KEYS_PATH "weak"
#define CRYPTO_TEST_KEY_P256				CRYPTO_TEST_KEYS_PATH "p256"
#define CRYPTO_TEST_KEY_P384				CRYPTO_TEST_KEYS_PATH "p384"
#define CRYPTO_TEST_KEY_P521				CRYPTO_TEST_KEYS_PATH "p521"
#define CRYPTO_TEST_KEY_ED25519				CRYPTO_TEST_KEYS_PATH "ed25519"
#define CRYPTO_TEST_KEY_ED448				CRYPTO_TEST_KEYS_PATH "ed448"

// the ones that cerver does not use: another curve, a key that only agrees, and the public key of a key of RSA
// of 16392 bits, 8 more than the largest one
#define CRYPTO_TEST_KEY_SECP256K1			CRYPTO_TEST_KEYS_PATH "secp256k1"
#define CRYPTO_TEST_KEY_X25519				CRYPTO_TEST_KEYS_PATH "x25519"
#define CRYPTO_TEST_KEY_HUGE				CRYPTO_TEST_KEYS_PATH "huge"

// the private key of RSA again: with a passphrase, and in the PEM of PKCS #1
#define CRYPTO_TEST_KEY_ENCRYPTED			CRYPTO_TEST_KEYS_PATH "encrypted"
#define CRYPTO_TEST_KEY_TRADITIONAL			CRYPTO_TEST_KEYS_PATH "traditional"

// the private key of P-256 again, in the PEM of SEC 1
#define CRYPTO_TEST_KEY_SEC1				CRYPTO_TEST_KEYS_PATH "sec1"

#define CRYPTO_TEST_KEY_PRIVATE_EXTENSION	".key"
#define CRYPTO_TEST_KEY_PUBLIC_EXTENSION	".pub"

// the whole text of a file, that the caller frees
extern char *crypto_test_file_read (const char *filename, size_t *size);

// the private or the public key of the PEM of one of the keys of the tests; it can fail with its error
extern CerverCryptoKey *crypto_test_key_load (
	const char *name, const bool is_private, CerverCryptoKeyError *error
);

// the same key, that must be there
extern CerverCryptoKey *crypto_test_key_get (const char *name, const bool is_private);

// the bytes of a text in lower case hex, returns how many
extern size_t crypto_test_hex_decode (const char *hex, u8 *bytes, const size_t capacity);

// the bytes of a canonical base64url text, returns how many
extern size_t crypto_test_base64url_decode (const char *encoded, u8 *bytes, const size_t capacity);

extern void crypto_tests_random (void);

extern void crypto_tests_hash (void);

extern void crypto_tests_memory (void);

extern void crypto_tests_keys (void);

extern void crypto_tests_jwk (void);

extern void crypto_tests_signatures (void);

#endif
