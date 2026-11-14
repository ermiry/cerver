#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include <sys/random.h>
#include <sys/types.h>

#include "cerver/types/types.h"

#include "cerver/crypto/random.h"

// the default source of getrandom (): the urandom pool, that blocks only until it is initialized
#define CERVER_CRYPTO_RANDOM_FLAGS			0

// a read of getrandom () that returned bytes or that was interrupted continues
static bool cerver_crypto_random_continues (const ssize_t got) {

	return (got > 0) || ((got < 0) && (errno == EINTR));

}

bool cerver_crypto_random_bytes (
	void *buffer, const size_t size
) {

	size_t filled = 0;

	if (buffer) {
		u8 *bytes = (u8 *) buffer;

		bool reading = true;
		while (reading && (filled < size)) {
			const ssize_t got = getrandom (bytes + filled, size - filled, CERVER_CRYPTO_RANDOM_FLAGS);
			reading = cerver_crypto_random_continues (got);
			if (got > 0) filled += (size_t) got;
		}
	}

	return buffer && (filled == size);

}
