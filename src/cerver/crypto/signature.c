#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/crypto/internal.h"
#include "cerver/crypto/key.h"
#include "cerver/crypto/memory.h"
#include "cerver/crypto/provider.h"
#include "cerver/crypto/signature.h"

// the bytes of the digests of an HMAC
#define CRYPTO_SIGNATURE_SHA256_SIZE			32
#define CRYPTO_SIGNATURE_SHA384_SIZE			48
#define CRYPTO_SIGNATURE_SHA512_SIZE			64

// the key that an algorithm takes
typedef struct CryptoSignatureAlgorithm {

	CerverCryptoKeyType type;

	// the curve of the EC key of an ECDSA
	CerverCryptoCurve curve;

	// the bytes of an HMAC, the ones of its digest
	size_t mac_size;

} CryptoSignatureAlgorithm;

// one for each row of CERVER_CRYPTO_SIGNATURE_MAP, in its order
static const CryptoSignatureAlgorithm crypto_signature_algorithms[CERVER_CRYPTO_SIGNATURES] = {
	{ CERVER_CRYPTO_KEY_TYPE_NONE, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_SECRET, CERVER_CRYPTO_CURVE_NONE, CRYPTO_SIGNATURE_SHA256_SIZE },
	{ CERVER_CRYPTO_KEY_TYPE_SECRET, CERVER_CRYPTO_CURVE_NONE, CRYPTO_SIGNATURE_SHA384_SIZE },
	{ CERVER_CRYPTO_KEY_TYPE_SECRET, CERVER_CRYPTO_CURVE_NONE, CRYPTO_SIGNATURE_SHA512_SIZE },
	{ CERVER_CRYPTO_KEY_TYPE_RSA, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_RSA, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_RSA, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_RSA, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_RSA, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_RSA, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_EC, CERVER_CRYPTO_CURVE_P256, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_EC, CERVER_CRYPTO_CURVE_P384, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_EC, CERVER_CRYPTO_CURVE_P521, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_ED25519, CERVER_CRYPTO_CURVE_NONE, 0 },
	{ CERVER_CRYPTO_KEY_TYPE_ED448, CERVER_CRYPTO_CURVE_NONE, 0 }
};

static const CryptoSignatureAlgorithm *crypto_signature_algorithm (
	const CerverCryptoSignature algorithm
) {

	return ((unsigned int) algorithm < CERVER_CRYPTO_SIGNATURES) ?
		&crypto_signature_algorithms[algorithm] :
		&crypto_signature_algorithms[CERVER_CRYPTO_SIGNATURE_NONE];

}

const char *cerver_crypto_signature_to_string (const CerverCryptoSignature algorithm) {

	switch (algorithm) {
		#define XX(num, name, string) case CERVER_CRYPTO_SIGNATURE_##name: return #string;
		CERVER_CRYPTO_SIGNATURE_MAP(XX)
		#undef XX
	}

	return cerver_crypto_signature_to_string (CERVER_CRYPTO_SIGNATURE_NONE);

}

bool cerver_crypto_signature_takes (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm
) {

	const CryptoSignatureAlgorithm *found = crypto_signature_algorithm (algorithm);

	return key
		&& (found->type != CERVER_CRYPTO_KEY_TYPE_NONE)
		&& (key->type == found->type)
		&& (key->curve == found->curve);

}

size_t cerver_crypto_signature_size (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm
) {

	if (!cerver_crypto_signature_takes (key, algorithm)) return 0;

	return (key->type == CERVER_CRYPTO_KEY_TYPE_SECRET) ?
		crypto_signature_algorithm (algorithm)->mac_size :
		cerver_crypto_provider_signature_size (key->provider, algorithm);

}

#pragma region sign

// the HMAC of the message is its signature; what is left of it in the stack is erased
static bool crypto_sign_mac (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	u8 *signature, size_t *size
) {

	u8 mac[CERVER_CRYPTO_MAC_SIZE_MAX] = { 0 };
	size_t mac_size = 0;

	const bool computed = cerver_crypto_provider_mac (
		algorithm, key->secret, key->secret_size, message, message_size, mac, &mac_size
	);

	if (computed) {
		(void) memcpy (signature, mac, mac_size);
		*size = mac_size;
	}

	cerver_crypto_cleanse (mac, sizeof (mac));

	return computed;

}

bool cerver_crypto_sign (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	u8 *signature, const size_t capacity, size_t *size
) {

	bool result = false;

	if (size) *size = 0;

	// a key that is not the one of the algorithm has signatures without bytes
	const size_t needed = cerver_crypto_signature_size (key, algorithm);
	if (
		needed && key->is_private
		&& signature && size && (capacity >= needed)
		&& (message || !message_size)
	) {
		result = (key->type == CERVER_CRYPTO_KEY_TYPE_SECRET) ?
			crypto_sign_mac (key, algorithm, message, message_size, signature, size) :
			cerver_crypto_provider_sign (key->provider, algorithm, message, message_size, signature, size);
	}

	return result;

}

#pragma endregion

#pragma region verify

// the HMAC of the message against the signature, in a time that does not depend on their bytes
static bool crypto_verify_mac (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
) {

	u8 mac[CERVER_CRYPTO_MAC_SIZE_MAX] = { 0 };
	size_t mac_size = 0;

	const bool verified = cerver_crypto_provider_mac (
		algorithm, key->secret, key->secret_size, message, message_size, mac, &mac_size
	)
		&& (signature_size == mac_size)
		&& cerver_crypto_provider_equal (mac, signature, mac_size);

	cerver_crypto_cleanse (mac, sizeof (mac));

	return verified;

}

bool cerver_crypto_verify (
	const CerverCryptoKey *key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
) {

	if (
		!cerver_crypto_signature_takes (key, algorithm)
		|| !signature || (!message && message_size)
	) {
		return false;
	}

	return (key->type == CERVER_CRYPTO_KEY_TYPE_SECRET) ?
		crypto_verify_mac (key, algorithm, message, message_size, signature, signature_size) :
		cerver_crypto_provider_verify (key->provider, algorithm, message, message_size, signature, signature_size);

}

#pragma endregion
