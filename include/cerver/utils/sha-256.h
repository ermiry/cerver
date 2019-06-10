#ifndef _CERVER_SHA_256_H_
#define _CERVER_SHA_256_H_

#include <stdlib.h>
#include <stdint.h>

extern void sha_256_calc (uint8_t hash[32], const void *input, size_t len);

extern void sha_256_hash_to_string (char string[65], const uint8_t hash[32]);

#endif