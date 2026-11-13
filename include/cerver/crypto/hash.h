#ifndef _CERVER_CRYPTO_HASH_H_
#define _CERVER_CRYPTO_HASH_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

// the bytes of a SHA-1 digest
#define CERVER_CRYPTO_SHA1_SIZE				20

#ifdef __cplusplus
extern "C" {
#endif

// writes in digest the SHA-1 of the size bytes of data,
// computed by the provider of the cryptography
// returns true on success
CERVER_PUBLIC bool cerver_crypto_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
);

#ifdef __cplusplus
}
#endif

#endif
