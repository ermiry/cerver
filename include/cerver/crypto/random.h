#ifndef _CERVER_CRYPTO_RANDOM_H_
#define _CERVER_CRYPTO_RANDOM_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

// fills the buffer with size random bytes of the kernel,
// retrying an interrupted call and a partial read
// returns true when the whole buffer was filled
CERVER_PUBLIC bool cerver_crypto_random_bytes (
	void *buffer, const size_t size
);

#ifdef __cplusplus
}
#endif

#endif
