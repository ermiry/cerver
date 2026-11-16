#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/crypto/internal.h"
#include "cerver/crypto/key.h"
#include "cerver/crypto/memory.h"
#include "cerver/crypto/provider.h"

// the bits of each byte of a secret
#define CRYPTO_KEY_BYTE_BITS				8

#pragma region strings

const char *cerver_crypto_key_type_to_string (const CerverCryptoKeyType type) {

	switch (type) {
		#define XX(num, name, string) case CERVER_CRYPTO_KEY_TYPE_##name: return #string;
		CERVER_CRYPTO_KEY_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_NONE);

}

const char *cerver_crypto_curve_to_string (const CerverCryptoCurve curve) {

	switch (curve) {
		#define XX(num, name, string) case CERVER_CRYPTO_CURVE_##name: return #string;
		CERVER_CRYPTO_CURVE_MAP(XX)
		#undef XX
	}

	return cerver_crypto_curve_to_string (CERVER_CRYPTO_CURVE_NONE);

}

const char *cerver_crypto_key_error_to_string (const CerverCryptoKeyError error) {

	switch (error) {
		#define XX(num, name, string) case CERVER_CRYPTO_KEY_ERROR_##name: return #string;
		CERVER_CRYPTO_KEY_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_crypto_key_error_to_string (CERVER_CRYPTO_KEY_ERROR_NONE);

}

#pragma endregion

#pragma region create

void crypto_key_error_set (
	CerverCryptoKeyError *error, const CerverCryptoKeyError code
) {

	if (error) *error = code;

}

static CerverCryptoKey *crypto_key_new (void) {

	CerverCryptoKey *key = (CerverCryptoKey *) malloc (sizeof (CerverCryptoKey));
	if (key) {
		key->type = CERVER_CRYPTO_KEY_TYPE_NONE;
		key->is_private = false;
		key->bits = 0;
		key->curve = CERVER_CRYPTO_CURVE_NONE;

		key->secret = NULL;
		key->secret_size = 0;

		key->provider = NULL;
	}

	return key;

}

void cerver_crypto_key_delete (CerverCryptoKey *key) {

	if (key) {
		if (key->secret) {
			cerver_crypto_cleanse (key->secret, key->secret_size);
			free (key->secret);
		}

		cerver_crypto_provider_key_delete (key->provider);

		free (key);
	}

}

// the copy of the bytes of a secret in its key
static CerverCryptoKeyError crypto_key_secret_set (
	CerverCryptoKey *key, const void *secret, const size_t size
) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_MEMORY;

	key->secret = (u8 *) malloc (size);
	if (key->secret) {
		(void) memcpy (key->secret, secret, size);
		key->secret_size = size;

		key->type = CERVER_CRYPTO_KEY_TYPE_SECRET;
		key->is_private = true;
		key->bits = (unsigned int) (size * CRYPTO_KEY_BYTE_BITS);

		error = CERVER_CRYPTO_KEY_ERROR_NONE;
	}

	return error;

}

CerverCryptoKey *cerver_crypto_key_create_secret (
	const void *secret, const size_t size,
	CerverCryptoKeyError *error
) {

	CerverCryptoKeyError result = CERVER_CRYPTO_KEY_ERROR_ARGUMENTS;

	CerverCryptoKey *key = NULL;

	if (secret && size && (size <= CERVER_CRYPTO_SECRET_SIZE_MAX)) {
		key = crypto_key_new ();
		result = key ? crypto_key_secret_set (key, secret, size) : CERVER_CRYPTO_KEY_ERROR_MEMORY;
	}

	if (result != CERVER_CRYPTO_KEY_ERROR_NONE) {
		cerver_crypto_key_delete (key);
		key = NULL;
	}

	crypto_key_error_set (error, result);

	return key;

}

// what the provider says that its key is; a key that cerver does not use has no description
static CerverCryptoKeyError crypto_key_describe (
	CerverCryptoKey *key, void *provider_key, const bool is_private
) {

	CerverCryptoKeyError error = CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED;

	// a key of RSA with more bits than the largest one has signatures that do not fit in their buffer
	if (
		cerver_crypto_provider_key_describe (provider_key, &key->type, &key->bits, &key->curve)
		&& ((key->type != CERVER_CRYPTO_KEY_TYPE_RSA) || (key->bits <= CERVER_CRYPTO_RSA_BITS_MAX))
	) {
		key->is_private = is_private;

		error = CERVER_CRYPTO_KEY_ERROR_NONE;
	}

	return error;

}

CerverCryptoKey *crypto_key_adopt (
	void *provider_key, const bool is_private,
	CerverCryptoKeyError *error
) {

	CerverCryptoKeyError result = CERVER_CRYPTO_KEY_ERROR_FORMAT;

	CerverCryptoKey *key = NULL;

	if (provider_key) {
		key = crypto_key_new ();
		result = key ? crypto_key_describe (key, provider_key, is_private) : CERVER_CRYPTO_KEY_ERROR_MEMORY;
	}

	// the key owns the one of the provider only when it was created
	if (result == CERVER_CRYPTO_KEY_ERROR_NONE) {
		key->provider = provider_key;
	}

	else {
		cerver_crypto_provider_key_delete (provider_key);
		cerver_crypto_key_delete (key);
		key = NULL;
	}

	crypto_key_error_set (error, result);

	return key;

}

CerverCryptoKey *cerver_crypto_key_create_pem (
	const void *pem, const size_t size, const bool is_private,
	CerverCryptoKeyError *error
) {

	if (!pem || !size) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS);
		return NULL;
	}

	return crypto_key_adopt (
		cerver_crypto_provider_key_read_pem (pem, size, is_private), is_private, error
	);

}

#pragma endregion

#pragma region getters

CerverCryptoKeyType cerver_crypto_key_type (const CerverCryptoKey *key) {

	return key ? key->type : CERVER_CRYPTO_KEY_TYPE_NONE;

}

bool cerver_crypto_key_is_private (const CerverCryptoKey *key) {

	return key ? key->is_private : false;

}

unsigned int cerver_crypto_key_bits (const CerverCryptoKey *key) {

	return key ? key->bits : 0;

}

CerverCryptoCurve cerver_crypto_key_curve (const CerverCryptoKey *key) {

	return key ? key->curve : CERVER_CRYPTO_CURVE_NONE;

}

#pragma endregion
