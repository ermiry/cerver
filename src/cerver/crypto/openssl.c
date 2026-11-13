#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/crypto/hash.h"
#include "cerver/crypto/provider.h"

#ifdef CERVER_CRYPTO_OPENSSL

#include <openssl/evp.h>

// EVP_Digest () returns 1 on success
#define CERVER_CRYPTO_OPENSSL_SUCCESS			1

// the digest of NULL data, that OpenSSL takes as an empty input
static const u8 cerver_crypto_openssl_empty = 0;

bool cerver_crypto_provider_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
) {

	unsigned int length = 0;

	const int result = EVP_Digest (
		data ? data : &cerver_crypto_openssl_empty, size,
		digest, &length,
		EVP_sha1 (), NULL
	);

	return (result == CERVER_CRYPTO_OPENSSL_SUCCESS) && (length == CERVER_CRYPTO_SHA1_SIZE);

}

#endif
