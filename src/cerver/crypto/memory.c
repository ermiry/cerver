#include <stddef.h>
#include <string.h>

#include "cerver/crypto/memory.h"

void cerver_crypto_cleanse (
	void *buffer, const size_t size
) {

	if (buffer) explicit_bzero (buffer, size);

}
