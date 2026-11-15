#ifndef _CERVER_CRYPTO_MEMORY_H_
#define _CERVER_CRYPTO_MEMORY_H_

#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

// overwrites the size bytes of the buffer with zeros,
// a write that the compiler never removes,
// for a secret before its memory is released
CERVER_PUBLIC void cerver_crypto_cleanse (
	void *buffer, const size_t size
);

#ifdef __cplusplus
}
#endif

#endif
