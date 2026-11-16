#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

// the input is the text of a JWK: a key that is created from it has a type, and with every algorithm that takes
// it, what it signs it verifies

#define JWK_FUZZ_MESSAGE				"the message of the keys of the fuzzer"

// a key that was created is one that cerver uses, and it says what it is
static void jwk_fuzz_key_check (const CerverCryptoKey *key) {

	const CerverCryptoKeyType type = cerver_crypto_key_type (key);
	if ((type <= CERVER_CRYPTO_KEY_TYPE_NONE) || (type > CERVER_CRYPTO_KEY_TYPE_ED448)) abort ();

	if (!cerver_crypto_key_bits (key)) abort ();

	const bool has_curve = cerver_crypto_key_curve (key) != CERVER_CRYPTO_CURVE_NONE;
	if (has_curve != (type == CERVER_CRYPTO_KEY_TYPE_EC)) abort ();

	if ((type == CERVER_CRYPTO_KEY_TYPE_SECRET) && !cerver_crypto_key_is_private (key)) abort ();

	if ((type == CERVER_CRYPTO_KEY_TYPE_RSA) && (cerver_crypto_key_bits (key) > CERVER_CRYPTO_RSA_BITS_MAX)) abort ();

}

// a signature of the key with an algorithm is one that the key verifies, and with one of its bits changed it is not;
// a key of RSA can be too small for a digest, so its signature can fail
static void jwk_fuzz_sign (const CerverCryptoKey *key, const CerverCryptoSignature algorithm) {

	const size_t needed = cerver_crypto_signature_size (key, algorithm);
	if (!needed || (needed > CERVER_CRYPTO_SIGNATURE_SIZE_MAX)) abort ();

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t size = 0;

	const bool is_signed = cerver_crypto_sign (
		key, algorithm, JWK_FUZZ_MESSAGE, strlen (JWK_FUZZ_MESSAGE), signature, sizeof (signature), &size
	);

	if (!is_signed) {
		if (size || (cerver_crypto_key_type (key) != CERVER_CRYPTO_KEY_TYPE_RSA)) abort ();
		return;
	}

	if (size != needed) abort ();

	if (!cerver_crypto_verify (key, algorithm, JWK_FUZZ_MESSAGE, strlen (JWK_FUZZ_MESSAGE), signature, size)) abort ();

	signature[0] ^= 1;
	if (cerver_crypto_verify (key, algorithm, JWK_FUZZ_MESSAGE, strlen (JWK_FUZZ_MESSAGE), signature, size)) abort ();

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_NONE;
	CerverCryptoKey *key = cerver_crypto_key_create_jwk ((const char *) data, size, &error);

	if ((key != NULL) != (error == CERVER_CRYPTO_KEY_ERROR_NONE)) abort ();
	if (!key) return 0;

	jwk_fuzz_key_check (key);

	unsigned int taken = 0;
	for (int algorithm = CERVER_CRYPTO_SIGNATURE_NONE + 1; algorithm < CERVER_CRYPTO_SIGNATURES; algorithm++) {
		if (cerver_crypto_signature_takes (key, (CerverCryptoSignature) algorithm)) {
			taken += 1;

			if (cerver_crypto_key_is_private (key)) jwk_fuzz_sign (key, (CerverCryptoSignature) algorithm);
		}
	}

	// every key is the one of an algorithm
	if (!taken) abort ();

	cerver_crypto_key_delete (key);

	return 0;

}
