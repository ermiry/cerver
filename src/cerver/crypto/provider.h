#ifndef _CERVER_CRYPTO_PROVIDER_H_
#define _CERVER_CRYPTO_PROVIDER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/crypto/hash.h"

#if !defined (CERVER_CRYPTO_OPENSSL)
#error "cerver needs a provider of the cryptography: CRYPTO=openssl"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// the SHA-1 of the provider of the cryptography,
// the arguments are already checked
// returns true on success
CERVER_HIDDEN bool cerver_crypto_provider_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
);

#ifdef __cplusplus
}
#endif

#endif
