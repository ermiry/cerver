#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/crypto/hash.h"
#include "cerver/crypto/provider.h"

bool cerver_crypto_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
) {

	return (data || !size) && digest && cerver_crypto_provider_sha1 (data, size, digest);

}
