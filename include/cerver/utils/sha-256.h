#ifndef _CERVER_UTILS_SHA_256_H_
#define _CERVER_UTILS_SHA_256_H_

#include <stdint.h>

#include "cerver/config.h"

#define SHA256_STRING_LEN		80

CERVER_PUBLIC void sha_256_calc (
	uint8_t hash[32], const void *input, size_t len
);

CERVER_PUBLIC void sha_256_hash_to_string (
	char string[65], const uint8_t hash[32]
);

// generates a sha256 string from the input and places it in output
// output must be at least 65 bytes long
CERVER_PUBLIC void sha_256_generate (
	char *output, const char *input, const size_t inlen
);

// generates a sha256 string from the input
// returns a newly allocated string
CERVER_PUBLIC char *sha_256_generate_output (
	const char *input, const size_t inlen
);

#endif