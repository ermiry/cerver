#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/crypto/memory.h"
#include "cerver/crypto/provider.h"

void cerver_crypto_cleanse (
	void *buffer, const size_t size
) {

	if (buffer) explicit_bzero (buffer, size);

}

bool cerver_crypto_equal (
	const void *first, const void *second, const size_t size
) {

	if (!size) return true;

	return first && second && cerver_crypto_provider_equal (first, second, size);

}
